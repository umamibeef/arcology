/*  dump.h -- the one sink for the developer dumps.  Every --x-dump switch
 *  prints through dumpf, and nothing else does.  The sink is stdout, where
 *  the tools in tools/ read the dumps, or the file --dump-to names, so a
 *  dump can be kept without the game's log in it.  The mesh check's own
 *  result lines are not dumps: they are the program's answer and stay on
 *  stdout. */
#ifndef ARC_DUMP_H
#define ARC_DUMP_H
#ifdef __cplusplus
extern "C" {
#endif
void dump_open(const char *path); /* NULL or "-": stdout; else the file, truncated */
void dumpf(const char *fmt, ...);
/*  While a buffer is set, dumpf writes into it and nowhere else, so a
 *  window can show what a report answered without it also going to the
 *  sink people pipe.  Set NULL to send the dumps back to the sink. */
void dump_capture(char *buf, unsigned long cap);
void dump_close(void);
#ifdef __cplusplus
}
#endif
#endif
