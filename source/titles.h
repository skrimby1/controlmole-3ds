// Game names and 48 x 48 icons, read from each title's SMDH.
#pragma once
#include <3ds.h>
#include <citro2d.h>

void titlesInit(void);
void titlesExit(void);
const char* titleName(u64 tid);
// NULL if the title isn't installed (or is sample data).
const C2D_Image* titleIcon(u64 tid);
