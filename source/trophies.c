#include "trophies.h"

#include <time.h>

#include "data.h"

const Trophy trophyList[TROPHIES] = {
    {"first_dig", "First Dig", "Finish a whole day under your limit.",
     "Look at that, a whole day under your limit! That's your very first trophy, and I dug it "
     "out just for you, dig dig."},
    {"streak_3", "Three Molehills", "Finish 3 days in a row under your limit.",
     "Three days in a row! One molehill is luck and two is a coincidence, but three? That's a "
     "habit, buddy!", false, 3},
    {"streak_7", "Week Underground", "Finish 7 days in a row under your limit.",
     "A whole week under your limit! Seven days of real life... I'm so proud I could cry into my "
     "headband.", false, 7},
    {"streak_30", "Full Moon", "Finish 30 days in a row under your limit.",
     "Thirty days! The moon went all the way round while you kept the rot out. I'm framing this "
     "one, dig dig.", false, 30},
    {"streak_100", "Mole Legend", "Finish 100 days in a row under your limit.",
     "ONE HUNDRED DAYS?! Everybody bow, we've got royalty in the burrow! You're a legend, and "
     "that's official.", false, 100},
    {"weekend", "Weekend Warrior", "Stay under your limit on a Saturday and the Sunday after.",
     "A whole weekend and you didn't sink into a game marathon once! Weekends are the sneakiest, "
     "so that's extra impressive."},
    {"half", "Half Measures", "Finish a day using half your limit or less.",
     "Only half your limit for a whole day! You've got minutes left over, so go spend 'em on "
     "something real."},
    {"zen_day", "Zen Burrow", "Finish a whole day with zero minutes played.",
     "Not one single minute of play all day... shh, listen. That's what peace sounds like, dig "
     "dig.", true},
    {"comeback", "Back in the Dirt", "Lose a streak of 3 or more, then build a new one.",
     "You lost your streak and dug right back in! Falling's easy, but getting back up? That's "
     "what really counts."},
    {"walk_1", "Walked Away", "Pick \"Okay, I'm done\" when I stop a game.",
     "You saw me pop up and just... walked away! No arguing, no sulking. That's the stuff, "
     "buddy."},
    {"walk_10", "Iron Paws", "Walk away from 10 stopped games.",
     "Ten times I stopped you, and ten times you walked away. Paws of iron, my friend, paws of "
     "iron!", false, 10},
    {"tightened", "Tightened Up", "Make one of your rules stricter.",
     "Stricter rules, and all on your own! Most folks only ever try to loosen them, so you're a "
     "rare kind of human."},
};

static DayRec days[400];
static int nDays;

static bool follows(const DayRec* a, const DayRec* b) { return b->day - a->day == 86400; }

static int bestRun(void) {
  int best = 0, run = 0;
  for (int i = 0; i < nDays; i++) {
    if (i == 0 || !follows(&days[i - 1], &days[i])) run = 0;
    run = days[i].under ? run + 1 : 0;
    if (run > best) best = run;
  }
  return best;
}

static bool comeback(void) {
  int run = 0;
  bool lost = false;
  for (int i = 0; i < nDays; i++) {
    if (i == 0 || !follows(&days[i - 1], &days[i])) run = 0;
    if (days[i].under) {
      if (++run >= 3 && lost) return true;
    } else {
      if (run >= 3) lost = true;
      run = 0;
    }
  }
  return false;
}

static bool weekend(void) {
  for (int i = 0; i + 1 < nDays; i++) {
    time_t t = (time_t)days[i].day;
    if (gmtime(&t)->tm_wday == 6 && follows(&days[i], &days[i + 1]) && days[i].under &&
        days[i + 1].under)
      return true;
  }
  return false;
}

static bool test(int i) {
  switch (i) {
    case 0:
      for (int d = 0; d < nDays; d++)
        if (days[d].under) return true;
      return false;
    case 1: return bestRun() >= 3;
    case 2: return bestRun() >= 7;
    case 3: return bestRun() >= 30;
    case 4: return bestRun() >= 100;
    case 5: return weekend();
    case 6:
      for (int d = 0; d < nDays; d++)
        if (days[d].minutes * 2 <= days[d].limit) return true;
      return false;
    case 7:
      for (int d = 0; d < nDays; d++)
        if (days[d].minutes == 0) return true;
      return false;
    case 8: return comeback();
    case 9: return settings.walkAways >= 1;
    case 10: return settings.walkAways >= 10;
    case 11: return settings.tightened;
  }
  return false;
}

u32 trophiesCheck(s64 now) {
  // Sample data never earns anything: only days Mole really watched count.
  nDays = playlog.real ? pastDays(days, 400, now) : 0;
  if (!settings.cleaned) {
    for (int i = 0; i < TROPHIES; i++)
      if ((settings.trophies & (1u << i)) && i != 9 && i != 10 && i != 11 && !test(i))
        settings.trophies &= ~(1u << i), settings.earnedAt[i] = 0;
    settings.cleaned = true;
    settingsSave();
  }
  u32 fresh = 0;
  for (int i = 0; i < TROPHIES; i++) {
    if (settings.trophies & (1u << i)) continue;
    if (test(i)) {
      fresh |= 1u << i;
      settings.trophies |= 1u << i;
      settings.earnedAt[i] = now;
    }
  }
  if (fresh) settingsSave();
  return fresh;
}

int trophyProgress(int i, s64 now) {
  if (i >= 1 && i <= 4) return bestRun();
  if (i == 10) return settings.walkAways;
  return 0;
}
