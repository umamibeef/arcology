/*  sound.h -- the game's sounds, played through SDL's audio.
 *
 *  The effects are the original's 'snd ' resources 500..527, decoded to
 *  assets/sounds by tools/snd.py.  Where the original plays each one is
 *  not yet read out of the binary; the app plays them where they plainly
 *  belong: the click on a palette button, the bulldozer, the disasters,
 *  the year's cheers and boos, and a building's own sound under the query
 *  tool, which is how the original lets a building be heard.
 */
#ifndef R_SOUND_H
#define R_SOUND_H

enum
{
    R_SND_BULLDOZE = 500, R_SND_ERROR, R_SND_WIND, R_SND_PLOP, R_SND_EXPLODE,
    R_SND_CLICK, R_SND_POLICE, R_SND_FIRE_LOOP, R_SND_DOZER, R_SND_FIRETRUCK,
    R_SND_COPTER, R_SND_FLOOD, R_SND_BOOS, R_SND_CHEERS, R_SND_ZZAP,
    R_SND_MAYDAY, R_SND_IMHIT, R_SND_SHIP, R_SND_TAKEOFF, R_SND_LAND,
    R_SND_SIREN, R_SND_HORNS, R_SND_PRISON, R_SND_SCHOOLBELL, R_SND_TRAIN,
    R_SND_SHOT, R_SND_ARCO, R_SND_ROAR,
    R_SND_FIRST = 500, R_SND_LAST = 527
};

typedef struct RSound RSound;

/*  Open the default playback device and load the effects.  NULL when the
 *  device or the files are not there; every call then does nothing. */
RSound *sound_create(const char *assets_dir);
void    sound_destroy(RSound *s);
void    sound_play(RSound *s, int id);
int     sound_loaded(const RSound *s);

#endif /* R_SOUND_H */
