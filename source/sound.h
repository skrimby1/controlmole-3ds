#pragma once
#include <stdbool.h>

#include "mole.h"

typedef enum {
  SFX_TAP, SFX_TAB, SFX_TOGGLE_ON, SFX_TOGGLE_OFF, SFX_TICK, SFX_HOP, SFX_THUD,
  SFX_GRUMBLE, SFX_TROPHY, SFX_STING, SFX_CHIME, SFX_PLOP, SFX_COUNT
} Sfx;

void soundInit(void);
void soundExit(void);
void sfx(Sfx s);
// One babble blip for a typed character, pitched by the letter and his mood.
void babble(char c, Mood mood);
void musicPlay(void);
void musicStop(void);
// The home music (marketing/music/mole_burrow.song): loops; pausing keeps its place.
void homePlay(void);
void homePause(void);
