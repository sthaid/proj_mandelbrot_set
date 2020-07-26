#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <assert.h>

#include <util_sdl.h>
#include <util_misc.h>

#define DEFAULT_WIN_WIDTH 800
#define DEFAULT_WIN_HEIGHT 800

int win_width;
int win_height;

int main(int argc, char **argv)
{ 
    win_width  = DEFAULT_WIN_WIDTH;
    win_height = DEFAULT_WIN_HEIGHT;
    if (sdl_init(&win_width, &win_height, true, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }

    sleep(5);

    return 0;
}
