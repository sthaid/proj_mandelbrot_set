
// XXX define controls
//   ZOOM: + / - zoom,  AND MAYBE mouse wheel is zoom
//   PAN:  left click is pan   AND arrows are pan
//   CENTER ON MOUSE:  right click center
//   'r' reset
// MAYBE
//   esc is undo pan or zoom

// XXX optimize performance
// - multiple threads, but this may not help, but define a number of threads
// - save values and migrate them to new save array when zoom changes
//    - save at twice the current resolution (pixel_size)
// - the render code need not wait for all the values to be ready, display a unique color if not ready
// - the threads(s) can spiral outward
// - can we more quickly stop iterating if we are sure early that we're in the mandelbrot set
// - print performance metric to terminal

// XXX colors

// XXX optimize depth
// - conditionally use double vs long double

// XXX Misc
// - INITIAL_PIXEL_SIZE macro use of pane->w
// - maybe center initial sizeos around +/-2
// - display status in terminal and not in window
// - control to click and either zoom in,out or none
// - how to have a control to center vs pan

// XXX Other features 
// - bookmark favorite locations, including zoom level, and possiley the cache too

// XXX enable resizeable later

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
#define DEFAULT_WIN_HEIGHT 800

// XXX fix this
#if 1
#define INITIAL_CTR_CA       -0.75
#define INITIAL_CTR_CB       0.0
#define INITIAL_PIXEL_SIZE  (3. / pane->w)
#else
#define INITIAL_CTR_CA       0.0
#define INITIAL_CTR_CB       0.0
#define INITIAL_PIXEL_SIZE  (4. / pane->w)
#endif

#define MBSVAL_IN_SET 1000

#define PIXEL_WHITE ((255 << 0) | (255 << 8) | (255 << 16) | (255 << 24))
#define PIXEL_BLACK ((  0 << 0) | (  0 << 8) | (  0 << 16) | (255 << 24))
#define PIXEL_BLUE  ((  0 << 0) | (  0 << 8) | (255 << 16) | (255 << 24))

#define ZOOM 2.0

//
// typedefs
//

typedef struct {
    int x;
    int y;
    int dir;
    int cnt;
    int maxcnt;
} spiral_t;

struct {
    short    mbsval[2000][2000];
    double   ctr_ca;  // XXX use complex
    double   ctr_cb;
    double   pixel_size;
    long     thread_request;
} cache;

//
// prototypes
//

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static int mandelbrot_set(double ca, double cb);
void cache_init(double ctr_ca, double ctr_cb, double pixel_size);
int cache_get(int idx_x, int idx_y);
void cache_change_ctr(double ctr_ca, double ctr_cb);
void *cache_thread(void *cx);
void get_next_spiral_loc(spiral_t *s);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    int win_width, win_height;

    // init sdl
    win_width  = DEFAULT_WIN_WIDTH;
    win_height = DEFAULT_WIN_HEIGHT;
    if (sdl_init(&win_width, &win_height, false, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    INFO("REQUESTED win_width=%d win_height=%d\n", DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT);
    INFO("ACTUAL    win_width=%d win_height=%d\n", win_width, win_height);
    // XXX for now exit if size is different

    // run the pane manger
    sdl_pane_manager(
        NULL,           // context
        NULL,           // called prior to pane handlers
        NULL,           // called after pane handlers
        20000,          // 0=continuous, -1=never, else us
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

    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_PAN      (SDL_EVENT_USER_DEFINED + 1)


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

        // XXX  where should this be called from
        cache_init(ctr_ca, ctr_cb, pixel_size);

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
        int    pixidx=0, pixel_x, pixel_y, mbsval;

        // XXX consider cache_update called here

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
                // XXX maybe pass pixel in
                mbsval = cache_get(pixel_x-pane->w/2, pixel_y-pane->h/2);

                if (mbsval == -1) {
                    pixels[pixidx++] = PIXEL_BLUE;
                } else {
                    pixels[pixidx++] = (mbsval == MBSVAL_IN_SET ? PIXEL_BLACK : PIXEL_WHITE);
                }
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
        sdl_register_event(pane, pane, SDL_EVENT_CENTER, SDL_EVENT_TYPE_MOUSE_RIGHT_CLICK, pane_cx);
        sdl_register_event(pane, pane, SDL_EVENT_PAN, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);

        // return
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch (event->event_id) {
        case SDL_EVENT_PAN:
            break;
        case '+':
            zoom_factor *= ZOOM;
            pixel_size  /= ZOOM;
            break;
        case '-':
            zoom_factor /= ZOOM;
            pixel_size  *= ZOOM;
            break;
        case SDL_EVENT_CENTER: {  // XXX not working now
            ctr_ca += (event->mouse_click.x - (pane->w/2)) * pixel_size;
            ctr_cb += (event->mouse_click.y - (pane->h/2)) * pixel_size;
            cache_change_ctr(ctr_ca, ctr_cb);  // xxx  move ?
            break; }
        case 'r':
            pixel_size  = INITIAL_PIXEL_SIZE;
            ctr_ca      = INITIAL_CTR_CA;
            ctr_cb      = INITIAL_CTR_CB;
            zoom_factor = 1;
            cache_change_ctr(ctr_ca, ctr_cb);  // xxx  move ?
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

// -----------------  MANDELBROT SET EVALUATOR  -------------------------

//XXX mbsval 0-999 means  NOT IN SET
//    mbsval 1000  means  IN SET

static int mandelbrot_set(double ca, double cb)
{
    complex z = 0;
    complex c = ca + cb*I;
    double  abs_za, abs_zb;
    int     mbsval;

    for (mbsval = 0; mbsval < MBSVAL_IN_SET; mbsval++) {
        z = z * z + c;

        abs_za = fabs(creal(z));
        abs_zb = fabs(cimag(z));
        if (abs_za < M_SQRT2 && abs_zb < M_SQRT2) {
            continue;
        } else if (abs_za >= 2 || abs_zb >= 2) {
            break;
        } else if (abs_za*abs_za + abs_zb*abs_zb >= 4) {
            break;
        }
    }

    return mbsval;
}

// -----------------  MANDELBROT SET CACHED RESULTS  --------------------

// XXX nearbyint here and in other places where this is being done
// XXX also support multiple threads



void cache_init(double ctr_ca, double ctr_cb, double pixel_size)
{
    pthread_t id;

    memset(cache.mbsval,0xff,sizeof(cache.mbsval));
    cache.ctr_ca = ctr_ca;
    cache.ctr_cb = ctr_cb;
    cache.pixel_size = pixel_size;

    pthread_create(&id, NULL, cache_thread, NULL);
}

// returns -1 : no value
//        else mandelbort mbsval
int cache_get(int idx_x, int idx_y)
{
    return cache.mbsval[idx_y+1000][idx_x+1000];
}

void *cache_thread(void *cx)
{
    long last_completed_request = -1;
    long current_request;
    spiral_t spiral;
    int idx_x, idx_y;

    while (true) {
        // wait for work to do
        while ((current_request = cache.thread_request) == last_completed_request) {
            usleep(1000);
        }
        INFO("STARTING\n");

        // spiral out from center of cache, filling in mandelbrot
        // set iteration results for cache elements that don't have 
        // them compute the mandelbrot set iteration result and save
        // that value in the cache

        // XXX locking considerations when cache ctr or pixelsize is adjusted

        static int cnt=0;
        memset(&spiral, 0, sizeof(spiral_t));
        while (true) {
            // move to next location in spiral
            get_next_spiral_loc(&spiral);

            idx_x = spiral.x + 1000;
            idx_y = spiral.y + 1000;
            if (idx_x < 0 || idx_x >= 2000 || idx_y < 0 || idx_y >= 2000) {
                break;
            }

            if (cache.mbsval[idx_y][idx_x] == -1) {
                double ca = (idx_x-1000) * cache.pixel_size + cache.ctr_ca;
                double cb = (idx_y-1000) * cache.pixel_size + cache.ctr_cb;
                cache.mbsval[idx_y][idx_x] = mandelbrot_set(ca, cb);
                cnt++;
            }

#if 0
            periodically drop mutex, yield and re acquire
            if (cache.thread_request != current_request) { 
                break;
            }
#endif

        }
        INFO("DONE cnt=%d\n", cnt);
        
        last_completed_request = current_request;
    }
}

void cache_change_ctr(double ctr_ca, double ctr_cb)
{
    short new_mbsval[2000][2000];
    int old_x, old_y, new_x, new_y, delta_x, delta_y;

    // XXX request thread abort, and wait for that

    // XXX
    delta_x = nearbyint((ctr_ca - cache.ctr_ca) / cache.pixel_size);
    delta_y = nearbyint((ctr_cb - cache.ctr_cb) / cache.pixel_size);
    INFO("%d %d\n",  delta_x, delta_y);

    // XXX this can be improved
    memset(new_mbsval,0xff,sizeof(new_mbsval));
    for (old_x = 0; old_x < 2000; old_x++) {
        new_x = old_x - delta_x;
        if (new_x < 0 || new_x >= 2000) continue;
        for (old_y = 0; old_y < 2000; old_y++) {
            new_y = old_y - delta_y;
            if (new_y < 0 || new_y >= 2000) continue;
            new_mbsval[new_y][new_x] = cache.mbsval[old_y][old_x];
        }
    }

    // update cache
    memcpy(cache.mbsval, new_mbsval, sizeof(new_mbsval));
    cache.ctr_ca = ctr_ca;
    cache.ctr_cb = ctr_cb;
    __sync_synchronize();

    // tell thread to run
    cache.thread_request++;
    __sync_synchronize();
}

// -----------------  XXXXXXXXXXXXXXXXXXXXXXXXXXXXX  --------------------

void get_next_spiral_loc(spiral_t *s)
{
    #define DIR_RIGHT  0
    #define DIR_DOWN   1
    #define DIR_LEFT   2
    #define DIR_UP     3

    if (s->maxcnt == 0) {
        s->maxcnt = 1;
    } else {
        switch (s->dir) {
        case DIR_RIGHT:  s->x++; break;
        case DIR_DOWN:   s->y--; break;
        case DIR_LEFT:   s->x--; break;
        case DIR_UP:     s->y++; break;
        default:         FATAL("invalid dir %d\n", s->dir); break;
        }

        s->cnt++;
        if (s->cnt == s->maxcnt) {
            s->cnt = 0;
            if (s->dir == DIR_DOWN || s->dir == DIR_UP) {
                s->maxcnt++;
            }
            s->dir = (s->dir+1) % 4;
        }
    }
}

