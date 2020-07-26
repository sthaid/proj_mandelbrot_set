#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <assert.h>

#include <util_sdl.h>
#include <util_misc.h>

#define DEFAULT_WIN_WIDTH 800
#define DEFAULT_WIN_HEIGHT 800

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    int win_width, win_height;

    // init sdl
    win_width  = DEFAULT_WIN_WIDTH;
    win_height = DEFAULT_WIN_HEIGHT;
    if (sdl_init(&win_width, &win_height, true, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    INFO("REQUESTED win_width=%d win_height=%d\n", DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT);
    INFO("ACTUAL    win_width=%d win_height=%d\n", win_width, win_height);

    // run the pane manger
    sdl_pane_manager(
        NULL,           // context
        NULL,           // called prior to pane handlers
        NULL,           // called after pane handlers
        0,              // 0=continuous, -1=never, else us
        1,              // number of pane handler varargs that follow
        pane_hndlr, NULL, 0, 0, win_width, win_height, PANE_BORDER_STYLE_NONE);

    // done
    return 0;
}

// -----------------  PANE_HNDLR  ---------------------------------------

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event)
{
    struct {
        texture_t     texture;
        unsigned int *pixels;
        int           win_width;
        int           win_height;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    #define SDL_EVENT_ZOOM     (SDL_EVENT_USER_DEFINED + 0)

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        vars = pane_cx->vars = calloc(1,sizeof(*vars));
        sdl_get_window_size(&vars->win_width, &vars->win_height);
        INFO("PANE x,y,w,h  %d %d %d %d\n",
            pane->x, pane->y, pane->w, pane->h);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        int curr_texture_width, curr_texture_height;
        int curr_win_width, curr_win_height;
        int i;

        // if window size has changed then update the pane's 
        // location within the window
        sdl_get_window_size(&curr_win_width, &curr_win_height);
        if (curr_win_width != vars->win_width || curr_win_height != vars->win_height) {
            INFO("XXX NEW WIN SIZE %d %d\n", curr_win_width, curr_win_height);
            sdl_pane_update(pane_cx, 0, 0, curr_win_width, curr_win_height);
            vars->win_width = curr_win_width;
            vars->win_height = curr_win_height;
        }

        // if the texture hasn't been allocated yet, or the size of the
        // texture doesn't match the size of the pane then
        // re-allocate the texture and the pixels array
        if ((vars->texture == NULL) || 
            ((sdl_query_texture(vars->texture, &curr_texture_width, &curr_texture_height), true) &&
             (curr_texture_width != pane->w || curr_texture_height != pane->h)))
        {
            sdl_destroy_texture(vars->texture);
            free(vars->pixels);

            INFO("ALLOCATING TEXTURE AND PIXELS\n");
            vars->texture = sdl_create_texture(pane->w, pane->h);
            vars->pixels = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        }

        // xxx test code
        for (i = 0; i < pane->w*pane->h; i++) {
            vars->pixels[i] =  (0 << 0) | (255 << 8) | (  0 << 16) | (255 << 24);
        }
        sdl_update_texture(vars->texture, (void*)vars->pixels, pane->w*BYTES_PER_PIXEL);

        // render the texture
        sdl_render_texture(pane, 0, 0, vars->texture);
        // xxx debug
        static int count;
        sdl_render_printf(pane, 0, 0, 20, WHITE, BLACK, "** %6d **", count++);

        // register for zoom events
        sdl_register_event(pane, pane, SDL_EVENT_ZOOM, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

        // return
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch (event->event_id) {
        case SDL_EVENT_ZOOM:
            INFO("ZOOM\n");
            break;
        case SDL_EVENT_WIN_SIZE_CHANGE:
            INFO("SIZE\n");
            break;
        }
        return PANE_HANDLER_RET_DISPLAY_REDRAW;
    }

    // ---------------------------
    // -------- TERMINATE --------
    // ---------------------------

    if (request == PANE_HANDLER_REQ_TERMINATE) {
        free(vars);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

