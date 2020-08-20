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

#define MBS_CACHE_DIR               ".mbs_cache"

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
    int zoom, rc;
    struct stat statbuf;

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

    // make the MBS_CACHE_DIR directory, if needed
    rc = stat(MBS_CACHE_DIR, &statbuf);
    if (rc == 0 && (statbuf.st_mode & S_IFDIR) == 0) {
        FATAL("%s exists and is not a directory\n", MBS_CACHE_DIR);
    }
    if (rc < 0 && errno == ENOENT) {
        rc = mkdir(MBS_CACHE_DIR, 0755);
        if (rc != 0) {
            FATAL("failed to create %s directory, %s\n", MBS_CACHE_DIR, strerror(errno));
        }
    }

    // xxx comment - get last_file_num
    cache_file_enumerate();

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

    // if either window dimension has increased then   xxx or just changed
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

#define PATHNAME(fn) \
    ({static char s[500]; sprintf(s, MBS_CACHE_DIR "/" "%s", fn), s;})

typedef struct {
    long         magic;
    complex      ctr;
    double       zoom;  // xxx maybe use zoom as int and also have zoom_fratction
    int          wavelen_start;
    int          wavelen_scale;
    int          reserved[10];
    unsigned int dir_pixels[200][300];
    struct file_format_cache_s {
        cache_t cache;
        unsigned short mbsval[CACHE_HEIGHT][CACHE_WIDTH];
    } cache[0];
} file_format_t;

static cache_file_info_t *file_info[1000];
static int                max_file_info;
static int                last_file_num;

static int compare(const void *arg1, const void *arg2)
{
    cache_file_info_t *a = *(cache_file_info_t**)arg1;
    cache_file_info_t *b = *(cache_file_info_t**)arg2;
    return strcmp(a->file_name, b->file_name);
}

int cache_file_enumerate(void)
{
    DIR               *d;
    struct dirent     *de;
    cache_file_info_t *fi;
    char              *file_name;
    int                file_num;
    int                i;

    d = opendir(MBS_CACHE_DIR);
    if (d == NULL) {
        FATAL("failed opendir %s, %s\n", MBS_CACHE_DIR, strerror(errno));
    }

    max_file_info = 0;
    last_file_num = 0;

    while ((de = readdir(d)) != NULL) {
        file_name = de->d_name;

        if (sscanf(file_name, "mbs_%d_.dat", &file_num) != 1) {
            continue;
        }

        INFO("GOT %s %d\n", file_name, file_num);
        if (file_num > last_file_num) {
            last_file_num = file_num;
        }

        fi = calloc(1,sizeof(cache_file_info_t));
        strcpy(fi->file_name, file_name);

        free(file_info[max_file_info]);
        file_info[max_file_info] = fi;

        max_file_info++;
    }

    qsort(file_info, max_file_info, sizeof(void*), compare);

    for (i = 0; i < max_file_info; i++) {
        INFO("sorted - %s\n", file_info[i]->file_name);
    }

    INFO("max_file_info=%d last_file_num=%d\n", max_file_info, last_file_num);

    closedir(d);

    return max_file_info;
}

cache_file_info_t * cache_file_get_dir_info(int idx)
{
    cache_file_info_t *fi;
    file_format_t      file;
    int                fd, len, rc;
    struct stat        statbuf;

    #define EXPECTED_FILE_SIZE_SINGLE_CACHE_LEVEL         (offsetof(file_format_t, cache[1]))
    #define EXPECTED_FILE_SIZE_SINGLE_ALL_CACHE_LEVELS    (offsetof(file_format_t, cache[MAX_ZOOM]))

    // verify idx is in range and file_info[idx] is not null
    if (idx >= max_file_info) {
        FATAL("idx=%d too large, max_file_info=%d\n", idx, max_file_info);
    }
    fi = file_info[idx];
    if (fi == NULL) {
        FATAL("file_info[%d] is null, max_file_info=%d\n", idx, max_file_info);
    }

    // the file_name may already have been set to 'deleted' or 'error'
    if (strncmp(fi->file_name, "mbs_", 4) != 0) {
        return fi;
    }

    // if file_info has not been initialized for this file then do so
    if (fi->initialized == false) {
        // open the file
        fd = open(PATHNAME(fi->file_name), O_RDONLY);
        if (fd == -1) {
            ERROR("failed open %s, %s\n", fi->file_name, strerror(errno));
            goto error;
        }

        // verify file size is as expected
        rc = fstat(fd, &statbuf);
        if (rc < 0) {
            ERROR("failed stat %s, %s\n", fi->file_name, strerror(errno));
            goto error;
        }
        if (statbuf.st_size != EXPECTED_FILE_SIZE_SINGLE_CACHE_LEVEL &&
            statbuf.st_size != EXPECTED_FILE_SIZE_SINGLE_ALL_CACHE_LEVELS)
        {
            ERROR("invalid file_size %ld\n", statbuf.st_size);
            goto error;
        }

        // read the beginning of the file, this does not include 
        // reading any of the cahced mbs values
        len = read(fd, &file, sizeof(file_format_t));
        if (len != sizeof(file_format_t)) {
            ERROR("failed read %s, %s\n", fi->file_name, strerror(errno));
            goto error;
        }

        // verify file magic 
        if (file.magic != MAGIC_MBS_FILE) {
            ERROR("bad magic 0x%lx expected 0x%lx\n", file.magic, MAGIC_MBS_FILE);
            goto error;
        }

        // close the fd
        close(fd);
        fd = -1;

        // initiailzie file_info fields
        fi->initialized = true;
        fi->entire_cache = (statbuf.st_size == EXPECTED_FILE_SIZE_SINGLE_ALL_CACHE_LEVELS);
        memcpy(fi->dir_pixels, file.dir_pixels, sizeof(fi->dir_pixels));
    }

    // success
    return fi;

error:
    // error occurred
    // - close fd
    // - delete the file
    // - set file_info fields to indicate an error
    // - return ptr to file_info
    if (fd != -1) {
        close(fd);
        fd = -1;
    }

    unlink(PATHNAME(fi->file_name));

    strcpy(fi->file_name, "error");
    fi->initialized = true;
    fi->entire_cache = false;
    memset(fi->dir_pixels, 0, sizeof(fi->dir_pixels));

    return fi;
}

bool cache_file_create(char *file_name_arg, bool entire_cache, 
        complex ctr, double zoom, int wavelen_start, int wavelen_scale,
        unsigned int *dir_pixels)
{
    int                  fd, z;
    char                 file_name[100];
    static file_format_t file;

    #define WRITE(addr,len) \
        do { \
            if (write(fd, addr, len) != len) {  \
                ERROR("failed to write %s, %s\n", file_name, strerror(errno)); \
                close(fd); \
                cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN); \
                return false; \
            } \
        } while (0)

    // xxx sanity check zoom arg and cache_zoom, there could be a corner case where they differ and ctr

    // if file_name is supplied by caller then
    //   use the caller supplied file_name, because the existing file is being overwritten
    // else
    //   create a new file_name
    // endif
    if (file_name_arg) {
        strcpy(file_name, file_name_arg);
    } else {
        sprintf(file_name, "mbs_%04d.dat", ++last_file_num);
    }

    // stop the cache thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    // if entire_cache is requested confirm cache_thread has completed, if not return an error
    if (entire_cache && cache_thread_finished != ctr) {
        // AAA print xxx
        // AAA restart the thread xxx
        return false;
    }

    // initialize file header
    file.magic         = MAGIC_MBS_FILE;
    file.ctr           = ctr;
    file.zoom          = zoom;
    file.wavelen_start = wavelen_start;
    file.wavelen_scale = wavelen_scale;
    memset(file.reserved, 0, sizeof(file.reserved));
    memcpy(file.dir_pixels, dir_pixels, sizeof(file.dir_pixels));

    // open the file for writing; if the file doesn't exist it will
    // be created, if it already exists then it will be truncated
    fd = open(PATHNAME(file_name), O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if (fd < 0) {
        ERROR("failed to open %s, %s\n", file_name, strerror(errno));
        return false;
    }

    // write the hdr
    WRITE(&file, sizeof(file_format_t));

    // write the file_format_cache_s for the current zoom level
    WRITE(&cache[cache_zoom], sizeof(cache_t));
    WRITE(cache[cache_zoom].mbsval, MBSVAL_BYTES);

    // if writing entire_cache then perform the additional writes
    if (entire_cache) {
        for (z = 0; z < MAX_ZOOM; z++) {
            if (z == cache_zoom) {
                continue;
            }
            WRITE(&cache[z], sizeof(cache_t));
            WRITE(cache[z].mbsval, MBSVAL_BYTES);
        }
    }

    // close fd
    close(fd);

    // run the cache thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);

    // success
    return true;
}

bool cache_file_read(int idx, complex *ctr, double *zoom, int *wavelen_start, int *wavelen_scale)
{
    int                 fd = -1, len, z;
    file_format_t       file;
    bool                cache_thread_stopped = false;
    cache_file_info_t * fi;

    // verify idx is in range and file_info[idx] is not null
    if (idx >= max_file_info) {
        FATAL("idx=%d too large, max_file_info=%d\n", idx, max_file_info);
    }
    fi = file_info[idx];
    if (fi == NULL) {
        FATAL("file_info[%d] is null, max_file_info=%d\n", idx, max_file_info);
    }

    // open file
    fd = open(PATHNAME(fi->file_name), O_RDONLY);
    if (fd < 0) {
        ERROR("open %s, %s", fi->file_name, strerror(errno));
        goto error;
    }

    // read file_format_t, this does not read any cache levels
    len = read(fd, &file, sizeof(file));
    if (len != sizeof(file)) {
        ERROR("read %s, %s", fi->file_name, strerror(errno));
        goto error;
    }

    // verify magic
    if (file.magic != MAGIC_MBS_FILE) {
        ERROR("bad magic 0x%lx expected 0x%lx\n", file.magic, MAGIC_MBS_FILE);
        goto error;
    }

    // stop cache thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);
    cache_thread_stopped = true;

    // loop reading cache levels
    while (true) {
        static struct file_format_cache_s fce;

        len = read(fd, &fce, sizeof(struct file_format_cache_s));
        if (len == 0) {
            INFO("done reading cache levels\n");
            break;
        }

        if (len != sizeof(struct file_format_cache_s)) {
            ERROR("read %s, %s", fi->file_name, strerror(errno));
            goto error;
        }

        z = fce.cache.zoom;
        if (z < 0 || z >= MAX_ZOOM) {
            ERROR("fce.cache.zoom=%d out of range\n", z);
            goto error;
        }

        fce.cache.mbsval = cache[z].mbsval;
        cache[z] = fce.cache;
        memcpy(cache[z].mbsval, fce.mbsval, MBSVAL_BYTES);

        INFO("got cache level %d\n", z);
    }

    // close file
    close(fd);
    fd = -1;

    // xxx comment
    cache_param_change(file.ctr, file.zoom, cache_win_width, cache_win_height, true);

    // xxx return stuff
    *ctr           = file.ctr;
    *zoom          = file.zoom;
    *wavelen_start = file.wavelen_start;
    *wavelen_scale = file.wavelen_scale;

    // return success
    return true;

error:
    if (cache_thread_stopped) {
        cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
    }
    if (fd != -1) {
        close(fd);
    }
    return false;
}

void cache_file_delete(int idx)
{
    cache_file_info_t *fi = file_info[idx];

    // verify idx is in range and file_info[idx] is not null
    if (idx >= max_file_info) {
        FATAL("idx=%d too large, max_file_info=%d\n", idx, max_file_info);
    }
    fi = file_info[idx];
    if (fi == NULL) {
        FATAL("file_info[%d] is null, max_file_info=%d\n", idx, max_file_info);
    }

    // confirm file_name, because it could have been set to 'error' or 'deleted'
    if (strncmp(fi->file_name, "mbs_", 4) != 0) {
        return;
    }

    INFO("deleting %s\n", fi->file_name);
    unlink(PATHNAME(fi->file_name));

    strcpy(fi->file_name, "deleted");
    fi->initialized = true;
    fi->entire_cache = false;
    memset(fi->dir_pixels, 0, sizeof(fi->dir_pixels));
}

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

#if 0 // xxx
    char str[200], *p=str;
    for (n = 0; n < MAX_ZOOM; n++) {
        p += sprintf(p, "%d ", zoom_lvl_tbl[n]);
    }
    INFO("dir=%s zoom=%d last=%d - %s\n", 
         (dir_is_up ? "UP" : "DOWN"),
         cache_zoom, last_cache_zoom, str);
#endif

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
