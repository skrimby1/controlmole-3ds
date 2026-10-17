#include "mole.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int clampPts(float ratio, int max) {
  if (ratio < 0) ratio = 0;
  if (ratio > 1) ratio = 1;
  return (int)(ratio * max + 0.5f);
}

static const char* plural(int n) { return n == 1 ? "" : "s"; }

void rotScore(RotScore* out, const Today* t, int limit, int sessionLimit) {
  RotPart* p = out->parts;
  p[0] = (RotPart){"minutes", "Minutes", clampPts(limit ? (float)t->used / limit : 1, 35), 35};
  snprintf(p[0].detail, sizeof p[0].detail, "%d of %d min", t->used, limit);

  p[1] = (RotPart){"sessions", "Longest sitting",
                   clampPts(t->longestMin / (sessionLimit * 1.5f), 25), 25};
  if (t->longestMin == 0)
    snprintf(p[1].detail, sizeof p[1].detail, "none yet");
  else
    snprintf(p[1].detail, sizeof p[1].detail, "%d min on %.40s", t->longestMin, t->longestGame);

  p[2] = (RotPart){"hopping", "Game hopping", clampPts(t->hopBursts / 3.0f, 20), 20};
  if (t->hopBursts == 0)
    snprintf(p[2].detail, sizeof p[2].detail, "calm");
  else
    snprintf(p[2].detail, sizeof p[2].detail, "%d burst%s, up to %d switches", t->hopBursts,
             plural(t->hopBursts), t->maxSwitches);

  p[3] = (RotPart){"checks", "Quick checks", clampPts(t->quickChecks / 15.0f, 20), 20};
  snprintf(p[3].detail, sizeof p[3].detail, "%d of %d launches under 2 min", t->quickChecks,
           t->opens);

  out->total = 0;
  for (int i = 0; i < 4; i++) out->total += p[i].points;
}

const char* rotVerdict(int total) {
  return total >= 85 ? "ROTTEN" : total >= 60 ? "ROTTING" : total >= 35 ? "SO-SO"
       : total >= 15 ? "GOOD" : "AMAZING";
}

int rotTone(const RotPart* p) {
  float r = p->max ? (float)p->points / p->max : 0;
  return r >= 0.66f ? 2 : r >= 0.33f ? 1 : 0;
}

static int hourOf(s64 now) {
  time_t tt = (time_t)now;
  return gmtime(&tt)->tm_hour;  // the 3DS clock is local time already
}

bool afterBedtime(s64 now, bool on, int hour) {
  if (!on) return false;
  int h = hourOf(now);
  return hour < 5 ? (h >= hour && h < 5) : (h >= hour || h < 5);
}

// Mole talks like a village neighbour: chatty, warm, a little cheeky, with
// flowing sentences and his catchphrase "dig dig".

static void add(MoleState* s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

#include <stdarg.h>
static void add(MoleState* s, const char* fmt, ...) {
  if (s->count >= MAX_LINES) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s->lines[s->count++], sizeof s->lines[0], fmt, ap);
  va_end(ap);
}

static const char* mins(int n, char* buf) {
  snprintf(buf, 24, n == 1 ? "1 minute" : "%d minutes", n);
  return buf;
}

static void habitLines(MoleState* s, const RotScore* score, const Today* t, int limit) {
  const RotPart* worst = &score->parts[0];
  for (int i = 1; i < 4; i++) {
    float a = (float)worst->points / worst->max, b = (float)score->parts[i].points / score->parts[i].max;
    if (b > a) worst = &score->parts[i];
  }
  char buf[24];
  if (!strcmp(worst->key, "sessions")) {
    add(s, "Hey, so... %s straight on %s? That's not a little break anymore, that's "
           "practically moving in, dig dig!", mins(t->longestMin, buf), t->longestGame);
    add(s, "Long sittings are super sneaky, you know? You blink once and the whole afternoon's "
           "gone, so maybe save it at the next checkpoint?");
  } else if (!strcmp(worst->key, "hopping")) {
    add(s, "You did %d bursts of game hopping today! Your poor cartridge slot must be dizzy, "
           "so why not pick one and give it a proper go?", t->hopBursts);
    add(s, "Up to %d game switches in ten minutes, wow! Nothing's gonna be more fun just "
           "because you swapped it, promise.", t->maxSwitches);
  } else if (!strcmp(worst->key, "checks")) {
    add(s, "%d quick peeks today, huh? I checked your town too, and guess what... the weeds "
           "are fine, nothing new happened!", t->quickChecks);
    add(s, "Open, peek, close, open again... that little loop is exactly where the rot sneaks "
           "in, dig dig.");
  } else {
    int left = limit - t->used;
    add(s, "That's %d out of %d minutes already! We're getting kinda close, so let's take it "
           "easy for the rest of the day, okay?", t->used, limit);
    add(s, "You've got %s left for today. Spend 'em wisely... or, y'know, don't spend 'em "
           "at all!", mins(left < 0 ? 0 : left, buf));
  }
}

void moleState(MoleState* s, const Today* t, int limit, int sessionLimit, int streak,
               bool bedtimeOn, int bedtimeHour, s64 now, bool hasLog) {
  memset(s, 0, sizeof *s);
  if (!hasLog) {
    s->mood = MOOD_FOCUSED;
    add(s, "Hiya! I can't read your Activity Log yet, so I'm kinda digging around in the "
           "dark here. Play something and come back, okay?");
    return;
  }
  RotScore score;
  rotScore(&score, t, limit, sessionLimit);
  bool hopped = t->lastBurstAt && now - t->lastBurstAt < 10 * 60;
  char buf[24];

  Mood m;
  if (t->used >= limit || score.total >= 85) m = MOOD_FURIOUS;
  else if (hopped) m = MOOD_SHOCKED;
  else if (afterBedtime(now, bedtimeOn, bedtimeHour) && t->used > 0) m = MOOD_ANNOYED;
  else if (score.total >= 60) m = MOOD_UNIMPRESSED;
  else if (score.total >= 35) m = MOOD_FOCUSED;
  else if (t->used == 0 && t->opens == 0) m = MOOD_ZEN;
  else if (streak >= 3) m = MOOD_SMUG;
  else m = MOOD_PROUD;
  s->mood = m;

  switch (m) {
    case MOOD_FURIOUS:
      if (t->used >= limit) {
        add(s, "Whoa, whoa, WHOA! That's %s today and your limit was %d. I'm not mad... okay, "
               "I'm a little mad. Save and power down, pretty please with dirt on top!",
            mins(t->used, buf), limit);
        add(s, "Hey! We shook paws on %d minutes, remember? It's %d now, so I'm closing the "
               "burrow gates for today, dig dig!", limit, t->used);
        add(s, "You know what time it is? It's close-the-lid o'clock, and honestly it's been "
               "that time for a while now!");
      } else {
        add(s, "Okay, deep breath... you broke your own rule, buddy! Let's close the lid and go "
               "do literally anything else, dig dig!");
        habitLines(s, &score, t, limit);
      }
      break;
    case MOOD_ANNOYED:
      add(s, "Uh-huh, I see you playing past %02d:00! Even moles go to bed eventually, and "
             "tomorrow-you is gonna be so grumpy about this.", bedtimeHour);
      add(s, "Psst! Bedtime was %02d:00, you know. The game will still be there tomorrow, I "
             "promise, so it's burrow time now!", bedtimeHour);
      break;
    case MOOD_SHOCKED:
      add(s, "Whoa, %d game switches in ten minutes?! My little head is spinning... what were "
             "you even looking for, huh?", t->lastBurstSwitches);
      add(s, "Swap, swap, swap... you're hopping around more than a bunny back there! Pick one "
             "game and stick with it for a bit, okay?");
      break;
    case MOOD_UNIMPRESSED:
      habitLines(s, &score, t, limit);
      break;
    case MOOD_FOCUSED:
      add(s, "Rot score %d, huh? Not amazing, not terrible either, so let's keep it right "
             "there, deal?", score.total);
      habitLines(s, &score, t, limit);
      break;
    case MOOD_ZEN:
      add(s, "Not a single game today! The burrow's so peaceful I could take a nap right here, "
             "dig dig.");
      add(s, "Zero minutes so far! Honestly, I could get really used to days like this, you "
             "know?");
      break;
    case MOOD_SMUG:
      add(s, "%d days in a row under your limit! Didn't I tell you we could do it? I totally "
             "did, I remember.", streak);
      add(s, "Day %d of keeping the lid closed! I'm starting to think you're kind of a legend, "
             "you know that?", streak);
      break;
    case MOOD_PROUD:
      add(s, "A rot score of %d, with short sittings and no hopping around... I'm seriously "
             "impressed, buddy!", score.total);
      add(s, "Only %s of play today, and you stayed in control the whole time. That's my "
             "human!", mins(t->used, buf));
      break;
    default:
      add(s, "Headband's on and I'm all fired up! Nothing's getting past us today, dig dig!");
      break;
  }
}
