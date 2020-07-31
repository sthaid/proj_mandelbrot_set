// XXX conditionally use double vs long double
// XXX INITIAL_PIXEL_SIZE macro use of pane->w
// XXX maybe center initial sizeos around +/-2

// XXX define controls

// XXX optimize performance
// XXX how to pan

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <complex.h>
#include <assert.h>

#include <util_sdl.h>
#include <util_misc.h>

//
// defines
//

#define DEFAULT_WIN_WIDTH  800
#define DEFAULT_WIN_HEIGHT  800

#define INITIAL_CTR_CA       -0.75
#define INITIAL_CTR_CB       0.0
#define INITIAL_PIXEL_SIZE  (3. / pane->w)

#define MAX_ITER 1000

#define PIXEL_WHITE ((255 << 0) | (255 << 8) | (255 << 16) | (255 << 24))
#define PIXEL_BLACK ((  0 << 0) | (  0 << 8) | (  0 << 16) | (255 << 24))

#define ZOOM 10.0

//
// prototypes
//

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static int mandelbrot_iterations(double ca, double cb);

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
        int  notyet;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    static texture_t     texture;
    static unsigned int *pixels;
    static int           win_width;
    static int           win_height;
    static double        pixel_size;
    static double        ctr_ca;
    static double        ctr_cb;
    static double        zoom_factor;

    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 1)

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        int w, h;

        vars = pane_cx->vars = calloc(1,sizeof(*vars));

        sdl_get_window_size(&w, &h);

        texture     = NULL;
        pixels      = NULL;
        win_width   = w;
        win_height  = h;
        pixel_size  = INITIAL_PIXEL_SIZE;
        ctr_ca      = INITIAL_CTR_CA;
        ctr_cb      = INITIAL_CTR_CB;
        zoom_factor = 1;

        INFO("PANE x,y,w,h  %d %d %d %d\n",
            pane->x, pane->y, pane->w, pane->h);

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        int    curr_texture_width, curr_texture_height;
        int    curr_win_width, curr_win_height;
        int    pixidx=0, pixel_x, pixel_y, its;
        double ca, cb;

        // if window size has changed then update the pane's 
        // location within the window
        sdl_get_window_size(&curr_win_width, &curr_win_height);
        if (curr_win_width != win_width || curr_win_height != win_height) {
            INFO("NEW WIN SIZE %d %d\n", curr_win_width, curr_win_height);
            sdl_pane_update(pane_cx, 0, 0, curr_win_width, curr_win_height);
            win_width = curr_win_width;
            win_height = curr_win_height;
        }

        // if the texture hasn't been allocated yet, or the size of the
        // texture doesn't match the size of the pane then
        // re-allocate the texture and the pixels array
        if ((texture == NULL) || 
            ((sdl_query_texture(texture, &curr_texture_width, &curr_texture_height), true) &&
             (curr_texture_width != pane->w || curr_texture_height != pane->h)))
        {
            INFO("ALLOCATING TEXTURE AND PIXELS\n");
            sdl_destroy_texture(texture);
            free(pixels);
            texture = sdl_create_texture(pane->w, pane->h);
            pixels = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        }

        // comment and clean up
        for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
            for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
                ca = ctr_ca + (pixel_x - pane->w/2) * pixel_size;
                cb = ctr_cb - (pixel_y - pane->h/2) * pixel_size;
                its = mandelbrot_iterations(ca, cb);
                pixels[pixidx++] = (its >= MAX_ITER ? PIXEL_BLACK : PIXEL_WHITE);
            }
        }
        sdl_update_texture(texture, (void*)pixels, pane->w*BYTES_PER_PIXEL);

        // render the texture
        sdl_render_texture(pane, 0, 0, texture);

        // debug
        static int count;
        sdl_render_printf(pane, 0, 0, 20, WHITE, BLACK, "%d %g",
              count++, zoom_factor);

        // register for events
        sdl_register_event(pane, pane, SDL_EVENT_CENTER, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // return
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch (event->event_id) {
        case '+':
            zoom_factor *= ZOOM;
            pixel_size  /= ZOOM;
            break;
        case '-':
            zoom_factor /= ZOOM;
            pixel_size  *= ZOOM;
            break;
        case SDL_EVENT_CENTER: {
            ctr_ca += (event->mouse_click.x - (pane->w/2)) * pixel_size;
            ctr_cb -= (event->mouse_click.y - (pane->h/2)) * pixel_size;
            break; }
        case 'r':
            pixel_size  = INITIAL_PIXEL_SIZE;
            ctr_ca      = INITIAL_CTR_CA;
            ctr_cb      = INITIAL_CTR_CB;
            zoom_factor = 1;
            break;
        case 'q':
            return PANE_HANDLER_RET_PANE_TERMINATE;
            break;

        }
        return PANE_HANDLER_RET_NO_ACTION;
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

// -----------------  MANDELBROT  ---------------------------------------

static int mandelbrot_iterations(double ca, double cb)
{
    complex z = 0;
    complex c = ca + cb*I;
    double  abs_za, abs_zb;
    int     i;

    for (i = 0; i < MAX_ITER; i++) {
        if (i == 0) {
            z = c;
        } else {
            z = z * z + c;
        }

        abs_za = fabs(creal(z));
        abs_zb = fabs(cimag(z));
        if (abs_za < M_SQRT2 && abs_zb < M_SQRT2) {
            continue;
        } else if (abs_za >= 2 || abs_zb >= 2) {
            break;
        } else if (abs_za*abs_za + abs_zb*abs_zb > 4) {
            break;
        }
    }

    return i;
}
