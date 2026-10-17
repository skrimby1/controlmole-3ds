#include "sound.h"

#include <3ds.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 22050
#define CH_SFX 0       // 0-3 take turns for effects
#define CH_MUSIC 5
#define CH_HOME 6      // the home music, under everything
#define CH_TALK 7      // 7-9 take turns for the babble
// babble.pcm holds one blip per semitone from MIDI 50 to 86, each 42 ms long
// (tools/make_3ds_assets.py), so no blip is ever resampled.
#define BABBLE_LO 50
#define BABBLE_HI 86
#define BABBLE_LEN 926  // samples: 0.042 s at 22050 Hz

static const char* NAMES[SFX_COUNT] = {"tap", "tab", "toggle_on", "toggle_off", "tick", "hop",
                                       "thud", "grumble", "trophy", "sting", "chime", "plop"};

typedef struct {
  s16* data;
  u32 samples;
} Clip;

static Clip clips[SFX_COUNT], talk, music, home;
static ndspWaveBuf bufs[4], talkBuf[3], musicBuf, homeBuf;
static bool on;
static int nextSfx, nextTalk;

static Clip load(const char* name) {
  char path[64];
  snprintf(path, sizeof path, "romfs:/sfx/%s.pcm", name);
  Clip c = {0};
  FILE* f = fopen(path, "rb");
  if (!f) return c;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  c.data = linearAlloc(n);
  if (c.data && fread(c.data, 1, n, f) == (size_t)n) {
    c.samples = n / 2;
    DSP_FlushDataCache(c.data, n);
  }
  fclose(f);
  return c;
}

static void channel(int ch, float rate) {
  ndspChnReset(ch);
  ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
  ndspChnSetRate(ch, rate);
  ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
  float mix[12] = {1.0f, 1.0f};
  ndspChnSetMix(ch, mix);
}

void soundInit(void) {
  if (R_FAILED(ndspInit())) return;
  on = true;
  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  for (int i = 0; i < SFX_COUNT; i++) clips[i] = load(NAMES[i]);
  talk = load("babble");
  music = load("disco");
  home = load("burrow");
  for (int ch = 0; ch <= CH_TALK + 2; ch++) channel(ch, SR);
  float mix[12] = {0.6f, 0.6f};
  ndspChnSetMix(CH_HOME, mix);
}

void soundExit(void) {
  if (!on) return;
  for (int ch = 0; ch <= CH_TALK + 2; ch++) ndspChnWaveBufClear(ch);
  for (int i = 0; i < SFX_COUNT; i++) linearFree(clips[i].data);
  linearFree(talk.data);
  linearFree(music.data);
  linearFree(home.data);
  ndspExit();
}

static void play(int ch, ndspWaveBuf* wb, const Clip* c, bool loop) {
  if (!on || !c->data) return;
  ndspChnWaveBufClear(ch);
  memset(wb, 0, sizeof *wb);
  wb->data_vaddr = c->data;
  wb->nsamples = c->samples;
  wb->looping = loop;
  ndspChnWaveBufAdd(ch, wb);
}

void sfx(Sfx s) {
  int ch = CH_SFX + nextSfx;
  nextSfx = (nextSfx + 1) % 4;
  play(ch, &bufs[ch - CH_SFX], &clips[s], false);
}

// Mood pitches and vowel steps from the rant clips (tools/make_rant_media.py).
static const int BASE[MOOD_COUNT] = {410, 470, 400, 430, 480, 340, 390, 520, 360, 300};

void babble(char c, Mood mood) {
  if (!on || !talk.data || !isalnum((unsigned char)c)) return;
  int semis;
  switch (tolower((unsigned char)c)) {
    case 'a': semis = 0; break;
    case 'e': semis = 3; break;
    case 'i': semis = 7; break;
    case 'o': semis = -2; break;
    case 'u': semis = -4; break;
    default: semis = rand() % 5 - 2;
  }
  if (isupper((unsigned char)c)) semis += 2;
  // The nearest semitone to the mood's base pitch moved by [semis].
  int midi = (int)lroundf(69 + 12 * log2f(BASE[mood] / 440.0f)) + semis;
  if (midi < BABBLE_LO) midi = BABBLE_LO;
  if (midi > BABBLE_HI) midi = BABBLE_HI;
  int ch = CH_TALK + nextTalk;
  ndspWaveBuf* wb = &talkBuf[nextTalk];
  nextTalk = (nextTalk + 1) % 3;
  ndspChnWaveBufClear(ch);
  memset(wb, 0, sizeof *wb);
  wb->data_vaddr = talk.data + (midi - BABBLE_LO) * BABBLE_LEN;
  wb->nsamples = BABBLE_LEN;
  ndspChnWaveBufAdd(ch, wb);
}

void musicPlay(void) {
  ndspChnSetRate(CH_MUSIC, SR);
  float mix[12] = {0.5f, 0.5f};  // under the jingle and Mole's babble
  ndspChnSetMix(CH_MUSIC, mix);
  play(CH_MUSIC, &musicBuf, &music, true);
}

void musicStop(void) {
  if (on) ndspChnWaveBufClear(CH_MUSIC);
}

void homePlay(void) {
  if (!on) return;
  if (ndspChnIsPlaying(CH_HOME)) {
    ndspChnSetPaused(CH_HOME, false);
    return;
  }
  play(CH_HOME, &homeBuf, &home, true);
}

void homePause(void) {
  if (on) ndspChnSetPaused(CH_HOME, true);
}
