/*  ui_none.c: the UI interface with no UI behind it. */
#include <stddef.h>

#include "ui.h"

RUi *ui_create(struct SDL_Window *win, struct SDL_GPUDevice *dev, int swap_fmt, float dpi, const char *assets_dir)
{
    (void)win;
    (void)dev;
    (void)swap_fmt;
    (void)dpi;
    (void)assets_dir;
    return NULL;
}
void ui_destroy(RUi *u) { (void)u; }
int  ui_set_theme(RUi *u, const char *dir)
{
    (void)u;
    (void)dir;
    return -1;
}
int ui_event(RUi *u, const union SDL_Event *e)
{
    (void)u;
    (void)e;
    return 0;
}
int ui_wants_mouse(const RUi *u)
{
    (void)u;
    return 0;
}
int ui_wants_keyboard(const RUi *u)
{
    (void)u;
    return 0;
}
void ui_frame(RUi *u, RUiState *s)
{
    (void)u;
    (void)s;
}
void ui_render(void *u, struct SDL_GPUCommandBuffer *cmd, struct SDL_GPUTexture *swap, uint32_t w, uint32_t h)
{
    (void)u;
    (void)cmd;
    (void)swap;
    (void)w;
    (void)h;
}
void ui_clear_theme(RUi *u) { (void)u; }
