#include "anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
  char magic[4];
  u16 frames, w, h, texW, texH, fmt, fps, tileRows;
  s16 ox, oy;
} Header;

bool animOpen(Anim* a, const char* name) {
  memset(a, 0, sizeof *a);
  a->shown = -1;
  char path[64];
  snprintf(path, sizeof path, "romfs:/anim/%s.bin", name);
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  a->file = malloc(n);
  bool ok = a->file && fread(a->file, 1, n, f) == (size_t)n;
  fclose(f);
  const Header* h = (const Header*)a->file;
  if (!ok || memcmp(h->magic, "MOLA", 4)) {
    animClose(a);
    return false;
  }
  a->frames = h->frames, a->w = h->w, a->h = h->h, a->fps = h->fps, a->ox = h->ox, a->oy = h->oy;
  a->tileBytes = h->tileRows * 8 * h->texW * 2;
  if (!C3D_TexInit(&a->tex, h->texW, h->texH, h->fmt ? GPU_RGBA5551 : GPU_RGB565)) {
    animClose(a);
    return false;
  }
  C3D_TexSetFilter(&a->tex, GPU_NEAREST, GPU_NEAREST);
  memset(a->tex.data, 0, a->tex.size);
  // Same mapping as the game icons: the first memory row is the top (v = 1).
  a->sub = (Tex3DS_SubTexture){h->w, h->h, 0.0f, 1.0f, (float)h->w / h->texW,
                               1.0f - (float)h->h / h->texH};
  return true;
}

void animClose(Anim* a) {
  if (a->tex.data) C3D_TexDelete(&a->tex);
  free(a->file);
  memset(a, 0, sizeof *a);
}

C2D_Image animFrame(Anim* a, int i) {
  if (i < 0) i = 0;
  if (i >= a->frames) i = a->frames - 1;
  if (i != a->shown) {
    const u32* offs = (const u32*)(a->file + sizeof(Header));
    uLongf size = a->tileBytes;
    uncompress(a->tex.data, &size, a->file + offs[i], offs[i + 1] - offs[i]);
    C3D_TexFlush(&a->tex);
    a->shown = i;
  }
  return (C2D_Image){&a->tex, &a->sub};
}
