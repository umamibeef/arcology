/*  messages.c: the interface's message log, the lines the Messages window
 *  shows.  Both builds of the interface keep it, with Dear ImGui and
 *  without it. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

void ui_log(RUiState *s, const char *fmt, ...)
{
    va_list ap;
    if (s->n_log == RUI_LOG_LINES)
    {
        memmove(s->log[0], s->log[1], sizeof s->log - sizeof s->log[0]);
        s->n_log--;
    }
    va_start(ap, fmt);
    vsnprintf(s->log[s->n_log++], RUI_LOG_LEN, fmt, ap);
    va_end(ap);
}
