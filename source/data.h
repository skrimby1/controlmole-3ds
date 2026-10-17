// Settings, the play log and the day log. Everything lives in
// sdmc:/3ds/ControlMole/ so the Luma plugin (plugin/) can read the same rules.
#pragma once
#include <3ds.h>
#include <stdbool.h>

#include "mole.h"

#define DATA_DIR "sdmc:/3ds/ControlMole"
// ControlMole's own title (the CIA, tools in 3ds/cia.py): never counted as play.
#define SELF_TID 0x000400000C0DE500ULL

// One weekday's own rules (the phone's weekly schedule): its daily limit and
// the bedtime for that day's night. Days without them follow the everyday ones.
typedef struct {
  bool own;
  int limit, bedtime;
} DayRules;

typedef struct {
  int limit;          // daily minutes, every day
  int sessionLimit;   // a long sitting, in minutes
  bool bedtimeOn;
  int bedtimeHour;
  bool block;         // the plugin stops games past the limit
  s64 installDay;     // first day Mole watched in full is the day after
  u32 trophies;       // earned bits, never cleared
  s64 earnedAt[16];
  int walkAways;      // "Okay, I'm done" on the plugin's block screen
  bool tightened;
  bool cleaned;       // the one-time trophy cleanup has run (see trophiesCheck)
  int skipped;        // titles that don't count, below
  u64 skip[32];
  DayRules days[7];   // Monday = 0 ... Sunday = 6
} Settings;

extern Settings settings;
void settingsLoad(void);
void settingsSave(void);

#define LIMIT_MAX (24 * 60)
#define SITTING_MAX (12 * 60)
// Monday = 0 for the day that starts at [day].
int weekday(s64 day);
// The daily limit on the day that starts at [day].
int limitOn(s64 day);
// Tonight's bedtime: after midnight (until 5) it's still the evening before's.
int bedtimeAt(s64 now);

// One sitting in a game, in local unix seconds.
typedef struct {
  u64 tid;
  s64 start, end;
} Sitting;

#define MAX_SITTINGS 512
typedef struct {
  Sitting s[MAX_SITTINGS];
  int count;
  bool real;          // false: sample data (emulator with no history)
  const char* source; // where it came from, for the about line
} PlayLog;

extern PlayLog playlog;
void playlogLoad(void);

bool watched(u64 tid);
s64 dayStart(s64 t);
// Today's behaviour from the play log, for the day that starts at [day].
void today(Today* out, s64 day, s64 now);
// Minutes in watched games on [day].
int minutesOn(s64 day, s64 now);

// Per-title minutes today, for the GAMES tab.
typedef struct {
  u64 tid;
  int minutes, launches;
} GameRow;
int gamesOn(s64 day, s64 now, GameRow* rows, int max);

// Finished days since install, newest last, for streaks and trophies.
typedef struct {
  s64 day;
  int minutes, limit;
  bool under;
} DayRec;
int pastDays(DayRec* out, int max, s64 now);
// Marks today as a day Mole saw (ControlMole was opened).
void seenToday(s64 now);
int streakNow(s64 now);
