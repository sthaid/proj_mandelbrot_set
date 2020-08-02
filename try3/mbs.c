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

#include <bignum.h>

#define DEFAULT_WIN_WIDTH 200
#define DEFAULT_WIN_HEIGHT 200

#define MAX_ITER 1000

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static int mandelbrot_iterations(num_t *a, num_t *b);

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

#define PIXEL_WHITE ((255 << 0) | (255 << 8) | (255 << 16) | (255 << 24))
#define PIXEL_BLACK ((  0 << 0) | (  0 << 8) | (  0 << 16) | (255 << 24))

#define INITIAL_CTR_A       -0.75
#define INITIAL_CTR_B       0.0
#define INITIAL_PIXEL_SIZE  (3. / pane->w)

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
    static double        zoom_factor;
    static num_t         pixel_size;
    static num_t         ctr_a;
    static num_t         ctr_b;

    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 0)

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
        zoom_factor = 1;
        num_init(&pixel_size, INITIAL_PIXEL_SIZE);
        num_init(&ctr_a, INITIAL_CTR_A);
        num_init(&ctr_b, INITIAL_CTR_B);

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
        int pixel_x_ctr = pane->w / 2;
        int pixel_y_ctr = pane->h / 2;
        int its, pixidx=0, pixel_x, pixel_y;
        num_t num_a,num_b;

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

        // determine mandelbrot set value at all pixel locations
        for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
            for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
                num_multiply2(&num_a, &pixel_size, (pixel_x - pixel_x_ctr));
                num_add(&num_a, &num_a, &ctr_a);
                num_multiply2(&num_b, &pixel_size, (pixel_y - pixel_y_ctr));
                num_add(&num_b, &num_b, &ctr_b);

                its = mandelbrot_iterations(&num_a, &num_b);

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
        case '+': {
            #define ZOOM 10.
            num_t zf2;
            num_init(&zf2, 1/ZOOM);
            num_multiply(&pixel_size, &pixel_size, &zf2);
            zoom_factor *= ZOOM;
            break; }
        case '-': {
            num_t zf1;
            num_init(&zf1, ZOOM);
            num_multiply(&pixel_size, &pixel_size, &zf1);
            zoom_factor /= ZOOM;
            break; }
        case SDL_EVENT_CENTER: {
            num_t delta_ctr_a, delta_ctr_b;
            num_multiply2(&delta_ctr_a, &pixel_size, (event->mouse_click.x - (pane->w/2)));
            num_add(&ctr_a, &ctr_a, &delta_ctr_a);
            num_multiply2(&delta_ctr_b, &pixel_size, (event->mouse_click.y - (pane->h/2)));
            num_add(&ctr_b, &ctr_b, &delta_ctr_b);
            break; }
        case 'r':
            num_init(&pixel_size, INITIAL_PIXEL_SIZE);
            num_init(&ctr_a, INITIAL_CTR_A);
            num_init(&ctr_b, INITIAL_CTR_B);
            zoom_factor = 1;
            break;
        case '?': {
            printf("num_init(&pixel_size , %0.30Le);\n", num_value(&pixel_size));
            printf("num_init(&ctr_a      , %0.30Le);\n", num_value(&ctr_a));
            printf("num_init(&ctr_b      , %0.30Le);\n", num_value(&ctr_b));
            printf("zoom_factor = %0.20lf;\n", zoom_factor);
            break; }
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

static int mandelbrot_iterations(num_t *ca, num_t *cb)
{
    // iterate  z = z^2 + c    starting with z = 0

    int i;
    num_t za, zb, tmp1, tmp2, next_za, next_zb;

    for (i = 0; i < MAX_ITER; i++) {
        if (i == 0) {
            next_za = *ca;
            next_zb = *cb;
        } else {
            num_multiply(&tmp1, &za, &za);
            num_multiply(&tmp2, &zb, &zb);
            num_negate(&tmp2);
            num_add(&next_za, &tmp1, &tmp2);
            num_add(&next_za, &next_za, ca);

            num_multiply(&tmp1, &za, &zb);
            num_lshift(&tmp1, 1);
            num_add(&next_zb, &tmp1, cb);
        }

        za = next_za;
        zb = next_zb;

        double abs_za, abs_zb;
        abs_za = fabs(num_value(&za));
        abs_zb = fabs(num_value(&zb));
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
