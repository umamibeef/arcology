/*  state.h: what a derivation leaves in a module of the network, kept
 *  whole.  Each module names every variable of its own in one list.
 *  NET_STATE makes three functions of the list.  `name_state_size` gives
 *  the bytes the list takes, `name_state_save` copies the variables out,
 *  and `name_state_load` copies them back.  The store (store.h) keeps a
 *  derivation that way.  It loads the derivation again for a reader of
 *  its place.  tools/code_smells.py holds each list to every variable of
 *  its file. */
#ifndef NET_STATE_H
#define NET_STATE_H

#include <stddef.h>
#include <string.h>

#define NET_STATE_SIZE(v) +sizeof(v)
#define NET_STATE_SAVE(v)       \
    memcpy(p, &(v), sizeof(v)); \
    p += sizeof(v);
#define NET_STATE_LOAD(v)       \
    memcpy(&(v), p, sizeof(v)); \
    p += sizeof(v);

#define NET_STATE(name, LIST)                      \
    size_t name##_state_size(void)                 \
    {                                              \
        return 0 LIST(NET_STATE_SIZE);             \
    }                                              \
    void name##_state_save(unsigned char *p)       \
    {                                              \
        LIST(NET_STATE_SAVE)                       \
    }                                              \
    void name##_state_load(const unsigned char *p) \
    {                                              \
        LIST(NET_STATE_LOAD)                       \
    }

#endif
