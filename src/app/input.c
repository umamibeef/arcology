/*  input.c: one event of the window, as the game takes it.  The mouse
 *  drags the map, works the selected tool, selects an area in the map view
 *  and picks for the inspector.  The wheel zooms.
 *
 *  Keys
 *      1..5            speed: paused, turtle, llama, cheetah, african swallow
 *      space           pause and resume
 *      arrows, wasd    pan          + -  zoom          [ ]  pixel scale
 *      , . q e 0       turn the camera, and back to the snap
 *      Cmd-V           the next data view, Shift-Cmd-V the one before
 *      Enter           the inspector's child, Shift-Enter its parent
 *      escape          the inspector's selection, then the tool, then quit
 *
 *  A key the menu bar shows beside an item runs that item's command
 *  (ui/commands.c), so the menu bar is the list of those keys. */
#include "internal.h"

#include <SDL3/SDL.h>

#include <math.h>

#include "dump.h"
#include "opt.h"

void input_event(App *a, SDL_Window *win, const SDL_Event *e)
{
    int ui_owns = ui_event(a->ui, e);
    if (g_dev.input_log)
        switch (e->type)
        {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                dumpf("input  button %s %d at %.0f,%.0f  ui_owns %d\n", e->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "down" : "up", e->button.button, (double)e->button.x, (double)e->button.y, ui_owns);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                dumpf("input  motion %.0f,%.0f rel %.0f,%.0f  state %02x  queried %02x  ui_owns %d  drag %d\n", (double)e->motion.x, (double)e->motion.y, (double)e->motion.xrel, (double)e->motion.yrel, (unsigned)e->motion.state, (unsigned)SDL_GetMouseState(NULL, NULL), ui_owns, a->drag);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                dumpf("input  wheel %.2f,%.2f  ui_owns %d\n", (double)e->wheel.x, (double)e->wheel.y, ui_owns);
                break;
            case SDL_EVENT_KEY_DOWN:
                dumpf("input  key %s mod %04x  ui_owns %d  inspect %d  record %d\n", SDL_GetKeyName(e->key.key), (unsigned)e->key.mod, ui_owns, a->insp.on, a->insp.net_ok);
                break;
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_MOTION:
                dumpf("input  finger %d at %.3f,%.3f\n", (int)e->type, (double)e->tfinger.x, (double)e->tfinger.y);
                break;
            default:
                break;
        }
    if (e->type == SDL_EVENT_QUIT)
        a->quit = 1;
    /*  A drag the tool works is the map's to the end.  Its
     *  motion and its release reach the tool over a window too,
     *  or the tool would go on working with the button up. */
    else if (ui_owns && !(a->gesturing && (e->type == SDL_EVENT_MOUSE_MOTION || e->type == SDL_EVENT_MOUSE_BUTTON_UP)))
        return;
    else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             e->button.button == SDL_BUTTON_LEFT)
    {
        if (a->insp.on && (SDL_GetModState() & SDL_KMOD_SHIFT) && !a->building)
        {
            /*  Shift-click while inspecting: the report on what is
             *  under the pointer, on the clipboard.  The pick and
             *  the fill are done again here so what is copied is
             *  what the click was over, not the frame before it. */
            pick_tile(a, e->button.x, e->button.y, win);
            a->win_density = SDL_GetWindowPixelDensity(win);
            ui_fill(a);
            inspect_copy(a);
        }
        else if (inspect_click(a, e->button.x, e->button.y, win))
            ;
        else if (a->plan && (SDL_GetModState() & SDL_KMOD_SHIFT))
        {
            /*  Shift-drag in the map view: select an area for a
             *  debug report (area.c), tile to tile.  A drag
             *  already in flight is dropped here.  The map must
             *  hold still while the area is drawn on it. */
            pick_tile(a, e->button.x, e->button.y, win);
            a->drag   = 0;
            a->sel    = a->q_col >= 0;
            a->sel_c0 = a->sel_c1 = a->q_col;
            a->sel_r0 = a->sel_r1 = a->q_row;
        }
        else if (gesture_ready(a))
        {
            /*  The selected tool works from the press of the
             *  button, and the map holds still while it does. */
            pick_tile(a, e->button.x, e->button.y, win);
            a->drag = 0;
            if (a->q_col >= 0)
                gesture_begin(a, a->q_col, a->q_row, (SDL_GetModState() & KMOD_CMD) != 0);
        }
        else
        {
            a->drag     = 1;
            a->drag_len = 0.0f;
        }
    }
    else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             (e->button.button == SDL_BUTTON_RIGHT || e->button.button == SDL_BUTTON_MIDDLE))
    {
        /*  The right and middle buttons always drag the map, so
         *  it can move while the left button works a tool. */
        a->drag     = 1;
        a->drag_len = 0.0f;
    }
    else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP &&
             (e->button.button == SDL_BUTTON_RIGHT || e->button.button == SDL_BUTTON_MIDDLE))
        a->drag = 0;
    else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP &&
             e->button.button == SDL_BUTTON_LEFT && a->gesturing)
    {
        pick_tile(a, e->button.x, e->button.y, win);
        gesture_end(a, a->q_col, a->q_row);
        a->drag = 0;
    }
    else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP &&
             e->button.button == SDL_BUTTON_LEFT && a->sel)
    {
        a->sel  = 0;
        a->drag = 0;
        area_report_ui(a);
    }
    else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP &&
             e->button.button == SDL_BUTTON_LEFT)
    {
        a->drag = 0;
        /*  A click, not a drag: the selected tool acts on the
         *  tile under the pointer.  Shift in the map view is a
         *  selection and never the tool, whether or not it
         *  found a tile to start at. */
        if (a->drag_len < 4.0f && !(a->plan && (SDL_GetModState() & SDL_KMOD_SHIFT)))
        {
            pick_tile(a, e->button.x, e->button.y, win);
            if (a->q_col >= 0)
                use_tool(a, win);
        }
    }
    else if (e->type == SDL_EVENT_MOUSE_MOTION)
    {
        /*  The left button held, by the event's own account or
         *  the mouse's. macOS synthesises a three-finger drag as
         *  a left-button drag.  The motion can arrive without
         *  the button in the event's state: and sometimes
         *  without the press that would have started the drag.
         *  Either report is enough.  The first such motion
         *  starts the drag itself. */
        const SDL_MouseButtonFlags pan  = SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK;
        int                        held = (e->motion.state & pan) || (SDL_GetMouseState(NULL, NULL) & pan);
        /*  SHIFT IN THE MAP VIEW SELECTS, AND THE MAP HOLDS
         *  STILL.  The test is made every motion.  A drag can
         *  be armed with no press in front of it.  The button
         *  UP that ends one is not delivered while the panel
         *  owns the mouse.  And macOS makes a drag out of a
         *  three finger motion.  Either one leaves the pan
         *  armed, and the map then slides under every later
         *  selection. */
        int selecting = a->sel || (a->plan && (SDL_GetModState() & SDL_KMOD_SHIFT));
        if (selecting || a->gesturing)
            a->drag = 0;
        else if (held && !a->drag)
            a->drag = 1, a->drag_len = 0.0f;
        if (a->drag && held)
        {
            /*  The map follows the pointer: the view moves the other way
             *  by the pointer's own motion, as the pan keys move it. */
            float dens = SDL_GetWindowPixelDensity(win);
            a->drag_len += fabsf(e->motion.xrel) + fabsf(e->motion.yrel);
            cam_pan(a, win, -e->motion.xrel * dens, -e->motion.yrel * dens);
        }
        pick_tile(a, e->motion.x, e->motion.y, win);
        if (a->gesturing && a->q_col >= 0)
            gesture_step(a, a->q_col, a->q_row);
        if (a->sel && a->q_col >= 0)
        {
            a->sel_c1 = a->q_col;
            a->sel_r1 = a->q_row;
        }
    }
    else if (e->type == SDL_EVENT_MOUSE_WHEEL)
    {
        /*  Each notch asks for a step on from the zoom already asked for,
         *  so notches in quick succession add up. */
        float dens = SDL_GetWindowPixelDensity(win);
        float step = powf(1.12f, e->wheel.y);
        zoom_by(a, step, e->wheel.mouse_x * dens, e->wheel.mouse_y * dens);
    }
    else if (e->type == SDL_EVENT_KEY_DOWN)
    {
        SDL_Keycode k     = e->key.key;
        int         shift = (e->key.mod & SDL_KMOD_SHIFT) != 0;
        int         mod   = (e->key.mod & KMOD_CMD) != 0;
        int         pw, ph, cmd;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        if (k == SDLK_ESCAPE)
        {
            /*  Escape clears the inspector's selection, then puts
             *  the selected tool down, and quits only when there is
             *  neither. */
            if (a->gesturing)
                ;
            else if (inspect_key(a, k, shift))
                ;
            else if (a->us.tool >= 0)
                a->us.tool = -1;
            else
                a->quit = 1;
        }
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && inspect_key(a, k, shift))
            ;
        else if (k >= SDLK_1 && k <= SDLK_5)
            set_speed(a, (int32_t)(k - SDLK_0));
        else if (k == SDLK_SPACE)
            set_speed(a, a->speed > 1 ? 1 : a->last_speed);
        else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS)
            zoom_by(a, 2.0f, (float)pw * 0.5f, (float)ph * 0.5f);
        else if (k == SDLK_MINUS || k == SDLK_KP_MINUS)
            zoom_by(a, 0.5f, (float)pw * 0.5f, (float)ph * 0.5f);
        else if (k == SDLK_LEFTBRACKET && a->gv.scale > 1)
            a->gv.scale--;
        else if (k == SDLK_RIGHTBRACKET && a->gv.scale < 4)
            a->gv.scale++;
        /*  A key the menu bar names runs the command its item
         *  runs (ui/commands.c), and the change is applied as the
         *  menu's is. */
        else if (mod && k < 128 && (cmd = ui_command_key((char)k, shift)) >= 0)
        {
            a->win_density = SDL_GetWindowPixelDensity(win);
            ui_fill(a);
            ui_command_do(&a->us, cmd);
            ui_apply(a, win, pw, ph);
        }
        else if (mod && k == SDLK_V)
        {
            a->opts.view = shift ? (a->opts.view + 11) % 12
                                 : (a->opts.view + 1) % 12;
            a->dirty     = 1;
        }
        else if (k == SDLK_COMMA || k == SDLK_PERIOD)
            app_turn(a, k == SDLK_COMMA ? -15.0f : 15.0f, win);
        else if ((k == SDLK_Q || k == SDLK_E) && !mod && !ui_wants_keyboard(a->ui) && !geometry_on(a) &&
                 !e->key.repeat)
            app_turn(a, k == SDLK_Q ? -15.0f : 15.0f, win); /* the art alone turns a quarter at a time */
        else if (k == SDLK_0)
            cam_go(a, a->gv.pitch, a->plan ? PLAN_YAW : 0.0f, win); /* back to the snap */
    }
}
