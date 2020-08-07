// XXX
// - lots of cleanup
// - complete review
//
// - use key ctls to adjust the color range
// - keyctl option for black and white vs color
// - use terminal window for debug output and not the display
// - key option to switch to color map display  ??
// - save and restore to a file
// - slow down the display processing
// - util_sdl.c 2019
// - use nearbyint where needed
// - add debug prints
// - display stats either in window or in terminal
//   if in window should have a control to enable or disable
// - maybe auto zoom should wrap instead of bounce
//
// - controls
//   - zoom with mouse wheel
//   - enable or disable status values
//   - help screen
//   - colors screen
//   - esc returns to main screen
//
// - program options
//   - fullscreen
//   - -g wxh
//
// - save / restore
//   - config file - still needs more work 
//     - generalize code for other save numbers
//     - use ALT-0 to save the data too
//        - command to save location and the mbsvalues 
//
// - performance
//   - how to speed up
//     - integer math
//     - multiple threads
//
// XXX maybe later
// - window resize
//   - cache size should track window size, and not be square
// - put the cache code in other file  ??

// XXX SAVE INFO
// - save point     0.27808593632993183764,-0.47566405952660278933


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

//#define DEFAULT_WIN_WIDTH   1600 
//#define DEFAULT_WIN_HEIGHT  900 
#define DEFAULT_WIN_WIDTH   600 
#define DEFAULT_WIN_HEIGHT  600 

//#define INITIAL_CTR         (-0.75 + 0.0*I)
#define INITIAL_CTR  (0.27808593632993183764 -0.47566405952660278933*I)

#define MBSVAL_IN_SET         1000
#define MBSVAL_NOT_COMPUTED  -1

#define PIXEL_WHITE ((255 << 0) | (255 << 8) | (255 << 16) | (255 << 24))
#define PIXEL_BLACK ((  0 << 0) | (  0 << 8) | (  0 << 16) | (255 << 24))
#define PIXEL_BLUE  ((  0 << 0) | (  0 << 8) | (255 << 16) | (255 << 24))

#define MAX_ZOOM 47

#define CACHE_WIDTH  (2000)
#define CACHE_HEIGHT (1200)

//xxx comment on being am multiple
#define ZOOM_STEP .1

//
// typedefs
//


//
// variables
//

int    win_width;
int    win_height;
double pixel_size_at_zoom0;

//
// prototypes
//

int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
int mandelbrot_set(complex c);
void cache_init(complex ctr, int zoom);
void cache_get_mbsval(short *mbsval);
void cache_set_ctr_and_zoom(complex ctr, int zoom);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    //AAA-NEXT getopt   -g NNNxNNN   -f

    // AAA-NEXT
    debug = false; // XXX set with '-d'  XXX and need seperate debugs for different files
                   //   maybe   use *debug in the macro, and set this in the file

    // init sdl
    win_width  = DEFAULT_WIN_WIDTH;
    win_height = DEFAULT_WIN_HEIGHT;
    if (sdl_init(&win_width, &win_height, true, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    INFO("REQUESTED win_width=%d win_height=%d\n", DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT);
    INFO("ACTUAL    win_width=%d win_height=%d\n", win_width, win_height);
    if (win_width != DEFAULT_WIN_WIDTH || win_height != DEFAULT_WIN_HEIGHT) { //xxx remove later
        FATAL("failed to create window %dx%d, got instead %dx%d\n",
              DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT, win_width, win_height);
    }

    // xxx
    pixel_size_at_zoom0 = 4. / win_width;

    // run the pane manger xxx what does this do
    sdl_pane_manager(
        NULL,           // context
        NULL,           // called prior to pane handlers
        NULL,           // called after pane handlers
        50000,          // 0=continuous, -1=never, else us
        1,              // number of pane handler varargs that follow
        pane_hndlr, NULL, 0, 0, win_width, win_height, PANE_BORDER_STYLE_NONE);

    // done
    return 0;
}

// -----------------  PANE_HNDLR  ---------------------------------------

double zoom_step(double z, bool dir_is_incr);

int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event)
{
    struct {
        texture_t     texture;
        unsigned int *pixels;
        complex       lcl_ctr;
        double        lcl_zoom;
        int           auto_zoom;
        int           auto_zoom_last;
        unsigned int  color_lut[65536];  // xxx maxshort
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_PAN      (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_ZOOM     (SDL_EVENT_USER_DEFINED + 2)

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        INFO("PANE x,y,w,h  %d %d %d %d\n", pane->x, pane->y, pane->w, pane->h);

        vars = pane_cx->vars = calloc(1,sizeof(*vars));

        vars->texture   = sdl_create_texture(pane->w, pane->h);
        vars->pixels    = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        vars->lcl_ctr   = INITIAL_CTR;
        vars->lcl_zoom  = 0;
        vars->auto_zoom = 0;    //xxx needs defines
        vars->auto_zoom_last = 1;    //xxx needs defines

        // xxx later vars->color_lut[65535]         = PIXEL_BLUE;

        vars->color_lut[MBSVAL_IN_SET] = PIXEL_BLACK;
        int i;
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            unsigned char r,g,b;
            double wavelen = 400. + i * ((700. - 400.) / (MBSVAL_IN_SET-1));
            sdl_wavelen_to_rgb(wavelen, &r, &g, &b);
            vars->color_lut[i] = (r << 0) | (  g << 8) | (b << 16) | (255 << 24); // xxx mavro
        }

        cache_init(vars->lcl_ctr, floor(vars->lcl_zoom));

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        int            idx = 0, pixel_x, pixel_y;
        unsigned int * pixels = vars->pixels;

#if 0
        // debug
        static unsigned long time_last;
        unsigned long time_now = microsec_timer();
        unsigned long delta_us = time_now - time_last;
        time_last = time_now;
        INFO("*** %ld ms\n", delta_us/1000);
#endif

        // if window size has changed then update the pane's 
        // location within the window
        int curr_win_width, curr_win_height;
        sdl_get_window_size(&curr_win_width, &curr_win_height);
        if (curr_win_width != win_width || curr_win_height != win_height) {
            INFO("NEW WIN SIZE %d %d\n", curr_win_width, curr_win_height);
            sdl_pane_update(pane_cx, 0, 0, curr_win_width, curr_win_height);
            // AAA is it safe to change these without stopping the thread
            win_width = curr_win_width;
            win_height = curr_win_height;
            //pixel_size_at_zoom0 = 4. / win_width;
            // AAA pixel_size_at_zoom0
        }

        // if the texture hasn't been allocated yet, or the size of the
        // texture doesn't match the size of the pane then
        // re-allocate the texture and the pixels array
        int curr_texture_width, curr_texture_height;
        if ((vars->texture == NULL) ||
            ((sdl_query_texture(vars->texture, &curr_texture_width, &curr_texture_height), true) &&
             (curr_texture_width != pane->w || curr_texture_height != pane->h)))
        {
            INFO("ALLOCATING TEXTURE AND PIXELS\n");
            sdl_destroy_texture(vars->texture);
            free(vars->pixels);
            INFO("   %d %d\n", pane->w, pane->h);
            vars->texture = sdl_create_texture(pane->w, pane->h);
            vars->pixels = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
            pixels = vars->pixels;
        }

        // xxx
        if (vars->auto_zoom != 0) {
            vars->lcl_zoom = zoom_step(vars->lcl_zoom, vars->auto_zoom == 1);
            if (vars->lcl_zoom == 0) {
                vars->auto_zoom = 1;
            }
            if (vars->lcl_zoom == MAX_ZOOM - ZOOM_STEP) {
                vars->auto_zoom = 2;
            }
        }

        // inform mandelbrot set cache of the current ctr and zoom
        cache_set_ctr_and_zoom(vars->lcl_ctr, floor(vars->lcl_zoom));

        // get the cached mandelbrot set values; and
        // convert them to pixel color values
        short * mbsval = malloc(win_height*win_width*2);  //xxx make this unsigned
        cache_get_mbsval(mbsval);
        for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
            for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
                pixels[idx] = (mbsval[idx] == MBSVAL_NOT_COMPUTED 
                               ? PIXEL_BLUE
                               : vars->color_lut[mbsval[idx]]);
                idx++;
            }
        }
        free(mbsval);

        // copy the pixels to the texture and render the texture
        sdl_update_texture(vars->texture, (void*)pixels, pane->w*BYTES_PER_PIXEL);

        // xxx
        rect_t dst = {0,0,pane->w,pane->h};
        rect_t src;
        //if changed then do prints
        static double lcl_zoom_last;
        bool do_print = vars->lcl_zoom != lcl_zoom_last;
        lcl_zoom_last = vars->lcl_zoom;
        do_print = false; //xxx

        double xxx = pow(2, -(vars->lcl_zoom - floor(vars->lcl_zoom)));

        src.w = pane->w * xxx;
        src.h = pane->h * xxx;
        src.x = (pane->w - src.w) / 2;
        src.y = (pane->h - src.h) / 2;

        if (do_print) INFO("xxx = %.25lf  xywh = %d %d %d %d\n", 
                            xxx, src.x, src.y, src.w, src.h);

        sdl_render_scaled_texture_ex(pane, &src, &dst, vars->texture);

        // xxx temp
        rect_t loc = { pane->w/2-100, pane->h/2-100, 200, 200};
        sdl_render_rect(pane, &loc, 1, WHITE);

        // register for events
        sdl_register_event(pane, pane, SDL_EVENT_CENTER, SDL_EVENT_TYPE_MOUSE_RIGHT_CLICK, pane_cx);
        sdl_register_event(pane, pane, SDL_EVENT_PAN, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
        sdl_register_event(pane, pane, SDL_EVENT_ZOOM, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);


        // return
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch (event->event_id) {
        case 'a':
            // xxx comment
            if (vars->auto_zoom != 0) {
                vars->auto_zoom_last = vars->auto_zoom;
                vars->auto_zoom = 0;
            } else {
                vars->auto_zoom = vars->auto_zoom_last;
            }
            break;
        case 'A': 
            // flip dir of autozoom
            if (vars->auto_zoom == 1) {
                vars->auto_zoom = 2;
            } else if (vars->auto_zoom == 2) {
                vars->auto_zoom = 1;
            }
            break;
        case 't': {
            static bool fullscreen; // xxx vars
            fullscreen = !fullscreen;
            INFO("setting fullscreen to %d\n", fullscreen);
            sdl_full_screen(fullscreen);
            break; }
        case '+': case '=': case '-':
            vars->lcl_zoom = zoom_step(vars->lcl_zoom, 
                                       event->event_id == '+' || event->event_id == '=');
            break;
        case SDL_EVENT_ZOOM:
            if (event->mouse_wheel.delta_y > 0) {
                vars->lcl_zoom = zoom_step(vars->lcl_zoom, true);
            } else if (event->mouse_wheel.delta_y < 0) {
                vars->lcl_zoom = zoom_step(vars->lcl_zoom, false);
            }
            break;
        case SDL_EVENT_PAN: {
            double pixel_size = pixel_size_at_zoom0 * pow(2,-vars->lcl_zoom);
            vars->lcl_ctr += -(event->mouse_motion.delta_x * pixel_size) + 
                             -(event->mouse_motion.delta_y * pixel_size) * I;
            break; }
        case SDL_EVENT_CENTER: {
            double pixel_size = pixel_size_at_zoom0 * pow(2,-vars->lcl_zoom);
            vars->lcl_ctr += ((event->mouse_click.x - (pane->w/2)) * pixel_size) + 
                             ((event->mouse_click.y - (pane->h/2)) * pixel_size) * I;
            break; }
        case 'r':
            vars->lcl_ctr  = INITIAL_CTR;
            vars->lcl_zoom = 0;
            break;
        case 'z':
            if (vars->lcl_zoom == MAX_ZOOM - ZOOM_STEP) {
                vars->lcl_zoom = 0;
            } else {
                vars->lcl_zoom = MAX_ZOOM - ZOOM_STEP;
            }
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

double zoom_step(double z, bool dir_is_incr)
{
    z += (dir_is_incr ? ZOOM_STEP : -ZOOM_STEP);

    if (fabs(z - nearbyint(z)) < 1e-6) {
        z = nearbyint(z);
    }

    // xxx check for near,
    // xxx max out at MAX_ZOOM-1
    if (z < 0) z = 0;
    if (z > MAX_ZOOM - ZOOM_STEP) z = MAX_ZOOM - ZOOM_STEP;

    return z;
}

// -----------------  MANDELBROT SET EVALUATOR  -------------------------

int mandelbrot_set(complex c)
{
    complex z = 0;
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
    short  (*mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
    complex  ctr;
    spiral_t spiral;
    bool     spiral_done;
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
        cache[i].mbsval = malloc(CACHE_HEIGHT*CACHE_WIDTH*2);
        memset(cache[i].mbsval, 0xff, CACHE_HEIGHT*CACHE_WIDTH*2);
    }

    pthread_create(&id, NULL, cache_thread, NULL);
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
}

void cache_get_mbsval(short *mbsval)
{
    int idx_b, idx_b_first, idx_b_last;
    cache_t *cache_ptr = &cache[cache_zoom];
    double pixel_size = pixel_size_at_zoom0 * pow(2,-cache_zoom);

    idx_b_first =  (CACHE_HEIGHT/2) + win_height / 2;
    idx_b_last  = idx_b_first - win_height + 1;

    if ((fabs(creal(cache_ptr->ctr) - creal(cache_ctr)) > 1.1 * pixel_size) ||
        (fabs(cimag(cache_ptr->ctr) - cimag(cache_ctr)) > 1.1 * pixel_size))
    {
        FATAL("cache_zoom=%d cache_ptr->ctr=%lg+%lgI cache_ctr=%lg+%lgI\n",
              cache_zoom, 
              creal(cache_ptr->ctr), cimag(cache_ptr->ctr),
              creal(cache_ctr), cimag(cache_ctr));
    }

    for (idx_b = idx_b_first; idx_b >= idx_b_last; idx_b--) {
        memcpy(mbsval, 
               &(*cache_ptr->mbsval)[idx_b][(CACHE_WIDTH/2)-win_width/2],
               win_width*sizeof(mbsval[0]));
        mbsval += win_width;
    }
}

void cache_set_ctr_and_zoom(complex ctr, int zoom)
{
    // if neither zoom or ctr has changed then return
    if (zoom == cache_zoom && ctr == cache_ctr) {
        return;
    }

    // stop the cache_thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    // update cache_ctr and cache_zoom
    cache_ctr  = ctr;
    cache_zoom = zoom;

    // xxx explain
    cache_adjust_mbsval_ctr(cache_zoom);

    // run the cache_thread 
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
}

// - - - - - - - - -  PRIVATE  - - - - - - -

void cache_adjust_mbsval_ctr(int zoom)
{
    cache_t *cache_ptr = &cache[zoom];
    int old_y, new_y, delta_x, delta_y;
    double pixel_size = pixel_size_at_zoom0 * pow(2,-zoom);
    short (*new_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
    short (*old_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];

    delta_x = nearbyint((creal(cache_ptr->ctr) - creal(cache_ctr)) / pixel_size);
    delta_y = nearbyint((cimag(cache_ptr->ctr) - cimag(cache_ctr)) / pixel_size);

    if (delta_x == 0 && delta_y == 0) {
        return;
    }

    new_mbsval = malloc(CACHE_HEIGHT*CACHE_WIDTH*2);
    old_mbsval = cache[zoom].mbsval;

    for (new_y = 0; new_y < CACHE_HEIGHT; new_y++) {
        old_y = new_y + delta_y;
        if (old_y < 0 || old_y >= CACHE_HEIGHT) {
            memset(&(*new_mbsval)[new_y][0], 0xff, CACHE_WIDTH*2);
            continue;
        }

        if (delta_x <= -CACHE_WIDTH || delta_x >= CACHE_WIDTH) {
            memset(&(*new_mbsval)[new_y][0], 0xff, CACHE_WIDTH*2);
            continue;
        }

        // XXX temp AAA further optimize, to not do this memset
        memset(&(*new_mbsval)[new_y][0], 0xff, CACHE_WIDTH*2);
        if (delta_x <= 0) {
            memcpy(&(*new_mbsval)[new_y][0],
                   &(*old_mbsval)[old_y][-delta_x],
                   (CACHE_WIDTH + delta_x) * 2);
        } else {
            memcpy(&(*new_mbsval)[new_y][delta_x],
                   &(*old_mbsval)[old_y][0],
                   (CACHE_WIDTH - delta_x) * 2);
        }
    }

    free(cache_ptr->mbsval);
    cache_ptr->mbsval = new_mbsval;
    cache_ptr->ctr = cache_ctr;

    memset(&cache_ptr->spiral, 0, sizeof(spiral_t));
    cache_ptr->spiral_done = false;
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
        usleep(1000);  // AAA shorter usleeps
        __sync_synchronize();
    }
}

void *cache_thread(void *cx)
{
    int        n, idx_a, idx_b, zoom;
    double     pixel_size;
    cache_t  * cache_ptr;

    while (true) {
        // state is now idle;
        // wait here for a request
        while (cache_thread_request == CACHE_THREAD_REQUEST_NONE) {
            usleep(1000);
            __sync_synchronize();
        }

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

        // xxx comment
        // XXX define all at top
        int mbs_call_count;
        unsigned long start_tsc, end_tsc, start_us, end_us, total_mbs_tsc;
        bool was_stopped;
        int dir;

        static int last_cache_zoom;

        INFO("STARTING\n");

        mbs_call_count = 0;
        total_mbs_tsc = 0;
        was_stopped = false;

        if (cache_zoom == 0) {
            dir = 1;
        } else if (cache_zoom >= MAX_ZOOM-1) {
            dir = -1;
        } else {
            dir = (cache_zoom >= last_cache_zoom ? 1 : -1);
        }

        last_cache_zoom = cache_zoom;

        start_tsc = tsc_timer();
        start_us = microsec_timer();

        // AAA spiral should first prioritize getting to window dimensions, and then the whole cache
        for (n = 0; n < 2*MAX_ZOOM; n++) {
            zoom = (cache_zoom + dir*n + 2*MAX_ZOOM) % MAX_ZOOM;
            __sync_synchronize();  // XXX why

            if (n == MAX_ZOOM || n == 2*MAX_ZOOM-1)
                INFO("  n=%d  zoom=%d\n", n, zoom);

            pixel_size = pixel_size_at_zoom0 * pow(2,-zoom);
            cache_ptr = &cache[zoom];

            // xxx maybe want 2 spiral done flags now
            if (cache_ptr->spiral_done) {
                //INFO("spiral is done at zoom %d\n", zoom);
                continue;
            }

            // AAA
            // XXX this routine should either not be called or do nothing if the ctr has not changed
            cache_adjust_mbsval_ctr(zoom);

            // XXX remember the spiral and pack off where left off
            //   - need spiral_done flag too
            //   - spiral needs to be reset at every zoom level when the ctr has changed
            //  may be helpful if a ctr_changed flag is set above when STARTING

            while (true) {
                cache_get_next_spiral_loc(&cache_ptr->spiral);
                idx_a = cache_ptr->spiral.x + (CACHE_WIDTH/2);
                idx_b = cache_ptr->spiral.y + (CACHE_HEIGHT/2);

                if (n < MAX_ZOOM) {   
                    if (win_width >= win_height) {
                        if (idx_a < CACHE_WIDTH/2 - win_width/2) {
                            break;
                        }
                    } else {
                        if (idx_b < CACHE_HEIGHT/2 - win_height/2) {
                            break;
                        }
                    }
#if 0
                    if (idx_a < CACHE_WIDTH/2 - win_width/2 ||
                        idx_a > CACHE_WIDTH/2 + win_width/2 ||
                        idx_b < CACHE_HEIGHT/2 - win_height/2 ||
                        idx_b > CACHE_HEIGHT/2 + win_height/2)
                    {
                        continue;
                    }
#endif
                }

                if (CACHE_WIDTH >= CACHE_HEIGHT) {
                    if (idx_a < 0) {
                        cache_ptr->spiral_done = true;
                        break;
                    }
                } else {
                    if (idx_b < 0) {
                        cache_ptr->spiral_done = true;
                        break;
                    }
                }
                if (idx_a < 0 || idx_a >= CACHE_WIDTH || idx_b < 0 || idx_b >= CACHE_HEIGHT) {
                    continue;
                }

                if ((*cache_ptr->mbsval)[idx_b][idx_a] == MBSVAL_NOT_COMPUTED) {
                    //AAA  check this
                    complex c;
                    unsigned long start_mbs_tsc;

                    start_mbs_tsc = tsc_timer();                    

                    c = (((idx_a-(CACHE_WIDTH/2)) * pixel_size) - 
                        ((idx_b-(CACHE_HEIGHT/2)) * pixel_size) * I) + 
                        cache_ctr;
                    (*cache_ptr->mbsval)[idx_b][idx_a] = mandelbrot_set(c);
                    mbs_call_count++;

                    total_mbs_tsc += tsc_timer() - start_mbs_tsc;
                }

                __sync_synchronize();  // xxx doing this too often?
                if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) {
                    break;
                }
            }
            if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) {
                was_stopped = true;
                cache_thread_request = CACHE_THREAD_REQUEST_NONE;
                __sync_synchronize();
                break;
            }
        }

        end_tsc = tsc_timer();
        end_us = microsec_timer();
        // AAA use DEBUG
        INFO("%s  mbs_call_count=%d  duration=%ld ms  mbs_calc=%ld %%\n",
              !was_stopped ? "DONE" : "STOPPED",
              mbs_call_count,
              (end_us - start_us) / 1000,
              total_mbs_tsc * 100 / (end_tsc - start_tsc));
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
