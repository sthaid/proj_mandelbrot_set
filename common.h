#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <complex.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define DEBUG_PRINT_ENABLED (debug_enabled)
#include <util_misc.h>

//
// defines
//

#define MAX_ZOOM             47
#define LAST_ZOOM            (MAX_ZOOM-1)

#define MBSVAL_IN_SET        1000
#define MBSVAL_NOT_COMPUTED  65535

//
// typedefs
//

typedef struct {
    unsigned long magic;
    char          file_name[300];
    int           file_type;             // 0,1,2  xxx explain
    complex       ctr;
    double        zoom;
    int           wavelen_start;
    int           wavelen_scale;
    bool          deleted;
    int           reserved[10];
    unsigned int  dir_pixels[200][300];
} cache_file_info_t;

//
// variables
//

bool                debug_enabled;

cache_file_info_t * file_info[1000];
int                 max_file_info;

//
// prototypes
//

int mandelbrot_set(complex c);

void cache_init(double pixel_size_at_zoom0);
void cache_param_change(complex ctr, int zoom, int win_width, int win_height, bool force);
void cache_get_mbsval(unsigned short *mbsval, int width, int height);
void cache_status(int *phase, int *percent_complete, int *zoom_lvl_inprog);

int cache_file_create(complex ctr, double zoom, int wavelen_start, int wavelen_scale,
                      unsigned int *dir_pixels);
void cache_file_update(int idx, int file_type);
void cache_file_delete(int idx);
void cache_file_read(int idx);
void cache_file_garbage_collect(void);

#endif
