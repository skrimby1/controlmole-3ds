#pragma once
#include <citro2d.h>
#include <stdbool.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

// Palette (app/lib/theme.dart, MoleColors). ABGR for citro2d.
#define RGB(r, g, b) C2D_Color32(r, g, b, 0xFF)
#define C_PAPER RGB(0xE9, 0xE1, 0xCF)
#define C_PAPER_D RGB(0xE1, 0xD6, 0xBF)
#define C_INK RGB(0x17, 0x14, 0x2B)
#define C_CREAM RGB(0xFB, 0xF3, 0xE2)
#define C_CREAM_D RGB(0xE4, 0xD6, 0xBA)
#define C_CHESTNUT RGB(0x8C, 0x54, 0x33)
#define C_CHESTNUT_D RGB(0x5C, 0x33, 0x1D)
#define C_BAND RGB(0xE5, 0x48, 0x3A)
#define C_BAND_D RGB(0xA9, 0x2C, 0x35)
#define C_DIRT RGB(0xA0, 0x8B, 0x74)
#define C_DIRT_D RGB(0x6D, 0x5C, 0x4D)
#define C_GOOD RGB(0x5F, 0xAE, 0x5A)
#define C_WARN RGB(0xF0, 0xA0, 0x2C)
#define C_MUTED RGB(0x6E, 0x66, 0x80)

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } Align;

void drawInit(void);
void drawExit(void);

// Plain rectangle on whole pixels.
void rect(int x, int y, int w, int h, u32 color);
// PixelBox: outline with one-pixel notched corners, fill, bevel, 1 px drop.
void box(int x, int y, int w, int h, u32 fill, bool pressed);
// The paper checkerboard, drifted by [frame] (60 per second).
void paper(int w, u32 frame, int shift);

// Wild World text. Returns the width in pixels.
int textWidth(const char* s);
int text(int x, int y, const char* s, u32 color, Align align);
// Same, scaled by a whole number (big score digits).
int textBig(int x, int y, const char* s, u32 color, int scale, Align align);
// Wraps [s] into [width]; draws up to [maxChars] characters (typewriter) and
// returns how many lines it took. Line height is 13 px.
int textWrap(int x, int y, int width, const char* s, u32 color, int maxChars);
#define LINE_H 13

// Atlases loaded from romfs.
extern C2D_SpriteSheet sheetHabits, sheetTrophies, sheetMisc;
void image(C2D_SpriteSheet s, int index, int x, int y);
void imageTint(C2D_SpriteSheet s, int index, int x, int y, u32 color, float blend);

// Mood loops: only the one on screen is in memory.
void moodLoad(int mood);
// Draws the 320 x 195 frame with its top-left at (x, y); [tick] counts 60 fps frames.
void moodDraw(int x, int y, u32 tick);
// With MoleHop's squash and stretch ([sx], [sy]) and [lift] px off the ground.
void moodDrawEx(int x, int y, u32 tick, float sx, float sy, int lift);
int moodLoaded(void);

// PixelToggle and PixelSlider, at [u] screen pixels per art pixel.
void toggle(int x, int y, bool on, int u);   // 16u x 9u
void slider(int x, int y, int w, int count, int index, int u);  // w x 13u
// A blinking down arrow (dialogue "more").
void moreArrow(int x, int y, u32 tick);

// Clips drawing to a box on the bottom screen (the lists scroll under it).
void clipBottom(int x, int y, int w, int h);
void noClip(void);
