// xxx now using usleep(100) check /bin/top when idle
// xxx see if it can work with window size smaller than cache size
// xxx window must be in the cache
// xxx should idx_a be called idx_x

#include <common.h>

//
// defines
//

#define CACHE_THREAD_REQUEST_NONE   0
#define CACHE_THREAD_REQUEST_RUN    1
#define CACHE_THREAD_REQUEST_STOP   2

#define CACHE_WIDTH  2000
#define CACHE_HEIGHT 2000

#define MBSVAL_BYTES   (CACHE_HEIGHT*CACHE_WIDTH*2)

#define MAGIC_MBS_FILE 0x11224567

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
    int      zoom;
    double   pixel_size;
    spiral_t phase1_spiral;
    bool     phase1_spiral_done;
    spiral_t phase2_spiral;
    bool     phase2_spiral_done;
} cache_t;

typedef struct {
    int     magic;
    complex ctr;
    double  zoom;
} file_hdr_t;

//
// variables
//

static complex  cache_ctr;
static int      cache_zoom;
static int      cache_win_width;
static int      cache_win_height;
static cache_t  cache[MAX_ZOOM];

static spiral_t cache_initial_spiral;
static int      cache_thread_request;
static complex  cache_thread_finished_for_cache_ctr;

static int      cache_status_phase;
static int      cache_status_percent_complete;
static int      cache_status_zoom_lvl_inprog;

//
// prototypes
//

static void cache_adjust_mbsval_ctr(cache_t *cp);
static void *cache_thread(void *cx);
static void cache_thread_issue_request(int req);
static void cache_spiral_init(spiral_t *s, int x, int y);
static void cache_spiral_get_next(spiral_t *s, int *x, int *y);

#define CTR_INVALID  (999 + 0 * I)

// -----------------  API  ------------------------------------------------------------

// xxx comments
void cache_init(double pixel_size_at_zoom0)
{
    pthread_t id;
    int zoom;

    cache_spiral_init(&cache_initial_spiral, CACHE_WIDTH/2, CACHE_HEIGHT/2);

    for (zoom = 0; zoom < MAX_ZOOM; zoom++) {
        cache_t *cp = &cache[zoom];

        cp->mbsval = malloc(MBSVAL_BYTES);

        memset(cp->mbsval, 0xff, MBSVAL_BYTES);
        cp->ctr                = CTR_INVALID;
        cp->zoom               = zoom;
        cp->pixel_size         = pixel_size_at_zoom0 * pow(2,-zoom);
        cp->phase1_spiral      = cache_initial_spiral;
        cp->phase1_spiral_done = true;
        cp->phase2_spiral      = cache_initial_spiral;
        cp->phase2_spiral_done = true;
    }

    pthread_create(&id, NULL, cache_thread, NULL);
}

void cache_param_change(complex ctr, int zoom, int win_width, int win_height)
{
    int z;

    // if zoom, ctr and window dims remain the same then return
    if (zoom == cache_zoom && ctr == cache_ctr && win_width == cache_win_width && win_height == cache_win_height) {
        return;
    }

    // stop the cache_thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    // if either window dimension has increased then 
    // all of the spirals need to be reset
    if (win_width > cache_win_width || win_height > cache_win_height) {
        for (z = 0; z < MAX_ZOOM; z++) {
            cache_t *cp = &cache[z];
            cp->phase1_spiral      = cache_initial_spiral;
            cp->phase1_spiral_done = false;
            cp->phase2_spiral      = cache_initial_spiral;
            cp->phase2_spiral_done = false;
        }
    }

    // update cache_ctr, cache_zoom, cache_win_width/height
    cache_ctr        = ctr;
    cache_zoom       = zoom;
    cache_win_width  = win_width;
    cache_win_height = win_height;

    // xxx explain
    cache_adjust_mbsval_ctr(&cache[cache_zoom]);

    // run the cache_thread, the cache thread restarts from the begining
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
}

void cache_get_mbsval(short *mbsval)
{
    int idx_b, idx_b_first, idx_b_last;
    cache_t *cp = &cache[cache_zoom];

    idx_b_first =  (CACHE_HEIGHT/2) + cache_win_height / 2;
    idx_b_last  = idx_b_first - cache_win_height + 1;

    if ((fabs(creal(cp->ctr) - creal(cache_ctr)) > 1.1 * cp->pixel_size) ||
        (fabs(cimag(cp->ctr) - cimag(cache_ctr)) > 1.1 * cp->pixel_size))
    {
        FATAL("cache_zoom=%d cp->ctr=%lg+%lgI cache_ctr=%lg+%lgI\n",
              cache_zoom, 
              creal(cp->ctr), cimag(cp->ctr),
              creal(cache_ctr), cimag(cache_ctr));
    }

    for (idx_b = idx_b_first; idx_b >= idx_b_last; idx_b--) {
        memcpy(mbsval, 
               &(*cp->mbsval)[idx_b][(CACHE_WIDTH/2)-cache_win_width/2],
               cache_win_width*sizeof(mbsval[0]));
        mbsval += cache_win_width;
    }
}

void cache_status(int *phase, int *percent_complete, int *zoom_lvl_inprog)
{
    *phase            = cache_status_phase;
    *percent_complete = cache_status_percent_complete;
    *zoom_lvl_inprog  = cache_status_zoom_lvl_inprog;
}

bool cache_write(int file_id, complex ctr, double zoom, bool require_cache_thread_finished)
{
    int        fd, len, z;
    char       file_name[100], errstr[200];
    file_hdr_t hdr;
    
    sprintf(file_name, "mbs_%d.dat", file_id);
    errstr[0] = '\0';
    fd = -1;

    INFO("starting, file_name=%s\n", file_name);

    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    if (require_cache_thread_finished) {
        if (ctr != cache_thread_finished_for_cache_ctr) {
            sprintf(errstr, "cache thread is not finished");
            goto done;
        }
    }

    if (ctr != cache_ctr) {
        sprintf(errstr, "ctr notequal cache_ctr");
        goto done;
    }

    fd = open(file_name, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if (fd < 0) {
        sprintf(errstr, "open %s, %s", file_name, strerror(errno));
        goto done;
    }

    hdr.magic = MAGIC_MBS_FILE;
    hdr.ctr   = ctr;
    hdr.zoom  = zoom;
    len = write(fd, &hdr, sizeof(hdr));
    if (len != sizeof(file_hdr_t)) {
        sprintf(errstr, "write-hdr %s, %s", file_name, strerror(errno));
        goto done;
    }

    for (z = 0; z < MAX_ZOOM; z++) {
        cache_t cache_tmp = cache[z];
        cache_tmp.mbsval = NULL;
        len = write(fd, &cache_tmp, sizeof(cache_tmp));
        if (len != sizeof(cache_tmp)) {
            sprintf(errstr, "write-cache[%d] %s, %s", z, file_name, strerror(errno));
            goto done;
        }

        len = write(fd, cache[z].mbsval, MBSVAL_BYTES);
        if (len != MBSVAL_BYTES) {
            sprintf(errstr, "write-mbsval[%d] %s, %s", z, file_name, strerror(errno));
            goto done;
        }
    }

done:
    if (errstr[0] != '\0') {
        ERROR("%s\n", errstr);
    } else {
        INFO("success\n");
    }
    if (fd != -1) {
        close(fd);
    }
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
    return errstr[0] == '\0';
}

bool cache_read(int file_id, complex *ctr, double *zoom)
{
    int         fd, len, z, rc;
    char        file_name[100], errstr[200];
    file_hdr_t  hdr;
    short     (*mbsval)[CACHE_WIDTH][CACHE_HEIGHT] = NULL;
    struct stat statbuf;

    #define EXPECTED_FILE_SIZE (sizeof(file_hdr_t) + MAX_ZOOM * (sizeof(cache_t) + MBSVAL_BYTES))

    sprintf(file_name, "mbs_%d.dat", file_id);
    errstr[0] = '\0';
    fd = -1;

    INFO("starting, file_name=%s\n", file_name);

    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    fd = open(file_name, O_RDONLY);
    if (fd < 0) {
        sprintf(errstr, "open %s, %s", file_name, strerror(errno));
        goto done;
    }

    rc = fstat(fd, &statbuf);
    if (rc < 0) {
        sprintf(errstr, "fstat %s, %s", file_name, strerror(errno));
        goto done;
    }
    if (statbuf.st_size != EXPECTED_FILE_SIZE) {
        sprintf(errstr, "file size %ld not expected %ld\n", statbuf.st_size, EXPECTED_FILE_SIZE);
        goto done;
    }

    len = read(fd, &hdr, sizeof(hdr));
    if (len != sizeof(file_hdr_t)) {
        sprintf(errstr, "read-hdr %s, %s", file_name, strerror(errno));
        goto done;
    }

    if (hdr.magic != MAGIC_MBS_FILE) {
        sprintf(errstr, "bad hdr.magic = 0x%x\n", hdr.magic);
        goto done;
    }

    for (z = 0; z < MAX_ZOOM; z++) {
        cache_t cache_tmp = cache[z];
        len = read(fd, &cache_tmp, sizeof(cache_tmp));
        if (len != sizeof(cache_tmp)) {
            sprintf(errstr, "read-cache[%d] %s, %s", z, file_name, strerror(errno));
            goto done;
        }

        mbsval = malloc(MBSVAL_BYTES);
        len = read(fd, mbsval, MBSVAL_BYTES);
        if (len != MBSVAL_BYTES) {
            sprintf(errstr, "read-mbsval[%d] %s, %s", z, file_name, strerror(errno));
            goto done;
        }

        cache_tmp.mbsval = mbsval;
        free(cache[z].mbsval);
        cache[z] = cache_tmp;
        mbsval = NULL;
    }

    *ctr       = hdr.ctr;
    *zoom      = hdr.zoom;
    cache_ctr  = hdr.ctr;
    cache_zoom = floor(hdr.zoom);

done:
    if (errstr[0] != '\0') {
        ERROR("%s\n", errstr);
    } else {
        INFO("success\n");
    }
    if (fd != -1) {
        close(fd);
    }
    if (mbsval) {
        free(mbsval);
    }
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
    return errstr[0] == '\0';
}

// -----------------  PRIVATE - ADJUST xxxxx NAME ?  -----------------------------------

static void cache_adjust_mbsval_ctr(cache_t *cp)
{
    int old_y, new_y, delta_x, delta_y;
    short (*new_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
    short (*old_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];

    delta_x = nearbyint((creal(cp->ctr) - creal(cache_ctr)) / cp->pixel_size);
    delta_y = nearbyint((cimag(cp->ctr) - cimag(cache_ctr)) / cp->pixel_size);

    if (delta_x == 0 && delta_y == 0) {
        return;
    }

    new_mbsval = malloc(MBSVAL_BYTES);
    old_mbsval = cp->mbsval;

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

        // xxx further optimize, to not do this memset
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

    free(cp->mbsval);
    cp->mbsval             = new_mbsval;
    cp->ctr                = cache_ctr;
    cp->phase1_spiral      = cache_initial_spiral;
    cp->phase1_spiral_done = false;
    cp->phase2_spiral      = cache_initial_spiral;
    cp->phase2_spiral_done = false;
}

// -----------------  PRIVATE - CACHE THREAD  -----------------------------------------

static void *cache_thread(void *cx)
{
    #define CHECK_FOR_STOP_REQUEST \
        do { \
            if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) { \
                was_stopped = true; \
                cache_thread_request = CACHE_THREAD_REQUEST_NONE; \
                __sync_synchronize(); \
                goto restart; \
            } \
        } while (0)

    #define COMPUTE_MBSVAL(_idx_a,_idx_b,_cp) \
        do { \
            if ((*(_cp)->mbsval)[_idx_b][_idx_a] == MBSVAL_NOT_COMPUTED) { \
                complex c; \
                unsigned long start_mbs_tsc = tsc_timer(); \
                c = ((((_idx_a)-(CACHE_WIDTH/2)) * (_cp)->pixel_size) -  \
                     (((_idx_b)-(CACHE_HEIGHT/2)) * (_cp)->pixel_size) * I) +  \
                    cache_ctr; \
                (*(_cp)->mbsval)[_idx_b][_idx_a] = mandelbrot_set(c); \
                total_mbs_tsc += tsc_timer() - start_mbs_tsc; \
                mbs_call_count++; \
            } \
        } while (0)

    #define SPIRAL_OUTSIDE_WINDOW \
        (idx_a < win_min_x || idx_a > win_max_x || idx_b < win_min_y || idx_b > win_max_y)
    #define SPIRAL_COMPLETE_WINDOW \
        (cache_win_width >= cache_win_height ? idx_a < win_min_x : idx_b < win_min_y)

    #define SPIRAL_OUTSIDE_CACHE \
        (idx_a < 0 || idx_a >= CACHE_WIDTH || idx_b < 0 || idx_b >= CACHE_HEIGHT)
    #define SPIRAL_COMPLETE_CACHE \
        (CACHE_WIDTH >= CACHE_HEIGHT ? idx_a < 0 : idx_b < 0)

    cache_t      * cp;
    int            n, idx_a, idx_b, dir;
    int            win_min_x, win_max_x, win_min_y, win_max_y;
    unsigned long  total_mbs_tsc, start_us, start_tsc=0;
    bool           was_stopped;
    int            mbs_call_count;

    while (true) {
restart:
        // now in idle phase
        cache_status_phase            = 0;
        cache_status_percent_complete = -1;
        cache_status_zoom_lvl_inprog  = -1;

        // debug print the completion status, using vars
        // - start_tsc
        // - start_us
        // - was_stopped
        // - mbs_call_count
        // - total_mbs_tsc
        if (start_tsc != 0) {
            DEBUG("%s  mbs_call_count=%d  duration=%ld ms  mbs_calc=%ld %%\n",
                 !was_stopped ? "DONE" : "STOPPED",
                 mbs_call_count,
                 (microsec_timer() - start_us) / 1000,
                 total_mbs_tsc * 100 / (tsc_timer() - start_tsc));
        }

        // state is now idle;
        // wait here for a request
        while (cache_thread_request == CACHE_THREAD_REQUEST_NONE) {
            usleep(100);
        }

        // handle stop request received when we are already stopped
        CHECK_FOR_STOP_REQUEST;

        // the request must be a run request; ack the request
        if (cache_thread_request != CACHE_THREAD_REQUEST_RUN) {
            FATAL("invalid cache_thread_request %d\n", cache_thread_request);
        }
        cache_thread_request = CACHE_THREAD_REQUEST_NONE;
        __sync_synchronize();

        // xxx comment

        cache_thread_finished_for_cache_ctr = CTR_INVALID;

        start_tsc      = tsc_timer();
        start_us       = microsec_timer();
        was_stopped    = false;
        mbs_call_count = 0;
        total_mbs_tsc  = 0;

        win_min_x = CACHE_WIDTH/2  - cache_win_width/2  - 3;
        win_max_x = CACHE_WIDTH/2  + cache_win_width/2  + 3;
        win_min_y = CACHE_HEIGHT/2 - cache_win_height/2 - 3;
        win_max_y = CACHE_HEIGHT/2 + cache_win_height/2 + 3;
        dir = 1; // XXX

        // phase1: loop over all zoom levels
        DEBUG("STARTING PHASE1\n");
        cache_status_phase = 1;
        for (n = 0; n < MAX_ZOOM; n++) {
            CHECK_FOR_STOP_REQUEST;

            cache_status_percent_complete = 100 * n / MAX_ZOOM;
            cache_status_zoom_lvl_inprog  = (cache_zoom + dir*n + MAX_ZOOM) % MAX_ZOOM;
            __sync_synchronize();

            cp = &cache[cache_status_zoom_lvl_inprog];

            cache_adjust_mbsval_ctr(cp);

            if (cp->phase1_spiral_done) {
                continue;
            }

            while (true) {
                CHECK_FOR_STOP_REQUEST;

                cache_spiral_get_next(&cp->phase1_spiral, &idx_a, &idx_b);

                if (SPIRAL_OUTSIDE_WINDOW) {
                    if (cp->phase2_spiral.maxcnt == 0) {
                        cp->phase2_spiral = cp->phase1_spiral;
                    }
                    if (SPIRAL_COMPLETE_WINDOW) {
                        cp->phase1_spiral_done = true;
                        break;
                    } else {
                        continue;
                    }
                }

                COMPUTE_MBSVAL(idx_a,idx_b,cp);
            }
        }

        // XXX phase2: 
        DEBUG("STARTING PHASE2\n");
        cache_status_phase = 2;
        for (n = 0; n < MAX_ZOOM; n++) {
            CHECK_FOR_STOP_REQUEST;

            cache_status_percent_complete = 100 * n / MAX_ZOOM;
            cache_status_zoom_lvl_inprog  = (cache_zoom + dir*n + MAX_ZOOM) % MAX_ZOOM;
            __sync_synchronize();

            cp = &cache[cache_status_zoom_lvl_inprog];

            if (cp->phase2_spiral_done) {
                continue;
            }

            while (true) {
                CHECK_FOR_STOP_REQUEST;

                cache_spiral_get_next(&cp->phase2_spiral, &idx_a, &idx_b);

                if (SPIRAL_OUTSIDE_CACHE) {
                    if (SPIRAL_COMPLETE_CACHE) {
                        cp->phase2_spiral_done = true;
                        break;
                    } else {
                        continue;
                    }
                }

                COMPUTE_MBSVAL(idx_a,idx_b,cp);
            }
        }

        // xxx
        cache_thread_finished_for_cache_ctr = cache_ctr;
    }
}

static void cache_thread_issue_request(int req)
{
    // xxx comments
    __sync_synchronize();
    cache_thread_request = req;
    __sync_synchronize();

    // xxx comments
    while (cache_thread_request != CACHE_THREAD_REQUEST_NONE) {
        usleep(100);
    }
}

// -----------------  PRIVATE - SPIRAL  -----------------------------------------------

static void cache_spiral_init(spiral_t *s, int x, int y)
{
    memset(s, 0, sizeof(spiral_t));
    s->x = x;
    s->y = y;
}

// xxx check the direction
static void cache_spiral_get_next(spiral_t *s, int *x, int *y)
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

    *x = s->x;
    *y = s->y;
}
