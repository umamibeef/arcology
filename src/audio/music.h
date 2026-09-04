/*  music.h -- the game's music: its own songs, through its own instruments.
 *
 *  The Macintosh original carries a complete sample-based synthesizer,
 *  Steve Hales' SoundMusicSys: a MIDI resource per song, an INST resource
 *  per instrument naming an 'snd ' sample, and a SONG resource per song
 *  whose remap table says which instrument each MIDI channel plays.  All
 *  of it is read out of your copy by tools/music.py into assets/music;
 *  this plays it back the way that engine does, as far as it has been
 *  read: the channel is the instrument (program changes are honoured
 *  only when the song's flags ask), a note is its sample resampled from
 *  the root key, looped while held where the sample loops, released
 *  over the song's decay, six voices to a song.
 */
#ifndef MUSIC_H
#define MUSIC_H

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct RMusic RMusic;

/*  Load assets/music and the instrument samples.  `dev` is the audio
 *  device to play on, or 0 to render offline only.  NULL when the files
 *  are not there. */
RMusic *music_create(const char *assets_dir, SDL_AudioDeviceID dev);
void    music_destroy(RMusic *m);

int         music_n_songs(const RMusic *m);
int         music_song_id(const RMusic *m, int k);
const char *music_song_name(const RMusic *m, int k);

/*  Play one song now, by its SONG resource id (10000..10018). */
int  music_play(RMusic *m, int song_id);
void music_stop(RMusic *m);
/*  The generator the scheduler draws from: n in, 0..n-1 out.  The app
 *  hands it the game's own rand, so the music shares the stream the
 *  simulation uses, as the original's does; without one a private
 *  copy of that generator, from the same state, is used. */
void music_set_rand(RMusic *m, uint16_t (*rand_fn)(uint16_t n));
/*  The Options menu's switch: off stops, on starts the rotation. */
void music_set_enabled(RMusic *m, int on);
int  music_enabled(const RMusic *m);
/*  Once a frame: moves the rotation on when a song has ended. */
void music_update(RMusic *m);
/*  The song playing now, its id, or 0. */
int music_playing(const RMusic *m);

/*  Offline: one song rendered whole to a 16-bit mono WAV at `rate`.
 *  `song` is a SONG id, or the path of a MIDI file played with the
 *  engine's default remap, channel c through instrument c + 1. */
int music_render_wav(RMusic *m, const char *song, const char *path, int rate);

#endif
