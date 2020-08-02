// gcc -Wall -O2 -o spiral -lncurses spiral.c

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curses.h>

typedef struct {
    int x;
    int y;
    int dir;
    int cnt;
    int maxcnt;
} spiral_t;

void curses_init(void);
void curses_exit(void);
void curses_runtime(void (*update_display)(int maxy, int maxx), int (*input_handler)(int input_char));

int input_handler(int input_char);
static void update_display(int maxy, int maxx);

void get_next_spiral_loc(spiral_t *s);

// -----------------  MAIN  --------------------------------------------------

int main()
{
    curses_init();
    curses_runtime(update_display, input_handler);
    curses_exit();
}

// -----------------  CURSES WRAPPER  ----------------------------------------

static WINDOW * window;

void curses_init(void)
{
    window = initscr();

    start_color();
    use_default_colors();

    cbreak();
    noecho();
    nodelay(window,TRUE);
    keypad(window,TRUE);
}

void curses_exit(void)
{
    endwin();
}

void curses_runtime(void (*update_display)(int maxy, int maxx), int (*input_handler)(int input_char))
{
    int input_char, maxy, maxx;
    int maxy_last=0, maxx_last=0;

    while (true) {
        // erase display
        erase();

        // get window size, and print whenever it changes
        getmaxyx(window, maxy, maxx);
        if (maxy != maxy_last || maxx != maxx_last) {
            maxy_last = maxy;
            maxx_last = maxx;
        }

        // update the display
        update_display(maxy, maxx);

        // put the cursor back to the origin, and
        // refresh display
        move(0,0);
        refresh();

        // process character inputs
        input_char = getch();
        if (input_char == KEY_RESIZE) {
            // immedeate redraw display
        } else if (input_char != ERR) {
            if (input_handler(input_char) != 0) {
                return;
            }
        } else {
            usleep(100000);
        }
    }
}

// -----------------  CURSES HANDLERS  --------------------------------------------------

int input_handler(int input_char)
{
    return 0;
}

static void update_display(int maxy, int maxx)
{
    static spiral_t s;
    static int cnt=1;
    int i;

    memset(&s, 0, sizeof(s));
    for (i = 0; i < cnt; i++) {
        get_next_spiral_loc(&s);
        mvprintw(maxy/2 + s.x, maxx/2 + s.y, i < cnt-1 ? "-" : "X");
    }
    cnt++;
}

// --------------------------------------------------------------------------------------

void get_next_spiral_loc(spiral_t *s)
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
        default:         printf("ERROR: dir=%d\n", s->dir);  exit(1); break;
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

