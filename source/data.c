#include "data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "titles.h"

Settings settings;
PlayLog playlog;

// ------------------------------------------------------------- settings ----
// A plain key=value file so the plugin (and people) can read it.
#define SETTINGS DATA_DIR "/settings.ini"

void settingsLoad(void) {
  // The phone app's defaults: an hour a day, bedtime at 23:00.
  settings = (Settings){.limit = 60, .sessionLimit = 30, .bedtimeOn = true, .bedtimeHour = 23,
                        .block = true};
  FILE* f = fopen(SETTINGS, "r");
  if (!f) return;
  char line[128];
  while (fgets(line, sizeof line, f)) {
    char key[32];
    long long v;
    if (sscanf(line, "%31[^=]=%lli", key, &v) != 2) continue;
    if (!strcmp(key, "limit")) settings.limit = v;
    else if (!strcmp(key, "session_limit")) settings.sessionLimit = v;
    else if (!strcmp(key, "bedtime_on")) settings.bedtimeOn = v;
    else if (!strcmp(key, "bedtime_hour")) settings.bedtimeHour = v;
    else if (!strcmp(key, "block")) settings.block = v;
    else if (!strcmp(key, "install_day")) settings.installDay = v;
    else if (!strcmp(key, "trophies")) settings.trophies = v;
    else if (!strcmp(key, "walk_aways")) settings.walkAways = v;
    else if (!strcmp(key, "tightened")) settings.tightened = v;
    else if (!strcmp(key, "cleaned")) settings.cleaned = v;
    else if (!strncmp(key, "earned_", 7)) {
      int i = atoi(key + 7);
      if (i >= 0 && i < 16) settings.earnedAt[i] = v;
    } else if (!strncmp(key, "day_", 4)) {
      int d = atoi(key + 4), lim, bed;
      if (d >= 0 && d < 7 && sscanf(line, "%*[^=]=%d,%d", &lim, &bed) == 2)
        settings.days[d] = (DayRules){true, lim, bed};
    } else if (!strcmp(key, "skip") && settings.skipped < 32) {
      unsigned long long tid;
      if (sscanf(line, "skip=%llx", &tid) == 1) settings.skip[settings.skipped++] = tid;
    }
  }
  fclose(f);
  // The plugin logs each "Okay, I'm done" on its block screen.
  f = fopen(DATA_DIR "/events.log", "r");
  if (f) {
    int walks = 0;
    while (fgets(line, sizeof line, f))
      if (!strncmp(line, "walk ", 5)) walks++;
    fclose(f);
    if (walks > settings.walkAways) settings.walkAways = walks;
  }
}

void settingsSave(void) {
  mkdir("sdmc:/3ds", 0777);
  mkdir(DATA_DIR, 0777);
  FILE* f = fopen(SETTINGS, "w");
  if (!f) return;
  fprintf(f, "# ControlMole for 3DS. The plugin reads limit, block and skip.\n");
  fprintf(f, "limit=%d\nsession_limit=%d\nbedtime_on=%d\nbedtime_hour=%d\nblock=%d\n",
          settings.limit, settings.sessionLimit, settings.bedtimeOn, settings.bedtimeHour,
          settings.block);
  fprintf(f, "install_day=%lld\ntrophies=%lu\nwalk_aways=%d\ntightened=%d\ncleaned=%d\n",
          settings.installDay, settings.trophies, settings.walkAways, settings.tightened,
          settings.cleaned);
  for (int i = 0; i < 16; i++)
    if (settings.earnedAt[i]) fprintf(f, "earned_%d=%lld\n", i, settings.earnedAt[i]);
  for (int i = 0; i < settings.skipped; i++)
    fprintf(f, "skip=%016llX\n", settings.skip[i]);
  // Days with their own rules: day_<Monday=0>=limit,bedtime hour.
  for (int d = 0; d < 7; d++)
    if (settings.days[d].own)
      fprintf(f, "day_%d=%d,%d\n", d, settings.days[d].limit, settings.days[d].bedtime);
  fclose(f);
}

bool watched(u64 tid) {
  for (int i = 0; i < settings.skipped; i++)
    if (settings.skip[i] == tid) return false;
  return true;
}

s64 dayStart(s64 t) { return t - ((t % 86400) + 86400) % 86400; }

int weekday(s64 day) { return (int)(((dayStart(day) / 86400) % 7 + 7 + 3) % 7); }  // 1970-01-01 was a Thursday

int limitOn(s64 day) {
  const DayRules* r = &settings.days[weekday(day)];
  return r->own ? r->limit : settings.limit;
}

int bedtimeAt(s64 now) {
  int h = (int)((now - dayStart(now)) / 3600);
  const DayRules* r = &settings.days[weekday(h < 5 ? now - 86400 : now)];
  return r->own ? r->bedtime : settings.bedtimeHour;
}

// ------------------------------------------------------------- behaviour ----
static s64 lo(s64 a, s64 b) { return a < b ? a : b; }
static s64 hi(s64 a, s64 b) { return a > b ? a : b; }

static int cmpS64(const void* a, const void* b) {
  s64 x = *(const s64*)a, y = *(const s64*)b;
  return x < y ? -1 : x > y;
}

void today(Today* t, s64 day, s64 now) {
  memset(t, 0, sizeof *t);
  s64 end = lo(day + 86400, now), longest = 0, used = 0;
  static s64 switches[MAX_SITTINGS];
  int nsw = 0;
  u64 last = 0;
  for (int i = 0; i < playlog.count; i++) {
    const Sitting* s = &playlog.s[i];
    s64 a = hi(s->start, day), b = lo(s->end, end);
    if (b <= a || !watched(s->tid)) continue;
    used += b - a;
    t->opens++;
    if (s->end - s->start < QUICK_CHECK_S) t->quickChecks++;
    if (b - a > longest) {
      longest = b - a;
      snprintf(t->longestGame, sizeof t->longestGame, "%s", titleName(s->tid));
    }
    if (last && s->tid != last) switches[nsw++] = s->start;
    last = s->tid;
  }
  t->used = used / 60;
  t->longestMin = longest / 60;
  // Bursts: windows of BURST_WINDOW_S with BURST_SWITCHES+ switches (Behaviour.kt).
  qsort(switches, nsw, sizeof(s64), cmpS64);
  int left = 0, maxLeft = 0;
  for (int i = 0; i < nsw; i++) {
    while (switches[i] - switches[left] > BURST_WINDOW_S) left++;
    while (switches[i] - switches[maxLeft] > BURST_WINDOW_S) maxLeft++;
    if (i - maxLeft + 1 > t->maxSwitches) t->maxSwitches = i - maxLeft + 1;
    int n = i - left + 1;
    if (n >= BURST_SWITCHES) {
      t->hopBursts++;
      t->lastBurstAt = switches[i];
      t->lastBurstSwitches = n;
      left = i + 1;
    }
  }
}

int minutesOn(s64 day, s64 now) {
  s64 end = lo(day + 86400, now), used = 0;
  for (int i = 0; i < playlog.count; i++) {
    const Sitting* s = &playlog.s[i];
    s64 a = hi(s->start, day), b = lo(s->end, end);
    if (b > a && watched(s->tid)) used += b - a;
  }
  return used / 60;
}

int gamesOn(s64 day, s64 now, GameRow* rows, int max) {
  int n = 0;
  s64 end = lo(day + 86400, now);
  static s64 secs[64];
  for (int i = 0; i < playlog.count; i++) {
    const Sitting* s = &playlog.s[i];
    s64 a = hi(s->start, day), b = lo(s->end, end);
    if (b <= a) continue;
    int j = 0;
    while (j < n && rows[j].tid != s->tid) j++;
    if (j == n) {
      if (n == max || n == 64) continue;
      rows[n] = (GameRow){s->tid, 0, 0};
      secs[n++] = 0;
    }
    secs[j] += b - a;
    rows[j].launches++;
  }
  for (int i = 0; i < n; i++) rows[i].minutes = secs[i] / 60;
  // Most played first.
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && rows[j].minutes > rows[j - 1].minutes; j--) {
      GameRow r = rows[j];
      rows[j] = rows[j - 1], rows[j - 1] = r;
    }
  return n;
}

// Days ControlMole was opened: sdmc:/3ds/ControlMole/seen.log, one day start a line.
#define SEEN_FILE DATA_DIR "/seen.log"
static s64 seen[512];
static int nSeen = -1;

static void seenLoad(void) {
  nSeen = 0;
  FILE* f = fopen(SEEN_FILE, "r");
  if (!f) return;
  long long d;
  while (nSeen < 512 && fscanf(f, "%lld", &d) == 1) seen[nSeen++] = d;
  fclose(f);
}

void seenToday(s64 now) {
  if (nSeen < 0) seenLoad();
  s64 d = dayStart(now);
  for (int i = 0; i < nSeen; i++)
    if (seen[i] == d) return;
  if (nSeen == 512) memmove(seen, seen + 1, 511 * sizeof(s64)), nSeen--;
  seen[nSeen++] = d;
  mkdir("sdmc:/3ds", 0777);
  mkdir(DATA_DIR, 0777);
  FILE* f = fopen(SEEN_FILE, "a");
  if (f) fprintf(f, "%lld\n", d), fclose(f);
}

static bool sawDay(s64 day) {
  if (nSeen < 0) seenLoad();
  for (int i = 0; i < nSeen; i++)
    if (seen[i] == day) return true;
  for (int i = 0; i < playlog.count; i++)
    if (playlog.s[i].end > day && playlog.s[i].start < day + 86400) return true;
  return false;
}

// Only days Mole watched in full count (the phone's DayLog.watched): from the
// day after install, the days he saw, at most a year back.
int pastDays(DayRec* out, int max, s64 now) {
  s64 todayStart = dayStart(now);
  s64 first = settings.installDay ? settings.installDay + 86400 : todayStart;
  if (first < todayStart - 366 * 86400LL) first = todayStart - 366 * 86400LL;
  int n = 0;
  for (s64 d = first; d < todayStart && n < max; d += 86400) {
    if (!sawDay(d)) continue;
    int m = minutesOn(d, now), lim = limitOn(d);
    out[n++] = (DayRec){d, m, lim, m < lim};
  }
  return n;
}

int streakNow(s64 now) {
  static DayRec days[400];
  int n = pastDays(days, 400, now), run = 0;
  // Finished days in a row under the limit, back from yesterday; a day Mole
  // didn't see ends it.
  for (int i = n - 1; i >= 0 && days[i].under; i--) {
    if (days[i].day != dayStart(now) - (s64)(n - i) * 86400) break;
    run++;
  }
  return run;
}
