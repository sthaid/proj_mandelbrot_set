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

#define MAGIC_MBS_FILE              0x5555555500000002

#define CTR_INVALID                 (999 + 0 * I)

#define MBS_CACHE_DIR               ".mbs_cache"

#define PATHNAME(fn) \
    ({static char s[500]; sprintf(s, MBS_CACHE_DIR "/" "%s", fn), s;})

#define OPEN(fn,fd,mode) \
    do { \
        fd = open(PATHNAME(fn), mode, 0644); \
        if (fd < 0) { \
            FATAL("failed to open %s, %s\n", fn, strerror(errno)); \
        } \
    } while (0)
#define READ(fn,fd,addr,len) \
    do { \
        int rc; \
        if ((rc = read(fd, addr, len)) != (len)) {  \
            FATAL("failed to read %s, %s, req=%d act=%d\n", fn, strerror(errno), (int)len, rc); \
        } \
    } while (0)
#define WRITE(fn,fd,addr,len) \
    do { \
        int rc; \
        if ((rc = write(fd, addr, len)) != (len)) {  \
            FATAL("failed to write %s, %s, req=%d act=%d\n", fn, strerror(errno), (int)len, rc); \
        } \
    } while (0)
#define CLOSE(fn,fd) \
    do { \
        if (close(fd) != 0) { \
            FATAL("failed to close %s, %s\n", fn, strerror(errno)); \
        } \
    } while (0)

#define IDX_TO_FI(idx) \
    ({  if (idx >= max_file_info) \
            FATAL("idx=%d too large, max_file_info=%d\n", idx, max_file_info); \
        if (file_info[idx] == NULL) \
            FATAL("file_info[%d] is null, max_file_info=%d\n", idx, max_file_info); \
        file_info[idx]; })

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
    complex          ctr;
    int              zoom;
    double           pixel_size;
    spiral_t         phase1_spiral;
    bool             phase1_spiral_done;
    spiral_t         phase2_spiral;
    bool             phase2_spiral_done;
    unsigned short (*mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
} cache_t;

typedef struct {
    cache_t cache;
    unsigned short mbsval[CACHE_HEIGHT][CACHE_WIDTH];
} file_format_cache_t;

typedef struct {
    cache_file_info_t   fi;
    file_format_cache_t cache[0];
} file_format_t;

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
static bool     cache_thread_first_phase1_zoom_lvl_finished;
static bool     cache_thread_all_finished;

static int      cache_status_phase_inprog;
static int      cache_status_zoom_lvl_inprog;

//
// prototypes
//

static void cache_file_init(void);

static void cache_adjust_mbsval_ctr(cache_t *cp);

static void *cache_thread(void *cx);
static void cache_thread_get_zoom_lvl_tbl(int *zoom_lvl_tbl);
static void cache_thread_issue_request(int req);
static void cache_spiral_init(spiral_t *s, int x, int y);
static void cache_spiral_get_next(spiral_t *s, int *x, int *y);

// -----------------  INITIALIZATION  -------------------------------------------------

void cache_init(double pixel_size_at_zoom0)
{
    pthread_t id;
    int       zoom;

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

    cache_file_init();

    pthread_create(&id, NULL, cache_thread, NULL);
}

// -----------------  PARAMETER CHANGE  -----------------------------------------------

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

// -----------------  GET MBS VALUES  -------------------------------------------------

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

// -----------------  STATUS  ---------------------------------------------------------

void cache_status(int *phase_inprog, int *zoom_lvl_inprog)
{
    *phase_inprog     = cache_status_phase_inprog;
    *zoom_lvl_inprog  = cache_status_zoom_lvl_inprog;
}

// -----------------  FILE SUPPORT  ---------------------------------------------------

static int last_file_num;

static int compare(const void *arg1, const void *arg2)
{
    return *(int*)arg1 > *(int*)arg2;
}

static void cache_file_init(void)
{
    struct stat    statbuf;
    int            rc, idx, file_num, max_file, file_num_array[1000];
    DIR           *d;
    struct dirent *de;

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

    // get sorted list of file_num contained in the MBS_CACHE_DIR;
    // the file_name format is 'mbs_NNNN.dat', where NNNN is the file_num
    d = opendir(MBS_CACHE_DIR);
    if (d == NULL) {
        FATAL("failed opendir %s, %s\n", MBS_CACHE_DIR, strerror(errno));
    }
    max_file = 0;
    while ((de = readdir(d)) != NULL) {
        if (sscanf(de->d_name, "mbs_%d_.dat", &file_num) != 1) {
            continue;
        }
        file_num_array[max_file++] = file_num;
    }
    closedir(d);
    qsort(file_num_array, max_file, sizeof(int), compare);

    // make available max_file_info and last_file_num
    last_file_num = (max_file > 0 ? file_num_array[max_file-1] : 0);
    max_file_info = max_file;

    // read each file's header, which is a cache_file_info_t, and
    // store it in the global file_info array
    for (idx = 0; idx < max_file_info; idx++) {
        char fn[100];
        cache_file_info_t *fi;
        int fd, len;

        // allocate memory for, and read the cache_file_info
        sprintf(fn, "mbs_%04d.dat", file_num_array[idx]);
        fi = calloc(1,sizeof(cache_file_info_t));
        fd = open(PATHNAME(fn), O_RDONLY);
        if (fd < 0) {
            FATAL("open %s, %s\n", fn, strerror(errno));
        }
        len = read(fd, fi, sizeof(cache_file_info_t));
        if (len != sizeof(cache_file_info_t)) {
            FATAL("read %s, %s, req=%zd act=%d\n", fn, strerror(errno), sizeof(cache_file_info_t), len);
        }
        close(fd);
        file_info[idx] = fi;

        // sanity checks on the cache_file_info just read
        if (fi->magic != MAGIC_MBS_FILE) {
            FATAL("file %s invalid magic 0x%lx\n", fn, fi->magic);
        }
        if (strcmp(fi->file_name, fn) != 0) {
            FATAL("file %s invalid file_name %s\n", fn, fi->file_name);
        }
        if (fi->deleted || fi->file_type < 0 || fi->file_type > 2) {
            FATAL("file %s invald deleted=%d type=%d\n",
                  fn, fi->deleted, fi->file_type);
        }
    }

    // debug print file_info
    INFO("max_file_info=%d last_file_num=%d\n", max_file_info, last_file_num);
    for (idx = 0; idx < max_file_info; idx++) {
        cache_file_info_t *fi = file_info[idx];
        INFO("idx=%d name=%s type=%d\n", idx, fi->file_name, fi->file_type);
    }
}

int cache_file_create(complex ctr, double zoom, int wavelen_start, int wavelen_scale,
                      unsigned int *dir_pixels)
{
    int                fd, idx;
    char               file_name[100];
    file_format_t      file;
    cache_file_info_t *fi = &file.fi;

    // create a new file_name
    sprintf(file_name, "mbs_%04d.dat", ++last_file_num);

    // initialize file header buffer
    memset(fi, 0, sizeof(cache_file_info_t));
    fi->magic         = MAGIC_MBS_FILE;
    strcpy(fi->file_name, file_name);
    fi->file_type     = 0;
    fi->ctr           = ctr;
    fi->zoom          = zoom;
    fi->wavelen_start = wavelen_start;
    fi->wavelen_scale = wavelen_scale;
    fi->deleted       = false;
    memcpy(fi->dir_pixels, dir_pixels, sizeof(fi->dir_pixels));

    // create the file
    OPEN(file_name, fd, O_CREAT|O_EXCL|O_WRONLY);
    WRITE(file_name, fd, fi, sizeof(cache_file_info_t));
    CLOSE(file_name, fd);

    // save the new fi at the end of the file_info array
    idx = max_file_info;
    if (file_info[idx] != NULL) {
        FATAL("file_info[%d] not NULL\n", idx);
    }
    file_info[idx] = calloc(1,sizeof(cache_file_info_t));
    *file_info[idx] = *fi;
    max_file_info++;

    // return the idx of the new file_info entry
    return idx;
}

void cache_file_delete(int idx)
{
    cache_file_info_t *fi = IDX_TO_FI(idx);

    // if already deleted just return
    if (fi->deleted) {
        return;
    }

    // unlink the file, and set the deleted flag
    unlink(PATHNAME(fi->file_name));
    fi->deleted = true;
}

void cache_file_read(int idx)
{
    cache_file_info_t *fi = IDX_TO_FI(idx);
    cache_file_info_t  fi_tmp;
    int                fd, i, n;

    // file_type 0 does not contain any cached mbs values
    if (fi->file_type == 0) {
        return;
    }

    // open and read the file hdr
    OPEN(fi->file_name, fd, O_RDONLY);
    READ(fi->file_name, fd, &fi_tmp, sizeof(fi_tmp));

    // sanity check that the file hdr just read is correct
    if (memcmp(&fi_tmp, fi, sizeof(cache_file_info_t)) != 0) {
        FATAL("file hdr error in %s, fn=%s\n", fi->file_name, fi_tmp.file_name);
    }

    // set the expected number of zoom levels saved in the file
    n = (fi->file_type == 1 ? 1 : 
         fi->file_type == 2 ? MAX_ZOOM
                            : ({FATAL("file_type=%d\n",fi->file_type);0;}));
    
    // read cached mbsvals from the file , and store them in the cache[] array
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);
    for (i = 0; i < n; i++) {
        static file_format_cache_t ffce;
        int z;

        READ(fi->file_name, fd, &ffce, sizeof(file_format_cache_t));

        z = ffce.cache.zoom;
        if (z < 0 || z >= MAX_ZOOM) {
            FATAL("ffce.cache.zoom=%d out of range\n", z);
        }

        ffce.cache.mbsval = cache[z].mbsval;
        cache[z] = ffce.cache;
        memcpy(cache[z].mbsval, ffce.mbsval, MBSVAL_BYTES);
    }
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);

    // close file
    CLOSE(fi->file_name, fd);
}

void cache_file_update(int idx, int file_type)
{
    cache_file_info_t *fi = IDX_TO_FI(idx);
    int z, fd;

    INFO("idx=%d fn=%s file_type=%d->%d\n", idx, fi->file_name, fi->file_type, file_type);

    // when called for file_type 1 or 2 the cache_ctr and cache_zoom
    // are supposed to be equal to the file_info
    if (file_type == 1 || file_type == 2) {
        if (fi->ctr != cache_ctr || (int)fi->zoom != cache_zoom) {
            FATAL("cache_ctr/zoom don't match file_info\n");
        }
    }

    OPEN(fi->file_name, fd, O_TRUNC|O_WRONLY);

    fi->file_type = file_type;
    WRITE(fi->file_name, fd, fi, sizeof(cache_file_info_t));

    for (z = 0; z < MAX_ZOOM; z++) {
        if ((file_type == 1 && z == (int)fi->zoom) || (file_type == 2)) {
            INFO("- writing zoom lvl %d\n", z);
            cache_t cache_tmp = cache[z];
            cache_tmp.mbsval = NULL;
            WRITE(fi->file_name, fd, &cache_tmp, sizeof(cache_tmp));
            WRITE(fi->file_name, fd, cache[z].mbsval, MBSVAL_BYTES);
        }
    }

    CLOSE(fi->file_name, fd);
}

void cache_file_garbage_collect(void)
{
    int idx = 0;

    while (idx < max_file_info) {
        if (file_info[idx]->deleted) {
            INFO("idx %d  max_file_info %d  %p %p %p\n",
                idx, max_file_info, 
                file_info[0], file_info[1], file_info[2]);
            INFO("  freeing %p\n", file_info[idx]);
            free(file_info[idx]);
            memmove(&file_info[idx], &file_info[idx+1], (max_file_info-1)*sizeof(void*));
            max_file_info--;
            file_info[max_file_info] = NULL;
        } else {
            idx++;
        }
    }
}

// -----------------  ADJUST MBSVAL CENTER  -------------------------------------------

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

// -----------------  CACHE THREAD  ---------------------------------------------------

bool cache_thread_first_phase1_zoom_lvl_is_finished(void)
{
    return cache_thread_first_phase1_zoom_lvl_finished;
}

bool cache_thread_all_is_finished(void)
{
    return cache_thread_all_finished;
}

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
        cache_status_phase_inprog     = 0;
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
        cache_status_phase_inprog = 1;
        for (n = 0; n < MAX_ZOOM; n++) {
            CHECK_FOR_STOP_REQUEST;

            cache_status_zoom_lvl_inprog  = zoom_lvl_tbl[n];
            DEBUG("- inprog : %d - %d\n", cache_status_phase_inprog, cache_status_zoom_lvl_inprog);
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

                // XXX check the idx here
                COMPUTE_MBSVAL(idx_a,idx_b,cp);
            }

            if (n == 0) {
                DEBUG("FINISHD phase1 lvl0\n");
                cache_thread_first_phase1_zoom_lvl_finished = true;
            }
        }

        // phase2: loop over all zoom levels  xxx describe phase1 vs 2
        DEBUG("STARTING PHASE2\n");
        cache_status_phase_inprog = 2;
        for (n = 0; n < MAX_ZOOM; n++) {
            CHECK_FOR_STOP_REQUEST;

            cache_status_zoom_lvl_inprog  = zoom_lvl_tbl[n];
            DEBUG("- inprog : %d - %d\n", cache_status_phase_inprog, cache_status_zoom_lvl_inprog);
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

        // caching of all zoom levels has completed
        DEBUG("ALL FINISHED \n");
        cache_thread_all_finished = true;
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

#if 0
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
    // xxx
    cache_thread_first_phase1_zoom_lvl_finished = false;
    cache_thread_all_finished = false;

    // xxx comments
    __sync_synchronize();
    cache_thread_request = req;
    __sync_synchronize();

    // xxx comments
    while (cache_thread_request != CACHE_THREAD_REQUEST_NONE) {
        usleep(100);
    }
}

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
