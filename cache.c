#include <common.h>

//
// defines
//

#define CACHE_THREAD_REQUEST_NONE   0
#define CACHE_THREAD_REQUEST_RUN    1
#define CACHE_THREAD_REQUEST_STOP   2

// xxx maybe in common
#define CACHE_WIDTH  (2000)
#define CACHE_HEIGHT (1200)

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

typedef struct {
    short  (*mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
    complex  ctr;
    spiral_t spiral;
    bool     spiral_done;
} cache_t;

//
// variables
//

static complex  cache_ctr;   // xxx review how this is used
static int      cache_zoom;  // xxx    ditto
static int      cache_win_width;
static int      cache_win_height;

static cache_t  cache[MAX_ZOOM];
static int      cache_thread_request;

//
// prototypes
//

static void cache_adjust_mbsval_ctr(int zoom);
static void cache_thread_issue_request(int req);
static void *cache_thread(void *cx);
static void cache_get_next_spiral_loc(spiral_t *s);

// - - - - - - - - -  API  - - - - - - - - -

// xxx comments
void cache_init(void)   // complex ctr, int zoom, int win_width, int win_height)
{
    pthread_t id;
    int i;

    //cache_ctr  = 999. + 0 * I;  // AAA   don't use 999 in here at all
    //cache_zoom = zoom; // AAA
    //cache_win_width = win_width;
    //cache_win_height = win_height;

    for (i = 0; i < MAX_ZOOM; i++) {
        cache[i].ctr = 999. + 0 * I;
        cache[i].mbsval = malloc(CACHE_HEIGHT*CACHE_WIDTH*2);
        memset(cache[i].mbsval, 0xff, CACHE_HEIGHT*CACHE_WIDTH*2);
    }

    pthread_create(&id, NULL, cache_thread, NULL);
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
}

void cache_param_change(complex ctr, int zoom, int win_width, int win_height)
{
    // if neither zoom or ctr has changed then return  xxx
    if (zoom == cache_zoom && ctr == cache_ctr && win_width == cache_win_width && win_height == cache_win_height) {
        return;
    }

    // stop the cache_thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    // update cache_ctr and cache_zoom xxx
    cache_ctr  = ctr;
    cache_zoom = zoom;
    cache_win_width = win_width;
    cache_win_height = win_height;

    // xxx explain
    cache_adjust_mbsval_ctr(cache_zoom);

    // run the cache_thread 
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
}

void cache_get_mbsval(short *mbsval)
{
    int idx_b, idx_b_first, idx_b_last;
    cache_t *cache_ptr = &cache[cache_zoom];
    double pixel_size = pixel_size_at_zoom0 * pow(2,-cache_zoom);

    idx_b_first =  (CACHE_HEIGHT/2) + cache_win_height / 2;
    idx_b_last  = idx_b_first - cache_win_height + 1;

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
               &(*cache_ptr->mbsval)[idx_b][(CACHE_WIDTH/2)-cache_win_width/2],
               cache_win_width*sizeof(mbsval[0]));
        mbsval += cache_win_width;
    }
}


// - - - - - - - - -  PRIVATE  - - - - - - -

static void cache_adjust_mbsval_ctr(int zoom)
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

static void cache_thread_issue_request(int req)
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

static void *cache_thread(void *cx)
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


            // AAA
            // XXX this routine should either not be called or do nothing if the ctr has not changed
            cache_adjust_mbsval_ctr(zoom);

            // xxx maybe want 2 spiral done flags now
            if (cache_ptr->spiral_done) {
                INFO("spiral is done at zoom %d\n", zoom);
                goto stop_check2;
            }



            // XXX remember the spiral and pack off where left off
            //   - need spiral_done flag too
            //   - spiral needs to be reset at every zoom level when the ctr has changed
            //  may be helpful if a ctr_changed flag is set above when STARTING

            while (true) {
                cache_get_next_spiral_loc(&cache_ptr->spiral);
                idx_a = cache_ptr->spiral.x + (CACHE_WIDTH/2);
                idx_b = cache_ptr->spiral.y + (CACHE_HEIGHT/2);

                if (n < MAX_ZOOM) {   
                    if (cache_win_width >= cache_win_height) {
                        if (idx_a < CACHE_WIDTH/2 - cache_win_width/2) {
                            break;
                        }
                    } else {
                        if (idx_b < CACHE_HEIGHT/2 - cache_win_height/2) {
                            break;
                        }
                    }
#if 0
                    if (idx_a < CACHE_WIDTH/2 - win_width/2 ||
                        idx_a > CACHE_WIDTH/2 + win_width/2 ||
                        idx_b < CACHE_HEIGHT/2 - win_height/2 ||
                        idx_b > CACHE_HEIGHT/2 + win_height/2)
                    {
                        goto stop_check;
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
                    goto stop_check;
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

stop_check:
                __sync_synchronize();  // xxx doing this too often?
                if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) {
                    break;
                }
            }
stop_check2:
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

static void cache_get_next_spiral_loc(spiral_t *s)
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
