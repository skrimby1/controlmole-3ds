#include "titles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SMDH layout (3dbrew.org/wiki/SMDH): 16 app titles, settings, then the
// 24 x 24 and 48 x 48 icons in RGB565, tiled 8 x 8 in Morton order.
typedef struct {
  u16 shortName[0x40];
  u16 longName[0x80];
  u16 publisher[0x40];
} SmdhTitle;

typedef struct {
  u32 magic;
  u16 version, reserved0;
  SmdhTitle titles[16];
  u8 settings[0x30];
  u8 reserved1[8];
  u16 smallIcon[24 * 24];
  u16 largeIcon[48 * 48];
} Smdh;

typedef struct {
  u64 tid;
  bool tried, hasIcon;
  char name[48];
  C3D_Tex tex;
  Tex3DS_SubTexture sub;
  C2D_Image img;
} Entry;

#define MAX_TITLES 96
static Entry cache[MAX_TITLES];
static int nCache;
static u8 lang = CFG_LANGUAGE_EN;

void titlesInit(void) {
  if (R_SUCCEEDED(cfguInit())) {
    CFGU_GetSystemLanguage(&lang);
    cfguExit();
  }
  if (lang >= 16) lang = CFG_LANGUAGE_EN;
}

void titlesExit(void) {
  for (int i = 0; i < nCache; i++)
    if (cache[i].hasIcon) C3D_TexDelete(&cache[i].tex);
  nCache = 0;
}

// Wild World only has ASCII: fold Latin-1 accents, drop the rest.
static void ascii(const u16* src, int n, char* out, int outSize) {
  static const char* latin = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
  int j = 0;
  for (int i = 0; i < n && src[i] && j < outSize - 1; i++) {
    u16 c = src[i];
    if (c == '\n') c = ' ';
    if (c >= 32 && c < 127) out[j++] = c;
    else if (c >= 0xC0 && c <= 0xFF) out[j++] = latin[c - 0xC0];
    else if (c == 0x2019) out[j++] = '\'';
  }
  while (j && out[j - 1] == ' ') j--;
  out[j] = 0;
}

static bool readSmdh(u64 tid, Smdh* smdh) {
  static const u32 path[5] = {0, 0, 2, 0x6E6F6369 /* "icon" */, 0};
  const FS_MediaType media[] = {MEDIATYPE_SD, MEDIATYPE_NAND, MEDIATYPE_GAME_CARD};
  for (int m = 0; m < 3; m++) {
    u32 arch[4] = {(u32)tid, (u32)(tid >> 32), media[m], 0};
    FS_Path archPath = {PATH_BINARY, sizeof arch, arch};
    FS_Path filePath = {PATH_BINARY, sizeof path, path};
    Handle h;
    if (R_FAILED(FSUSER_OpenFileDirectly(&h, ARCHIVE_SAVEDATA_AND_CONTENT, archPath, filePath,
                                         FS_OPEN_READ, 0)))
      continue;
    u32 read = 0;
    Result r = FSFILE_Read(h, &read, 0, smdh, sizeof *smdh);
    FSFILE_Close(h);
    if (R_SUCCEEDED(r) && read == sizeof *smdh && smdh->magic == 0x48444D53) return true;
  }
  return false;
}

// Sample titles for a 3DS (or emulator) with no play history yet.
static const char* sampleName(u64 tid) {
  switch (tid & 0xFF) {
    case 1: return "Village Life";
    case 2: return "Kart Rally 3D";
    case 3: return "Monster Catcher";
    case 4: return "Block Puzzle";
    case 5: return "Sky Story";
  }
  return NULL;
}

static Entry* lookup(u64 tid) {
  for (int i = 0; i < nCache; i++)
    if (cache[i].tid == tid) return &cache[i];
  Entry* e = nCache < MAX_TITLES ? &cache[nCache++] : &cache[MAX_TITLES - 1];
  memset(e, 0, sizeof *e);
  e->tid = tid;
  e->tried = true;
  const char* sample = (tid >> 32) == 0x000400FF ? sampleName(tid) : NULL;
  if (sample) {
    snprintf(e->name, sizeof e->name, "%s", sample);
    return e;
  }
  Smdh* smdh = malloc(sizeof *smdh);
  if (smdh && readSmdh(tid, smdh)) {
    ascii(smdh->titles[lang].shortName, 0x40, e->name, sizeof e->name);
    if (!e->name[0]) ascii(smdh->titles[CFG_LANGUAGE_EN].shortName, 0x40, e->name, sizeof e->name);
    if (C3D_TexInit(&e->tex, 64, 64, GPU_RGB565)) {
      // Both are 8 x 8 Morton tiles in the same order: the 48 px icon's tile
      // rows go into the first 6 tile rows of the 64 px texture.
      u16* dst = e->tex.data;
      memset(dst, 0, 64 * 64 * 2);
      for (int ty = 0; ty < 6; ty++)
        for (int tx = 0; tx < 6; tx++)
          memcpy(dst + (ty * 8 + tx) * 64, smdh->largeIcon + (ty * 6 + tx) * 64, 64 * 2);
      C3D_TexFlush(&e->tex);
      C3D_TexSetFilter(&e->tex, GPU_NEAREST, GPU_NEAREST);
      e->sub = (Tex3DS_SubTexture){48, 48, 0.0f, 1.0f, 0.75f, 0.25f};
      e->img = (C2D_Image){&e->tex, &e->sub};
      e->hasIcon = true;
    }
  }
  free(smdh);
  if (!e->name[0]) snprintf(e->name, sizeof e->name, "Title %08lX", (u32)tid);
  return e;
}

const char* titleName(u64 tid) { return lookup(tid)->name; }

const C2D_Image* titleIcon(u64 tid) {
  Entry* e = lookup(tid);
  return e->hasIcon ? &e->img : NULL;
}
