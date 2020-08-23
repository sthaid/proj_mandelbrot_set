// xxx try to use pane->w instead of win_width
// xxx add support for fully cache the selected items
// xxx use defines for autozoom
// xxx use defines for the 300x200 dir image size

// XXX use zoom and zoom_fraction


#include <common.h>

#include <util_sdl.h>

//
// defines
//

#define DEFAULT_WIN_WIDTH         1500
#define DEFAULT_WIN_HEIGHT        1000 

#define INITIAL_CTR               (-0.75 + 0.0*I)

#define ZOOM_STEP                 .1   // must be a submultiple of 1

#define WAVELEN_FIRST             400
#define WAVELEN_LAST              700
#define WAVELEN_START_DEFAULT     400
#define WAVELEN_SCALE_DEFAULT     2

#define DISPLAY_SELECT_MBS        1
#define DISPLAY_SELECT_HELP       2
#define DISPLAY_SELECT_COLOR_LUT  3
#define DISPLAY_SELECT_DIRECTORY  4

//
// typedefs
//

//
// variables
//

static int     win_width   = DEFAULT_WIN_WIDTH;
static int     win_height  = DEFAULT_WIN_HEIGHT;
static double  pixel_size_at_zoom0;

//
// prototypes
//

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static void set_alert(int color, char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void display_alert(rect_t *pane);

static void init_hndlr_mbs(void);
static void render_hndlr_mbs(pane_cx_t *pane_cx);
static int event_hndlr_mbs(pane_cx_t *pane_cx, sdl_event_t *event);
static void zoom_step(bool dir_is_incr);
static void init_color_lut(int wavelen_start, int wavelen_scale, unsigned int *color_lut);
static void display_info_proc(rect_t *pane, unsigned long update_intvl_ms);
static void save_file(rect_t *pane);

static void render_hndlr_help(pane_cx_t *pane_cx);
static int event_hndlr_help(pane_cx_t *pane_cx, sdl_event_t *event);

static void render_hndlr_color_lut(pane_cx_t *pane_cx);
static int event_hndlr_color_lut(pane_cx_t *pane_cx, sdl_event_t *event);

static void render_hndlr_directory(pane_cx_t *pane_cx);
static int event_hndlr_directory(pane_cx_t *pane_cx, sdl_event_t *event);
static void thread_directory(void);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    int requested_win_width;
    int requested_win_height;

    // get and process options
    // -g NNNxNNN  : window size
    // -d          : debug mode
    while (true) {
        char opt_char = getopt(argc, argv, "g:v");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'g': {
            int cnt = sscanf(optarg, "%dx%d", &win_width, &win_height);
            //xxx 2000 don't allow window to exceed cache size, either here or below
            if (cnt != 2 || win_width < 100 || win_width > 2000 || win_height < 100 || win_height > 2000) {
                FATAL("-g %s invalid\n", optarg);
            }
            break; }
        case 'd':
            debug_enabled = true;
            break;
        default:
            return 1;
        }
    }

    // init sdl
    requested_win_width  = win_width;
    requested_win_height = win_height;
    if (sdl_init(&win_width, &win_height, true, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    INFO("REQUESTED win_width=%d win_height=%d\n", requested_win_width, requested_win_height);
    INFO("ACTUAL    win_width=%d win_height=%d\n", win_width, win_height);

    // xxx
    pixel_size_at_zoom0 = 4. / win_width;
    cache_init(pixel_size_at_zoom0);

    // run the pane manger;
    // the sdl_pane_manager is the runtime loop, and it will repeatedly call the pane_hndlr,
    //  first to initialize the pane_hndlr and subsequently to render the display and
    //  process events
    sdl_pane_manager(
        NULL,           // context
        NULL,           // called prior to pane handlers
        NULL,           // called after pane handlers
        50000,          // 0=continuous, -1=never, else us
        1,              // number of pane handler varargs that follow
        pane_hndlr, NULL, 0, 0, win_width, win_height, PANE_BORDER_STYLE_NONE);

    // done
    return 0;
}

// -----------------  PANE_HNDLR  ---------------------------------------

typedef struct {
    char          str[200];
    int           color;
    unsigned long expire_us;
} alert_t;

static bool    full_screen          = false;
static int     display_select       = DISPLAY_SELECT_MBS;
static int     display_select_count = 0;
static alert_t alert                = {.expire_us = 0};

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event)
{
    rect_t * pane = &pane_cx->pane;

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        INFO("PANE x,y,w,h  %d %d %d %d\n", pane->x, pane->y, pane->w, pane->h);
        init_hndlr_mbs();
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        // if window size has changed then update the pane's 
        // location within the window
        int new_win_width, new_win_height;
        sdl_get_window_size(&new_win_width, &new_win_height);
        if (new_win_width != win_width || new_win_height != win_height) {
            DEBUG("NEW WIN SIZE w=%d %d\n", new_win_width, new_win_height);
            sdl_pane_update(pane_cx, 0, 0, new_win_width, new_win_height);
            win_width = new_win_width;
            win_height = new_win_height;
        }

        // call the selected render_hndlr
        switch (display_select) {
        case DISPLAY_SELECT_MBS:
            render_hndlr_mbs(pane_cx);
            break;
        case DISPLAY_SELECT_HELP:
            render_hndlr_help(pane_cx);
            break;
        case DISPLAY_SELECT_COLOR_LUT:
            render_hndlr_color_lut(pane_cx);
            break;
        case DISPLAY_SELECT_DIRECTORY:
            render_hndlr_directory(pane_cx);
            break;
        }

        // display alert messages in the center of the pane
        display_alert(pane);

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        int rc = PANE_HANDLER_RET_NO_ACTION;

        switch (event->event_id) {
        case 'f':  // full screen
            full_screen = !full_screen;
            DEBUG("set full_screen to %d\n", full_screen);
            sdl_full_screen(full_screen);
            break;
        case 'q':  // quit
            rc = PANE_HANDLER_RET_PANE_TERMINATE;
            break;
        case 'h':  // display help
            display_select = (display_select == DISPLAY_SELECT_HELP
                              ? DISPLAY_SELECT_MBS : DISPLAY_SELECT_HELP);
            display_select_count++;
            break;
        case 'c':  // display color lut
            display_select = (display_select == DISPLAY_SELECT_COLOR_LUT
                              ? DISPLAY_SELECT_MBS : DISPLAY_SELECT_COLOR_LUT);
            display_select_count++;
            break;
        case 'd':  // display directory
            display_select = (display_select == DISPLAY_SELECT_DIRECTORY
                              ? DISPLAY_SELECT_MBS : DISPLAY_SELECT_DIRECTORY);
            display_select_count++;
            break;
        case SDL_EVENT_KEY_ESC: // another way back to the MBS display
            if (display_select != DISPLAY_SELECT_MBS) {
                display_select = DISPLAY_SELECT_MBS;
                display_select_count++;
            }
            break;
        default:
            // it is not a common event, so
            // call the selected event_hndlr
            switch (display_select) {
            case DISPLAY_SELECT_MBS:
                rc = event_hndlr_mbs(pane_cx, event);
                break;
            case DISPLAY_SELECT_HELP:
                rc = event_hndlr_help(pane_cx, event);
                break;
            case DISPLAY_SELECT_COLOR_LUT:
                rc = event_hndlr_color_lut(pane_cx, event);
                break;
            case DISPLAY_SELECT_DIRECTORY:
                rc = event_hndlr_directory(pane_cx, event);
                break;
            }
            break;
        }

        return rc;
    }

    // ---------------------------
    // -------- TERMINATE --------
    // ---------------------------

    if (request == PANE_HANDLER_REQ_TERMINATE) {
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

static void set_alert(int color, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(alert.str, sizeof(alert.str), fmt, ap);
    va_end(ap);

    alert.color = color;
    alert.expire_us = microsec_timer() + 2000000;
}

static void display_alert(rect_t *pane)
{
    static const int alert_font_ptsize = 30;
    int x, y;

    if (microsec_timer() > alert.expire_us) {
        return;
    }

    x = pane->w / 2 - COL2X(strlen(alert.str), alert_font_ptsize) / 2;
    y = pane->h / 2 - ROW2Y(1,alert_font_ptsize) / 2;
    sdl_render_printf(pane, x, y, alert_font_ptsize, alert.color, BLACK, "%s", alert.str);
}

// - - - - - - - - -  PANE_HNDLR : MBS   - - - - - - - - - - - - - - - -

#define ZOOM_TOTAL (zoom + zoom_fraction)

static int          wavelen_start                = WAVELEN_START_DEFAULT;
static int          wavelen_scale                = WAVELEN_SCALE_DEFAULT;
static int          auto_zoom                    = 0;
static int          auto_zoom_last               = 1;            //xxx needs defines
static complex      ctr                          = INITIAL_CTR;  // xxx rename 
static int          zoom                         = 0;            // xxx rename
static double       zoom_fraction                = 0;
static bool         display_info                 = true;
static bool         debug_force_cache_thread_run = false;
static unsigned int color_lut[65536];

static void init_hndlr_mbs(void)
{
    init_color_lut(wavelen_start, wavelen_scale, color_lut);
}

static void render_hndlr_mbs(pane_cx_t *pane_cx)
{
    int            idx = 0, pixel_x, pixel_y;
    unsigned long  time_now_us = microsec_timer();
    unsigned long  update_intvl_ms;
    rect_t       * pane = &pane_cx->pane;

    static texture_t       texture;
    static unsigned int   *pixels;
    static unsigned short *mbsval;
    static unsigned long   last_update_time_us;

    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_PAN      (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_ZOOM     (SDL_EVENT_USER_DEFINED + 2)

    // determine the display update interval, 
    // which may be displayed in the info area at top left
    update_intvl_ms = (last_update_time_us != 0
                       ? (time_now_us - last_update_time_us) / 1000
                       : 0);
    last_update_time_us = time_now_us;

    // if the texture hasn't been allocated yet, or the size of the
    // texture doesn't match the size of the pane then
    // re-allocate the texture, pixels, and mbsval
    int new_texture_width, new_texture_height;
    if ((texture == NULL) ||
        ((sdl_query_texture(texture, &new_texture_width, &new_texture_height), true) &&
         (new_texture_width != pane->w || new_texture_height != pane->h)))
    {
        DEBUG("allocating texture,pixels,mbsval w=%d h=%d\n", pane->w, pane->h);
        sdl_destroy_texture(texture);
        free(pixels);
        free(mbsval);

        texture = sdl_create_texture(pane->w, pane->h);
        pixels = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        mbsval = malloc(pane->w*pane->h*2);
    }

    // if auto_zoom is enabled then increment or decrement the zoom until limit is reached
    if (auto_zoom != 0) {
        zoom_step(auto_zoom == 1);
        if (ZOOM_TOTAL == 0) {
            auto_zoom = 0;
        }
        if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
            auto_zoom = 0;
        }
    }

    // inform mandelbrot set cache of the current ctr and zoom
    cache_param_change(ctr, zoom, win_width, win_height, debug_force_cache_thread_run);
    debug_force_cache_thread_run = false;

    // get the cached mandelbrot set values; and
    // convert them to pixel color values
    cache_get_mbsval(mbsval, win_width, win_height);
    for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
        for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
            pixels[idx] = color_lut[mbsval[idx]];
            idx++;
        }
    }

    // copy the pixels to the texture
    sdl_update_texture(texture, (void*)pixels, pane->w*BYTES_PER_PIXEL);

    // xxx comments
    rect_t src;
    double tmp = pow(2, -zoom_fraction);
    src.w = pane->w * tmp;
    src.h = pane->h * tmp;
    src.x = (pane->w - src.w) / 2;
    src.y = (pane->h - src.h) / 2;

    // render the src area of the texture to the entire pane
    rect_t dst = {0,0,pane->w,pane->h};
    sdl_render_scaled_texture_ex(pane, &src, &dst, texture);

    // display info in upper left corner
    if (display_info) {
        display_info_proc(pane, update_intvl_ms);
    }

    // when debug_enabled display a squae in the center of the pane;
    // the purpose is to be able to check that the screen's pixels are square
    // (if the display settings aspect ratio doesn't match the physical screen
    //  dimensions then the pixels will not be square)
    if (debug_enabled) {
        rect_t loc = {pane->w/2-100, pane->h/2-100, 200, 200};
        sdl_render_rect(pane, &loc, 1, WHITE);
    }

    // register for events
    sdl_register_event(pane, pane, SDL_EVENT_CENTER, SDL_EVENT_TYPE_MOUSE_RIGHT_CLICK, pane_cx);
    sdl_register_event(pane, pane, SDL_EVENT_PAN, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    sdl_register_event(pane, pane, SDL_EVENT_ZOOM, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);
}

static int event_hndlr_mbs(pane_cx_t *pane_cx, sdl_event_t *event)
{
    rect_t * pane = &pane_cx->pane;
    int      rc   = PANE_HANDLER_RET_NO_ACTION;

    switch (event->event_id) {

    // --- GENERAL ---
    case 'r':
        ctr           = INITIAL_CTR;
        zoom          = 0;
        zoom_fraction = 0.0;
        break;
    case 'R':
        wavelen_start = WAVELEN_START_DEFAULT;
        wavelen_scale = WAVELEN_SCALE_DEFAULT;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    case 'i':
        display_info = !display_info;
        break;
    case 's': {
        save_file(pane);
        break; }

    // --- DEBUG ---
    case SDL_EVENT_KEY_F(1):
        debug_enabled = !debug_enabled;
        break;
    case SDL_EVENT_KEY_F(2):
        debug_force_cache_thread_run = true;
        break;

    // --- COLOR CONTROLS ---
    case SDL_EVENT_KEY_UP_ARROW: case SDL_EVENT_KEY_DOWN_ARROW:
        wavelen_scale = wavelen_scale +
                              (event->event_id == SDL_EVENT_KEY_UP_ARROW ? 1 : -1);
        if (wavelen_scale < 0) wavelen_scale = 0;
        if (wavelen_scale > 16) wavelen_scale = 16;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    case SDL_EVENT_KEY_LEFT_ARROW: case SDL_EVENT_KEY_RIGHT_ARROW:
        wavelen_start = wavelen_start +
                              (event->event_id == SDL_EVENT_KEY_RIGHT_ARROW ? 1 : -1);
        if (wavelen_start < WAVELEN_FIRST) wavelen_start = WAVELEN_LAST;
        if (wavelen_start > WAVELEN_LAST) wavelen_start = WAVELEN_FIRST;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;

    // --- CENTER ---
    case SDL_EVENT_PAN: {
        double pixel_size = pixel_size_at_zoom0 * pow(2,-ZOOM_TOTAL);
        ctr += -(event->mouse_motion.delta_x * pixel_size) + 
               -(event->mouse_motion.delta_y * pixel_size) * I;
        break; }
    case SDL_EVENT_CENTER: {
        double pixel_size = pixel_size_at_zoom0 * pow(2,-ZOOM_TOTAL);
        ctr += ((event->mouse_click.x - (pane->w/2)) * pixel_size) + 
               ((event->mouse_click.y - (pane->h/2)) * pixel_size) * I;
        break; }

    // --- ZOOM ---
    case '+': case '=': case '-':
        zoom_step(event->event_id == '+' || event->event_id == '=');
        break;
    case SDL_EVENT_ZOOM:
        if (event->mouse_wheel.delta_y > 0) {
            zoom_step(true);
        } else if (event->mouse_wheel.delta_y < 0) {
            zoom_step(false);
        }
        break;
    case 'z':
        if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
            zoom = 0;
            zoom_fraction = 0;
        } else {
            zoom = (MAX_ZOOM-1);
            zoom_fraction = 0;
        }
        break;

    // --- AUTO ZOOM ---
    case 'a':
        // xxx comment
        if (auto_zoom != 0) {
            auto_zoom_last = auto_zoom;
            auto_zoom = 0;
        } else {
            if (ZOOM_TOTAL == 0) {
                auto_zoom = 1;
            } else if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
                auto_zoom = 2;
            } else {
                auto_zoom = auto_zoom_last;
            }
        }
        break;
    case 'A': 
        // flip dir of autozoom
        if (auto_zoom == 1) {
            auto_zoom = 2;
        } else if (auto_zoom == 2) {
            auto_zoom = 1;
        } else {
            auto_zoom_last = (auto_zoom_last == 1 ? 2 : 1);
        }
        break;
    }

    return rc;
}

static void zoom_step(bool dir_is_incr)
{
    double z = ZOOM_TOTAL;

    z += (dir_is_incr ? ZOOM_STEP : -ZOOM_STEP);

    if (fabs(z - nearbyint(z)) < 1e-6) {
        z = nearbyint(z);
    }

    if (z < 0) {
        z = 0;
    } else if (z > (MAX_ZOOM-1)) {
        z = (MAX_ZOOM-1);
    }

    zoom = z;
    zoom_fraction = z - zoom;
}

static void init_color_lut(int wavelen_start, int wavelen_scale, unsigned int *color_lut)
{
    int           i;
    unsigned char r,g,b;
    double        wavelen;
    double        wavelen_step;

    color_lut[65535]         = PIXEL_BLUE;
    color_lut[MBSVAL_IN_SET] = PIXEL_BLACK;

    if (wavelen_scale == 0) {
        // black and white
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            color_lut[i] = PIXEL_WHITE;
        }
    } else {
        // color
        wavelen_step  = (double)(WAVELEN_LAST-WAVELEN_FIRST) / (MBSVAL_IN_SET-1) * wavelen_scale;
        wavelen = wavelen_start;
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            if (i == MBSVAL_IN_SET-1) {
                INFO("%d  %lf\n", i, wavelen);
            }
            sdl_wavelen_to_rgb(wavelen, &r, &g, &b);
            color_lut[i] = PIXEL(r,g,b);
            wavelen += wavelen_step;
            if (wavelen > WAVELEN_LAST+.01) {
                wavelen = WAVELEN_FIRST;
            }
        }
    }
}

static void display_info_proc(rect_t *pane, unsigned long update_intvl_ms)
{
    char line[20][50];
    int  line_len[20];
    int  n=0, max_len=0, i;
    int  phase_inprog, zoom_lvl_inprog;

    // print info to line[] array
    sprintf(line[n++], "Window: %d %d", win_width, win_height);
    sprintf(line[n++], "Zoom:   %0.2f", ZOOM_TOTAL);
    sprintf(line[n++], "Color:  %d %d", wavelen_start, wavelen_scale);   
    cache_status(&phase_inprog, &zoom_lvl_inprog);
    if (phase_inprog == 0) {
        sprintf(line[n++], "Cache:  Idle");
    } else {
        sprintf(line[n++], "Cache:  Phase%d Zoom=%d", phase_inprog, zoom_lvl_inprog);
    }
    sprintf(line[n++], "Intvl:  %ld ms", update_intvl_ms);
    sprintf(line[n++], "Debug:  %s", debug_enabled ? "True" : "False");

    // determine each line_len and the max_len
    for (i = 0; i < n; i++) {
        line_len[i] = strlen(line[i]);
        if (line_len[i] > max_len) max_len = line_len[i];
    }

    // extend each line with spaces, out to max_len,
    // so that each line is max_len long
    for (i = 0; i < n; i++) {
        sprintf(line[i]+line_len[i], "%*s", max_len-line_len[i], "");
    }

    // render the lines
    for (i = 0; i < n; i++) {
        sdl_render_printf(pane, 0, ROW2Y(i,20), 20,  WHITE, BLACK, "%s", line[i]);
    }
}

static void save_file(rect_t *pane)
{
    int             x_idx, y_idx, idx, w, h;
    unsigned int   *pixels;
    unsigned short *mbsval;
    double          x, y, x_step, y_step;

    w      = pane->w *  pow(2, -zoom_fraction);
    h      = pane->h *  pow(2, -zoom_fraction);
    x      = 0;
    y      = 0;
    y_step = h / 200.;
    x_step = w / 300.;
    idx    = 0;

    mbsval = malloc(w * h * 2);
    pixels = malloc(w * h * 4);

    cache_get_mbsval(mbsval, w, h);

    for (y_idx = 0; y_idx < 200; y_idx++) {
        x = 0;
        for (x_idx = 0; x_idx < 300; x_idx++) {
            pixels[idx] = color_lut[
                             mbsval[(int)nearbyint(y) * w  +  (int)nearbyint(x)]
                                        ];
            idx++;
            x = x + x_step;
        }
        y = y + y_step;
    }

    cache_file_create(ctr, zoom, zoom_fraction, wavelen_start, wavelen_scale, pixels);
    set_alert(GREEN, "SAVE COMPLETE");

    free(mbsval);
    free(pixels);
}

// - - - - - - - - -  PANE_HNDLR : HELP  - - - - - - - - - - - - - - - -

static void render_hndlr_help(pane_cx_t *pane_cx)
{
    FILE *fp;
    char s[200];
    int row, len;
    rect_t * pane = &pane_cx->pane;

    if ((fp = fopen("mbs_help.txt", "r")) != NULL) {
        for (row = 0; fgets(s,sizeof(s),fp); row++) {
            len = strlen(s);
            if (len > 0 && s[len-1] == '\n') s[len-1] = '\0';
            sdl_render_printf(pane, 0, ROW2Y(row,20), 20, WHITE, BLACK, "%s", s);
        }
        fclose(fp);
    }
}

static int event_hndlr_help(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_NO_ACTION;
    return rc;
}

// - - - - - - - - -  PANE_HNDLR : COLOR_LUT   - - - - - - - - - - - - -

static void render_hndlr_color_lut(pane_cx_t *pane_cx)
{
    int i, x, y, x_start;
    unsigned char r,g,b;
    char title[100];
    rect_t * pane = &pane_cx->pane;

    x_start = (pane->w - MBSVAL_IN_SET) / 2;
    for (i = 0; i < MBSVAL_IN_SET; i++) {
        PIXEL_TO_RGB(color_lut[i],r,g,b);           
        x = x_start + i;
        y = pane->h/2 - 400/2;
        sdl_define_custom_color(20, r,g,b);
        sdl_render_line(pane, x, y, x, y+400, 20);
    }

    sprintf(title,"COLOR MAP - START=%d nm  SCALE=%d", wavelen_start, wavelen_scale);
    x   = pane->w/2 - COL2X(strlen(title),30)/2;
    sdl_render_printf(pane, x, 0, 30,  WHITE, BLACK, "%s", title);
}

static int event_hndlr_color_lut(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_NO_ACTION;

    switch (event->event_id) {
    case 'R':
        wavelen_start = WAVELEN_START_DEFAULT;
        wavelen_scale = WAVELEN_SCALE_DEFAULT;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    case SDL_EVENT_KEY_UP_ARROW: case SDL_EVENT_KEY_DOWN_ARROW:
        wavelen_scale = wavelen_scale +
                              (event->event_id == SDL_EVENT_KEY_UP_ARROW ? 1 : -1);
        if (wavelen_scale < 0) wavelen_scale = 0;
        if (wavelen_scale > 8) wavelen_scale = 8;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    case SDL_EVENT_KEY_LEFT_ARROW: case SDL_EVENT_KEY_RIGHT_ARROW:
        wavelen_start = wavelen_start +
                              (event->event_id == SDL_EVENT_KEY_RIGHT_ARROW ? 1 : -1);
        if (wavelen_start < WAVELEN_FIRST) wavelen_start = WAVELEN_LAST;
        if (wavelen_start > WAVELEN_LAST) wavelen_start = WAVELEN_FIRST;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    }

    return rc;
}

// - - - - - - - - -  PANE_HNDLR : DIRECTORY  - - - - - - - - - - - - -

static bool init_request;

static int  y_top;
static int  thread_run;
static int  thread_preempt_loc;
static int  activity_indicator;
static bool selected[1000];

static void render_hndlr_directory(pane_cx_t *pane_cx)
{
    rect_t * pane = &pane_cx->pane;
    int      idx, x, y;

    static texture_t texture;
    static int       last_display_select_count;

    #define SDL_EVENT_SCROLL_WHEEL (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_CHOICE       (SDL_EVENT_USER_DEFINED + 10)
    #define SDL_EVENT_SELECT       (SDL_EVENT_USER_DEFINED + 1100)

    // one time only init
    if (texture == NULL) {
        texture = sdl_create_texture(300, 200);
    }

    // initialize when needed
    if (display_select_count != last_display_select_count || init_request) {
        cache_file_garbage_collect();

        y_top              = 0;
        thread_run         = 0;
        thread_preempt_loc = 0;
        activity_indicator = -1;
        memset(selected, 0, sizeof(selected));

        init_request = false;
        last_display_select_count = display_select_count;
    }

    // xxx comment
    thread_directory();

    // display the directory images
    // xxx comment on what is displayed with the image
    for (idx = 0; idx < max_file_info; idx++) {
        cache_file_info_t *fi = file_info[idx];

        // if file has been deleted then continue,
        // - this should not happen because whenever a file is deleted init_request is set,
        //   which causes cache_file_garbage_collect to be called
        if (fi->deleted) {
            continue;
        }

        // determine location of upper left
        x = (idx % (pane->w/300)) * 300;
        y = (idx / (pane->w/300)) * 200 + y_top;

        // continue if location is outside of the pane
        if (y <= -200 || y >= pane->h) {
            continue;
        }

        // display the file's directory image
        sdl_update_texture(texture, (void*)fi->dir_pixels, 300*BYTES_PER_PIXEL);
        sdl_render_texture(pane, x, y, texture);

        // if the activity_indicator is active for this file then 
        // display the activity indicator
        if (activity_indicator == idx) {
            static char *ind = "|/-\\";
            static int   ind_idx;
            sdl_render_printf(pane, 
                              x+(300/2-COL2X(1,80)/2), y+(200/2-ROW2Y(1,80)/2), 
                              80, WHITE, BLACK, 
                              "%c", ind[ind_idx]);
            ind_idx = (ind_idx + 1) % 4;
        }

        // display a small red box in it's upper left, if this image is selected
        if (selected[idx]) {
            rect_t loc = {x,y,17,20};
            sdl_render_fill_rect(pane, &loc, RED);
        }

        // display the file number
        sdl_render_printf(pane, x+(300/2-COL2X(2,20)), y+0, 20, WHITE, BLACK, 
            "%c%c%c%c", 
            fi->file_name[4], fi->file_name[5], fi->file_name[6], fi->file_name[7]);

        // display file type
        sdl_render_printf(pane, x+300-COL2X(1,20), y+0, 20, WHITE, BLACK,
            "%d", fi->file_type);

        // register for events for each directory image that is displayed
        rect_t loc = {x,y,300,200};
        sdl_register_event(pane, &loc, SDL_EVENT_CHOICE + idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        sdl_register_event(pane, &loc, SDL_EVENT_SELECT + idx, SDL_EVENT_TYPE_MOUSE_RIGHT_CLICK, pane_cx);
    }

    // separate the directory images with black lines
    // xxx check that this is working in full scrn
    int i;
    for (i = 1; i < (pane->w/300); i++) {
        x = i * 300;
        sdl_render_line(pane, x-2, 0, x-2, pane->h-1, BLACK);
        sdl_render_line(pane, x-1, 0, x-1, pane->h-1, BLACK);
        sdl_render_line(pane, x+0, 0, x+0, pane->h-1, BLACK);
        sdl_render_line(pane, x+1, 0, x+1, pane->h-1, BLACK);
    }
    for (i = 1; i <= max_file_info-1; i++) {
        y = (i / (pane->w/300)) * 200 + y_top;   // XXX this needs recoding
        if (y+1 < 0 || y-2 > pane->h-1) {
            continue;
        }
        sdl_render_line(pane, 0, y-2, pane->w-1, y-2, BLACK);
        sdl_render_line(pane, 0, y-1, pane->w-1, y-1, BLACK);
        sdl_render_line(pane, 0, y+0, pane->w-1, y+0, BLACK);
        sdl_render_line(pane, 0, y+1, pane->w-1, y+1, BLACK);
    }

    // register for additional events
    sdl_register_event(pane, pane, SDL_EVENT_SCROLL_WHEEL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);
}

static int event_hndlr_directory(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_DISPLAY_REDRAW;
    int idx;

    switch (event->event_id) {
    case SDL_EVENT_SCROLL_WHEEL:
    case SDL_EVENT_KEY_PGUP:
    case SDL_EVENT_KEY_PGDN:
    case SDL_EVENT_KEY_UP_ARROW:
    case SDL_EVENT_KEY_DOWN_ARROW:
    case SDL_EVENT_KEY_HOME:
    case SDL_EVENT_KEY_END:
        if (event->event_id == SDL_EVENT_SCROLL_WHEEL) {
            if (event->mouse_wheel.delta_y > 0) {
                y_top += 20;
            } else if (event->mouse_wheel.delta_y < 0) {
                y_top -= 20;
            }
        } else if (event->event_id == SDL_EVENT_KEY_PGUP) {
            y_top += 600;
        } else if (event->event_id == SDL_EVENT_KEY_PGDN) {
            y_top -= 600;
        } else if (event->event_id == SDL_EVENT_KEY_UP_ARROW) {
            y_top += 20;
        } else if (event->event_id == SDL_EVENT_KEY_DOWN_ARROW) {
            y_top -= 20;
        } else if (event->event_id == SDL_EVENT_KEY_HOME) {
            y_top = 0;
        } else if (event->event_id == SDL_EVENT_KEY_END) {
            y_top = -((max_file_info - 1) / 4 + 1) * 200 + 600;
        } else {
            FATAL("unexpected event_id 0x%x\n", event->event_id);
        }

        int y_top_limit = -((max_file_info - 1) / 4 + 1) * 200 + 600;
        if (y_top < y_top_limit) y_top = y_top_limit;
        if (y_top > 0) y_top = 0;
        break;

    case SDL_EVENT_CHOICE...SDL_EVENT_CHOICE+1000:
        idx = event->event_id - SDL_EVENT_CHOICE;

        cache_file_read(idx);

        ctr           = file_info[idx]->ctr;
        zoom          = file_info[idx]->zoom;
        zoom_fraction = file_info[idx]->zoom_fraction;
        wavelen_start = file_info[idx]->wavelen_start;
        wavelen_scale = file_info[idx]->wavelen_scale;

        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        display_select = DISPLAY_SELECT_MBS;
        display_select_count++;
        break;

    case SDL_EVENT_SELECT...SDL_EVENT_SELECT+1000:
        idx = event->event_id - SDL_EVENT_SELECT;
        selected[idx] = !selected[idx];
        break;
    case 's':
        for (idx = 0; idx < max_file_info; idx++) {
            selected[idx] = true;
        }
        break;
    case 'S':
        for (idx = 0; idx < max_file_info; idx++) {
            selected[idx] = false;
        }
        break;

    case SDL_EVENT_KEY_DELETE:
        if (thread_run != 0) {
            set_alert(RED, "BUSY");
            break;
        }
        for (idx = 0; idx < max_file_info; idx++) {
            if (!selected[idx] || file_info[idx]->deleted) {
                selected[idx] = false;
                continue;
            }
            cache_file_delete(idx);
            selected[idx] = false;
        }
        init_request = true;
        break;

    case '0': {
        if (thread_run != 0) {
            set_alert(RED, "BUSY");
            break;
        }
        for (idx = 0; idx < max_file_info; idx++) {
            if (!selected[idx] || file_info[idx]->deleted) {
                selected[idx] = false;
                continue;
            }
            cache_file_update(idx, 0);
            selected[idx] = false;
        }
        break; }
    case '1':
        if (thread_run != 0) {
            set_alert(RED, "BUSY");
            break;
        }
        thread_run = 1;
        break;
    case '2':
        if (thread_run != 0) {
            set_alert(RED, "BUSY");
            break;
        }
        thread_run = 2;
        break;
    }

    return rc;
}

#define LABEL(n) lab_ ## n
#define PREEMPT(n) \
    { \
    thread_preempt_loc = n; \
    return; \
    LABEL(n): ; \
    }

static void thread_directory(void)
{
    switch (thread_preempt_loc) {
    case 0: break;
    case 1: goto lab_1;
    case 2: goto lab_2;
    default: FATAL("thread_preempt_loc=%d\n", thread_preempt_loc);
    }

    static int idx;

    idx = 0;

    while (true) {
        while (thread_run == 0) {
            PREEMPT(1);
        }

        INFO("starting, thread_run=%d\n", thread_run);

        for (idx = 0; idx < max_file_info; idx++) {
            // xxx comments
            if (!selected[idx] || file_info[idx]->deleted) {
                selected[idx] = false;
                continue;
            }

            if (file_info[idx]->file_type == thread_run) {
                selected[idx] = false;
                continue;
            }

            // enable the activity_indicator
            activity_indicator = idx;

            // xxx check the cache thread code if there is a problem 
            //     if these are equal to the cache size
            cache_param_change(file_info[idx]->ctr, file_info[idx]->zoom, 1990, 1990, true);

            // wait for xxx
            INFO("- waiting for cache complete for %s\n", file_info[idx]->file_name);
            while (true) {
                if ((thread_run == 1 && cache_thread_first_phase1_zoom_lvl_is_finished()) ||
                    (thread_run == 2 && cache_thread_all_is_finished()))
                {
                    break;
                }
                if (!selected[idx]) {
                    break;
                }
                PREEMPT(2);
            }
            if (!selected[idx]) {
                activity_indicator = -1;
                continue;
            }

            // update file
            INFO("- updating file %s\n", file_info[idx]->file_name);
            cache_file_update(idx, thread_run);
            INFO("- updating file completed\n");

            // done updating this file, clear the activity indicator and selected
            activity_indicator = -1;
            selected[idx] = false;
        }

        // done, set thread_run to 0 because this is now idle
        INFO("done\n");
        thread_run = 0;
    }
}
