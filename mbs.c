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

#define DEFAULT_WIN_WIDTH 800
#define DEFAULT_WIN_HEIGHT 800

#define MAX_ITER 1000

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static int mandelbrot_iterations(complex c);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    int win_width, win_height;

    // XXX temp
    printf("%zd\n", sizeof(complex));
    printf("%zd\n", sizeof(double complex));
    printf("%zd\n", sizeof(long double complex));
    printf("%zd\n", sizeof(int complex));
    printf("%zd\n", sizeof(long complex));

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
    // XXX simplify by making these all static
    struct {
        texture_t     texture;
        unsigned int *pixels;
        int           win_width;
        int           win_height;
        double        pixel_size;
        double        ctr_a;
        double        ctr_b;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    #define SDL_EVENT_ZOOM     (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 1)

#define INITIAL_CTR_A  -0.75
#define INITIAL_CTR_B  0.0
#define INITIAL_PIXEL_SIZE  (3. / pane->w)

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        int w, h;

        sdl_get_window_size(&w, &h);

        vars = pane_cx->vars = calloc(1,sizeof(*vars));
        vars->texture    = NULL;
        vars->pixels     = NULL;
        vars->win_width  = w;
        vars->win_height = h;
        vars->pixel_size = INITIAL_PIXEL_SIZE;
        vars->ctr_a      = INITIAL_CTR_A;
        vars->ctr_b      = INITIAL_CTR_B;

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
        //vars->pixels[i] =  (0 << 0) | (255 << 8) | (  0 << 16) | (255 << 24);


        // comment and clean up
        double pixel_x_ctr = pane->w / 2;
        double pixel_y_ctr = pane->h / 2;
        double a, b;
        int its, xxx=0, pixel_x, pixel_y;
        complex c;

        #define PIXEL_WHITE ((255 << 0) | (255 << 8) | (255 << 16) | (255 << 24))
        #define PIXEL_BLACK ((  0 << 0) | (  0 << 8) | (  0 << 16) | (255 << 24))

        for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
            for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
                a = vars->ctr_a + (pixel_x - pixel_x_ctr) * vars->pixel_size;
                b = vars->ctr_b - (pixel_y - pixel_y_ctr) * vars->pixel_size;
                c = a + b * I;
                its = mandelbrot_iterations(c);
                vars->pixels[xxx++] = (its >= MAX_ITER ? PIXEL_BLACK : PIXEL_WHITE);
            }
        }
        sdl_update_texture(vars->texture, (void*)vars->pixels, pane->w*BYTES_PER_PIXEL);

        // render the texture
        sdl_render_texture(pane, 0, 0, vars->texture);

#if 1
        // xxx debug
        static int count;
        sdl_render_printf(pane, 0, 0, 20, WHITE, BLACK, "** %6d **", count++);
        sdl_render_printf(pane, 0, ROW2Y(1,20), 20, WHITE, BLACK, "** %g **", 
            INITIAL_PIXEL_SIZE / vars->pixel_size);
#endif

        // register for events
        sdl_register_event(pane, pane, SDL_EVENT_ZOOM, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);
        sdl_register_event(pane, pane, SDL_EVENT_CENTER, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // return
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    // XXX other funcstions needed
    // - reset pan and zoom
    // - also display the zoom and pan info
    // - keep on calculating when idle to get better detail on center

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch (event->event_id) {
        case SDL_EVENT_ZOOM:
            INFO("ZOOM\n");
            if (event->mouse_wheel.delta_y > 0) {
                vars->pixel_size /= 2.;
            } else if (event->mouse_wheel.delta_y < 0) {
                vars->pixel_size *= 2.;
            }
            break;
        case SDL_EVENT_CENTER:
            vars->ctr_a += (event->mouse_click.x - (pane->w/2)) * vars->pixel_size;
            vars->ctr_b -= (event->mouse_click.y - (pane->h/2)) * vars->pixel_size;
            break;
        case 'r':
            // xxx case 'r' to reset
            vars->ctr_a = INITIAL_CTR_A;
            vars->ctr_b = INITIAL_CTR_B;
            vars->pixel_size = INITIAL_PIXEL_SIZE;
            break;
        case 'q':
            // xxx maybe should clean up first
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

// XXX need other version for bignum
// XXX pass in a or b
// XXX don't use complex

static int mandelbrot_iterations(complex c)
{
    complex z = 0;
    int i;

    for (i = 0; i < MAX_ITER; i++) {
        z = z * z + c;
#if 0
        if (cabsl(z) >= 2) {
            break;
        }
#else
        // XXX verify this is really better
        double abs_a = fabs(creall(z));
        double abs_b = fabs(cimagl(z));
        if (abs_a < M_SQRT2 && abs_b < M_SQRT2) {
            continue;
        } else if (abs_a >= 2 || abs_b >= 2) {
            break;
        } else if (abs_a*abs_a + abs_b*abs_b > 4) {
            break;
        }
#endif
    }

    return i;
}

