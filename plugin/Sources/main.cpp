#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "gen_block.h"
#include "gen_sounds.h"

namespace CTRPluginFramework {

static const char* DIR = "/3ds/ControlMole";

// ------------------------------------------------------------------ rules ----
struct Rules {
  int limit = 60;
  bool block = true;
  bool bedtimeOn = true;
  int bedtimeHour = 23;
  // Weekdays with their own rules (Monday = 0): daily limit, bedtime that night.
  bool own[7] = {};
  int dayLimit[7] = {}, dayBed[7] = {};
  std::vector<u64> skip;
};

static const int NO_LIMIT = 24 * 60;

static std::string readAll(const std::string& path) {
  File f;
  if (File::Open(f, path, File::READ) != 0) return "";
  u64 n = f.GetSize();
  std::string s(n, '\0');
  if (n) f.Read(&s[0], n);
  f.Close();
  return s;
}

static Rules readRules() {
  Rules r;
  std::string s = readAll(std::string(DIR) + "/settings.ini");
  size_t at = 0;
  while (at < s.size()) {
    size_t e = s.find('\n', at);
    if (e == std::string::npos) e = s.size();
    std::string line = s.substr(at, e - at);
    at = e + 1;
    long long v;
    unsigned long long tid;
    if (sscanf(line.c_str(), "limit=%lld", &v) == 1) r.limit = v;
    else if (sscanf(line.c_str(), "block=%lld", &v) == 1) r.block = v;
    else if (sscanf(line.c_str(), "bedtime_on=%lld", &v) == 1) r.bedtimeOn = v;
    else if (sscanf(line.c_str(), "bedtime_hour=%lld", &v) == 1) r.bedtimeHour = v;
    else if (sscanf(line.c_str(), "skip=%llx", &tid) == 1) r.skip.push_back(tid);
    else {
      int d, lim, bed;
      if (sscanf(line.c_str(), "day_%d=%d,%d", &d, &lim, &bed) == 3 && d >= 0 && d < 7)
        r.own[d] = true, r.dayLimit[d] = lim, r.dayBed[d] = bed;
    }
  }
  return r;
}

static s64 now() { return (s64)time(nullptr); }  // the 3DS clock is local time
static s64 dayStart(s64 t) { return t - ((t % 86400) + 86400) % 86400; }

static int weekday(s64 t) { return (int)(((dayStart(t) / 86400) % 7 + 7 + 3) % 7); }  // 1970-01-01 was a Thursday

// Today's limit and tonight's bedtime, from the weekly schedule like the app.
static int limitToday(const Rules& r, s64 t) {
  int d = weekday(t);
  return r.own[d] ? r.dayLimit[d] : r.limit;
}

static int bedtimeTonight(const Rules& r, s64 t) {
  int h = (int)((t - dayStart(t)) / 3600);
  int d = weekday(h < 5 ? t - 86400 : t);  // after midnight it's still last evening's bedtime
  return r.own[d] ? r.dayBed[d] : r.bedtimeHour;
}

// Bedtime runs from the hour until 5 in the morning (app/lib/mole/mood.dart).
static bool afterBedtime(const Rules& r, s64 t) {
  if (!r.bedtimeOn) return false;
  int h = (int)((t - dayStart(t)) / 3600), bed = bedtimeTonight(r, t);
  return bed < 5 ? h >= bed && h < 5 : h >= bed || h < 5;
}

// Seconds already played today in counted games, from the log.
static s64 playedToday(const Rules& r) {
  std::string s = readAll(std::string(DIR) + "/plays.log");
  s64 d = dayStart(now()), total = 0;
  size_t at = 0;
  while (at < s.size()) {
    size_t e = s.find('\n', at);
    if (e == std::string::npos) e = s.size();
    unsigned long long tid;
    long long a, b;
    if (sscanf(s.c_str() + at, "%llx %lld %lld", &tid, &a, &b) == 3) {
      bool skipped = false;
      for (u64 k : r.skip) skipped |= k == tid;
      if (a < d) a = d;
      if (!skipped && b > a) total += b - a;
    }
    at = e + 1;
  }
  return total;
}

static bool usedGraceToday() {
  std::string s = readAll(std::string(DIR) + "/events.log");
  s64 d = dayStart(now());
  size_t at = 0;
  while ((at = s.find("grace ", at)) != std::string::npos) {
    long long t = 0;
    sscanf(s.c_str() + at, "grace %lld", &t);
    if (t >= d) return true;
    at += 6;
  }
  return false;
}

static void logEvent(const char* what) {
  File f;
  std::string path = std::string(DIR) + "/events.log";
  if (File::Open(f, path, File::WRITE | File::CREATE | File::APPEND) != 0) return;
  char line[48];
  int n = snprintf(line, sizeof line, "%s %lld\n", what, now());
  f.Write(line, n);
  f.Close();
}

// A step-by-step trace of the block screen in /3ds/ControlMole/plugin.log, so a
// crash on a real console says how far it got.
static void plog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void plog(const char* fmt, ...) {
  File f;
  if (File::Open(f, std::string(DIR) + "/plugin.log", File::WRITE | File::CREATE | File::APPEND | File::SYNC) != 0)
    return;
  char line[160];
  int n = snprintf(line, sizeof line, "%lld ", now());
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(line + n, sizeof line - n, fmt, ap);
  va_end(ap);
  if (n > (int)sizeof line - 2) n = sizeof line - 2;
  line[n++] = '\n';
  f.Write(line, n);
  f.Close();
}

// ---------------------------------------------------------------- the log ----
// One fixed-width line per sitting, rewritten in place while it runs.
struct Sitting {
  u64 tid;
  s64 start, end;
  s64 offset;
  Sitting(u64 tid = 0, s64 start = 0, s64 end = 0) : tid(tid), start(start), end(end), offset(-1) {}
};

static void writeSitting(Sitting& s) {
  Directory::Create("/3ds");
  Directory::Create(DIR);
  File f;
  std::string path = std::string(DIR) + "/plays.log";
  if (File::Open(f, path, File::RWC) != 0) return;
  if (s.offset < 0) s.offset = f.GetSize();
  char line[48];
  int n = snprintf(line, sizeof line, "%016llX %010lld %010lld\n", s.tid, s.start, s.end);
  f.Seek(s.offset, File::SET);
  f.Write(line, n);
  f.Close();
}

// --------------------------------------------------------------- drawing ----
static const u32 INK = 0x17142B, CREAM = 0xFBF3E2, PAPER = 0xE9E1CF, PAPER_D = 0xE1D6BF,
                 CHESTNUT = 0x8C5433, BAND = 0xE5483A, MUTED = 0x6E6680;

static u32 lerp(u32 a, u32 b, float t) {
  u32 out = 0;
  for (int sh = 0; sh < 24; sh += 8) {
    int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
    out |= ((u32)(ca + (cb - ca) * t + 0.5f) & 0xFF) << sh;
  }
  return out;
}

enum Fmt { F565, F5551, F4444, FBGR8, FABGR8 };

struct Fb {
  u8 *base, *right;
  u8 *start = nullptr, *rightStart = nullptr;  // the whole buffer in memory, and its size
  u32 bytes = 0;
  int dx, dy, bpp, w, h;
  Fmt fmt;

  // The same layout on another piece of memory (a snapshot of the screen).
  Fb(const Fb& like, u8* mem) : right(nullptr), dx(like.dx), dy(like.dy), bpp(like.bpp),
                                w(like.w), h(like.h), fmt(like.fmt) {
    base = like.base ? mem + (like.base - like.start) : nullptr;
    start = mem;
    bytes = like.bytes;
  }
  void span() {
    if (!base) return;
    u8* c[4] = {base, base + (w - 1) * dx, base + (h - 1) * dy, base + (w - 1) * dx + (h - 1) * dy};
    u8 *lo = c[0], *hi = c[0];
    for (u8* p : c) lo = p < lo ? p : lo, hi = p > hi ? p : hi;
    start = lo;
    bytes = hi - lo + bpp;
    if (right) rightStart = right - (base - start);
  }
  // Whole-screen copies: one memcpy instead of 96,000 pixels.
  void save(u8* to) const { if (base) memcpy(to, start, bytes); }
  void load(const u8* from) const {
    if (!base) return;
    memcpy(start, from, bytes);
    if (rightStart) memcpy(rightStart, from, bytes);
  }

  // Only addresses the process can really write to are used: on a real 3DS
  // the right-eye buffer of a 3D game isn't always one (writing it crashed).
  Fb(const Screen& s, int w, int h) : w(w), h(h) {
    u32 size = 0;
    base = s.LeftFramebuffer && Process::CheckAddress(s.LeftFramebuffer) ? s.GetFramebuffer(0, 0)
                                                                         : nullptr;
    right = nullptr;
    if (base && s.IsTop && s.Is3DEnabled && s.RightFramebuffer &&
        s.RightFramebuffer != s.LeftFramebuffer && Process::CheckRegion(s.RightFramebuffer, size) &&
        size >= 240 * 400 * s.BytesPerPixel)
      right = s.GetFramebuffer(0, 0, true);
    bpp = s.BytesPerPixel;
    dx = base ? s.GetFramebuffer(1, 0) - base : 0;
    dy = base ? s.GetFramebuffer(0, 1) - base : 0;
    switch (s.Format) {
      case GSP_RGB565_OES: fmt = F565; break;
      case GSP_RGB5_A1_OES: fmt = F5551; break;
      case GSP_RGBA4_OES: fmt = F4444; break;
      case GSP_BGR8_OES: fmt = FBGR8; break;
      default: fmt = FABGR8; break;
    }
    if (bpp < 2 || bpp > 4) base = right = nullptr;
    span();
  }
  bool ok() const { return base != nullptr; }
  u32 enc(u32 c) const {
    u32 r = c >> 16 & 0xFF, g = c >> 8 & 0xFF, b = c & 0xFF;
    switch (fmt) {
      case F565: return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
      case F5551: return (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1;
      case F4444: return (r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | 0xF;
      case FBGR8: return b | g << 8 | r << 16;
      default: return 0xFF | b << 8 | g << 16 | r << 24;
    }
  }
  u32 dec(u32 v) const {
    u32 r, g, b;
    switch (fmt) {
      case F565: r = (v >> 11 & 31) * 255 / 31, g = (v >> 5 & 63) * 255 / 63, b = (v & 31) * 255 / 31; break;
      case F5551: r = (v >> 11 & 31) * 255 / 31, g = (v >> 6 & 31) * 255 / 31, b = (v >> 1 & 31) * 255 / 31; break;
      case F4444: r = (v >> 12 & 15) * 17, g = (v >> 8 & 15) * 17, b = (v >> 4 & 15) * 17; break;
      case FBGR8: b = v & 0xFF, g = v >> 8 & 0xFF, r = v >> 16 & 0xFF; break;
      default: b = v >> 8 & 0xFF, g = v >> 16 & 0xFF, r = v >> 24 & 0xFF; break;
    }
    return r << 16 | g << 8 | b;
  }
  u32 raw(int x, int y) const {
    u32 v = 0;
    if (base) memcpy(&v, base + x * dx + y * dy, bpp);
    return v;
  }
  inline void put(int x, int y, u32 v) const {
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h || !base) return;
    int o = x * dx + y * dy;
    store(base + o, v);
    if (right) store(right + o, v);
  }
  inline void store(u8* p, u32 v) const {
    if (bpp == 2) *(u16*)p = v;
    else if (bpp == 4) *(u32*)p = v;
    else p[0] = v, p[1] = v >> 8, p[2] = v >> 16;
  }
};

struct Canvas {
  const Fb& f;
  Canvas(const Fb& f) : f(f) {}

  void px(int x, int y, u32 c) const { f.put(x, y, f.enc(c)); }
  void rect(int x, int y, int rw, int rh, u32 c) const {
    u32 v = f.enc(c);
    for (int j = 0; j < rh; j++)
      for (int i = 0; i < rw; i++) f.put(x + i, y + j, v);
  }
  void notched(int x, int y, int rw, int rh, u32 c) const {
    rect(x + 1, y, rw - 2, rh, c);
    rect(x, y + 1, 1, rh - 2, c);
    rect(x + rw - 1, y + 1, 1, rh - 2, c);
  }
  // PixelBox: ink outline, notched corners, bevel, one-pixel drop.
  void box(int x, int y, int bw, int bh, u32 fill) const {
    notched(x, y + 1, bw, bh - 1, INK);
    notched(x, y, bw, bh - 1, INK);
    int ix = x + 1, iy = y + 1, iw = bw - 2, ih = bh - 3;
    notched(ix, iy, iw, ih, fill);
    rect(ix + 1, iy, iw - 2, 1, lerp(fill, 0xFFFFFF, 0.45f));
    rect(ix + 1, iy + ih - 1, iw - 2, 1, lerp(fill, INK, 0.12f));
  }
  void paper() const {
    u32 a = f.enc(PAPER), b = f.enc(PAPER_D);
    for (int y = 0; y < f.h; y++)
      for (int x = 0; x < f.w; x++) f.put(x, y, ((x / 30 + y / 30) & 1) ? a : b);
  }
  int glyph(int x, int y, char c, u32 v) const {
    if (c < 32 || c > 126) return 0;
    const u16* rows = FONT_BITS[c - 32];
    for (int r = 0; r < 16; r++)
      for (int b = 0; b < 16; b++)
        if (rows[r] >> b & 1) f.put(x + b, y - 2 + r, v);
    return FONT_W[c - 32];
  }
  static int width(const char* s) {
    int w = 0;
    for (; *s; s++) w += (*s >= 32 && *s <= 126) ? FONT_W[*s - 32] : 0;
    return w;
  }
  int text(int x, int y, const char* s, u32 color, bool center = false) const {
    if (center) x -= width(s) / 2;
    u32 v = f.enc(color);
    for (; *s; s++) x += glyph(x, y, *s, v);
    return x;
  }
  // Word-wrapped, drawing only the first [upTo] characters (typewriter).
  void wrap(int x, int y, int ww, const char* s, u32 color, int upTo) const {
    int cx = 0, cy = 0, n = 0;
    u32 v = f.enc(color);
    while (*s) {
      if (*s == ' ') {
        cx += FONT_W[0];
        s++, n++;
        continue;
      }
      const char* e = s;
      int len = 0;
      while (*e && *e != ' ') len += FONT_W[*e++ - 32];
      if (cx && cx + len > ww) cx = 0, cy += 13;
      for (; s < e; s++, n++) {
        if (n >= upTo) return;
        cx += glyph(x + cx, y + cy, *s, v);
      }
    }
  }
  // One frame of Mole: runs of palette indices, 255 = see-through.
  void mole(const u8* rle, int x0, int y0, int w, int h) const {
    static u32 pal[sizeof(MOLE_PAL) / 4];
    static const Fb* last = nullptr;
    if (last != &f) {
      for (u32 i = 0; i < sizeof(MOLE_PAL) / 4; i++) pal[i] = f.enc(MOLE_PAL[i]);
      last = &f;
    }
    int n = w * h, at = 0;
    while (at < n) {
      int run = *rle++, idx = *rle++;
      if (idx != 255) {
        for (int k = 0; k < run; k++) f.put(x0 + (at + k) % w, y0 + (at + k) / w, pal[idx]);
      }
      at += run;
    }
  }
};

// ----------------------------------------------------------------- sound ----
// CTRPF plays BCWAVs (made by tools/make_3ds_assets.py, the same synth as the
// rant clips): the burst, Mole's babble at his furious pitch, a chime, A and B.
static Sound* sndBlips[14];
static Sound *sndSting, *sndChime, *sndAccept, *sndPlop;

static void soundsInit() {
  if (sndSting) return;
  for (int i = 0; i < 14; i++) sndBlips[i] = new Sound(SND_BLIPS[i], 2);
  sndSting = new Sound(SND_STING);
  sndChime = new Sound(SND_CHIME);
  sndAccept = new Sound(SND_ACCEPT);
  sndPlop = new Sound(SND_PLOP);
}

static void babble(char c) {
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return;
  int semis;
  switch (c | 0x20) {
    case 'a': semis = 0; break;
    case 'e': semis = 3; break;
    case 'i': semis = 7; break;
    case 'o': semis = -2; break;
    case 'u': semis = -4; break;
    default: semis = (int)(svcGetSystemTick() >> 8) % 5 - 2;
  }
  if (c >= 'A' && c <= 'Z') semis += 2;
  semis += 4;
  if (semis < 0) semis = 0;
  if (semis > 13) semis = 13;
  sndBlips[semis]->Play();
}

// ------------------------------------------------------------ the block ----
static char moleLine[240];
static const int MOLE_X = 40, CAM_BURST = 12, CAM_MOLE = 6 - 129;

static double seconds() { return svcGetSystemTick() / (double)SYSCLOCK_ARM11; }
static float ease(float t) { return t < 0 ? 0 : t > 1 ? 1 : t < 0.5f ? 4 * t * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2; }

static void drawBottom(const Canvas& c, bool bedtime, int minutes, int limit, bool grace) {
  c.paper();
  c.box(12, 16, 296, 116, CREAM);
  c.text(22, 24, bedtime ? "IT'S BEDTIME" : "TIME'S UP FOR TODAY", MUTED);
  char buf[64];
  if (bedtime) snprintf(buf, sizeof buf, "%02d:00", limit);
  else snprintf(buf, sizeof buf, "%d", minutes);
  int x = 22;
  u32 ink = c.f.enc(INK);
  for (const char* p = buf; *p; p++) {
    // Big digits: the font at 3x, like the app's score.
    const u16* rows = FONT_BITS[*p - 32];
    for (int r = 0; r < 16; r++)
      for (int b = 0; b < 16; b++)
        if (rows[r] >> b & 1)
          for (int k = 0; k < 9; k++) c.f.put(x + b * 3 + k % 3, 38 + (r - 2) * 3 + k / 3, ink);
    x += FONT_W[*p - 32] * 3;
  }
  if (bedtime) snprintf(buf, sizeof buf, "was bedtime, and it's late");
  else snprintf(buf, sizeof buf, "of %d minutes played", limit);
  c.text(x + 6, 52, buf, MUTED);
  c.wrap(22, 84, 276, bedtime ? "Games are way more fun when you've had a good night's sleep. "
                                "See you tomorrow, dig dig!"
                              : "Games are way more fun when they don't eat the whole day. "
                                "See you tomorrow, dig dig!", INK, 999);
  c.box(40, 146, 240, 30, BAND);
  c.text(160, 157, "A  OKAY, I'M DONE", CREAM, true);
  if (grace) {
    c.box(70, 186, 180, 24, CREAM);
    c.text(160, 194, "B  let me save first", MUTED, true);
  } else {
    c.text(160, 194, "(you already had your save break)", MUTED, true);
  }
}

// The top screen at time [t] (seconds since Mole came in). Returns how many
// letters are showing.
struct Timeline {
  double burstEnd, panEnd, boxEnd;
};

// [snap] is the dimmed game (and, once it has landed, the dialogue box) in the
// framebuffer's own layout: each frame starts from one copy of it.
static int drawTop(const Fb& f, u8* snap, bool& boxBaked, double t, const Timeline& tl, bool skip) {
  Canvas c(f);
  f.load(snap);
  if (!skip && t < tl.burstEnd) {
    int i = (int)(t * 30);
    if (i >= BURST_FRAMES) i = BURST_FRAMES - 1;
    c.mole(BURST_RLE + BURST_OFF[i], MOLE_X + BURST_X, CAM_BURST + BURST_Y, BURST_W, BURST_H);
    return 0;
  }
  float p = skip ? 1 : ease((float)((t - tl.burstEnd) / (tl.panEnd - tl.burstEnd)));
  int cam = CAM_BURST + (int)((CAM_MOLE - CAM_BURST) * p - 0.5f);
  int i = (int)(t * 30) % FURIOUS_FRAMES;
  c.mole(FURIOUS_RLE + FURIOUS_OFF[i], MOLE_X + FURIOUS_X, cam + FURIOUS_Y, FURIOUS_W, FURIOUS_H);
  if (!skip && t < tl.panEnd) return 0;
  float q = skip ? 1 : (float)((t - tl.panEnd) / (tl.boxEnd - tl.panEnd));
  if (q > 1) q = 1;
  float e = 1 - (1 - q) * (1 - q) * (1 - q);
  const int bx = 8, bw = 384, bh = 72;
  int by = 162 + (int)((1 - e) * 90 + 0.5f);
  if (!boxBaked) {
    auto drawBox = [&](const Canvas& k) {
      k.box(bx, by, bw, bh, CREAM);
      k.box(bx + 10, by - 11, 44, 17, CHESTNUT);
      k.text(bx + 32, by - 7, "MOLE", CREAM, true);
    };
    if (q < 1) {
      drawBox(c);
      return 0;
    }
    // Landed: draw it into the snapshot once, then it costs nothing per frame.
    Fb s(f, snap);
    drawBox(Canvas(s));
    boxBaked = true;
    f.load(snap);
    c.mole(FURIOUS_RLE + FURIOUS_OFF[i], MOLE_X + FURIOUS_X, cam + FURIOUS_Y, FURIOUS_W, FURIOUS_H);
  }
  int typed = (int)((t - tl.boxEnd) * 30);
  int len = strlen(moleLine);
  if (typed > len) typed = len;
  c.wrap(bx + 10, by + 12, bw - 24, moleLine, INK, typed);
  if (typed >= len && ((int)(t * 3) & 1)) {  // the "more" arrow, blinking
    for (int k = 0; k < 4; k++) c.rect(bx + bw - 16 + k, by + bh - 13 + k, 7 - 2 * k, 1, BAND);
  }
  return typed;
}

// For bedtime, [limit] is the bedtime hour. Returns true if they took the
// save break; walking away never returns.
static bool block(bool bedtime, int minutes, int limit) {
  plog("block: %s, %d of %d min, title %016llX", bedtime ? "bedtime" : "limit", minutes, limit,
       Process::GetTitleID());
  Process::Pause();
  plog("paused");
  soundsInit();
  plog("sounds ready");
  bool grace = !usedGraceToday();
  if (bedtime)
    snprintf(moleLine, sizeof moleLine,
             "Uh-huh, I see you playing past %02d:00! Even moles go to bed eventually, and "
             "tomorrow-you is gonna be so grumpy about this. Save up, it's burrow time, dig dig!",
             limit);
  else
    snprintf(moleLine, sizeof moleLine,
             "Whoa, whoa, WHOA! That's %d minutes of games today and your limit was %d. "
             "I'm not mad... okay, I'm a little mad. Save up and let's call it a day, dig dig!",
             minutes, limit);
  // The game under an ink veil, like the app's dialogs, kept in its own format.
  const Screen& ts = OSD::GetTopScreen();
  plog("top: left %08lX right %08lX 3d %d stride %lu bpp %lu fmt %d", ts.LeftFramebuffer,
       ts.RightFramebuffer, ts.Is3DEnabled, ts.Stride, ts.BytesPerPixel, (int)ts.Format);
  Fb top(ts, 400, 240);
  if (!top.ok()) {  // no framebuffer we can use: let the game go on, never crash it
    plog("no usable framebuffer");
    Process::Play();
    return true;
  }
  plog("fb ok, right eye %s", top.right ? "on" : "off");
  // The dimmed game, built once in the framebuffer's own layout.
  u8* dimmed = new u8[top.bytes];
  top.save(dimmed);
  {
    Fb d(top, dimmed);
    for (int y = 0; y < 240; y++)
      for (int x = 0; x < 400; x++) {
        u32 v = 0;
        u8* q = d.base + x * d.dx + y * d.dy;
        memcpy(&v, q, d.bpp);
        d.store(q, d.enc(lerp(d.dec(v), INK, 0.6f)));
      }
  }
  plog("snapshot %lu bytes", top.bytes);
  bool baked = false;
  for (int i = 0; i < 2; i++) {
    Fb b(OSD::GetBottomScreen(), 320, 240);
    drawBottom(Canvas(b), bedtime, minutes, limit, grace);
    Fb t(OSD::GetTopScreen(), 400, 240);
    drawTop(t, dimmed, baked, 0, Timeline{1, 2, 3}, false);
    OSD::SwapBuffers();
  }
  plog("first frames drawn");
  Timeline tl;
  tl.burstEnd = (BURST_FRAMES + 8) / 30.0;
  tl.panEnd = tl.burstEnd + 0.4;
  tl.boxEnd = tl.panEnd + 0.23;
  sndSting->Play();
  double start = seconds();
  bool skip = false;
  int lastTyped = 0, len = strlen(moleLine);
  u32 prevHeld = ~0u;  // ignore whatever is already held when Mole comes in
  bool prevTouch = true;
  int frames = 0;
  bool fpsLogged = false;
  while (true) {
    double t = seconds() - start;
    if (++frames && !fpsLogged && t > 5) fpsLogged = true, plog("%.1f fps", frames / t);
    Fb f(OSD::GetTopScreen(), 400, 240);
    int typed = drawTop(f, dimmed, baked, t, tl, skip);
    OSD::SwapBuffers();
    for (int k = lastTyped; k < typed; k++)
      if (k % 2 == 0) babble(moleLine[k]);
    lastTyped = typed;
    // Buttons: every new press counts, and so does tapping the two buttons on
    // the touch screen (the red one is A, the cream one B).
    Controller::Update();
    u32 held = Controller::GetKeysDown();
    u32 down = held & ~prevHeld;
    prevHeld = held;
    bool touch = Touch::IsDown();
    if (touch && !prevTouch) {
      UIntVector p = Touch::GetPosition();
      if (p.x >= 40 && p.x < 280 && p.y >= 142 && p.y < 180) down |= Key::A | (1u << 31);
      if (grace && p.x >= 70 && p.x < 250 && p.y >= 182 && p.y < 214) down |= Key::B;
    }
    prevTouch = touch;
    if (down & Key::A) {
      if (typed < len && !(down & (1u << 31))) {  // A first: Mole in place, the whole line
        skip = true;
        start = seconds() - tl.boxEnd - len / 30.0 - 1;
        continue;
      }
      sndAccept->Play();
      plog("walk away");
      logEvent("walk");
      Sleep(Milliseconds(150));
      delete[] dimmed;
      Process::ReturnToHomeMenu();
    }
    if (grace && (down & Key::B)) {
      sndPlop->Play();
      logEvent("grace");
      delete[] dimmed;
      Process::Play();
      return true;
    }
  }
}

// ------------------------------------------------------------ heads-up ----
static s64 headsUpUntil = 0;
static char headsUpLine[96];
static volatile bool osdAlive = false;

static void drawHeadsUp(const Canvas& c) {
  int w = Canvas::width(headsUpLine) + 24;
  c.box((400 - w) / 2, 14, w, 22, CREAM);
  c.box((400 - w) / 2 + 8, 4, 44, 17, CHESTNUT);
  c.text((400 - w) / 2 + 30, 8, "MOLE", CREAM, true);
  c.text(200, 22, headsUpLine, INK, true);
}

static volatile u32 osdFrames = 0;  // frames the game has shown (retail games)

static bool headsUpOsd(const Screen& screen) {
  osdAlive = true;
  if (screen.IsTop) osdFrames++;
  if (!screen.IsTop || now() > headsUpUntil) return false;
  Fb f(screen, 400, 240);
  drawHeadsUp(Canvas(f));
  return true;
}

static void headsUp(int left) {
  soundsInit();
  if (left == 1) snprintf(headsUpLine, sizeof headsUpLine, "Psst! Just 1 minute left for today, dig dig!");
  else snprintf(headsUpLine, sizeof headsUpLine, "Psst! %d minutes left for today, dig dig!", left);
  sndChime->Play();
  if (osdAlive) {
    headsUpUntil = now() + 6;
    return;
  }
  Process::Pause();
  for (int i = 0; i < 2; i++) {
    Fb f(OSD::GetTopScreen(), 400, 240);
    drawHeadsUp(Canvas(f));
    OSD::SwapBuffers();
  }
  for (int t = 0; t < 180; t++) {  // three seconds, or until A
    Controller::Update();
    if (t > 20 && (Controller::GetKeysPressed() & Key::A)) break;
    Sleep(Milliseconds(16));
  }
  Process::Play();
}

// ---------------------------------------------------------------- main ----
static volatile bool away = false;  // in HOME Menu or asleep: not playing

static void onEvent(Process::Event e) {
  if (e == Process::Event::HOME_ENTER || e == Process::Event::SLEEP_ENTER ||
      e == Process::Event::SWAP_ENTER)
    away = true;
  else if (e == Process::Event::HOME_EXIT || e == Process::Event::SLEEP_EXIT ||
           e == Process::Event::SWAP_EXIT)
    away = false;
}

void PatchProcess(FwkSettings& settings) {
  settings.AllowActionReplay = false;
  settings.AllowSearchEngine = false;
  settings.WaitTimeToBoot = Seconds(5);  // CTRPF's default: let the game boot first
  // Homebrew games have no HID code CTRPF can hook; read the buttons itself.
  settings.UseGameHidMemory = false;
}

void OnProcessExit(void) {}

int main(void) {
  Process::SetProcessEventCallback(onEvent);
  OSD::Run(headsUpOsd);
  u64 tid = Process::GetTitleID();
  if (tid == 0x000400000C0DE500ULL) return 0;  // ControlMole itself: nothing to watch
  // The app shows this line, so you can tell the plugin is really running.
  plog("start %016llX", tid);
  Rules rules = readRules();
  bool counted = true;
  for (u64 k : rules.skip) counted &= k != tid;

  s64 before = playedToday(rules), day = dayStart(now());
  s64 mine = 0;  // seconds this run
  Sitting cur(tid);
  bool wasAway = true, warned = false;
  s64 graceUntil = 0;
  int sinceWrite = 0;
  s64 bootAt = now(), backAt = 0;
  u32 lastFrames = 0;
  int drawingFor = 0;  // seconds in a row the game showed new frames

  while (true) {
    Sleep(Seconds(1));
    s64 t = now();
    if (dayStart(t) != day) {  // midnight: a fresh day
      day = dayStart(t), before = 0, mine = 0, warned = false;
      if (!wasAway) writeSitting(cur), cur = Sitting{tid, t, t};
    }
    if (away) {
      if (!wasAway) writeSitting(cur);
      wasAway = true;
      continue;
    }
    if (wasAway) {
      backAt = t;  // back from the HOME Menu or sleep: give the game a moment
      cur = Sitting{tid, t, t};
      writeSitting(cur);
      wasAway = false;
      rules = readRules();
    }
    cur.end = t;
    mine++;
    u32 f = osdFrames;
    drawingFor = f != lastFrames ? drawingFor + 1 : 0;
    lastFrames = f;
    bool settled = t - bootAt >= 20 && t - backAt >= 3 && (!osdAlive || drawingFor >= 3);
    if (++sinceWrite >= 15) sinceWrite = 0, writeSitting(cur);
    if (!counted || !rules.block) continue;

    int minutes = (before + mine) / 60, limit = limitToday(rules, t);
    int left = limit >= NO_LIMIT ? 1 << 20 : limit - minutes;
    if (!settled) continue;
    if (!warned && left <= 5 && left > 0 && !afterBedtime(rules, t)) {
      warned = true;
      headsUp(left);
    }
    bool late = afterBedtime(rules, t);
    if ((left <= 0 || late) && t >= graceUntil) {
      writeSitting(cur);
      if (block(late && left > 0, minutes, late && left > 0 ? bedtimeTonight(rules, t) : limit))
        graceUntil = now() + 3 * 60;
    }
  }
  return 0;
}

}  // namespace CTRPluginFramework
