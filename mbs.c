// XXX NEXT
// - search for and cleanup AAA, XXX xxx
// - complete review

// XXX  how to speed up
// - integer math
// - don't use 2000x2000
// - multiple threads

// XXX general improvements
// - window resize
// - colors
// - zoom using textures,  zoom vars need to be doulbe to do this
// - command to save location and the mbsvalues 

// XXX deeper search
// - long double

// XXX general cleanup
// - use nearbyint where needed
// - put the cache code in other file
// - review util/util_sdl.c history
// - add debug prints

// XXX debug 
// - display stats either in window or in terminal
//   if in window should have a control to enable or disable


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

// AAA make it dynamic resizeable
#if 1
#define DEFAULT_WIN_WIDTH  800
#define DEFAULT_WIN_HEIGHT 800
#else   // XXX try to get resizing to work this way first
#define DEFAULT_WIN_WIDTH  1600
#define DEFAULT_WIN_HEIGHT 800
#endif

#define INITIAL_CTR         (-0.75 + 0.0*I)
#define INITIAL_ZOOM        (0)
#define PIXEL_SIZE_AT_ZOOM0 (3./DEFAULT_WIN_WIDTH)

#define MBSVAL_IN_SET   1000
#define MBSVAL_NOT_COMPUTED  -1

#define PIXEL_WHITE ((255 << 0) | (255 << 8) | (255 << 16) | (255 << 24))
#define PIXEL_BLACK ((  0 << 0) | (  0 << 8) | (  0 << 16) | (255 << 24))
#define PIXEL_BLUE  ((  0 << 0) | (  0 << 8) | (255 << 16) | (255 << 24))

#define MAX_ZOOM 50  //xxx temp

//
// typedefs
//


//
// variables
//

int debug_zoom; //AAA  temp

//
// prototypes
// xxx check these
//

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);

static int mandelbrot_set(complex c);

void cache_init(complex ctr, int zoom);
void cache_get_mbsval(short *mbsval);
void cache_set_ctr_and_zoom(complex ctr, int zoom);

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
    // xxx for now exit if size is different

    // run the pane manger xxx what does this do
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
        texture_t     texture;
        unsigned int *pixels;
        complex       lcl_ctr;
        int           lcl_zoom;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_PAN      (SDL_EVENT_USER_DEFINED + 1)

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        INFO("PANE x,y,w,h  %d %d %d %d\n", pane->x, pane->y, pane->w, pane->h);

        vars = pane_cx->vars = calloc(1,sizeof(*vars));

        vars->texture  = sdl_create_texture(pane->w, pane->h);
        vars->pixels   = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        vars->lcl_ctr  = INITIAL_CTR;
        vars->lcl_zoom = INITIAL_ZOOM;

        cache_init(vars->lcl_ctr, vars->lcl_zoom);

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        int            idx = 0, pixel_x, pixel_y;
        unsigned int * pixels = vars->pixels;
        short          mbsval[800*800];

        // debug
        static unsigned long time_last;
        unsigned long time_now = microsec_timer();
        unsigned long delta_us = time_now - time_last;
        time_last = time_now;
        INFO("*******************************************  %ld ms\n", delta_us/1000);

        // inform mandelbrot set cache of the current ctr and zoom
        cache_set_ctr_and_zoom(vars->lcl_ctr, vars->lcl_zoom);

        // get the cached mandelbrot set values; and
        // convert them to pixel color values
        cache_get_mbsval(mbsval);
        for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
            for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
                pixels[idx] = (mbsval[idx] == MBSVAL_NOT_COMPUTED ? PIXEL_BLUE  :
                               mbsval[idx] == MBSVAL_IN_SET  ? PIXEL_BLACK :
                                                               PIXEL_WHITE);
                idx++;
            }
        }

        // copy the pixels to the texture and render the texture
        sdl_update_texture(vars->texture, (void*)pixels, pane->w*BYTES_PER_PIXEL);
        sdl_render_texture(pane, 0, 0, vars->texture);

        // debug
        static int count;
        sdl_render_printf(pane, 0, 0, 20, WHITE, BLACK, "%d %d %d",
              count++, vars->lcl_zoom, debug_zoom);

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
        case '+': case '=':
            if (vars->lcl_zoom < MAX_ZOOM-1) {
                vars->lcl_zoom++;
            }
            break;
        case '-':
            if (vars->lcl_zoom > 0) {
                vars->lcl_zoom--;
            }
            break;
        case SDL_EVENT_PAN: {
            double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-vars->lcl_zoom);
            vars->lcl_ctr += -(event->mouse_motion.delta_x * pixel_size) + 
                             -(event->mouse_motion.delta_y * pixel_size) * I;
            break; }
        case SDL_EVENT_CENTER: {
            double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-vars->lcl_zoom);
            vars->lcl_ctr += ((event->mouse_click.x - (pane->w/2)) * pixel_size) + 
                             ((event->mouse_click.y - (pane->h/2)) * pixel_size) * I;
            break; }
        case 'r':
            vars->lcl_ctr  = INITIAL_CTR;
            vars->lcl_zoom = INITIAL_ZOOM;
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
        sdl_destroy_texture(vars->texture);
        free(vars->pixels);
        free(vars);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

// -----------------  MANDELBROT SET EVALUATOR  -------------------------

static int mandelbrot_set(complex c)
{
#if 0
    complex z = 0;
#else
    long double complex z = 0;   // AAA make a define to make it easy to switch between these,  and all the associated routiens
#endif
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

// defines

#define CACHE_THREAD_REQUEST_NONE   0
#define CACHE_THREAD_REQUEST_RUN    1
#define CACHE_THREAD_REQUEST_STOP   2

// typedefs

typedef struct {
    int x;
    int y;
    int dir;
    int cnt;
    int maxcnt;
} spiral_t;

typedef struct {
    short (*mbsval)[2000][2000];
    complex ctr;
} cache_t;

// variables

cache_t  cache[MAX_ZOOM];
int      cache_thread_request;
complex  cache_ctr;   // xxx review how this is used
int      cache_zoom;  // xxx    ditto

// prototypes

void cache_adjust_mbsval_ctr(int zoom);
void cache_thread_issue_request(int req);
void *cache_thread(void *cx);
void cache_get_next_spiral_loc(spiral_t *s);

// - - - - - - - - -  API  - - - - - - - - -

// xxx comments
void cache_init(complex ctr, int zoom)
{
    pthread_t id;
    int i;

    cache_ctr  = 999. + 0 * I;  // AAA
    cache_zoom = 0;  // AAA

    for (i = 0; i < MAX_ZOOM; i++) {
        cache[i].ctr = 999. + 0 * I;
        cache[i].mbsval = malloc(2000*2000*2);
        memset(cache[i].mbsval, 0xff, 2000*2000*2);
    }

    pthread_create(&id, NULL, cache_thread, NULL);
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
}

void cache_get_mbsval(short *mbsval)
{
    int idx_b, idx_b_first, idx_b_last;
    cache_t *cache_ptr = &cache[cache_zoom];

    idx_b_first =  1000 + 800 / 2;
    idx_b_last  = idx_b_first - 800 + 1;

    if (cache_ptr->ctr != cache_ctr) {
        FATAL("cache_zoom=%d cache_ptr->ctr=%lg+%lgI cache_ctr=%lg+%lgI\n",
              cache_zoom, 
              creal(cache_ptr->ctr), cimag(cache_ptr->ctr),
              creal(cache_ctr), cimag(cache_ctr));
    }

    for (idx_b = idx_b_first; idx_b >= idx_b_last; idx_b--) {
        memcpy(mbsval, 
               &(*cache_ptr->mbsval)[idx_b][1000-800/2],
               800*sizeof(mbsval[0]));
        mbsval += 800;
    }
}

// AAA time the steps in this routine
void cache_set_ctr_and_zoom(complex ctr, int zoom)
{
    INFO("XXX CALLED\n");

    // if neither zoom or ctr has changed then return
    if (zoom == cache_zoom && ctr == cache_ctr) {
        return;
    }

    // stop the cache_thread
    INFO("XXX STOPPING CACHE THREAD\n");
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);
    INFO("XXX STOPPED CACHE THREAD\n");

    // update cache_ctr and cache_zoom
    cache_ctr  = ctr;
    cache_zoom = zoom;

    // xxx
    INFO("XXX CALLING ADJUST FOR %d\n", cache_zoom);
    cache_adjust_mbsval_ctr(cache_zoom);
    INFO("XXX DONE CALLING ADJUST FOR %d\n", cache_zoom);

    // run the cache_thread 
    INFO("XXX STARTING CACHE THREAD\n");
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
    INFO("XXX DONE STARTING CACHE THREAD\n");
}

// - - - - - - - - -  PRIVATE  - - - - - - -

// AAA this routine needs cleanup and optimize
void cache_adjust_mbsval_ctr(int zoom)
{
    cache_t *cache_ptr = &cache[zoom];
    int old_y, new_y, delta_x, delta_y;
    double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-zoom);
    short (*new_mbsval)[2000][2000];
    short (*old_mbsval)[2000][2000];

    delta_x = nearbyint((creal(cache_ptr->ctr) - creal(cache_ctr)) / pixel_size);
    delta_y = nearbyint((cimag(cache_ptr->ctr) - cimag(cache_ctr)) / pixel_size);
    INFO("%d %d  zoom=%d\n",  delta_x, delta_y, zoom);

    // AAA if these are near zero then don't do it

    new_mbsval = malloc(2000*2000*2);
    old_mbsval = cache[zoom].mbsval;

    for (new_y = 0; new_y < 2000; new_y++) {
        old_y = new_y + delta_y;
        if (old_y < 0 || old_y >= 2000) {
            memset(&(*new_mbsval)[new_y][0], 0xff, 2000*2);
            continue;
        }

        if (delta_x <= -2000 || delta_x >= 2000) {
            memset(&(*new_mbsval)[new_y][0], 0xff, 2000*2);
            continue;
        }

        // XXX temp AAA further optimize, to not do this memset
        memset(&(*new_mbsval)[new_y][0], 0xff, 2000*2);
        if (delta_x <= 0) {
            memcpy(&(*new_mbsval)[new_y][0],
                   &(*old_mbsval)[old_y][-delta_x],
                   (2000 + delta_x) * 2);
        } else {
            memcpy(&(*new_mbsval)[new_y][delta_x],
                   &(*old_mbsval)[old_y][0],
                   (2000 - delta_x) * 2);
        }
    }

    free(cache_ptr->mbsval);
    cache_ptr->mbsval = new_mbsval;
    cache_ptr->ctr = cache_ctr;
}

void cache_thread_issue_request(int req)
{
    // xxx comments
    __sync_synchronize();

    // xxx comments
    cache_thread_request = req;
    __sync_synchronize();

    // xxx comments
    while (cache_thread_request != CACHE_THREAD_REQUEST_NONE) {
        //usleep(1000);
        __sync_synchronize();
    }
}

// AAA are requests working properly?
// AAA put prints in here
void *cache_thread(void *cx)
{
    int        n, idx_a, idx_b, zoom;
    double     pixel_size;
    spiral_t   spiral;
    cache_t  * cache_ptr;

    while (true) {
        //xxx temp
        debug_zoom = -1;

        // state is now idle;
        // wait here for a request
        while (cache_thread_request == CACHE_THREAD_REQUEST_NONE) {
            //usleep(1000);
            __sync_synchronize();
        }
        INFO("  got request %d\n", cache_thread_request);

        // if received stop request then 
        // ack the request and remain idle
        if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) {
            cache_thread_request = CACHE_THREAD_REQUEST_NONE;
            __sync_synchronize();
            continue;
        }

        // the request must be a run request;
        // set state to running and ack the request
        if (cache_thread_request != CACHE_THREAD_REQUEST_RUN) {
            FATAL("invalid cache_thread_request %d\n", cache_thread_request);
        }
        cache_thread_request = CACHE_THREAD_REQUEST_NONE;
        __sync_synchronize();

        INFO("cache thread is starting\n");

        // xxx comment
        for (n = 0; n < MAX_ZOOM; n++) {
            zoom = (cache_zoom + n) % MAX_ZOOM;
            __sync_synchronize();
            if (zoom < 0 || zoom >= MAX_ZOOM) {  //AAA not needed
                continue;
            }
            debug_zoom = zoom; //AAA

            pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-zoom);
            cache_ptr = &cache[zoom];

            // AAA
            cache_adjust_mbsval_ctr(zoom);

            memset(&spiral, 0, sizeof(spiral_t));
            while (true) {
                cache_get_next_spiral_loc(&spiral);

                idx_a = spiral.x + 1000;
                idx_b = spiral.y + 1000;
                if (idx_a < 0 || idx_a >= 2000 || idx_b < 0 || idx_b >= 2000) {
                    break;
                }

                if ((*cache_ptr->mbsval)[idx_b][idx_a] == MBSVAL_NOT_COMPUTED) {
//AAA  check this
                    complex c = (((idx_a-1000) * pixel_size) - ((idx_b-1000) * pixel_size) * I) + cache_ctr;
                    (*cache_ptr->mbsval)[idx_b][idx_a] = mandelbrot_set(c);
                }

                __sync_synchronize();  // xxx doing this too often?
                if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) {
                    INFO("BEING STOPPED AAA print count\n");
                    break;
                }
            }
            if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) {
                cache_thread_request = CACHE_THREAD_REQUEST_NONE;
                __sync_synchronize();
                break;
            }
        }
    }
}

void cache_get_next_spiral_loc(spiral_t *s)
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

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#if 0
        // spiral out from center of cache, filling in mandelbrot
        // set iteration results for cache elements that don't have 
        // them compute the mandelbrot set iteration result and save
        // that value in the cache
        INFO("STARTING\n");
        int cnt;
        spiral_t spiral;
        double pixel_size = 0;  //xxx PIXEL_SIZE_AT_ZOOM0 * pow(2,-zoom);
        memset(&spiral, 0, sizeof(spiral_t));
        int idx_x, idx_y;

        // xxx first do this for current level
        //     then for other levels 
        //       - if recenter needed then do that
        //       - calcu mbs val, but first check if it is already available

        cnt = 0;
        INFO("DONE cnt=%d\n", cnt);
#endif

#if 0
void cache_change_ctr(double ctr_ca, double ctr_cb)
{
    short new_mbsval[2000][2000];
    int old_x, old_y, new_x, new_y, delta_x, delta_y;

    // XXX request thread abort, and wait for that

    // XXX  THIS IS actually delta pixels
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
#endif
