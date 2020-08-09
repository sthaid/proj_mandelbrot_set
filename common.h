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
#include <assert.h>

#include <util_misc.h>

//
// defines
//

#define MAX_ZOOM             47

#define MBSVAL_IN_SET        1000
#define MBSVAL_NOT_COMPUTED  -1

//
// variables
//

//
// prototypes
//

int mandelbrot_set(complex c);

void cache_init(double pixel_size_at_zoom0);
void cache_param_change(complex ctr, int zoom, int win_width, int win_height);
void cache_get_mbsval(short *mbsval);
void cache_status(int *phase, int *percent_complete, int *zoom_lvl_inprog);

#endif
