#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <complex.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEBUG_PRINT_ENABLED (debug_enabled)
#include <util_misc.h>

//
// defines
//

#define MAX_ZOOM             47

#define MBSVAL_IN_SET        1000
#define MBSVAL_NOT_COMPUTED  65535

//
// variables
//

bool debug_enabled;

//
// prototypes
//

int mandelbrot_set(complex c);

void cache_init(double pixel_size_at_zoom0);
void cache_param_change(complex ctr, int zoom, int win_width, int win_height, bool force);
void cache_get_mbsval(unsigned short *mbsval);
void cache_status(int *phase, int *percent_complete, int *zoom_lvl_inprog);
bool cache_write(int file_id, complex ctr, double zoom, bool require_cache_thread_finished);
bool cache_read(int file_id, complex *ctr, double *zoom);

#endif
