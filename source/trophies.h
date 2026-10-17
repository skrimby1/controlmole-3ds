// The trophy shelf, ported from app/lib/mole/achievements.dart (the ones that
// make sense on a 3DS). Order matches TROPHIES in tools/make_3ds_assets.py.
#pragma once
#include <3ds.h>
#include <stdbool.h>

typedef struct {
  const char* id;
  const char* name;
  const char* how;
  const char* cheer;
  bool secret;
  int goal;  // 0 = not a counting one
} Trophy;

#define TROPHIES 12
extern const Trophy trophyList[TROPHIES];

// Earns whatever the finished days add up to now. Returns a bit mask of the
// fresh ones (Mole announces those). Trophies are never taken away.
u32 trophiesCheck(s64 now);
// Progress towards a counting trophy.
int trophyProgress(int i, s64 now);
