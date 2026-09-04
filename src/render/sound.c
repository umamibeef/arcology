/*  sound.c -- the game's sounds through SDL_AudioStream.
 *
 *  One stream per playing sound, bound to the device, which mixes them;
 *  a stream that has drained is reused.  The WAVs are 8-bit mono at the
 *  resources' own rates and SDL converts them to the device's format.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "sound.h"

#define N_SOUNDS  (R_SND_LAST - R_SND_FIRST + 1)
#define N_STREAMS 8

static const char *const FILES[N_SOUNDS] = {
    "500-BullDoze.wav", "501-Error.wav", "502-WIND.wav", "503-PLOP.wav",
    "504-Explode.wav", "505-Click.wav", "506-POLICE.wav", "507-Looping_Fire.wav",
    "508-Dozer.wav", "509-FireTruck.wav", "510-COPTER.wav", "511-FLOOD.wav",
    "512-BOOS.wav", "513-CHEERS.wav", "514-ZZap.wav", "515-MAYDAY5_VOC.wav",
    "516-IMHIT_VOC.wav", "517-SHIP3_VOC.wav", "518-Takeoff.wav", "519-Land.wav",
    "520-Siren2.wav", "521-Horns7_5.wav", "522-Prison2.wav", "523-ScBell1.wav",
    "524-TrainB7_5.wav", "525-Shot1.wav", "526-Arco2.wav", "527-Roar1.wav"};

struct RSound
{
    SDL_AudioDeviceID dev;
    SDL_AudioSpec     spec[N_SOUNDS];
    Uint8            *data[N_SOUNDS];
    Uint32            len[N_SOUNDS];
    SDL_AudioStream  *stream[N_STREAMS];
    int               loaded;
};

RSound *sound_create(const char *assets_dir)
{
    RSound *s;
    int     k;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return NULL;
    s = (RSound *) calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!s->dev)
    {
        free(s);
        return NULL;
    }
    for (k = 0; k < N_SOUNDS; ++k)
    {
        char path[1024];
        snprintf(path, sizeof path, "%s/sounds/%s", assets_dir, FILES[k]);
        if (SDL_LoadWAV(path, &s->spec[k], &s->data[k], &s->len[k]))
            s->loaded++;
        else
            s->data[k] = NULL;
    }
    return s;
}

void sound_destroy(RSound *s)
{
    int k;
    if (!s)
        return;
    for (k = 0; k < N_STREAMS; ++k)
        if (s->stream[k])
            SDL_DestroyAudioStream(s->stream[k]);
    for (k = 0; k < N_SOUNDS; ++k)
        if (s->data[k])
            SDL_free(s->data[k]);
    SDL_CloseAudioDevice(s->dev);
    free(s);
}

int sound_loaded(const RSound *s)
{
    return s ? s->loaded : 0;
}

void sound_play(RSound *s, int id)
{
    int k = id - R_SND_FIRST, slot = -1, j;
    if (!s || k < 0 || k >= N_SOUNDS || !s->data[k])
        return;
    /* a drained stream, else a fresh one, else the oldest */
    for (j = 0; j < N_STREAMS; ++j)
        if (s->stream[j] && SDL_GetAudioStreamAvailable(s->stream[j]) == 0)
        {
            slot = j;
            break;
        }
    if (slot < 0)
        for (j = 0; j < N_STREAMS; ++j)
            if (!s->stream[j])
            {
                slot = j;
                break;
            }
    if (slot < 0)
        slot = 0;
    if (s->stream[slot])
    {
        SDL_DestroyAudioStream(s->stream[slot]);
        s->stream[slot] = NULL;
    }
    s->stream[slot] = SDL_CreateAudioStream(&s->spec[k], NULL);
    if (!s->stream[slot])
        return;
    if (!SDL_BindAudioStream(s->dev, s->stream[slot]))
    {
        SDL_DestroyAudioStream(s->stream[slot]);
        s->stream[slot] = NULL;
        return;
    }
    SDL_PutAudioStreamData(s->stream[slot], s->data[k], (int) s->len[k]);
    SDL_FlushAudioStream(s->stream[slot]);
}
