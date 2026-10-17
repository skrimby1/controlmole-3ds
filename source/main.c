#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <math.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "anim.h"
#include "data.h"
#include "draw.h"
#include "mole.h"
#include "sound.h"
#include "titles.h"
#include "trophies.h"

extern int drawDX;

typedef enum { TAB_TODAY, TAB_GAMES, TAB_RULES, TAB_TROPHIES, TAB_COUNT } Tab;
static const char* TAB_NAMES[TAB_COUNT] = {"TODAY", "GAMES", "RULES", "TROPHIES"};

static Tab tab = TAB_TODAY;
static u32 tick;
static bool title = true;  // the title screen first

typedef enum { SCENE_MOLE, SCENE_BURST, SCENE_PAN, SCENE_DANCE } Scene;
static Scene scene = SCENE_MOLE;
static u32 sceneAt;     // tick the scene started
static u32 boxAt;       // tick the dialogue box started sliding in
static Anim burst, dance;

static float easeOut(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }
static float easeInOut(float t) { return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }
static float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

// --------------------------------------------------------------- dialogue ----
static char lines[6][220];
static int nLines, lineAt, typed;

static void say(int n, char src[][200]) {
  nLines = n > 6 ? 6 : n;
  for (int i = 0; i < nLines; i++) snprintf(lines[i], sizeof lines[i], "%s", src[i]);
  lineAt = 0, typed = 0;
}

static void say1(const char* s) {
  snprintf(lines[0], sizeof lines[0], "%s", s);
  nLines = 1, lineAt = 0, typed = 0;
}

static bool dialogueDone(void) { return typed >= (int)strlen(lines[lineAt]); }

// The typewriter: a letter every other frame (30 a second), and a babble blip
// on every other letter, like a villager talking.
static void typeOn(Mood mood) {
  if (dialogueDone() || tick % 2) return;
  char c = lines[lineAt][typed++];
  if (typed % 2) babble(c, mood);
}

static void dialogueNext(void) {
  if (!dialogueDone()) typed = 9999;
  else if (nLines > 1) lineAt = (lineAt + 1) % nLines, typed = 0;
}

// ------------------------------------------------------------ hop, shake ----
static u32 hopAt = ~0u, shakeAt = ~0u;
static bool landed = true;

static float track(const float* k, float p) {
  static const float W[5] = {26, 22, 12, 20, 20};
  float t = p * 100;
  for (int i = 0; i < 5; i++) {
    if (t <= W[i] || i == 4) {
      float u = clamp01(t / W[i]), e;
      switch (i) {
        case 0: e = easeOut(u); break;                      // swell and rise
        case 1: e = u * u; break;                           // fall back down
        case 2: e = 1 - (1 - u) * (1 - u); break;           // squash on landing
        case 3: e = easeInOut(u); break;                    // spring back
        default: e = 1 - (1 - u) * (1 - u); break;          // settle
      }
      return k[i] + (k[i + 1] - k[i]) * e;
    }
    t -= W[i];
  }
  return k[5];
}

static void hopState(float* sx, float* sy, int* lift, int* dx) {
  static const float SX[6] = {1.0f, 1.06f, 1.0f, 1.05f, 0.985f, 1.0f};
  static const float SY[6] = {1.0f, 1.07f, 1.0f, 0.93f, 1.015f, 1.0f};
  static const float LIFT[6] = {0, 5, 0, 0, 0, 0};
  static const int SHAKE[8] = {0, -3, 3, -3, 2, -2, 1, 0};
  *sx = *sy = 1, *lift = 0, *dx = 0;
  if (hopAt != ~0u && tick - hopAt < 34) {
    float p = (tick - hopAt) / 33.6f;
    *sx = track(SX, p), *sy = track(SY, p), *lift = (int)(track(LIFT, p) + 0.5f);
    if (!landed && p >= 0.48f) landed = true, sfx(SFX_THUD);
  }
  if (shakeAt != ~0u && tick - shakeAt < 25) *dx = SHAKE[(tick - shakeAt) * 7 / 25];
}

static void poke(Mood mood) {
  if (mood == MOOD_FURIOUS) {
    shakeAt = tick;
    sfx(SFX_GRUMBLE);
  } else {
    hopAt = tick, landed = false;
    sfx(SFX_HOP);
  }
}

// ------------------------------------------------------------------ state ----
static Today today_;
static RotScore score;
static MoleState mole;
static int streak;
static s64 now;
static GameRow games[48];
static int nGames;
static u32 freshTrophies;

static s64 clock_(void) { return (s64)time(NULL); }

static void refresh(bool speak) {
  now = clock_();
  seenToday(now);
  today(&today_, dayStart(now), now);
  int limit = limitOn(now);
  rotScore(&score, &today_, limit, settings.sessionLimit);
  streak = streakNow(now);
  moleState(&mole, &today_, limit, settings.sessionLimit, streak, settings.bedtimeOn,
            bedtimeAt(now), now, playlog.count > 0 || playlog.real);
  nGames = gamesOn(dayStart(now), now, games, 48);
  freshTrophies |= trophiesCheck(now);
  if (speak) say(mole.count, mole.lines);
}

// ---------------------------------------------------------------- top ----
#define MOLE_X ((TOP_W - 320) / 2)
#define CAM_BURST 12
#define CAM_MOLE (12 - 125)  // his feet tuck behind the dialogue box
#define PAN_TICKS 24
#define BOX_TICKS 14
#define BOX_Y 162

static int boxY(void) {
  float t = clamp01((tick - boxAt) / (float)BOX_TICKS);
  return BOX_Y + (int)((1 - easeOut(t)) * (SCR_H - BOX_Y + 12) + 0.5f);
}

static bool boxReady(void) { return scene == SCENE_MOLE && tick - boxAt >= BOX_TICKS; }

static void drawBox(int by) {
  const int bx = 8, bw = TOP_W - 16, bh = 72;
  box(bx, by, bw, bh, C_CREAM, false);
  // The name tag, like the phone's speech bubble.
  box(bx + 10, by - 11, 44, 17, C_CHESTNUT, false);
  text(bx + 32, by - 7, "MOLE", C_CREAM, ALIGN_CENTER);
  textWrap(bx + 10, by + 12, bw - 24, lines[lineAt], C_INK, typed);
  if (dialogueDone() && nLines > 1) moreArrow(bx + bw - 16, by + bh - 13, tick);
}

// ------------------------------------------------------------------ title ----
static Anim happy;
static u32 titleAt;
static bool titleLanded, titleHopped;
#define DROP_TICKS 34   // the drop, with two bounces
#define HOP_IN_AT 44
#define HOP_IN_TICKS 20
#define PROMPT_AT 80

static int titleTick(void) { return tick - titleAt; }

static float bounce(float t) {  // easeOutBounce
  const float n = 7.5625f, d = 2.75f;
  if (t < 1 / d) return n * t * t;
  if (t < 2 / d) return t -= 1.5f / d, n * t * t + 0.75f;
  if (t < 2.5f / d) return t -= 2.25f / d, n * t * t + 0.9375f;
  return t -= 2.625f / d, n * t * t + 0.984375f;
}

static void drawTitle(int eye, int near) {
  int t = titleTick();
  // The thud: a few pixels of jolt as the word lands.
  static const int JOLT[6] = {3, -2, 2, -1, 1, 0};
  int jolt = t >= DROP_TICKS && t < DROP_TICKS + 6 ? JOLT[t - DROP_TICKS] : 0;
  drawDX = (eye ? -near : near);
  C2D_Image wm = C2D_SpriteSheetGetImage(sheetMisc, 5);
  int wh = wm.subtex->height;
  float p = bounce(clamp01(t / (float)DROP_TICKS));
  int wy = -wh - 8 + (int)((44 + wh + 8) * p + 0.5f);
  image(sheetMisc, 5, 6, wy + jolt);
  drawDX = 0;
  if (t >= HOP_IN_AT && happy.file) {
    float q = easeOut(clamp01((t - HOP_IN_AT) / (float)HOP_IN_TICKS));
    int x = TOP_W + (int)((228 - TOP_W) * q + 0.5f);
    C2D_Image f = animFrame(&happy, ((t - HOP_IN_AT) / 2) % happy.frames);
    C2D_DrawImageAt(f, x + drawDX, 50 + jolt, 0.5f, NULL, 1, 1);
  }
  if (t >= PROMPT_AT && ((t - PROMPT_AT) / 30) % 2 == 0) {
    box(TOP_W / 2 - 74, 194, 148, 22, C_CREAM, false);
    text(TOP_W / 2, 200, "TOUCH THE SCREEN", C_INK, ALIGN_CENTER);
  }
  if (t >= PROMPT_AT) text(TOP_W / 2, 225, "Homebrew for the 3DS family, dig dig!", C_MUTED, ALIGN_CENTER);
}

static void updateTitle(void) {
  int t = titleTick();
  if (!titleLanded && t >= DROP_TICKS) titleLanded = true, sfx(SFX_THUD);
  if (!titleHopped && t >= HOP_IN_AT) titleHopped = true, sfx(SFX_HOP);
  if (t == PROMPT_AT) homePlay();
}

static void drawTop(int eye, float depth) {
  // Paper behind the screen, Mole on it, the dialogue box in front.
  int far = (int)(depth * 5 + 0.5f), near = (int)(depth * 3 + 0.5f);
  drawDX = eye ? far : -far;
  paper(TOP_W, tick, 0);

  if (title) {
    drawTitle(eye, near);
    return;
  }

  if (scene == SCENE_DANCE) {
    // The disco, in a little window floating over the paper.
    drawDX = eye ? -near : near;
    box(86, 4, 228, 232, C_INK, false);
    C2D_Image f = animFrame(&dance, ((tick - sceneAt) / 2) % dance.frames);
    C2D_DrawImageAt(f, 88 + drawDX, 6, 0.5f, NULL, 1, 1);
    drawDX = 0;
    return;
  }

  drawDX = 0;
  if (scene == SCENE_BURST) {
    C2D_Image f = animFrame(&burst, (tick - sceneAt) / 2);
    C2D_DrawImageAt(f, MOLE_X + burst.ox, CAM_BURST + burst.oy, 0.5f, NULL, 1, 1);
    return;
  }
  int cam = CAM_MOLE;
  if (scene == SCENE_PAN)
    cam = CAM_BURST + (int)((CAM_MOLE - CAM_BURST) *
                            easeInOut(clamp01((tick - sceneAt) / (float)PAN_TICKS)) - 0.5f);
  float sx, sy;
  int lift, dx;
  hopState(&sx, &sy, &lift, &dx);
  moodDrawEx(MOLE_X + dx, cam + 125, tick, sx, sy, lift);
  if (scene == SCENE_MOLE) {
    drawDX = eye ? -near : near;
    drawBox(boxY());
    drawDX = 0;
  }
}

// Furious? He bursts out of the molehill first. Otherwise the box slides in.
static void enterMole(void) {
  if (mole.mood == MOOD_FURIOUS && animOpen(&burst, "burst")) {
    moodLoad(MOOD_FURIOUS);
    scene = SCENE_BURST, sceneAt = tick;
    sfx(SFX_STING);
  } else {
    scene = SCENE_MOLE, boxAt = tick;
  }
  say(mole.count, mole.lines);
}

static void updateScene(void) {
  if (scene == SCENE_BURST && (tick - sceneAt) / 2 >= (u32)burst.frames + 8) {
    animClose(&burst);
    scene = SCENE_PAN, sceneAt = tick;
  } else if (scene == SCENE_PAN && tick - sceneAt >= PAN_TICKS) {
    scene = SCENE_MOLE, boxAt = tick;
  }
}

// ----------------------------------------------------------------- dance ----
// New trophies: the disco dance plays on top with its music, and the bottom
// screen shows each trophy with Mole's cheer.
static int danceList[TROPHIES], danceN, danceAt;

static void sayCheer(void) {
  say1(trophyList[danceList[danceAt]].cheer);
  sfx(SFX_TROPHY);
}

static void startDance(u32 fresh) {
  danceN = danceAt = 0;
  for (int i = 0; i < TROPHIES; i++)
    if (fresh & (1u << i)) danceList[danceN++] = i;
  if (!danceN) return;
  if (!animOpen(&dance, "dance")) {  // no dance? Mole still says it
    static char l[6][200];
    for (int i = 0; i < danceN && i < 6; i++)
      snprintf(l[i], 200, "New trophy, %s! %s", trophyList[danceList[i]].name,
               trophyList[danceList[i]].cheer);
    say(danceN < 6 ? danceN : 6, l);
    return;
  }
  scene = SCENE_DANCE, sceneAt = tick;
  homePause();
  musicPlay();
  sayCheer();
}

static void danceNext(void) {
  if (!dialogueDone()) {
    typed = 9999;
    return;
  }
  sfx(SFX_TAP);
  if (++danceAt < danceN) {
    sayCheer();
    return;
  }
  musicStop();
  homePlay();
  animClose(&dance);
  enterMole();
}

static void leaveTitle(void) {
  if (titleTick() < PROMPT_AT) {  // skip the intro first
    titleAt = tick - PROMPT_AT, titleLanded = titleHopped = true;
    homePlay();
    return;
  }
  sfx(SFX_TAP);
  title = false;
  animClose(&happy);
  if (freshTrophies) {
    startDance(freshTrophies);
    freshTrophies = 0;
    if (scene == SCENE_DANCE) return;
  }
  enterMole();
}

// ------------------------------------------------------------- bottom ----
static int scrollY, cursor;
static touchPosition touch;
static bool touching, tapped;
static int tapX, tapY;

static bool hit(int x, int y, int w, int h) {
  return tapped && tapX >= x && tapX < x + w && tapY >= y && tapY < y + h;
}

static void section(int x, int y, int w, int h, const char* label) {
  box(x, y, w, h, C_CREAM, false);
  text(x + 8, y + 7, label, C_MUTED, ALIGN_LEFT);
}

static u32 toneColor(int tone) { return tone == 0 ? C_GOOD : tone == 1 ? C_WARN : C_BAND; }

static void drawToday(void) {
  section(4, 4, BOT_W - 8, 200, "HOW YOU PLAYED TODAY");
  // Where the minutes come from, so a wrong number can be traced.
  text(BOT_W - 14, 11, !playlog.real ? "SAMPLE DAY"
                       : playlog.source[0] == 'M' ? "from Mole's plugin"
                       : playlog.source[0] == 'b' ? "plugin + Activity Log"
                                                  : "from the Activity Log",
       C_MUTED, ALIGN_RIGHT);
  char buf[32];
  snprintf(buf, sizeof buf, "%d", score.total);
  int w = textBig(12, 26, buf, C_INK, 3, ALIGN_LEFT);
  text(12 + w + 6, 38, "ROT SCORE", C_MUTED, ALIGN_LEFT);
  int tone = score.total >= 60 ? 2 : score.total >= 35 ? 1 : 0;
  text(BOT_W - 14, 38, rotVerdict(score.total), toneColor(tone), ALIGN_RIGHT);
  for (int i = 0; i < 4; i++) {
    const RotPart* p = &score.parts[i];
    int y = 58 + i * 36;
    image(sheetHabits, i * 3 + rotTone(p), 12, y);
    text(52, y + 4, p->label, C_INK, ALIGN_LEFT);
    text(52, y + 18, p->detail, C_MUTED, ALIGN_LEFT);
    snprintf(buf, sizeof buf, "%d/%d", p->points, p->max);
    text(BOT_W - 14, y + 11, buf, C_MUTED, ALIGN_RIGHT);
  }
}

static void drawGames(void) {
  section(4, 4, BOT_W - 8, 200, "GAMES TODAY");
  if (nGames == 0) {
    textWrap(14, 30, BOT_W - 30, "No games yet today. The burrow's nice and quiet, dig dig!",
             C_MUTED, -1);
    return;
  }
  const int rowH = 54, top = 24, viewH = 176;
  int maxScroll = nGames * rowH - viewH;
  if (scrollY > maxScroll) scrollY = maxScroll > 0 ? maxScroll : 0;
  clipBottom(5, top, BOT_W - 10, viewH + 2);
  for (int i = 0; i < nGames; i++) {
    int y = top + i * rowH - scrollY;
    if (y + rowH < top || y > top + viewH) continue;
    if (i == cursor) rect(10, y, BOT_W - 20, rowH - 2, C_CREAM_D);
    const C2D_Image* icon = titleIcon(games[i].tid);
    if (icon)
      C2D_DrawImageAt(*icon, 12, y + 2, 0.5f, NULL, 1, 1);
    else
      box(12, y + 2, 48, 48, C_DIRT, false);
    char name[40];
    snprintf(name, sizeof name, "%.26s", titleName(games[i].tid));
    text(66, y + 8, name, C_INK, ALIGN_LEFT);
    char buf[48];
    snprintf(buf, sizeof buf, "%d min, %d launch%s", games[i].minutes, games[i].launches,
             games[i].launches == 1 ? "" : "es");
    text(66, y + 24, buf, C_MUTED, ALIGN_LEFT);
    bool on = watched(games[i].tid);
    text(BOT_W - 52, y + 38, on ? "counts" : "free", C_MUTED, ALIGN_CENTER);
    toggle(BOT_W - 68, y + 14, on, 2);
    if (hit(BOT_W - 72, y + 8, 60, 30) && y >= top) {
      cursor = i;
      if (on && settings.skipped < 32) settings.skip[settings.skipped++] = games[i].tid;
      else if (!on)
        for (int k = 0; k < settings.skipped; k++)
          if (settings.skip[k] == games[i].tid) settings.skip[k] = settings.skip[--settings.skipped];
      sfx(on ? SFX_TOGGLE_OFF : SFX_TOGGLE_ON);
      settingsSave();
      refresh(false);
    }
  }
  noClip();
}

// ------------------------------------------------------------------ rules ----
static const char* DAY_SHORT[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
static const char* DAY_NAME[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                  "Saturday", "Sunday"};
static int scope = -1;           // -1 = every day, 0..6 = Monday..Sunday
static int rulesPlugin = -1;     // is the plugin on the SD card? (checked once per visit)
static bool rulesDirty;
static u32 rulesChangedAt;
static int askRow = -1;          // a number pad to open after this frame

typedef enum { R_LIMIT, R_SITTING, R_BEDTIME, R_BLOCK, R_OWN } RuleRow;

// The last game the plugin started in, from the "start" lines it writes to
// plugin.log (0 = never), so the rules can say whether it's really running.
static u64 pluginLastTid;
static s64 pluginLastAt;

static bool pluginInstalled(void) {
  if (rulesPlugin < 0) {
    FILE* f = fopen("sdmc:/luma/plugins/default.3gx", "r");
    rulesPlugin = f != NULL;
    if (f) fclose(f);
    pluginLastTid = 0, pluginLastAt = 0;
    f = fopen(DATA_DIR "/plugin.log", "r");
    if (f) {
      char line[160];
      while (fgets(line, sizeof line, f)) {
        long long at;
        unsigned long long tid;
        if (sscanf(line, "%lld start %llx", &at, &tid) == 2) pluginLastAt = at, pluginLastTid = tid;
      }
      fclose(f);
    }
  }
  return rulesPlugin;
}

// Games that have their own plugin (a /luma/plugins/<title ID>/ folder with a
// .3gx): Luma loads that one there instead of Mole.
static int otherPlugins = -1;

static int countOtherPlugins(void) {
  if (otherPlugins >= 0) return otherPlugins;
  otherPlugins = 0;
  DIR* d = opendir("sdmc:/luma/plugins");
  if (!d) return 0;
  struct dirent* e;
  while ((e = readdir(d))) {
    if (strlen(e->d_name) != 16 || strspn(e->d_name, "0123456789ABCDEFabcdef") != 16) continue;
    char path[64];
    snprintf(path, sizeof path, "sdmc:/luma/plugins/%s", e->d_name);
    DIR* g = opendir(path);
    if (!g) continue;
    struct dirent* f;
    while ((f = readdir(g))) {
      const char* dot = strrchr(f->d_name, '.');
      if (dot && !strcasecmp(dot, ".3gx")) {
        otherPlugins++;
        break;
      }
    }
    closedir(g);
  }
  closedir(d);
  return otherPlugins;
}

static void pluginStatus(char* buf, int n) {
  if (pluginInstalled() && pluginLastTid && countOtherPlugins()) {
    // Another plugin wins in those games; their time still counts (Activity Log).
    snprintf(buf, n, "running; %d game%s use%s another plugin", otherPlugins,
             otherPlugins == 1 ? "" : "s", otherPlugins == 1 ? "s" : "");
    return;
  }
  if (!pluginInstalled()) {
    snprintf(buf, n, "needs Mole's Luma plugin");
  } else if (!pluginLastTid) {
    snprintf(buf, n, "installed, hasn't run yet: turn on Luma's plugin loader");
  } else {
    time_t t = (time_t)pluginLastAt;
    struct tm* tm = gmtime(&t);
    snprintf(buf, n, "last watched %.18s, %02d/%02d %02d:%02d", titleName(pluginLastTid), tm->tm_mday,
             tm->tm_mon + 1, tm->tm_hour, tm->tm_min);
  }
}

static void saveRules(void) {
  if (!rulesDirty) return;
  rulesDirty = false;
  settingsSave();
  refresh(false);
}

static void changed(void) {
  rulesDirty = true;
  rulesChangedAt = tick;
}

static void fmtMinutes(char* buf, int n, int m) {
  if (m >= LIMIT_MAX) snprintf(buf, n, "no limit");
  else if (m < 60) snprintf(buf, n, "%d min", m);
  else if (m % 60 == 0) snprintf(buf, n, "%d h", m / 60);
  else snprintf(buf, n, "%d h %d min", m / 60, m % 60);
}

// The rows on screen for the current scope.
static int ruleRows(RuleRow* rows) {
  int n = 0;
  if (scope < 0) {
    rows[n++] = R_LIMIT, rows[n++] = R_SITTING, rows[n++] = R_BEDTIME, rows[n++] = R_BLOCK;
  } else if (settings.days[scope].own) {
    rows[n++] = R_LIMIT, rows[n++] = R_BEDTIME, rows[n++] = R_OWN;
  } else {
    rows[n++] = R_OWN;
  }
  return n;
}

static int* limitRef(void) { return scope < 0 ? &settings.limit : &settings.days[scope].limit; }
static int* bedRef(void) { return scope < 0 ? &settings.bedtimeHour : &settings.days[scope].bedtime; }

static void stricter(const char* what) {
  if (!settings.tightened) {
    settings.tightened = true;
    freshTrophies |= trophiesCheck(clock_());
  }
  char line[200];
  snprintf(line, sizeof line, "A stricter %s, and all on your own! Most folks only ever try to "
           "loosen 'em, dig dig.", what);
  if (strcmp(line, lines[lineAt])) say1(line);
}

static void looser(const char* line) {
  if (strcmp(line, lines[lineAt])) say1(line);
}

// Bedtime hours run evening to small hours: 18:00 ... 23:00, 00:00 ... 04:00.
static int bedOrder(int h) { return h < 5 ? h + 24 : h; }

static void step(RuleRow r, int dir, int size) {
  sfx(SFX_TICK);
  if (r == R_LIMIT || r == R_SITTING) {
    int* v = r == R_LIMIT ? limitRef() : &settings.sessionLimit;
    int max = r == R_LIMIT ? LIMIT_MAX : SITTING_MAX;
    int nv = (*v / size) * size + (dir > 0 ? size : (*v % size ? 0 : -size));
    if (nv < 5) nv = 5;
    if (nv > max) nv = max;
    if (nv == *v) return;
    *v = nv;
    if (dir < 0) stricter(r == R_LIMIT ? "limit" : "sitting limit");
    else looser("More minutes, huh? Okay... but I'll be keeping an eye on you, dig dig.");
  } else if (r == R_BEDTIME) {
    int* h = bedRef();
    int o = bedOrder(*h) + dir;
    if (o < 18 || o > 28) return;
    *h = o % 24;
    if (dir < 0) stricter("bedtime");
    else looser("Staying up later, huh? Even moles need their sleep, you know!");
  }
  changed();
}

static void toggleRule(RuleRow r) {
  if (r == R_BEDTIME && scope < 0) {
    settings.bedtimeOn = !settings.bedtimeOn;
    sfx(settings.bedtimeOn ? SFX_TOGGLE_ON : SFX_TOGGLE_OFF);
    if (settings.bedtimeOn) stricter("bedtime");
  } else if (r == R_BLOCK) {
    settings.block = !settings.block;
    sfx(settings.block ? SFX_TOGGLE_ON : SFX_TOGGLE_OFF);
    if (settings.block)
      say1(pluginInstalled()
               ? "Deal! When your minutes run out or it's bedtime, I'll pop up in the game and "
                 "close the burrow gates, dig dig."
               : "Deal! Just copy my plugin to /luma/plugins/default.3gx and turn on Luma's "
                 "plugin loader, so I can get into your games.");
    else
      say1("No stopping, just watching? Alright, but I'll still grumble when you go over.");
  } else if (r == R_OWN && scope >= 0) {
    DayRules* d = &settings.days[scope];
    char line[200];
    if (d->own) {
      d->own = false;
      sfx(SFX_TOGGLE_OFF);
      snprintf(line, sizeof line, "Okay, %s goes back to the everyday rules.", DAY_NAME[scope]);
    } else {
      *d = (DayRules){true, settings.limit, settings.bedtimeHour};
      sfx(SFX_TOGGLE_ON);
      snprintf(line, sizeof line, "%s gets its own rules! Set its limit and bedtime, and I'll "
               "stick to 'em that day, dig dig.", DAY_NAME[scope]);
    }
    say1(line);
  } else {
    return;
  }
  changed();
}

static void setScope(int s) {
  if (s == scope) return;
  scope = s;
  cursor = 0;
  sfx(SFX_TAP);
  if (s < 0) {
    say1("These are the everyday rules. Tap a day up top to give it its own, dig dig!");
  } else {
    char line[200];
    snprintf(line, sizeof line, settings.days[s].own
             ? "%s has its own rules. Change 'em, or send it back to the everyday ones."
             : "%s follows the everyday rules right now. Want it to have its own?",
             DAY_NAME[s]);
    say1(line);
  }
}

// The number pad (the 3DS's own keyboard applet); opened between frames.
static void askNumber(void) {
  RuleRow rows[5];
  int n = ruleRows(rows);
  if (askRow < 0 || askRow >= n) return;
  RuleRow r = rows[askRow];
  askRow = -1;
  if (r != R_LIMIT && r != R_SITTING) return;
  int* v = r == R_LIMIT ? limitRef() : &settings.sessionLimit;
  int max = r == R_LIMIT ? LIMIT_MAX : SITTING_MAX;
  SwkbdState kb;
  swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 4);
  swkbdSetHintText(&kb, r == R_LIMIT ? "Minutes a day (1440 = no limit)" : "Minutes in one go");
  swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
  char init[8], out[8];
  snprintf(init, sizeof init, "%d", *v);
  swkbdSetInitialText(&kb, init);
  if (swkbdInputText(&kb, out, sizeof out) != SWKBD_BUTTON_CONFIRM) return;
  int nv = atoi(out);
  if (nv < 1) nv = 1;
  if (nv > max) nv = max;
  if (nv == *v) return;
  bool down = nv < *v;
  *v = nv;
  if (down) stricter(r == R_LIMIT ? "limit" : "sitting limit");
  else looser("More minutes, huh? Okay... but I'll be keeping an eye on you, dig dig.");
  changed();
  saveRules();
}

// Held - and + buttons repeat, then speed up: 5 minutes a step, 15 after a
// couple of seconds, 60 after four.
static int heldRow = -1, heldDir;
static u32 heldAt;

static void stepper(int row, RuleRow r, int y, const char* value, bool enabled) {
  const int mx = 166, px = 286, w = 26;
  u32 ink = enabled ? C_INK : C_MUTED;
  bool downM = touching && heldRow == row && heldDir < 0, downP = touching && heldRow == row && heldDir > 0;
  box(mx, y - 3, w, 20, enabled ? C_CREAM : C_CREAM_D, downM);
  text(mx + w / 2, y + 2 + downM, "-", ink, ALIGN_CENTER);
  box(px, y - 3, w, 20, enabled ? C_CREAM : C_CREAM_D, downP);
  text(px + w / 2, y + 2 + downP, "+", ink, ALIGN_CENTER);
  text((mx + w + px) / 2, y + 2, value, ink, ALIGN_CENTER);
  if (!enabled) return;
  if (hit(mx - 4, y - 6, w + 8, 26)) heldRow = row, heldDir = -1, heldAt = tick, step(r, -1, 5);
  if (hit(px - 4, y - 6, w + 8, 26)) heldRow = row, heldDir = 1, heldAt = tick, step(r, 1, 5);
  if ((r == R_LIMIT || r == R_SITTING) && hit(mx + w + 4, y - 6, px - mx - w - 8, 26)) {
    cursor = row;
    askRow = row;
    sfx(SFX_TAP);
  }
}

static void holdRepeat(void) {
  if (heldRow < 0) return;
  if (!touching) {
    heldRow = -1;
    return;
  }
  RuleRow rows[5];
  if (heldRow >= ruleRows(rows)) return;
  u32 t = tick - heldAt;
  if (t < 24 || (t - 24) % 5) return;  // a pause, then about 12 steps a second
  int size = t > 240 ? 60 : t > 120 ? 15 : 5;
  step(rows[heldRow], heldDir, rows[heldRow] == R_BEDTIME ? 1 : size);
}

static void drawRules(void) {
  section(4, 4, BOT_W - 8, 200, "YOUR RULES");
  pluginInstalled();
  // The days: EVERY DAY, then Monday to Sunday (a dot marks days with their own rules).
  box(10, 22, 72, 20, scope < 0 ? C_BAND : C_CREAM, scope < 0);
  text(46, 27 + (scope < 0), "EVERY DAY", scope < 0 ? C_CREAM : C_MUTED, ALIGN_CENTER);
  if (hit(10, 20, 72, 24)) setScope(-1);
  for (int d = 0; d < 7; d++) {
    int x = 86 + d * 32;
    bool on = scope == d;
    box(x, 22, 30, 20, on ? C_BAND : C_CREAM, on);
    text(x + 15, 27 + on, DAY_SHORT[d], on ? C_CREAM : C_MUTED, ALIGN_CENTER);
    if (settings.days[d].own) rect(x + 13, 37 + on, 4, 2, on ? C_CREAM : C_GOOD);
    if (hit(x, 20, 30, 24)) setScope(d);
  }

  RuleRow rows[5];
  int n = ruleRows(rows);
  char buf[48];
  for (int i = 0; i < n; i++) {
    int y = 56 + i * 37;
    if (cursor > 0 && i == cursor - 1) rect(9, y - 6, BOT_W - 18, 34, C_CREAM_D);
    switch (rows[i]) {
      case R_LIMIT:
        text(14, y, "Daily limit", C_INK, ALIGN_LEFT);
        text(14, y + 14, "tap the time to type it", C_MUTED, ALIGN_LEFT);
        fmtMinutes(buf, sizeof buf, *limitRef());
        stepper(i, R_LIMIT, y + 4, buf, true);
        break;
      case R_SITTING:
        text(14, y, "Long sitting", C_INK, ALIGN_LEFT);
        text(14, y + 14, "one go before it's a lot", C_MUTED, ALIGN_LEFT);
        fmtMinutes(buf, sizeof buf, settings.sessionLimit);
        stepper(i, R_SITTING, y + 4, buf, true);
        break;
      case R_BEDTIME:
        text(14, y, scope < 0 ? "Bedtime" : "Bedtime that night", C_INK, ALIGN_LEFT);
        if (scope < 0) {
          toggle(14, y + 13, settings.bedtimeOn, 1);
          text(36, y + 14, settings.bedtimeOn ? "until 05:00" : "off", C_MUTED, ALIGN_LEFT);
          if (hit(10, y + 10, 70, 16)) toggleRule(R_BEDTIME);
        } else {
          text(14, y + 14, settings.bedtimeOn ? "until 05:00" : "bedtime is off", C_MUTED, ALIGN_LEFT);
        }
        snprintf(buf, sizeof buf, "%02d:00", *bedRef());
        stepper(i, R_BEDTIME, y + 4, buf, settings.bedtimeOn);
        break;
      case R_BLOCK:
        text(14, y, "Stop games at the limit and bedtime", C_INK, ALIGN_LEFT);
        pluginStatus(buf, sizeof buf);
        text(14, y + 14, buf, pluginInstalled() && pluginLastTid ? C_GOOD : C_MUTED, ALIGN_LEFT);
        toggle(BOT_W - 48, y + 2, settings.block, 2);
        if (hit(BOT_W - 52, y - 2, 44, 28)) cursor = i + 1, toggleRule(R_BLOCK);
        break;
      case R_OWN:
        if (settings.days[scope].own) {
          box(BOT_W / 2 - 80, y - 2, 160, 22, C_CREAM, false);
          text(BOT_W / 2, y + 4, "FOLLOW EVERY DAY", C_MUTED, ALIGN_CENTER);
          if (hit(BOT_W / 2 - 80, y - 4, 160, 26)) toggleRule(R_OWN);
        } else {
          snprintf(buf, sizeof buf, "%s follows the everyday rules.", DAY_NAME[scope]);
          text(BOT_W / 2, y + 6, buf, C_MUTED, ALIGN_CENTER);
          box(BOT_W / 2 - 100, y + 30, 200, 28, C_BAND, false);
          snprintf(buf, sizeof buf, "GIVE %s ITS OWN RULES", DAY_SHORT[scope]);
          text(BOT_W / 2, y + 39, buf, C_CREAM, ALIGN_CENTER);
          if (hit(BOT_W / 2 - 100, y + 26, 200, 34)) toggleRule(R_OWN);
        }
        break;
    }
  }
}

// D-pad on the rules: up and down pick a row (0 is the day strip), left and
// right change it (held, they repeat).
static void rulesInput(u32 down, u32 repeat) {
  RuleRow rows[5];
  int n = ruleRows(rows);
  if (down & KEY_DOWN && cursor < n) cursor++, sfx(SFX_TAP);
  if (down & KEY_UP && cursor > 0) cursor--, sfx(SFX_TAP);
  int dir = repeat & KEY_RIGHT ? 1 : repeat & KEY_LEFT ? -1 : 0;
  if (cursor == 0) {
    if (dir) setScope(scope + dir < -1 ? 6 : scope + dir > 6 ? -1 : scope + dir);
    return;
  }
  RuleRow r = rows[cursor - 1];
  if (dir && (r == R_LIMIT || r == R_SITTING || (r == R_BEDTIME && settings.bedtimeOn))) step(r, dir, r == R_BEDTIME ? 1 : 5);
  if (down & KEY_A) {
    if (r == R_LIMIT || r == R_SITTING) askRow = cursor - 1;
    else if (r == R_BEDTIME || r == R_BLOCK || r == R_OWN) toggleRule(r);
  }
}

static void sayTrophy(int i) {
  const Trophy* t = &trophyList[i];
  bool earned = settings.trophies & (1u << i);
  char line[220];
  if (earned) {
    snprintf(line, sizeof line, "%s! %s", t->name, t->cheer);
  } else if (t->secret) {
    snprintf(line, sizeof line, "Ooh, that one's a secret! Keep at it and maybe you'll find out "
             "what's in the box, dig dig.");
  } else if (t->goal) {
    snprintf(line, sizeof line, "%s: %s You're at %d of %d so far!", t->name, t->how,
             trophyProgress(i, now) < t->goal ? trophyProgress(i, now) : t->goal, t->goal);
  } else {
    snprintf(line, sizeof line, "%s: %s", t->name, t->how);
  }
  say1(line);
}

static void drawTrophies(void) {
  section(4, 4, BOT_W - 8, 200, "TROPHY SHELF");
  int earned = 0;
  for (int i = 0; i < TROPHIES; i++) earned += (settings.trophies >> i) & 1;
  char buf[16];
  snprintf(buf, sizeof buf, "%d/%d", earned, TROPHIES);
  text(BOT_W - 14, 11, buf, C_MUTED, ALIGN_RIGHT);
  const int cell = 100, top = 22, viewH = 180;
  int rows = (TROPHIES + 2) / 3, maxScroll = rows * cell - viewH;
  int want = (cursor / 3) * cell;
  if (want < scrollY) scrollY = want;
  if (want + cell > scrollY + viewH) scrollY = want + cell - viewH;
  if (scrollY > maxScroll) scrollY = maxScroll;
  clipBottom(5, top, BOT_W - 10, viewH);
  for (int i = 0; i < TROPHIES; i++) {
    int x = 10 + (i % 3) * cell, y = top + (i / 3) * cell - scrollY;
    if (y + cell < top || y > top + viewH) continue;
    bool got = settings.trophies & (1u << i);
    if (i == cursor) {
      rect(x, y, 96, 96, C_CREAM_D);
    }
    int idx = got ? i * 2 : trophyList[i].secret ? TROPHIES * 2 : i * 2 + 1;
    image(sheetTrophies, idx, x, y);
    if (hit(x, y, 96, 96) && y >= top) cursor = i, sayTrophy(i), sfx(SFX_TAP);
  }
  noClip();
}

static void drawTabs(void) {
  const int w = BOT_W / TAB_COUNT, y = 210;
  for (int i = 0; i < TAB_COUNT; i++) {
    bool on = i == (int)tab;
    box(i * w + 2, y, w - 4, 28, on ? C_BAND : C_CREAM, on);
    text(i * w + w / 2, y + 9 + (on ? 1 : 0), TAB_NAMES[i], on ? C_CREAM : C_MUTED, ALIGN_CENTER);
  }
}

// Tabs slide across like pages: 14 frames at 60 fps, on whole pixels.
#define SLIDE_TICKS 14
static Tab slideFrom;
static int slideDir;
static u32 slideAt = ~0u;

static bool sliding(void) { return slideAt != ~0u && tick - slideAt < SLIDE_TICKS; }

static void switchTabDir(Tab t, int dir) {
  if (t == tab || sliding()) return;
  slideFrom = tab, slideDir = dir, slideAt = tick;
  sfx(SFX_TAB);
  if (tab == TAB_RULES) saveRules();
  if (t == TAB_RULES) rulesPlugin = -1, otherPlugins = -1, scope = -1, heldRow = -1;
  tab = t, scrollY = 0, cursor = 0;
  if (t == TAB_TROPHIES) sayTrophy(0);
  else if (t == TAB_RULES) say1("These are our rules. Tap a day up top to give it its own, "
                                "and I'll keep you to them, dig dig!");
  else say(mole.count, mole.lines);
}

static void switchTab(Tab t) { switchTabDir(t, t > tab ? 1 : -1); }

static void drawTab(Tab t) {
  switch (t) {
    case TAB_TODAY: drawToday(); break;
    case TAB_GAMES: drawGames(); break;
    case TAB_RULES: drawRules(); break;
    case TAB_TROPHIES: drawTrophies(); break;
    default: break;
  }
}

static void drawDance(void) {
  const Trophy* t = &trophyList[danceList[danceAt]];
  section(4, 4, BOT_W - 8, 200, "NEW TROPHY!");
  if (danceN > 1) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d/%d", danceAt + 1, danceN);
    text(BOT_W - 14, 11, buf, C_MUTED, ALIGN_RIGHT);
  }
  image(sheetTrophies, danceList[danceAt] * 2, 12, 24);
  text(116, 40, t->name, C_INK, ALIGN_LEFT);
  textWrap(116, 58, BOT_W - 132, t->how, C_MUTED, -1);
  textWrap(14, 126, BOT_W - 30, lines[lineAt], C_INK, typed);
  bool last = danceAt + 1 >= danceN;
  box(BOT_W / 2 - 70, 208, 140, 28, C_BAND, touching && tapY >= 204);
  text(BOT_W / 2, 217, last ? "A  YAY!" : "A  NEXT ONE", C_CREAM, ALIGN_CENTER);
  if (hit(BOT_W / 2 - 74, 204, 148, 36)) danceNext();
}

static void drawBottom(void) {
  paper(BOT_W, tick, 0);
  if (title) {
    if (titleTick() < PROMPT_AT) {
      if (tapped) titleAt = tick - PROMPT_AT, titleLanded = titleHopped = true, homePlay();
      return;
    }
    section(20, 60, BOT_W - 40, 100, "HIYA!");
    textWrap(32, 84, BOT_W - 64, "I'm Mole! I keep an eye on how long you play, so games stay "
             "fun and don't eat your whole day.", C_INK, -1);
    if (tapped) leaveTitle();
    return;
  }
  if (scene == SCENE_DANCE) {
    drawDance();
    return;
  }
  if (sliding()) {
    float p = easeInOut((tick - slideAt) / (float)SLIDE_TICKS);
    int off = (int)(p * BOT_W + 0.5f);
    tapped = false;
    drawDX = -slideDir * off;
    drawTab(slideFrom);
    drawDX = slideDir * (BOT_W - off);
    drawTab(tab);
    drawDX = 0;
  } else {
    drawTab(tab);
  }
  drawTabs();
  for (int i = 0; i < TAB_COUNT; i++)
    if (hit(i * (BOT_W / TAB_COUNT), 206, BOT_W / TAB_COUNT, 34)) switchTab(i);
}

// ------------------------------------------------------------------ input ----
static void input(u32 down, u32 held, u32 repeat) {
  if (title) {
    if (down & (KEY_A | KEY_START)) leaveTitle();
    return;
  }
  if (scene == SCENE_DANCE) {
    if (down & KEY_A) danceNext();
    return;
  }
  if (scene == SCENE_BURST || scene == SCENE_PAN) {
    if (down & KEY_A) {  // skip ahead to Mole
      animClose(&burst);
      scene = SCENE_MOLE, boxAt = tick;
    }
    return;
  }
  if (down & KEY_A && tab != TAB_RULES && boxReady()) {
    if (dialogueDone()) poke(mole.mood);
    dialogueNext();
  }
  // X pokes Mole on any tab.
  if (down & KEY_X && boxReady()) poke(mole.mood);
  if (down & KEY_R) switchTabDir((tab + 1) % TAB_COUNT, 1);
  if (down & KEY_L) switchTabDir((tab + TAB_COUNT - 1) % TAB_COUNT, -1);
  if (tab == TAB_RULES) {
    if (!sliding()) rulesInput(down, repeat);
    return;
  }
  int rows = tab == TAB_GAMES ? nGames : tab == TAB_TROPHIES ? TROPHIES : 0;
  int step = tab == TAB_TROPHIES ? 3 : 1;
  int old = cursor;
  if (down & KEY_DOWN && cursor + step < rows) cursor += step;
  if (down & KEY_UP && cursor - step >= 0) cursor -= step;
  if (tab == TAB_TROPHIES) {
    if (down & KEY_RIGHT && cursor + 1 < rows) cursor++;
    if (down & KEY_LEFT && cursor > 0) cursor--;
    if (cursor != old) sayTrophy(cursor), sfx(SFX_TAP);
  } else if (tab == TAB_GAMES && cursor != old) {
    int y = cursor * 54;
    if (y < scrollY) scrollY = y;
    if (y + 54 > scrollY + 176) scrollY = y + 54 - 176;
  }
}

int main(void) {
  romfsInit();
  gfxInitDefault();
  gfxSet3D(true);
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
  C2D_Prepare();
  C3D_RenderTarget* topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  C3D_RenderTarget* topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
  C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

  hidSetRepeatParameters(24, 5);
  drawInit();
  soundInit();
  titlesInit();
  settingsLoad();
  if (!settings.installDay) {
    settings.installDay = dayStart(clock_());
    settingsSave();
  }
  playlogLoad();
  refresh(true);
  moodLoad(mole.mood);
  animOpen(&happy, "happy");

  u32 lastRefresh = 0;
  while (aptMainLoop()) {
    hidScanInput();
    u32 down = hidKeysDown(), held = hidKeysHeld(), repeat = hidKeysDownRepeat();
    if (down & KEY_START && !title) break;
    hidTouchRead(&touch);
    bool wasTouching = touching;
    touching = held & KEY_TOUCH;
    tapped = touching && !wasTouching;
    if (tapped) tapX = touch.px, tapY = touch.py;
    input(down, held, repeat);
    if (tab == TAB_RULES) holdRepeat();
    if (rulesDirty && tick - rulesChangedAt > 45 && heldRow < 0) saveRules();

    // Dragging scrolls the lists.
    static int lastY;
    if (touching && wasTouching && (tab == TAB_GAMES || tab == TAB_TROPHIES)) {
      scrollY -= touch.py - lastY;
      if (scrollY < 0) scrollY = 0;
    }
    lastY = touch.py;

    if (tick - lastRefresh > 60 * 30) {
      lastRefresh = tick;
      playlogLoad();
      Mood before = mole.mood;
      refresh(false);
      if (mole.mood != before && scene == SCENE_MOLE && !title) enterMole();
    }
    if (freshTrophies && !title && scene == SCENE_MOLE) {
      startDance(freshTrophies);
      freshTrophies = 0;
    }
    if (moodLoaded() != (int)mole.mood && scene != SCENE_BURST) moodLoad(mole.mood);
    updateScene();
    if (title) updateTitle();
    if (title) {
      // nothing talks on the title screen
    } else if (scene == SCENE_DANCE) {
      typeOn(MOOD_PROUD);
    } else if (boxReady()) {
      typeOn(mole.mood);
    }

    float depth = osGet3DSliderState();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(topL, C_PAPER);
    C2D_SceneBegin(topL);
    drawTop(0, depth);
    if (depth > 0) {
      C2D_TargetClear(topR, C_PAPER);
      C2D_SceneBegin(topR);
      drawTop(1, depth);
    }
    C2D_TargetClear(bot, C_PAPER);
    C2D_SceneBegin(bot);
    drawBottom();
    C3D_FrameEnd(0);
    tick++;
    if (askRow >= 0) askNumber();  // the keyboard applet runs between frames
  }

  saveRules();
  musicStop();
  animClose(&burst);
  animClose(&dance);
  animClose(&happy);
  titlesExit();
  soundExit();
  drawExit();
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  romfsExit();
  return 0;
}
