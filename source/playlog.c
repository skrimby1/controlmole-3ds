#include <stdio.h>
#include <string.h>
#include <time.h>

#include "data.h"

#define UNIX_2000 946684800LL
#define RING 0x11D28

typedef struct {
  u32 tidHigh, tidLow, w;  // w: event in bits 0-3, minutes since 2000 above
} PlayEntry;

static s64 cmd(Handle h, u32 header, u32* out) {
  u32* c = getThreadCommandBuffer();
  c[0] = header;
  c[1] = 0;
  c[2] = 0;  // Azahar leaves unimplemented replies untouched
  if (R_FAILED(svcSendSyncRequest(h)) || R_FAILED((Result)c[1])) return -1;
  *out = c[2];
  return 0;
}

// [relative]: the offset counts from the oldest entry (the ring's start);
// otherwise it's an index into the ring. 3dbrew doesn't say which.
static int readPtmMode(PlayEntry* buf, int max, bool relative, u32* outStart, u32* outLen);

static int readPtm(PlayEntry* buf, int max) { return readPtmMode(buf, max, false, NULL, NULL); }

static int readPtmMode(PlayEntry* buf, int max, bool relative, u32* outStart, u32* outLen) {
  Handle h;
  if (R_FAILED(srvGetServiceHandle(&h, "ptm:play"))) return 0;
  u32 start = 0, len = 0;
  if (cmd(h, IPC_MakeHeader(0x808, 0, 0), &start) < 0 ||
      cmd(h, IPC_MakeHeader(0x809, 0, 0), &len) < 0 || len == 0 || len > RING) {
    svcCloseHandle(h);
    if (outStart) *outStart = start, *outLen = len;
    return 0;
  }
  if (outStart) *outStart = start, *outLen = len;
  // Only the most recent entries matter (about a year of play).
  u32 skip = len > (u32)max ? len - max : 0;
  int n = 0;
  for (u32 i = skip; i < len;) {
    u32 chunk = len - i > 256 ? 256 : len - i;
    memset(&buf[n], 0, chunk * sizeof(PlayEntry));
    u32* c = getThreadCommandBuffer();
    c[0] = IPC_MakeHeader(0x807, 2, 2);
    c[1] = relative ? i : (start + i) % RING;
    c[2] = chunk;
    c[3] = IPC_Desc_Buffer(chunk * sizeof(PlayEntry), IPC_BUFFER_W);
    c[4] = (u32)&buf[n];
    if (R_FAILED(svcSendSyncRequest(h)) || R_FAILED((Result)c[1])) break;
    n += chunk, i += chunk;
  }
  svcCloseHandle(h);
  return n;
}

static bool isApp(u64 tid) { return (tid >> 32) == 0x00040000; }

// Games (and days) the plugin logged itself. The Activity Log only fills in
// the rest: games where Mole's plugin didn't run, e.g. because another plugin
// (Vapecord in New Leaf, CTGP-7 in Mario Kart 7) loads there instead, since
// Luma loads one plugin per game.
typedef struct {
  u64 tid;
  s64 day;
} Covered;
static Covered covered[MAX_SITTINGS];
static int nCovered, fromLogAdded;
static bool filling;  // pushing Activity Log sittings

static bool isCovered(u64 tid, s64 day) {
  for (int i = 0; i < nCovered; i++)
    if (covered[i].tid == tid && covered[i].day == day) return true;
  return false;
}

static void push(u64 tid, s64 a, s64 b) {
  if (b <= a || playlog.count >= MAX_SITTINGS || tid == SELF_TID) return;
  if (filling) {
    if (isCovered(tid, dayStart(a))) return;
    fromLogAdded++;
  }
  playlog.s[playlog.count++] = (Sitting){tid, a, b};
}

// What the Activity Log really returns, read both ways, for checking the
// parser against a real console: sdmc:/3ds/ControlMole/ptm_debug.txt (once a run).
static void ptmDebug(void) {
  static bool done;
  if (done) return;
  done = true;
  static PlayEntry e[64];
  FILE* f = fopen(DATA_DIR "/ptm_debug.txt", "w");
  if (!f) return;
  for (int mode = 0; mode < 2; mode++) {
    u32 start = 0, len = 0;
    int n = readPtmMode(e, 64, mode, &start, &len);
    fprintf(f, "mode %s: start %lu len %lu read %d (the newest %d)\n",
            mode ? "relative" : "ring index", start, len, n, n);
    for (int i = 0; i < n; i++) {
      s64 t = UNIX_2000 + (s64)(e[i].w >> 4) * 60;
      time_t tt = (time_t)t;
      struct tm* tm = gmtime(&tt);
      fprintf(f, "  %08lX %08lX %08lX  ev %lX  %04d-%02d-%02d %02d:%02d\n", e[i].tidHigh,
              e[i].tidLow, e[i].w, e[i].w & 0xF, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
              tm->tm_hour, tm->tm_min);
    }
  }
  fprintf(f, "clock now: %lld\n", (long long)time(NULL));
  fclose(f);
}

// Launch / resume open a sitting; exit / suspend / sleep / power-off end it.
static int fromPtm(void) {
  static PlayEntry e[8192];
  int n = readPtm(e, 8192);
  if (n == 0) return 0;
  s64 now = time(NULL), since = dayStart(now) - 400 * 86400LL;
  u64 open = 0, slept = 0;
  s64 openAt = 0;
  int valid = 0;
  for (int i = 0; i < n; i++) {
    u64 tid = ((u64)e[i].tidHigh << 32) | e[i].tidLow;
    if (!isApp(tid) && tid != ~0ULL) {
      u64 swapped = ((u64)e[i].tidLow << 32) | e[i].tidHigh;  // the other word order
      if (isApp(swapped)) tid = swapped;
    }
    int ev = e[i].w & 0xF;
    s64 t = UNIX_2000 + (s64)(e[i].w >> 4) * 60;
    if (t < since || t > now + 86400) continue;
    valid++;
    if (tid == ~0ULL) {
      if (ev == 8 || ev == 0xA) {  // sleep start, power off
        if (open) push(open, openAt, t);
        slept = ev == 8 ? open : 0;
        open = 0;
      } else if (ev == 9 && slept) {  // sleep end
        open = slept, openAt = t, slept = 0;
      }
      continue;
    }
    if (!isApp(tid) || (ev & 2)) continue;  // applets don't count
    if (ev == 0 || ev == 4) {
      if (open) push(open, openAt, t);
      open = tid, openAt = t;
    } else if ((ev == 1 || ev == 5) && open == tid) {
      push(open, openAt, t);
      open = 0;
    }
  }
  if (open) push(open, openAt, now);
  return valid;
}

// The plugin's log: one fixed-width line per sitting, updated while it runs.
static int fromPlugin(void) {
  FILE* f = fopen(DATA_DIR "/plays.log", "r");
  if (!f) return 0;
  char line[64];
  int n = 0;
  while (fgets(line, sizeof line, f)) {
    unsigned long long tid;
    long long a, b;
    if (sscanf(line, "%llx %lld %lld", &tid, &a, &b) == 3) push(tid, a, b), n++;
  }
  fclose(f);
  return n;
}

// A made-up but typical day: a long sitting, some hopping, a few quick peeks.
static void sample(void) {
  s64 d = dayStart(time(NULL));
  const u64 base = 0x000400FF00000000ULL;
  const struct { int game; int from, to; } day[] = {
      {1, 8 * 60 + 5, 8 * 60 + 6},     {1, 12 * 60 + 10, 12 * 60 + 11},
      {2, 13 * 60, 13 * 60 + 4},       {3, 13 * 60 + 5, 13 * 60 + 7},
      {2, 13 * 60 + 8, 13 * 60 + 9},   {4, 13 * 60 + 10, 13 * 60 + 12},
      {1, 17 * 60 + 30, 17 * 60 + 31}, {3, 18 * 60, 18 * 60 + 41},
      {5, 19 * 60 + 15, 19 * 60 + 23},
  };
  s64 now = time(NULL);
  for (size_t i = 0; i < sizeof day / sizeof day[0]; i++) {
    s64 a = d + day[i].from * 60, b = d + day[i].to * 60;
    if (a >= now) break;
    push(base | day[i].game, a, b < now ? b : now);
  }
}

void playlogLoad(void) {
  playlog.count = 0;
  playlog.real = true;
  ptmDebug();
  int fromMole = fromPlugin();
  nCovered = 0;
  for (int i = 0; i < playlog.count && nCovered < MAX_SITTINGS; i++)
    if (!isCovered(playlog.s[i].tid, dayStart(playlog.s[i].start)))
      covered[nCovered++] = (Covered){playlog.s[i].tid, dayStart(playlog.s[i].start)};
  filling = true, fromLogAdded = 0;
  fromPtm();
  filling = false;
  if (fromMole && fromLogAdded) {
    playlog.source = "both";
  } else if (fromMole) {
    playlog.source = "Mole's plugin";
  } else if (fromLogAdded) {
    playlog.source = "Activity Log";
  } else {
    playlog.real = false;
    playlog.source = "sample day";
    sample();
  }
}
