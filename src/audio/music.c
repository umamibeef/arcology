/*  music.c -- see music.h.  The player is small because the data is:
 *  every instrument is one sample, no key splits, no tremolo, every
 *  instrument flag clear.  What SoundMusicSys does beyond that -- its
 *  exact envelope, its mixer's headroom -- is read off the original's
 *  driver when the emulator runs it; until then the two guesses are
 *  marked below.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#include "jsmn.h"
#include "music.h"

#define MAX_INST    32
#define MAX_SAMPLES 32
#define MAX_SONGS   24
#define MAX_VOICES  16
#define OUT_RATE    44100
/*  The engine's reference rate.  With ZBF_useSampleRate clear -- and it
 *  is clear on every instrument here -- SoundMusicSys ignores the rate
 *  in a sample's header and plays it as if recorded at its own
 *  reference, 22254.54 Hz on the Macintosh of its day (the rate the
 *  drums and bass are actually at; the rest are 11 kHz recordings meant
 *  to be played an octave up).  Honouring the headers instead played
 *  eleven of the fifteen samples an octave low and at half speed (the
 *  user: "music is too slow").  GenSynth.c 4794-4811 in miniBAE. */
#define REF_RATE              22254.54
#define XBF_enableMIDIProgram 0x04

typedef struct
{
    int    id;
    float *pcm;
    int    len;
    double rate;
    int    loop0, loop1;
    int    base;
} Sample;

typedef struct
{
    int  used, snd, root;
    char name[16];
} Inst;

typedef struct
{
    int    id, midi_id, max_notes, note_decay, flags1;
    int    remap[128];
    char   name[16], midi[96];
    double seconds;
} Song;

typedef struct
{
    double        t; /* seconds from the start */
    unsigned char st, a, b;
} Ev;

typedef struct
{
    int      on, releasing, chan, note;
    Sample  *smp;
    double   pos, step;
    float    amp, env, rel;
    unsigned age;
} Voice;

struct RMusic
{
    char   dir[1024];
    Sample smp[MAX_SAMPLES];
    int    n_smp;
    Inst   inst[MAX_INST];
    Song   song[MAX_SONGS];
    int    n_song;
    /* the song playing */
    Song     cur;
    int      cur_id;
    Ev      *ev;
    int      n_ev, next_ev, ended;
    double   t;
    Voice    v[MAX_VOICES];
    float    chan_vol[16];
    int      chan_prog[16];
    unsigned age;
    /* the scheduler, $471E: the last four songs, and when the next may start */
    int enabled;
    uint16_t (*rand_fn)(uint16_t);
    uint32_t          lcg; /* the private copy: THINK C's rand, from 1 */
    int               history[4];
    Uint64            next_tick;
    SDL_AudioDeviceID dev;
    SDL_AudioStream  *stream;
};

/* ---- json ---------------------------------------------------------------- */

static int tok_eq(const char *js, const jsmntok_t *t, const char *s)
{
    size_t n = (size_t)(t->end - t->start);
    return t->type == JSMN_STRING && strlen(s) == n && memcmp(js + t->start, s, n) == 0;
}

static int tok_skip(const jsmntok_t *t, int i)
{
    int k, n = t[i].size;
    if (t[i].type == JSMN_OBJECT)
    {
        i++;
        for (k = 0; k < n; k++)
            i = tok_skip(t, i + 1); /* the key, then its value */
        return i;
    }
    if (t[i].type == JSMN_ARRAY)
    {
        i++;
        for (k = 0; k < n; k++)
            i = tok_skip(t, i);
        return i;
    }
    return i + 1;
}

/* the value token of `key` in the object at `obj`, or -1 */
static int obj_get(const char *js, const jsmntok_t *t, int obj, const char *key)
{
    int i = obj + 1, k;
    if (t[obj].type != JSMN_OBJECT)
        return -1;
    for (k = 0; k < t[obj].size; k++)
    {
        if (tok_eq(js, &t[i], key))
            return i + 1;
        i = tok_skip(t, i + 1);
    }
    return -1;
}

static long tok_long(const char *js, const jsmntok_t *t, int i, long dflt)
{
    return i < 0 ? dflt : strtol(js + t[i].start, NULL, 10);
}

static double tok_dbl(const char *js, const jsmntok_t *t, int i, double dflt)
{
    return i < 0 ? dflt : strtod(js + t[i].start, NULL);
}

static void tok_str(const char *js, const jsmntok_t *t, int i, char *out, size_t n)
{
    size_t k = i < 0 ? 0 : (size_t)(t[i].end - t[i].start);
    if (k >= n)
        k = n - 1;
    if (i >= 0)
        memcpy(out, js + t[i].start, k);
    out[k] = 0;
}

/* ---- loading ------------------------------------------------------------- */

static Sample *sample_by_id(RMusic *m, int id)
{
    int k;
    for (k = 0; k < m->n_smp; k++)
        if (m->smp[k].id == id)
            return &m->smp[k];
    return NULL;
}

static int load_sample(RMusic *m, int id, const char *file, double rate, int l0, int l1, int base)
{
    char          path[1200];
    SDL_AudioSpec spec;
    Uint8        *data;
    Uint32        len;
    Sample       *s;
    int           k;
    if (m->n_smp >= MAX_SAMPLES)
        return -1;
    snprintf(path, sizeof path, "%s/%s", m->dir, file);
    if (!SDL_LoadWAV(path, &spec, &data, &len))
        return -1;
    s      = &m->smp[m->n_smp];
    s->id  = id;
    s->len = (int)(len / (Uint32)SDL_AUDIO_BYTESIZE(spec.format) / (Uint32)spec.channels);
    s->pcm = (float *)malloc((size_t)s->len * sizeof *s->pcm);
    if (!s->pcm)
    {
        SDL_free(data);
        return -1;
    }
    /* the resources are 8-bit unsigned mono; anything else is taken as such a first channel */
    for (k = 0; k < s->len; k++)
    {
        if (spec.format == SDL_AUDIO_U8)
            s->pcm[k] = ((float)data[k * spec.channels] - 128.0f) / 128.0f;
        else if (spec.format == SDL_AUDIO_S16)
            s->pcm[k] = (float)((Sint16 *)data)[k * spec.channels] / 32768.0f;
        else
            s->pcm[k] = 0.0f;
    }
    SDL_free(data);
    s->rate  = rate > 0.0 ? rate : (double)spec.freq;
    s->loop0 = l0;
    s->loop1 = l1;
    s->base  = base;
    m->n_smp++;
    return 0;
}

static int load_json(RMusic *m)
{
    char        path[1200];
    FILE       *f;
    long        len;
    char       *js;
    jsmn_parser jp;
    jsmntok_t  *t;
    int         nt, i, k, sec;
    snprintf(path, sizeof path, "%s/music/music.json", m->dir);
    f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    js = (char *)malloc((size_t)len + 1);
    if (!js || fread(js, 1, (size_t)len, f) != (size_t)len)
    {
        fclose(f);
        free(js);
        return -1;
    }
    fclose(f);
    js[len] = 0;
    jsmn_init(&jp);
    nt = jsmn_parse(&jp, js, (size_t)len, NULL, 0);
    if (nt < 1)
    {
        free(js);
        return -1;
    }
    t = (jsmntok_t *)malloc((size_t)nt * sizeof *t);
    jsmn_init(&jp);
    jsmn_parse(&jp, js, (size_t)len, t, (unsigned)nt);

    /* the samples */
    sec = obj_get(js, t, 0, "samples");
    if (sec >= 0)
        for (i = sec + 1, k = 0; k < t[sec].size; k++)
        {
            int  v = i + 1, lp = obj_get(js, t, v, "loop");
            char file[128];
            tok_str(js, t, obj_get(js, t, v, "file"), file, sizeof file);
            load_sample(m, (int)strtol(js + t[i].start, NULL, 10), file, tok_dbl(js, t, obj_get(js, t, v, "rate"), 0.0), lp >= 0 ? (int)tok_long(js, t, lp + 1, 0) : 0, lp >= 0 ? (int)tok_long(js, t, lp + 2, 0) : 0, (int)tok_long(js, t, obj_get(js, t, v, "base"), 60));
            i = tok_skip(t, v);
        }
    /* the instruments */
    sec = obj_get(js, t, 0, "instruments");
    if (sec >= 0)
        for (i = sec + 1, k = 0; k < t[sec].size; k++)
        {
            int v = i + 1, id = (int)strtol(js + t[i].start, NULL, 10);
            if (id >= 0 && id < MAX_INST)
            {
                m->inst[id].used = 1;
                m->inst[id].snd  = (int)tok_long(js, t, obj_get(js, t, v, "snd"), 0);
                m->inst[id].root = (int)tok_long(js, t, obj_get(js, t, v, "root"), 0);
                tok_str(js, t, obj_get(js, t, v, "name"), m->inst[id].name, sizeof m->inst[id].name);
            }
            i = tok_skip(t, v);
        }
    /* the songs */
    sec = obj_get(js, t, 0, "songs");
    if (sec >= 0)
        for (i = sec + 1, k = 0; k < t[sec].size && m->n_song < MAX_SONGS; k++)
        {
            int   v = i + 1, rm = obj_get(js, t, v, "remap"), j, c;
            Song *s       = &m->song[m->n_song];
            s->id         = (int)strtol(js + t[i].start, NULL, 10);
            s->midi_id    = (int)tok_long(js, t, obj_get(js, t, v, "midi_id"), 0);
            s->max_notes  = (int)tok_long(js, t, obj_get(js, t, v, "max_notes"), 6);
            s->note_decay = (int)tok_long(js, t, obj_get(js, t, v, "note_decay"), 40);
            s->flags1     = (int)tok_long(js, t, obj_get(js, t, v, "flags1"), 0);
            s->seconds    = tok_dbl(js, t, obj_get(js, t, v, "seconds"), 0.0);
            tok_str(js, t, obj_get(js, t, v, "name"), s->name, sizeof s->name);
            tok_str(js, t, obj_get(js, t, v, "midi"), s->midi, sizeof s->midi);
            for (c = 0; c < 128; c++)
                s->remap[c] = -1;
            if (rm >= 0)
                for (j = rm + 1, c = 0; c < t[rm].size; c++)
                {
                    int from = (int)strtol(js + t[j].start, NULL, 10), to = (int)tok_long(js, t, j + 1, -1);
                    if (from >= 0 && from < 128)
                        s->remap[from] = to;
                    j = tok_skip(t, j + 1);
                }
            if (s->midi[0])
                m->n_song++;
            i = tok_skip(t, v);
        }
    free(t);
    free(js);
    return m->n_song ? 0 : -1;
}

/* ---- the MIDI file ------------------------------------------------------- */

typedef struct
{
    long          tick;
    int           seq;
    unsigned char st, a, b;
    long          tempo; /* for a tempo meta event; else 0 */
} Raw;

static const unsigned char *vlq(const unsigned char *p, const unsigned char *end, long *v)
{
    *v = 0;
    while (p < end)
    {
        unsigned char c = *p++;
        *v              = (*v << 7) | (c & 0x7F);
        if (!(c & 0x80))
            break;
    }
    return p;
}

static int raw_cmp(const void *pa, const void *pb)
{
    const Raw *a = (const Raw *)pa, *b = (const Raw *)pb;
    if (a->tick != b->tick)
        return a->tick < b->tick ? -1 : 1;
    return a->seq - b->seq;
}

/*  Every channel event and tempo change, in time order, with its
 *  moment in seconds through the tempo map. */
static int load_midi(RMusic *m, const char *path)
{
    FILE                *f = fopen(path, "rb");
    unsigned char       *d;
    long                 len;
    int                  ntr, div, k, seq = 0, n = 0, cap = 0;
    Raw                 *raw = NULL;
    const unsigned char *p, *end;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    d = (unsigned char *)malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len || len < 14 || memcmp(d, "MThd", 4) != 0)
    {
        fclose(f);
        free(d);
        return -1;
    }
    fclose(f);
    ntr = (d[10] << 8) | d[11];
    div = (d[12] << 8) | d[13];
    p   = d + 8 + ((d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]);
    end = d + len;
    for (k = 0; k < ntr && p + 8 <= end; k++)
    {
        long                 tlen = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7], tick = 0;
        const unsigned char *q = p + 8, *tend = q + tlen;
        unsigned char        st = 0;
        if (memcmp(p, "MTrk", 4) != 0 || tend > end)
            break;
        while (q < tend)
        {
            long dt;
            Raw  r;
            q = vlq(q, tend, &dt);
            tick += dt;
            if (q >= tend)
                break;
            memset(&r, 0, sizeof r);
            r.tick = tick;
            r.seq  = seq++;
            if (*q == 0xFF)
            {
                long          L;
                unsigned char typ = q[1];
                q                 = vlq(q + 2, tend, &L);
                if (typ == 0x51 && L == 3)
                {
                    r.tempo = (q[0] << 16) | (q[1] << 8) | q[2];
                    r.st    = 0xFF;
                }
                q += L;
            }
            else if (*q == 0xF0 || *q == 0xF7)
            {
                long L;
                q = vlq(q + 1, tend, &L);
                q += L;
                continue;
            }
            else
            {
                unsigned char hi;
                if (*q & 0x80)
                    st = *q++;
                hi   = st & 0xF0;
                r.st = st;
                r.a  = *q++;
                if (hi != 0xC0 && hi != 0xD0)
                    r.b = *q++;
            }
            if (r.st)
            {
                if (n == cap)
                {
                    Raw *nr = (Raw *)realloc(raw, (size_t)(cap = cap ? cap * 2 : 1024) * sizeof *nr);
                    if (!nr)
                    {
                        free(raw);
                        free(d);
                        return -1;
                    }
                    raw = nr;
                }
                raw[n++] = r;
            }
        }
        p = tend;
    }
    free(d);
    qsort(raw, (size_t)n, sizeof *raw, raw_cmp);
    free(m->ev);
    m->ev   = (Ev *)malloc((size_t)(n ? n : 1) * sizeof *m->ev);
    m->n_ev = 0;
    {
        double secs = 0.0, us = 500000.0;
        long   tick = 0;
        for (k = 0; k < n; k++)
        {
            secs += (double)(raw[k].tick - tick) / (double)div * us / 1e6;
            tick = raw[k].tick;
            if (raw[k].st == 0xFF)
            {
                us = (double)raw[k].tempo;
                continue;
            }
            m->ev[m->n_ev].t  = secs;
            m->ev[m->n_ev].st = raw[k].st;
            m->ev[m->n_ev].a  = raw[k].a;
            m->ev[m->n_ev].b  = raw[k].b;
            m->n_ev++;
        }
    }
    free(raw);
    return 0;
}

/* ---- the synthesizer ----------------------------------------------------- */

static void song_reset(RMusic *m)
{
    int c;
    memset(m->v, 0, sizeof m->v);
    for (c = 0; c < 16; c++)
    {
        m->chan_vol[c]  = 1.0f;
        m->chan_prog[c] = c; /* the channel is the program until a change says otherwise */
    }
    m->t       = 0.0;
    m->next_ev = 0;
    m->ended   = 0;
}

static void note_on(RMusic *m, int chan, int note, int vel)
{
    const Song *s    = &m->cur;
    int         prog = (s->flags1 & XBF_enableMIDIProgram) ? m->chan_prog[chan] : chan;
    int         id   = prog >= 0 && prog < 128 ? s->remap[prog] : -1;
    Inst       *in   = id >= 0 && id < MAX_INST && m->inst[id].used ? &m->inst[id] : NULL;
    Sample     *smp  = in ? sample_by_id(m, in->snd) : NULL;
    Voice      *v    = NULL;
    int         k, live = 0, base;
    if (!smp)
        return;
    for (k = 0; k < MAX_VOICES; k++)
        live += m->v[k].on;
    for (k = 0; k < MAX_VOICES; k++)
        if (!m->v[k].on)
        {
            v = &m->v[k];
            break;
        }
    /*  polyphony: the song's maxNotes; past it, or with no free voice,
     *  the oldest note gives way */
    if (!v || live >= s->max_notes)
    {
        Voice *old = NULL;
        for (k = 0; k < MAX_VOICES; k++)
            if (m->v[k].on && (!old || m->v[k].age < old->age))
                old = &m->v[k];
        if (old)
            v = old;
    }
    if (!v)
        return;
    /*  GenSynth.c 4502 and 4652: an instrument root key transposes the
     *  note so the root plays as 60, and the sample's base key is then
     *  taken from 60 -- so the offset is (note - root + 60) - base, or
     *  note - base with no root. */
    base = in->root ? smp->base + in->root - 60 : smp->base;
    memset(v, 0, sizeof *v);
    v->on   = 1;
    v->chan = chan;
    v->note = note;
    v->smp  = smp;
    v->pos  = 0.0;
    v->step = pow(2.0, (double)(note - base) / 12.0) * REF_RATE / (double)OUT_RATE;
    v->amp  = ((float)vel / 127.0f) * m->chan_vol[chan];
    v->env  = 1.0f;
    /*  GUESS: the song's noteDecay in sixtieths of a second, released
     *  linearly.  To be read off the original's driver. */
    v->rel = 1.0f / ((float)(s->note_decay > 0 ? s->note_decay : 40) / 60.0f * (float)OUT_RATE);
    v->age = ++m->age;
}

static void note_off(RMusic *m, int chan, int note)
{
    int k;
    for (k = 0; k < MAX_VOICES; k++)
        if (m->v[k].on && !m->v[k].releasing && m->v[k].chan == chan && m->v[k].note == note)
            m->v[k].releasing = 1;
}

static void handle(RMusic *m, const Ev *e)
{
    int chan = e->st & 0x0F;
    switch (e->st & 0xF0)
    {
        case 0x90:
            if (e->b)
                note_on(m, chan, e->a, e->b);
            else
                note_off(m, chan, e->a);
            break;
        case 0x80:
            note_off(m, chan, e->a);
            break;
        case 0xB0:
            if (e->a == 7)
                m->chan_vol[chan] = (float)e->b / 127.0f;
            else if (e->a == 123 || e->a == 120)
            {
                int k;
                for (k = 0; k < MAX_VOICES; k++)
                    if (m->v[k].on && m->v[k].chan == chan)
                        m->v[k].releasing = 1;
            }
            break;
        case 0xC0:
            m->chan_prog[chan] = e->a;
            break;
        default:
            break;
    }
}

/*  n frames of mono float at OUT_RATE. */
static void render(RMusic *m, float *out, int n)
{
    int         i, k;
    const float master = 0.35f; /* GUESS: the driver's own scaler/clipper; six voices at full scale must not clip */
    for (i = 0; i < n; i++)
    {
        float mix  = 0.0f;
        int   live = 0;
        while (m->next_ev < m->n_ev && m->ev[m->next_ev].t <= m->t)
            handle(m, &m->ev[m->next_ev++]);
        for (k = 0; k < MAX_VOICES; k++)
        {
            Voice  *v = &m->v[k];
            Sample *s = v->smp;
            int     p0;
            float   f, a, b;
            if (!v->on)
                continue;
            live++;
            p0 = (int)v->pos;
            if (p0 >= s->len - 1)
            {
                v->on = 0;
                continue;
            }
            f = (float)(v->pos - (double)p0);
            a = s->pcm[p0];
            b = s->pcm[p0 + 1];
            mix += (a + (b - a) * f) * v->amp * v->env;
            v->pos += v->step;
            /* a looping sample sustains while the note is held */
            if (!v->releasing && s->loop1 > s->loop0 + 2 && v->pos >= (double)s->loop1)
                v->pos -= (double)(s->loop1 - s->loop0);
            if (v->releasing)
            {
                v->env -= v->rel;
                if (v->env <= 0.0f)
                    v->on = 0;
            }
        }
        mix *= master;
        out[i] = mix > 1.0f ? 1.0f : mix < -1.0f ? -1.0f
                                                 : mix;
        m->t += 1.0 / (double)OUT_RATE;
        if (m->next_ev >= m->n_ev && !live)
            m->ended = 1;
    }
}

static void SDLCALL feed(void *ud, SDL_AudioStream *st, int additional, int total)
{
    RMusic *m = (RMusic *)ud;
    float   buf[1024];
    (void)total;
    while (additional > 0)
    {
        int n = additional / (int)sizeof(float);
        if (n > 1024)
            n = 1024;
        if (n <= 0)
            break;
        if (m->cur_id && !m->ended)
            render(m, buf, n);
        else
            memset(buf, 0, (size_t)n * sizeof *buf);
        SDL_PutAudioStreamData(st, buf, n * (int)sizeof(float));
        additional -= n * (int)sizeof(float);
    }
}

/* ---- the face ------------------------------------------------------------ */

RMusic *music_create(const char *assets_dir, SDL_AudioDeviceID dev)
{
    RMusic *m = (RMusic *)calloc(1, sizeof *m);
    if (!m)
        return NULL;
    snprintf(m->dir, sizeof m->dir, "%s", assets_dir);
    if (load_json(m) != 0)
    {
        music_destroy(m);
        return NULL;
    }
    m->dev = dev;
    memset(m->history, 0, sizeof m->history);
    m->lcg = 1;
    if (dev)
    {
        SDL_AudioSpec spec;
        spec.format   = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq     = OUT_RATE;
        m->stream     = SDL_CreateAudioStream(&spec, NULL);
        if (m->stream && SDL_BindAudioStream(dev, m->stream))
            SDL_SetAudioStreamGetCallback(m->stream, feed, m);
        else if (m->stream)
        {
            SDL_DestroyAudioStream(m->stream);
            m->stream = NULL;
        }
    }
    return m;
}

void music_destroy(RMusic *m)
{
    int k;
    if (!m)
        return;
    if (m->stream)
        SDL_DestroyAudioStream(m->stream);
    for (k = 0; k < m->n_smp; k++)
        free(m->smp[k].pcm);
    free(m->ev);
    free(m);
}

int         music_n_songs(const RMusic *m) { return m ? m->n_song : 0; }
int         music_song_id(const RMusic *m, int k) { return m && k >= 0 && k < m->n_song ? m->song[k].id : 0; }
const char *music_song_name(const RMusic *m, int k) { return m && k >= 0 && k < m->n_song ? m->song[k].name : ""; }
int         music_enabled(const RMusic *m) { return m ? m->enabled : 0; }
int         music_playing(const RMusic *m) { return m && !m->ended ? m->cur_id : 0; }

static int start(RMusic *m, const Song *s)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", m->dir, s->midi);
    if (load_midi(m, path) != 0)
        return -1;
    m->cur    = *s;
    m->cur_id = s->id;
    song_reset(m);
    return 0;
}

int music_play(RMusic *m, int song_id)
{
    int k, rc = -1;
    if (!m)
        return -1;
    if (m->stream)
        SDL_LockAudioStream(m->stream);
    for (k = 0; k < m->n_song; k++)
        if (m->song[k].id == song_id)
        {
            rc = start(m, &m->song[k]);
            break;
        }
    if (m->stream)
        SDL_UnlockAudioStream(m->stream);
    return rc;
}

void music_stop(RMusic *m)
{
    if (!m)
        return;
    if (m->stream)
        SDL_LockAudioStream(m->stream);
    m->cur_id = 0;
    m->ended  = 1;
    memset(m->v, 0, sizeof m->v);
    if (m->stream)
        SDL_UnlockAudioStream(m->stream);
}

/*  The original's scheduler, CODE 2 at $471E, run once a pass through
 *  the event loop.  Two nine-entry tables in its A5 world: a draw of
 *  0..8 from the game's own rand (lib_rand, $20EE6) picks a song index
 *  through ROTATION (A5-$65C4), and WAIT (A5-$65B2) says how many ticks
 *  after the start the NEXT may begin.  A song already among the last
 *  four played is rejected and the pass ends -- the next pass draws
 *  again.  A song only ever starts when none is playing, so a wait
 *  shorter than the song means the next follows at once, and a longer
 *  one is silence.
 *
 *  WAIT has nine entries but is indexed by the song index, which runs
 *  to 18: for RastaMin (11), SimCitAy (12) and SimGoodN (13) the
 *  original reads its own ROTATION copy instead, 4, 7 and 8 ticks, and
 *  for the Theme (18) whatever lies beyond its frame.  All four come to
 *  the same thing under the idle rule: the next song follows the
 *  moment that one ends.  That is what this does for them. */
/*  The generator is never seeded, in the original or here: its state is
 *  1 in the A5 image, and the first draw of nine from 1 is 18 -- so the
 *  first song after launch is the Theme, every time, and only then does
 *  the order depend on the simulation's own draws in between.  That is
 *  the original's behaviour and it is kept. */
static const int ROTATION[9] = {0, 3, 4, 7, 8, 11, 12, 13, 18};
static const int WAIT[9]     = {9000, 6000, 7200, 11000, 13000, 6000, 6000, 9000, 9000};

/*  THINK C's rand as the game's rng.c has it, for when no generator
 *  is handed in: the ANSI constants, the high word masked to 15 bits. */
static uint16_t private_rand(RMusic *m, uint16_t n)
{
    uint16_t v;
    m->lcg = m->lcg * 0x41C64E6Du + 0x3039u;
    v      = (uint16_t)((m->lcg >> 16) & 0x7FFF);
    return n ? (uint16_t)(v % n) : 0;
}

void music_set_rand(RMusic *m, uint16_t (*rand_fn)(uint16_t n))
{
    if (m)
        m->rand_fn = rand_fn;
}

static Uint64 ticks_now(void)
{
    return SDL_GetTicksNS() * 60u / 1000000000u; /* the Mac's TickCount, sixtieths */
}

void music_set_enabled(RMusic *m, int on)
{
    if (!m)
        return;
    m->enabled = on ? 1 : 0;
    if (!on)
        music_stop(m);
    else
        m->next_tick = 0; /* the next pass may start one */
}

void music_update(RMusic *m)
{
    Uint64 now;
    int    draw, idx, k;
    if (!m || !m->enabled || !m->stream)
        return;
    if (music_playing(m))
        return;
    now = ticks_now();
    if (now < m->next_tick)
        return;
    draw = m->rand_fn ? m->rand_fn(9) : private_rand(m, 9);
    idx  = ROTATION[draw];
    for (k = 0; k < 4; k++)
        if (m->history[k] == idx)
            return; /* one of the last four: not this pass */
    for (k = 0; k < 3; k++)
        m->history[k] = m->history[k + 1];
    m->history[3] = idx;
    if (music_play(m, 10000 + idx) == 0)
        m->next_tick = now + (Uint64)(idx < 9 ? WAIT[idx] : 0);
}

static void put32(FILE *f, unsigned v)
{
    unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    fwrite(b, 1, 4, f);
}

int music_render_wav(RMusic *m, const char *song, const char *path, int rate)
{
    FILE *f;
    float buf[4096];
    long  frames = 0, cap;
    Song  s;
    int   k, found = 0;
    if (!m || !song || !path)
        return -1;
    if (strchr(song, '.'))
    {
        /* a MIDI file on its own: the engine's default remap */
        memset(&s, 0, sizeof s);
        for (k = 0; k < 128; k++)
            s.remap[k] = k + 1;
        s.max_notes  = 6;
        s.note_decay = 40;
        s.seconds    = 0.0;
        snprintf(s.name, sizeof s.name, "midi");
        if (load_midi(m, song) != 0)
            return -1;
        m->cur    = s;
        m->cur_id = 1;
        song_reset(m);
        found = 1;
    }
    else
        for (k = 0; k < m->n_song; k++)
            if (m->song[k].id == atoi(song) && start(m, &m->song[k]) == 0)
                found = 1;
    if (!found)
        return -1;
    (void)rate; /* the render runs at OUT_RATE; the header says so */
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite("RIFF", 1, 4, f);
    put32(f, 0);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    fputc(1, f);
    fputc(0, f); /* PCM */
    fputc(1, f);
    fputc(0, f); /* mono */
    put32(f, OUT_RATE);
    put32(f, OUT_RATE * 2);
    fputc(2, f);
    fputc(0, f);
    fputc(16, f);
    fputc(0, f);
    fwrite("data", 1, 4, f);
    put32(f, 0);
    cap = (long)((m->ev && m->n_ev ? m->ev[m->n_ev - 1].t : 0.0) + 10.0) * OUT_RATE;
    while (!m->ended && frames < cap)
    {
        short out[4096];
        render(m, buf, 4096);
        for (k = 0; k < 4096; k++)
            out[k] = (short)(buf[k] * 32767.0f);
        fwrite(out, 2, 4096, f);
        frames += 4096;
    }
    fseek(f, 4, SEEK_SET);
    put32(f, (unsigned)(36 + frames * 2));
    fseek(f, 40, SEEK_SET);
    put32(f, (unsigned)(frames * 2));
    fclose(f);
    m->cur_id = 0;
    return 0;
}
