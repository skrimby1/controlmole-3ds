// Mole's Dress up looks, as in the phone app: a fur, a headband, a hat,
// something on his body and an accessory. Furs and headbands are colour
// swaps of his textures as they load; the pieces are overlays drawn over
// him frame by frame (romfs/looks/, from tools/make_3ds_looks.py).
#pragma once
#include <citro2d.h>
#include <stdbool.h>

#include "gen_looks.h"

// Indexes into LOOK_FURS, LOOK_BANDS and LOOK_PIECES (piece 0 is "none").
typedef struct {
  int fur, band, hat, body, accessory;
} Look;

extern const Look LOOK_CLASSIC;

// "fur/band/hat/outfit/accessory", the phone app's MoleLook.key.
void lookParse(const char* key, Look* out);
void lookKey(const Look* l, char* buf, int n);
bool lookEqual(const Look* a, const Look* b);

// Whether [trophies] (earned bits) unlock a fur or headband here; every
// piece is free on the 3DS.
bool furEarned(int fur, u32 trophies);
bool bandEarned(int band, u32 trophies);

// The look Mole shows. Changing it reloads what he wears; [moodKey] is the
// mood loop on screen (gen_assets.h's names).
void looksShow(const Look* l);
const Look* looksShown(void);
// Bumped every time the shown look changes, so textures know to redo.
u32 looksVersion(void);

// Recolours a mood sheet's frames and clears what a costume hides on them.
// [frames] are the sheet's images, [first] the loop's frame index of the
// sheet's first one; (ox, oy) puts a frame back on the 320 px canvas.
void looksDressMood(const char* mood, C2D_Image* frames, int n, int first, int ox, int oy);
// The pieces over a mood frame, scaled like Mole around (bx, by): the
// canvas point (cx, cy) lands at bx + (cx - ox) * sx.
void looksDrawMood(int frame, float bx, float by, int ox, int oy, float sx, float sy);

// How far to move him down so the worn pieces' tops show on a screen whose
// first row is frame row [-frameTop] (0 if they already fit, at most
// LOOK_ROOM_LIFT).
int looksLift(int frameTop);

// The burst and the dance: recolour a frame just inflated into an RGBA5551
// or RGB565 texture (w x h at the texture's top-left), and the pieces over it.
void looksDressFrame(const char* anim, int frame, C3D_Tex* tex, int w, int h, int ox, int oy,
                     bool rgb565);
void looksDrawFrame(const char* anim, int frame, float x, float y);

// Copies the worn pieces' plugin frames to the SD card, where the plugin
// reads them (sdmc:/3ds/ControlMole/look_<slot>.bin).
void looksWritePlugin(const Look* l);

void looksExit(void);
