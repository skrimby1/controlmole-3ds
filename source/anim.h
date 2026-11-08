#pragma once
#include <citro2d.h>
#include <stdbool.h>

typedef struct {
  u8* file;
  int frames, w, h, fps, ox, oy;
  C3D_Tex tex;
  Tex3DS_SubTexture sub;
  int shown;  // frame in the texture, -1 = none
  int tileBytes;
  bool rgb565;
  bool dressed;   // the burst and the dance wear Mole's look (looks.h)
  u32 look;       // the look version the shown frame was dressed in
  char name[12];
} Anim;

bool animOpen(Anim* a, const char* name);
void animClose(Anim* a);
// Fills the texture with frame [i] (call after C3D_FrameBegin) and returns it.
C2D_Image animFrame(Anim* a, int i);
