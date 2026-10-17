// Mole's brain, ported from app/lib/mole/mood.dart: the rot score, his mood
// and what he says. Games stand in for apps; the habits are the same four.
#pragma once
#include <3ds.h>
#include <stdbool.h>

typedef enum {
  MOOD_FOCUSED, MOOD_PROUD, MOOD_ZEN, MOOD_SMUG, MOOD_FIRED_UP,
  MOOD_UNIMPRESSED, MOOD_ANNOYED, MOOD_SHOCKED, MOOD_SAD, MOOD_FURIOUS,
  MOOD_COUNT
} Mood;

// How today went, from the play log.
typedef struct {
  int used;            // minutes in watched games
  int opens;           // game launches
  int longestMin;      // longest single sitting
  char longestGame[64];
  int hopBursts;       // bursts of game switching
  int maxSwitches;     // most switches inside one window
  int lastBurstSwitches;
  s64 lastBurstAt;     // unix seconds, 0 = none
  int quickChecks;     // sittings under QUICK_CHECK_S
} Today;

#define QUICK_CHECK_S 120
#define BURST_WINDOW_S 600
#define BURST_SWITCHES 3

typedef struct {
  const char* key;  // minutes, sessions, hopping, checks (habit icon order)
  const char* label;
  int points, max;
  char detail[64];
} RotPart;

typedef struct {
  RotPart parts[4];
  int total;
} RotScore;

void rotScore(RotScore* out, const Today* t, int limit, int sessionLimit);
const char* rotVerdict(int total);
// 0 good, 1 warn, 2 bad: the habit icon's tone.
int rotTone(const RotPart* p);

#define MAX_LINES 4
typedef struct {
  Mood mood;
  int count;
  char lines[MAX_LINES][200];
} MoleState;

void moleState(MoleState* out, const Today* t, int limit, int sessionLimit, int streak,
               bool bedtimeOn, int bedtimeHour, s64 now, bool hasLog);

bool afterBedtime(s64 now, bool on, int hour);
