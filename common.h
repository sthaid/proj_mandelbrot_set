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

// defines

#define MAX_ZOOM             47

#define MBSVAL_IN_SET        1000
#define MBSVAL_NOT_COMPUTED  -1

// variables

int    win_width;
int    win_height;
double pixel_size_at_zoom0;  // xxx ? tbd

// prototypes

int mandelbrot_set(complex c);

void cache_init(void);
void cache_param_change(complex ctr, int zoom, int win_width, int win_height);
void cache_get_mbsval(short *mbsval);
char *cache_status_str(void);

#endif
