// xxx comments throughout

#include <common.h>

//
// defines
//

#define CACHE_THREAD_REQUEST_NONE   0
#define CACHE_THREAD_REQUEST_RUN    1
#define CACHE_THREAD_REQUEST_STOP   2

#define CACHE_WIDTH                 2000
#define CACHE_HEIGHT                2000
#define MBSVAL_BYTES                (CACHE_HEIGHT*CACHE_WIDTH*2)

#define MAGIC_MBS_FILE              0x5555555500000001

#define CTR_INVALID                 (999 + 0 * I)

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
    unsigned short  (*mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
    complex           ctr;
    int               zoom;
    double            pixel_size;
    spiral_t          phase1_spiral;
    bool              phase1_spiral_done;
    spiral_t          phase2_spiral;
    bool              phase2_spiral_done;
} cache_t;

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
static complex  cache_thread_finished;

static int      cache_status_phase;
static int      cache_status_percent_complete;
static int      cache_status_zoom_lvl_inprog;

//
// prototypes
//

static void cache_adjust_mbsval_ctr(cache_t *cp);
static void *cache_thread(void *cx);
static void cache_thread_get_zoom_lvl_tbl(int *zoom_lvl_tbl);
static void cache_thread_issue_request(int req);
static void cache_spiral_init(spiral_t *s, int x, int y);
static void cache_spiral_get_next(spiral_t *s, int *x, int *y);

// -----------------  API  ------------------------------------------------------------

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

void cache_param_change(complex ctr, int zoom, int win_width, int win_height, bool force)
{
    int z;

    // if zoom, ctr and window dims remain the same then return
    if (zoom == cache_zoom && 
        ctr == cache_ctr && 
        win_width == cache_win_width && 
        win_height == cache_win_height &&
        force == false)
    {
        return;
    }

    // stop the cache_thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    // if either window dimension has increased then   XXX or just changed
    // all of the spirals need to be reset; also
    // reset the spirals when the force flag is set
    if (win_width > cache_win_width || win_height > cache_win_height || force) {
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

// xxx maybe should pass in win_width/height
void cache_get_mbsval(unsigned short *mbsval, int width, int height)
{
    int idx_b, idx_b_first, idx_b_last;
    cache_t *cp = &cache[cache_zoom];

    idx_b_first =  (CACHE_HEIGHT/2) + height / 2;
    idx_b_last  = idx_b_first - height + 1;

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
               &(*cp->mbsval)[idx_b][(CACHE_WIDTH/2)-width/2],
               width*sizeof(mbsval[0]));
        mbsval += width;
    }
}

void cache_status(int *phase, int *percent_complete, int *zoom_lvl_inprog)
{
    *phase            = cache_status_phase;
    *percent_complete = cache_status_percent_complete;
    *zoom_lvl_inprog  = cache_status_zoom_lvl_inprog;
}

// -----------------  API : FILE  -----------------------------------------------------

typedef struct {
    long         magic;
    complex      ctr;
    double       zoom;  // xxx maybe use zoom as int and also have zoom_fratction
    int          wavelen_start;
    int          wavelen_scale;
    int          reserved[10];
    unsigned int dir_pixels[200][300];
    struct xxx_s {
        cache_t cache;
        unsigned short mbsval[CACHE_HEIGHT][CACHE_WIDTH];
    } cache[0];
} file_t;

typedef struct {
    char filename[300];
    bool fi_valid;
    cache_file_info_t fi;
} xxx_t;

xxx_t *xxx[1000];
int max_xxx;

int compare(const void *arg1, const void *arg2)
{
    xxx_t *a = *(xxx_t**)arg1;
    xxx_t *b = *(xxx_t**)arg2;
    return strcmp(a->filename, b->filename);
}

int cache_file_enumerate(void)
{
    DIR *d;
    struct dirent *de;

    d = opendir(".save");
    if (d == NULL) {
        FATAL("failed opendir .save, %s\n", strerror(errno));
    }

    max_xxx = 0;

    while ((de = readdir(d)) != NULL) {
        xxx_t *x;

        char *filename = de->d_name;
        if (strncmp(filename, "mbs_", 4) != 0 && strncmp(filename, "fav_", 4) != 0) {
            continue;
        }
        INFO("GOT %s\n", filename);

        x = malloc(sizeof(xxx_t));
        memset(x,0,sizeof(xxx_t));

        sprintf(x->filename, ".save/%s", filename);
        free(xxx[max_xxx]);
        xxx[max_xxx++] = x;
    }
    qsort(xxx, max_xxx, sizeof(void*), compare);

    int i;
    for (i = 0; i < max_xxx; i++) {
        INFO("sorted - %s\n", xxx[i]->filename);
    }

    closedir(d);

    return max_xxx;
}

void cache_file_read_directory_info(int idx, cache_file_info_t **fi)
{
    bool err;
    int fd, len;
    file_t file;
    xxx_t *x;

    static cache_file_info_t fi_error = {.error=true};

    if (idx >= max_xxx) {
        *fi = &fi_error;
        return;
    }

    x = xxx[idx];

    if (!x->fi_valid) {
        err = false;

        fd = open(x->filename, O_RDONLY);
        if (fd == -1) {
            ERROR("failed open %s, %s\n", x->filename, strerror(errno));
            err = true;
        } else {
            len = read(fd, &file, sizeof(file_t));
            if (len != sizeof(file_t)) {
                ERROR("failed read %s, %s\n", x->filename, strerror(errno));
                err = true;
            }
            close(fd);
        }

        x->fi.deleted   = false;
        x->fi.error     = err;
        x->fi.cached    = false;
        x->fi.favorite  = false;
        if (!err) {
            memcpy(x->fi.pixels, file.dir_pixels, sizeof(x->fi.pixels));
        } else {
            memset(x->fi.pixels, 0, sizeof(x->fi.pixels));
        }

        x->fi_valid = true;
        INFO("x->fi_valid is true for %s\n", x->filename);
    }

    *fi = &x->fi;
}

bool cache_file_save(complex ctr, double zoom, int wavelen_start, int wavelen_scale,
                     unsigned int * dir_pixels)
{
    int rc, fd, len;
    struct stat statbuf;
    char filename[100];
    time_t t;
    struct tm *tm;

    static file_t *file;

    #define FILE_SIZE (sizeof(file_t) + sizeof(struct xxx_s))
    if (file == NULL) {
        file = malloc(FILE_SIZE);
    }

    // make the .save directory, if needed
    rc = stat(".save", &statbuf);
    INFO("stat rc %d %d\n", rc, errno);
    if (rc == 0 && (statbuf.st_mode & S_IFDIR) == 0) {
        FATAL(".save exists and is not a directory\n");
    }
    if (rc < 0 && errno == ENOENT) {
        rc = mkdir(".save", 0755);
        if (rc != 0) {
            FATAL("failed to create .save directory, %s\n", strerror(errno));
        }
        INFO("creted .save dir\n");
    }

    // construct filename from date, format: 
    // - mbs_yyyy_mm_day_hhmmss.dat OR
    // - fav_yyyy_mm_day_hhmmss.dat  (when the file is converted to a favorite
    strcpy(filename, ".save/mbs_xxx.dat");
    t = time(NULL);
    tm = localtime(&t);
    sprintf(filename, ".save/mbs_%04d_%02d_%02d_%02d%02d%02d.dat",
            tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

    // stop the cache thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    // constuct a buffer containing:
    // - file_hdr
    // - directory_pixels
    // - cache_t for the zoom level being saved
    file->magic         = MAGIC_MBS_FILE;
    file->ctr           = ctr;
    file->zoom          = zoom;
    file->wavelen_start = wavelen_start;
    file->wavelen_scale = wavelen_scale;
    memset(file->reserved, 0, sizeof(file->reserved));

    memcpy(file->dir_pixels, dir_pixels, sizeof(file->dir_pixels));

    file->cache[0].cache = cache[cache_zoom];
    file->cache[0].cache.mbsval = NULL;
    memcpy(file->cache[0].mbsval,  cache[cache_zoom].mbsval, MBSVAL_BYTES);

    // run the cache thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);

    // open, write, close
    fd = open(filename, O_CREAT|O_EXCL|O_WRONLY, 0644);
    if (fd < 0) {
        ERROR("failed to create %s, %s\n", filename, strerror(errno));
        return false;
    }
    len = write(fd, file, FILE_SIZE);
    if (len != FILE_SIZE) {
        ERROR("failed to write %s, %s\n", filename, strerror(errno));
        return false;
    }
    close(fd);

    // success
    return true;
}

#if 0
bool cache_write(char *file_name, complex ctr, double zoom, 
                 int wavelen_start, int wavelen_scale,
                 bool require_cache_thread_finished)
{
    int        fd=-1, len, z;
    char       errstr[200]="";
    file_hdr_t hdr;
    
    INFO("starting, file_name=%s\n", file_name);

    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    if (require_cache_thread_finished) {
        if (ctr != cache_thread_finished) {
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

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic         = MAGIC_MBS_FILE;
    hdr.ctr           = ctr;
    hdr.zoom          = zoom;
    hdr.wavelen_start = wavelen_start;
    hdr.wavelen_scale = wavelen_scale;
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

bool cache_read(char *file_name, complex *ctr, double *zoom, int *wavelen_start, int *wavelen_scale)
{
    int              fd=1, len, z, rc;
    char             errstr[200]="";
    file_hdr_t       hdr;
    unsigned short (*mbsval)[CACHE_WIDTH][CACHE_HEIGHT] = NULL;
    struct stat      statbuf;

    #define EXPECTED_FILE_SIZE (sizeof(file_hdr_t) + MAX_ZOOM * (sizeof(cache_t) + MBSVAL_BYTES))

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
        sprintf(errstr, "bad hdr.magic = 0x%lx\n", hdr.magic);
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

    *ctr           = hdr.ctr;
    *zoom          = hdr.zoom;
    *wavelen_start = hdr.wavelen_start;
    *wavelen_scale = hdr.wavelen_scale;

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

#endif

// -----------------  PRIVATE - ADJUST MBSVAL CENTER  ---------------------------------

static void cache_adjust_mbsval_ctr(cache_t *cp)
{
    int old_y, new_y, delta_x, delta_y;
    unsigned short (*new_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
    unsigned short (*old_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];

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
                mbs_calc_count++; \
            } else { \
                mbs_not_calc_count++; \
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
    int            n, idx_a, idx_b;
    int            win_min_x, win_max_x, win_min_y, win_max_y;
    unsigned long  total_mbs_tsc, start_us, start_tsc=0;
    bool           was_stopped;
    int            mbs_calc_count;
    int            mbs_not_calc_count;
    int            zoom_lvl_tbl[MAX_ZOOM];

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
        // - mbs_calc_count
        // - mbs_not_calc_count
        // - total_mbs_tsc
        if (start_tsc != 0) {
            DEBUG("%s  mbs_calc_count=%d,%d  duration=%ld ms  mbs_calc=%ld %%\n",
                 !was_stopped ? "DONE" : "STOPPED",
                 mbs_calc_count,
                 mbs_not_calc_count,
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

        // xxx comments for all of the following
        cache_thread_finished = CTR_INVALID;

        start_tsc          = tsc_timer();
        start_us           = microsec_timer();
        was_stopped        = false;
        mbs_calc_count     = 0;
        mbs_not_calc_count = 0;
        total_mbs_tsc      = 0;

        win_min_x = CACHE_WIDTH/2  - cache_win_width/2  - 3;
        win_max_x = CACHE_WIDTH/2  + cache_win_width/2  + 3;
        win_min_y = CACHE_HEIGHT/2 - cache_win_height/2 - 3;
        win_max_y = CACHE_HEIGHT/2 + cache_win_height/2 + 3;

        cache_thread_get_zoom_lvl_tbl(zoom_lvl_tbl);

        // phase1: loop over all zoom levels
        DEBUG("STARTING PHASE1\n");
        cache_status_phase = 1;
        for (n = 0; n < MAX_ZOOM; n++) {
            CHECK_FOR_STOP_REQUEST;

            cache_status_percent_complete = 100 * n / MAX_ZOOM;
            cache_status_zoom_lvl_inprog  = zoom_lvl_tbl[n];
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

        // phase2: loop over all zoom levels  xxx describe phase1 vs 2
        DEBUG("STARTING PHASE2\n");
        cache_status_phase = 2;
        for (n = 0; n < MAX_ZOOM; n++) {
            CHECK_FOR_STOP_REQUEST;

            cache_status_percent_complete = 100 * n / MAX_ZOOM;  // xxx maybe get rid of percent_comp
            cache_status_zoom_lvl_inprog  = zoom_lvl_tbl[n];
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
        cache_thread_finished = cache_ctr;
    }
}

static void cache_thread_get_zoom_lvl_tbl(int *zoom_lvl_tbl)
{
    int         n, idx;

    static bool dir_is_up      = true;
    static int  last_cache_zoom = 0;

    if (cache_zoom == 0) {
        dir_is_up = true;
    } else if (cache_zoom == LAST_ZOOM) {
        dir_is_up = false;
    } else if (cache_zoom > last_cache_zoom) {
        dir_is_up = true;
    } else if (cache_zoom < last_cache_zoom) {
        dir_is_up = false;
    } else {
        // no change to dir
    }

    n = 0;
    if (dir_is_up) {
        for (idx = cache_zoom; idx <= LAST_ZOOM; idx++) zoom_lvl_tbl[n++] = idx;
        for (idx = cache_zoom-1; idx >= 0; idx--) zoom_lvl_tbl[n++] = idx;
    } else {
        for (idx = cache_zoom; idx >= 0; idx--) zoom_lvl_tbl[n++] = idx;
        for (idx = cache_zoom+1; idx <= LAST_ZOOM; idx++) zoom_lvl_tbl[n++] = idx;
    }
    if (n != MAX_ZOOM) FATAL("n = %d\n",n);

    char str[200], *p=str;
    for (n = 0; n < MAX_ZOOM; n++) {
        p += sprintf(p, "%d ", zoom_lvl_tbl[n]);
    }
    INFO("dir=%s zoom=%d last=%d - %s\n", 
         (dir_is_up ? "UP" : "DOWN"),
         cache_zoom, last_cache_zoom, str);

    last_cache_zoom = cache_zoom;
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
