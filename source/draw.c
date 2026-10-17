#include "draw.h"

#include <stdio.h>
#include <string.h>

#include "gen_assets.h"

int drawDX = 0;  // stereo shift for the layer being drawn

C2D_SpriteSheet sheetHabits, sheetTrophies, sheetMisc;
static C2D_SpriteSheet sheetFont;
static C2D_Image fontImg;

static C2D_SpriteSheet load(const char* path) {
  C2D_SpriteSheet s = C2D_SpriteSheetLoad(path);
  if (!s) svcBreak(USERBREAK_PANIC);
  for (size_t i = 0; i < C2D_SpriteSheetCount(s); i++) {
    C2D_Image img = C2D_SpriteSheetGetImage(s, i);
    C3D_TexSetFilter(img.tex, GPU_NEAREST, GPU_NEAREST);
  }
  return s;
}

void drawInit(void) {
  sheetFont = load("romfs:/gfx/font.t3x");
  fontImg = C2D_SpriteSheetGetImage(sheetFont, 0);
  sheetHabits = load("romfs:/gfx/habits.t3x");
  sheetTrophies = load("romfs:/gfx/trophies.t3x");
  sheetMisc = load("romfs:/gfx/misc.t3x");
}

void drawExit(void) {
  C2D_SpriteSheetFree(sheetFont);
  C2D_SpriteSheetFree(sheetHabits);
  C2D_SpriteSheetFree(sheetTrophies);
  C2D_SpriteSheetFree(sheetMisc);
}

void rect(int x, int y, int w, int h, u32 color) {
  if (w <= 0 || h <= 0) return;
  C2D_DrawRectSolid(x + drawDX, y, 0.5f, w, h, color);
}

static void notched(int x, int y, int w, int h, u32 c) {
  rect(x + 1, y, w - 2, h, c);
  rect(x, y + 1, w, h - 2, c);
}

static u32 lerp(u32 a, u32 b, float t) {
  u32 out = 0xFF000000;
  for (int sh = 0; sh < 24; sh += 8) {
    int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
    out |= ((u32)(ca + (cb - ca) * t + 0.5f) & 0xFF) << sh;
  }
  return out;
}

void box(int x, int y, int w, int h, u32 fill, bool pressed) {
  int by = pressed ? y + 1 : y, bh = h - 1;
  if (!pressed) notched(x, by + 1, w, bh, C_INK);
  notched(x, by, w, bh, C_INK);
  int ix = x + 1, iy = by + 1, iw = w - 2, ih = bh - 2;
  notched(ix, iy, iw, ih, fill);
  if (ih > 3) {
    rect(ix + 1, iy, iw - 2, 1, lerp(fill, 0xFFFFFFFF, 0.45f));
    rect(ix + 1, iy + ih - 1, iw - 2, 1, lerp(fill, C_INK, 0.12f));
  }
}

void paper(int w, u32 frame, int shift) {
  const int cell = 30;
  // One square diagonally every 4.5 s, like PaperBackground (2 squares / 9 s).
  int d = (int)((frame * 2 * cell) / (60 * 9)) % (cell * 2);
  int ox = d, oy = d;
  rect(0, 0, w, SCR_H, C_PAPER);
  for (int y = -cell * 2 - oy, row = 0; y < SCR_H + cell; y += cell, row++) {
    for (int x = -cell * 2 + ox + shift, col = 0; x < w + cell; x += cell, col++) {
      if (((row + col) & 1) == 0) rect(x, y, cell, cell, C_PAPER_D);
    }
  }
}

// ----------------------------------------------------------------- text ----
static int glyphW(char c) {
  if (c < 32 || c > 126) return 0;
  int w = FONT_W[c - 32];
  return w ? w : 6;
}

int textWidth(const char* s) {
  int w = 0;
  for (; *s && *s != '\n'; s++) w += glyphW(*s);
  return w;
}

static void glyph(int x, int y, char c, u32 color, int scale) {
  if (c <= 32 || c > 126) return;
  int i = c - 32, gx = (i % FONT_COLS) * FONT_CELL, gy = (i / FONT_COLS) * FONT_CELL;
  const Tex3DS_SubTexture* st = fontImg.subtex;
  float iw = st->width, ih = st->height;
  Tex3DS_SubTexture sub = {
      .width = FONT_CELL,
      .height = FONT_CELL,
      .left = st->left + (st->right - st->left) * (gx / iw),
      .right = st->left + (st->right - st->left) * ((gx + FONT_CELL) / iw),
      .top = st->top + (st->bottom - st->top) * (gy / ih),
      .bottom = st->top + (st->bottom - st->top) * ((gy + FONT_CELL) / ih),
  };
  C2D_Image img = {fontImg.tex, &sub};
  C2D_ImageTint tint;
  C2D_PlainImageTint(&tint, color, 1.0f);
  // The glyph cell's baseline sits 12 px down; callers pass the text top.
  C2D_DrawImageAt(img, x + drawDX, y - 2 * scale, 0.5f, &tint, scale, scale);
}

int textBig(int x, int y, const char* s, u32 color, int scale, Align align) {
  int w = textWidth(s) * scale;
  if (align == ALIGN_CENTER) x -= w / 2;
  if (align == ALIGN_RIGHT) x -= w;
  for (; *s; s++) {
    glyph(x, y, *s, color, scale);
    x += glyphW(*s) * scale;
  }
  return w;
}

int text(int x, int y, const char* s, u32 color, Align align) {
  return textBig(x, y, s, color, 1, align);
}

int textWrap(int x, int y, int width, const char* s, u32 color, int maxChars) {
  int lines = 1, cx = 0, drawn = 0;
  const char* p = s;
  while (*p) {
    if (*p == '\n') {
      lines++, cx = 0, p++, drawn++;
      continue;
    }
    if (*p == ' ') {
      if (cx) cx += glyphW(' ');
      p++, drawn++;
      continue;
    }
    const char* e = p;
    int ww = 0;
    while (*e && *e != ' ' && *e != '\n') ww += glyphW(*e++);
    if (cx && cx + ww > width) lines++, cx = 0;
    for (; p < e; p++, drawn++) {
      if (maxChars >= 0 && drawn >= maxChars) return lines;
      glyph(x + cx, y + (lines - 1) * LINE_H, *p, color, 1);
      cx += glyphW(*p);
    }
  }
  return lines;
}

// --------------------------------------------------------------- images ----
void image(C2D_SpriteSheet s, int index, int x, int y) {
  C2D_DrawImageAt(C2D_SpriteSheetGetImage(s, index), x + drawDX, y, 0.5f, NULL, 1, 1);
}

void imageTint(C2D_SpriteSheet s, int index, int x, int y, u32 color, float blend) {
  C2D_ImageTint tint;
  C2D_PlainImageTint(&tint, color, blend);
  C2D_DrawImageAt(C2D_SpriteSheetGetImage(s, index), x + drawDX, y, 0.5f, &tint, 1, 1);
}

// ---------------------------------------------------------------- moods ----
static int curMood = -1;
static C2D_SpriteSheet moodSheets[4];

void moodLoad(int mood) {
  if (mood == curMood) return;
  for (int i = 0; i < 4; i++) {
    if (moodSheets[i]) C2D_SpriteSheetFree(moodSheets[i]);
    moodSheets[i] = NULL;
  }
  const MoodSheet* m = &MOOD_SHEETS[mood];
  char path[64];
  for (int i = 0; i < m->sheets && i < 4; i++) {
    snprintf(path, sizeof path, "romfs:/gfx/mood_%s_%d.t3x", m->name, i);
    moodSheets[i] = load(path);
  }
  curMood = mood;
}

int moodLoaded(void) { return curMood; }

void moodDraw(int x, int y, u32 tick) { moodDrawEx(x, y, tick, 1, 1, 0); }

void moodDrawEx(int x, int y, u32 tick, float sx, float sy, int lift) {
  if (curMood < 0) return;
  const MoodSheet* m = &MOOD_SHEETS[curMood];
  int step = tick / 2;  // 30 fps
  int n = m->frames, i;
  if (m->pingPong) {
    int len = 2 * n - 2;
    i = step % len;
    if (i >= n) i = len - i;
  } else {
    i = step % n;
  }
  C2D_Image img = C2D_SpriteSheetGetImage(moodSheets[i / m->perSheet], i % m->perSheet);
  // Squash and stretch around the bottom middle, like MoleHop.
  int w = (int)(m->w * sx + 0.5f), h = (int)(m->h * sy + 0.5f);
  int dx = x + m->ox + (m->w - w) / 2, dy = y + m->oy + (m->h - h) - lift;
  C2D_DrawImageAt(img, dx + drawDX, dy, 0.5f, NULL, (float)w / m->w, (float)h / m->h);
}

// ------------------------------------------------------------- controls ----
static void notchedU(int x, int y, int w, int h, int u, u32 c) {
  rect(x + u, y, w - 2 * u, h, c);
  rect(x, y + u, w, h - 2 * u, c);
}

void toggle(int x, int y, bool on, int u) {
  notchedU(x, y + 2 * u, 16 * u, 7 * u, u, C_INK);
  notchedU(x, y + u, 16 * u, 7 * u, u, C_INK);
  notchedU(x + u, y + 2 * u, 14 * u, 5 * u, u, on ? C_GOOD : C_CREAM_D);
  int kx = x + (on ? 8 * u : 0);
  notchedU(kx, y, 8 * u, 8 * u, u, C_INK);
  notchedU(kx + u, y + u, 6 * u, 6 * u, u, C_CREAM);
  rect(kx + 2 * u, y + u, 4 * u, u, 0xFFFFFFFF);
}

void slider(int x, int y, int w, int count, int index, int u) {
  int ty = y + 3 * u;
  notchedU(x, ty + u, w, 7 * u, u, C_INK);
  notchedU(x, ty, w, 7 * u, u, C_INK);
  int ix = x + u, iw = w - 2 * u, iy = ty + u, ih = 5 * u;
  int seg = (iw - (count - 1) * u) / count;
  ix += (iw - (seg * count + (count - 1) * u)) / 2;
  for (int i = 0; i < count; i++) {
    int sx = ix + i * (seg + u);
    rect(sx, iy, seg, ih, i <= index ? C_BAND : C_CREAM_D);
    if (i <= index) rect(sx, iy, seg, u, lerp(C_BAND, 0xFFFFFFFF, 0.35f));
  }
  int cx = ix + index * (seg + u) + seg / 2;
  int tx = cx - 4 * u;
  notchedU(tx, y + u, 8 * u, 12 * u, u, C_INK);
  notchedU(tx, y, 8 * u, 12 * u, u, C_INK);
  notchedU(tx + u, y + u, 6 * u, 10 * u, u, C_CREAM);
  rect(tx + 2 * u, y + u, 4 * u, u, 0xFFFFFFFF);
  for (int i = 0; i < 3; i++) rect(tx + 3 * u, y + (4 + i * 2) * u, 2 * u, u, C_DIRT);
}

void moreArrow(int x, int y, u32 tick) {
  if ((tick / 20) % 2) y += 1;
  for (int i = 0; i < 4; i++) rect(x + i, y + i, 7 - 2 * i, 1, C_BAND);
}

// The screens are portrait framebuffers turned sideways, so the scissor box
// is in rotated coordinates.
void clipBottom(int x, int y, int w, int h) {
  // Follows the page while it slides, kept on the screen.
  x += drawDX;
  if (x < 0) w += x, x = 0;
  if (x + w > BOT_W) w = BOT_W - x;
  if (w <= 0) w = 1, x = x >= BOT_W ? BOT_W - 1 : 0;
  C2D_Flush();
  C3D_SetScissor(GPU_SCISSOR_NORMAL, SCR_H - (y + h), BOT_W - (x + w), SCR_H - y, BOT_W - x);
}

void noClip(void) {
  C2D_Flush();
  C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}
