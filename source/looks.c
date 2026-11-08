#include "looks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "data.h"

const Look LOOK_CLASSIC = {0, 0, 0, 0, 0};

#define NFURS ((int)(sizeof LOOK_FURS / sizeof LOOK_FURS[0]))
#define NBANDS ((int)(sizeof LOOK_BANDS / sizeof LOOK_BANDS[0]))
#define NPIECES ((int)(sizeof LOOK_PIECES / sizeof LOOK_PIECES[0]))

// ------------------------------------------------------------------ keys ----
static int find(const char* key, int len, int row) {
  if (row < 0) {
    for (int i = 0; i < NFURS; i++)
      if ((int)strlen(LOOK_FURS[i].key) == len && !strncmp(LOOK_FURS[i].key, key, len)) return i;
    return 0;
  }
  if (row == 99) {
    for (int i = 0; i < NBANDS; i++)
      if ((int)strlen(LOOK_BANDS[i].key) == len && !strncmp(LOOK_BANDS[i].key, key, len)) return i;
    return 0;
  }
  for (int i = 1; i < NPIECES; i++) {
    PieceRow r = LOOK_PIECES[i].row;
    bool fits = row == ROW_HAT ? r == ROW_HAT
              : row == ROW_BODY ? (r == ROW_BODY || r == ROW_COSTUME)
              : r == ROW_ACCESSORY;
    if (fits && (int)strlen(LOOK_PIECES[i].key) == len && !strncmp(LOOK_PIECES[i].key, key, len))
      return i;
  }
  return 0;
}

void lookParse(const char* key, Look* out) {
  *out = LOOK_CLASSIC;
  if (!key) return;
  const char* p = key;
  int part = 0;
  while (*p && part < 5) {
    const char* e = strchr(p, '/');
    int len = e ? (int)(e - p) : (int)strlen(p);
    while (len && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' ')) len--;
    switch (part) {
      case 0: out->fur = find(p, len, -1); break;
      case 1: out->band = find(p, len, 99); break;
      case 2: out->hat = find(p, len, ROW_HAT); break;
      case 3: out->body = find(p, len, ROW_BODY); break;
      default: out->accessory = find(p, len, ROW_ACCESSORY); break;
    }
    part++;
    if (!e) break;
    p = e + 1;
  }
}

void lookKey(const Look* l, char* buf, int n) {
  snprintf(buf, n, "%s/%s/%s/%s/%s", LOOK_FURS[l->fur].key, LOOK_BANDS[l->band].key,
           LOOK_PIECES[l->hat].key, LOOK_PIECES[l->body].key, LOOK_PIECES[l->accessory].key);
}

bool lookEqual(const Look* a, const Look* b) { return !memcmp(a, b, sizeof *a); }

bool furEarned(int fur, u32 trophies) {
  int t = LOOK_FURS[fur].trophy;
  return t < 0 || (trophies >> t & 1);
}

bool bandEarned(int band, u32 trophies) {
  int t = LOOK_BANDS[band].trophy;
  return t < 0 || (trophies >> t & 1);
}

// --------------------------------------------------------------- colours ----
// The sheets' chestnut and red colours -> the look's, by exact RGB (the app's
// tables), and the same squeezed into the burst's 5551 and the dance's 565.
#define MAP_SIZE 2048
typedef struct {
  u32 key[MAP_SIZE], val[MAP_SIZE];
  int count;
} ColourMap;

static ColourMap toonMap, danceMap;
static u16* lut5551;
static u16* lut565;

static void mapPut(ColourMap* m, u32 src, u32 dst) {
  u32 h = (src * 2654435761u) >> 21;
  while (m->key[h & (MAP_SIZE - 1)] && m->key[h & (MAP_SIZE - 1)] != src + 1) h++;
  h &= MAP_SIZE - 1;
  if (!m->key[h]) m->count++;
  m->key[h] = src + 1, m->val[h] = dst;
}

static inline bool mapGet(const ColourMap* m, u32 src, u32* dst) {
  for (u32 h = (src * 2654435761u) >> 21;; h++) {
    u32 k = m->key[h & (MAP_SIZE - 1)];
    if (!k) return false;
    if (k == src + 1) {
      *dst = m->val[h & (MAP_SIZE - 1)];
      return true;
    }
  }
}

// An RGBA colour through the map (alpha kept).
static inline u32 mapRGBA(const ColourMap* m, u32 c) {
  u32 to;
  return (c & 0xFF) && mapGet(m, c >> 8, &to) ? to << 8 | (c & 0xFF) : c;
}

static void mapLook(ColourMap* m, const Look* l, bool dance) {
  memset(m, 0, sizeof *m);
  const u16* at[2] = {dance ? FUR_DANCE_AT : FUR_TOON_AT, dance ? BAND_DANCE_AT : BAND_TOON_AT};
  const u32* t[2] = {dance ? FUR_DANCE : FUR_TOON, dance ? BAND_DANCE : BAND_TOON};
  int idx[2] = {l->fur, l->band};
  for (int k = 0; k < 2; k++)
    for (int i = at[k][idx[k]]; i < at[k][idx[k] + 1]; i++) mapPut(m, t[k][2 * i], t[k][2 * i + 1]);
}

static u16 to5551(u32 c) { return (c >> 19 & 31) << 11 | (c >> 11 & 31) << 6 | (c >> 3 & 31) << 1 | 1; }
static u16 to565(u32 c) { return (c >> 19 & 31) << 11 | (c >> 10 & 63) << 5 | (c >> 3 & 31); }

static Look shown;
static u32 version = 1;

static void buildColours(void) {
  mapLook(&toonMap, &shown, false);
  mapLook(&danceMap, &shown, true);
  if (!lut5551) lut5551 = malloc(65536 * 2);
  if (!lut565) lut565 = malloc(65536 * 2);
  for (int i = 0; i < 65536; i++) lut5551[i] = lut565[i] = i;
  for (int i = 0; i < MAP_SIZE; i++) {
    if (toonMap.key[i]) lut5551[to5551(toonMap.key[i] - 1)] = to5551(toonMap.val[i]);
    if (danceMap.key[i]) lut565[to565(danceMap.key[i] - 1)] = to565(danceMap.val[i]);
  }
}

// ------------------------------------------------------------- overlays ----
typedef struct {
  u8* file;
  int frames, w, h, x, y, npal;
  u32* pal;        // RGBA: alpha under 128 = the piece hides Mole there
  const u32* off;
  bool hides;
  u8* idx;         // one decoded frame
  int idxFrame;
  C3D_Tex tex;
  Tex3DS_SubTexture sub;
  int texFrame;
} Overlay;

typedef struct {
  char sheet[16];
  u32 version;
  Overlay o[3];
  int n;
} Worn;

static Worn wornMood, wornBurst, wornDance;

static void ovlClose(Overlay* o) {
  if (o->tex.data) C3D_TexDelete(&o->tex);
  free(o->file);
  free(o->idx);
  memset(o, 0, sizeof *o);
}

static bool ovlOpen(Overlay* o, const char* piece, const char* sheet) {
  memset(o, 0, sizeof *o);
  char path[64];
  snprintf(path, sizeof path, "romfs:/looks/%s_%s.bin", piece, sheet);
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  o->file = malloc(n);
  bool ok = o->file && fread(o->file, 1, n, f) == (size_t)n;
  fclose(f);
  if (!ok || memcmp(o->file, "MOLO", 4)) {
    ovlClose(o);
    return false;
  }
  const u16* h = (const u16*)(o->file + 4);
  o->frames = h[0], o->w = h[1], o->h = h[2], o->npal = h[3];
  o->x = (s16)h[4], o->y = (s16)h[5];
  o->pal = (u32*)(o->file + 16);
  o->off = o->pal + o->npal;
  // The fur and headband lines a piece draws take the look's colours too.
  const ColourMap* m = !strcmp(sheet, "dance") ? &danceMap : &toonMap;
  for (int i = 1; i < o->npal; i++) {
    if ((o->pal[i] & 0xFF) < 128) o->hides = true;
    o->pal[i] = mapRGBA(m, o->pal[i]);
  }
  o->idx = malloc(o->w * o->h);
  o->idxFrame = o->texFrame = -1;
  return o->idx != NULL;
}

static const u8* ovlIndices(Overlay* o, int i) {
  if (i < 0) i = 0;
  if (i >= o->frames) i = o->frames - 1;
  if (i != o->idxFrame) {
    uLongf size = o->w * o->h;
    uncompress(o->idx, &size, o->file + o->off[i], o->off[i + 1] - o->off[i]);
    o->idxFrame = i;
  }
  return o->idx;
}

static inline u32 morton(u32 x, u32 y) {
  u32 m = 0;
  for (int i = 0; i < 3; i++) m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
  return m;
}

// Texel (x, y) of a tiled texture, row 0 at the top (v = 1), as citro2d maps it.
static inline u32 texel(int texW, int x, int y) {
  return ((y >> 3) * (texW >> 3) + (x >> 3)) * 64 + morton(x & 7, y & 7);
}

static C2D_Image ovlFrame(Overlay* o, int i) {
  if (!o->tex.data) {
    int tw = 8, th = 8;
    while (tw < o->w) tw <<= 1;
    while (th < o->h) th <<= 1;
    if (!C3D_TexInit(&o->tex, tw, th, GPU_RGBA8)) return (C2D_Image){NULL, NULL};
    C3D_TexSetFilter(&o->tex, GPU_NEAREST, GPU_NEAREST);
    memset(o->tex.data, 0, o->tex.size);
    o->sub = (Tex3DS_SubTexture){o->w, o->h, 0.0f, 1.0f, (float)o->w / tw, 1.0f - (float)o->h / th};
  }
  if (i != o->texFrame) {
    const u8* idx = ovlIndices(o, i);
    u32* d = o->tex.data;
    for (int y = 0; y < o->h; y++)
      for (int x = 0; x < o->w; x++) {
        u32 c = o->pal[idx[y * o->w + x]];
        // Hidden-Mole pixels are cut out of his texture instead; nothing to draw.
        d[texel(o->tex.width, x, y)] = (c & 0xFF) < 128 ? 0 : c;
      }
    C3D_TexFlush(&o->tex);
    o->texFrame = i;
  }
  return (C2D_Image){&o->tex, &o->sub};
}

// The pieces worn on [sheet], in the order they stack: body, accessory, hat.
// A costume is worn alone.
static Worn* worn(Worn* w, const char* sheet) {
  if (w->version == version && !strcmp(w->sheet, sheet)) return w;
  for (int i = 0; i < w->n; i++) ovlClose(&w->o[i]);
  w->n = 0;
  snprintf(w->sheet, sizeof w->sheet, "%s", sheet);
  w->version = version;
  bool costume = LOOK_PIECES[shown.body].row == ROW_COSTUME;
  int slots[3] = {shown.body, costume ? 0 : shown.accessory, costume ? 0 : shown.hat};
  for (int s = 0; s < 3; s++)
    if (slots[s] && ovlOpen(&w->o[w->n], LOOK_PIECES[slots[s]].key, sheet)) w->n++;
  return w;
}

// ----------------------------------------------------------------- shown ----
void looksShow(const Look* l) {
  if (lookEqual(l, &shown) && lut5551) return;
  shown = *l;
  version++;
  buildColours();
}

const Look* looksShown(void) { return &shown; }
u32 looksVersion(void) { return version; }

// --------------------------------------------------------------- moods ----
void looksDressMood(const char* mood, C2D_Image* frames, int n, int first, int ox, int oy) {
  Worn* w = worn(&wornMood, mood);
  bool recolour = toonMap.count > 0;
  for (int j = 0; j < n; j++) {
    C2D_Image im = frames[j];
    const Tex3DS_SubTexture* s = im.subtex;
    if (!im.tex || s->top < s->bottom) continue;  // rotated in the atlas: leave it be
    int tw = im.tex->width, th = im.tex->height;
    int x0 = (int)(s->left * tw + 0.5f), y0 = (int)((1.0f - s->top) * th + 0.5f);
    u32* d = im.tex->data;
    if (recolour)
      for (int y = 0; y < s->height; y++)
        for (int x = 0; x < s->width; x++) {
          u32* p = &d[texel(tw, x0 + x, y0 + y)];
          u32 v = *p, c;
          if ((v & 0xFF) && mapGet(&toonMap, v >> 8, &c)) *p = c << 8 | (v & 0xFF);
        }
    for (int k = 0; k < w->n; k++) {
      Overlay* o = &w->o[k];
      if (!o->hides) continue;
      const u8* idx = ovlIndices(o, first + j);
      for (int y = 0; y < o->h; y++)
        for (int x = 0; x < o->w; x++) {
          u32 c = o->pal[idx[y * o->w + x]];
          if (!idx[y * o->w + x] || (c & 0xFF) >= 128) continue;
          int fx = o->x + x - ox, fy = o->y + y - oy;
          if (fx < 0 || fy < 0 || fx >= s->width || fy >= s->height) continue;
          d[texel(tw, x0 + fx, y0 + fy)] = 0;
        }
    }
    C3D_TexFlush(im.tex);
  }
}

void looksDrawMood(int frame, float bx, float by, int ox, int oy, float sx, float sy) {
  Worn* w = &wornMood;
  if (w->version != version) return;
  for (int k = 0; k < w->n; k++) {
    Overlay* o = &w->o[k];
    C2D_Image im = ovlFrame(o, frame);
    if (!im.tex) continue;
    C2D_DrawImageAt(im, bx + (o->x - ox) * sx, by + (o->y - oy) * sy, 0.5f, NULL, sx, sy);
  }
}

int looksLift(int frameTop) {
  Worn* w = &wornMood;
  if (w->version != version) return 0;
  int lift = 0;
  for (int k = 0; k < w->n; k++) {
    int need = 2 - (frameTop + w->o[k].y);
    if (need > lift) lift = need;
  }
  return lift > LOOK_ROOM_LIFT ? LOOK_ROOM_LIFT : lift;
}

// ------------------------------------------------------- burst, dance ----
void looksDressFrame(const char* anim, int frame, C3D_Tex* tex, int w, int h, int ox, int oy,
                     bool rgb565) {
  u16* d = tex->data;
  u16* lut = rgb565 ? lut565 : lut5551;
  if (lut && (shown.fur || shown.band)) {
    int n = ((h + 7) / 8) * 8 * tex->width;
    for (int i = 0; i < n; i++) d[i] = lut[d[i]];
  }
  if (rgb565) return;  // the dance is a whole scene: nothing to cut out
  Worn* wr = worn(&wornBurst, anim);
  for (int k = 0; k < wr->n; k++) {
    Overlay* o = &wr->o[k];
    if (!o->hides) continue;
    const u8* idx = ovlIndices(o, frame);
    for (int y = 0; y < o->h; y++)
      for (int x = 0; x < o->w; x++) {
        u8 v = idx[y * o->w + x];
        if (!v || (o->pal[v] & 0xFF) >= 128) continue;
        int fx = o->x + x - ox, fy = o->y + y - oy;
        if (fx < 0 || fy < 0 || fx >= w || fy >= h) continue;
        d[texel(tex->width, fx, fy)] = 0;
      }
  }
}

void looksDrawFrame(const char* anim, int frame, float x, float y) {
  Worn* w = worn(!strcmp(anim, "dance") ? &wornDance : &wornBurst, anim);
  for (int k = 0; k < w->n; k++) {
    Overlay* o = &w->o[k];
    C2D_Image im = ovlFrame(o, frame);
    if (im.tex) C2D_DrawImageAt(im, x + o->x, y + o->y, 0.5f, NULL, 1, 1);
  }
}

// ---------------------------------------------------------------- plugin ----
void looksWritePlugin(const Look* l) {
  static const char* SLOT[3] = {"body", "accessory", "hat"};
  bool costume = LOOK_PIECES[l->body].row == ROW_COSTUME;
  int slots[3] = {l->body, costume ? 0 : l->accessory, costume ? 0 : l->hat};
  static ColourMap plug;
  mapLook(&plug, l, false);
  mkdir("sdmc:/3ds", 0777);
  mkdir(DATA_DIR, 0777);
  for (int s = 0; s < 3; s++) {
    char dst[64], src[64];
    snprintf(dst, sizeof dst, DATA_DIR "/look_%s.bin", SLOT[s]);
    if (!slots[s]) {
      remove(dst);
      continue;
    }
    snprintf(src, sizeof src, "romfs:/looks/plugin_%s.bin", LOOK_PIECES[slots[s]].key);
    FILE* in = fopen(src, "rb");
    if (!in) continue;
    fseek(in, 0, SEEK_END);
    long n = ftell(in);
    fseek(in, 0, SEEK_SET);
    u8* b = malloc(n);
    if (b && fread(b, 1, n, in) == (size_t)n) {
      // Two sections (furious, burst): x, y, w, h, frames, colours, then the
      // colours (RGB), offsets and runs. The colours take the look's fur and band.
      u8* p = b + 4;
      for (int sec = 0; sec < 2 && p + 12 <= b + n; sec++) {
        const u16* h = (const u16*)p;
        int frames = h[4], ncol = h[5];
        u32* col = (u32*)(p + 12);
        for (int i = 0; i < ncol; i++) col[i] = mapRGBA(&plug, col[i] << 8 | 0xFF) >> 8;
        const u32* off = col + ncol;
        p = (u8*)(off + frames + 1) + ((off[frames] + 3) & ~3u);
      }
      FILE* out = fopen(dst, "wb");
      if (out) {
        fwrite(b, 1, n, out);
        fclose(out);
      }
    }
    free(b);
    fclose(in);
  }
}

void looksExit(void) {
  Worn* all[3] = {&wornMood, &wornBurst, &wornDance};
  for (int k = 0; k < 3; k++)
    for (int i = 0; i < all[k]->n; i++) ovlClose(&all[k]->o[i]);
  free(lut5551);
  free(lut565);
  lut5551 = lut565 = NULL;
}
