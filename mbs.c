#include <common.h>

#include <util_sdl.h>

//
// defines
//

#define DEFAULT_WIN_WIDTH         1200
#define DEFAULT_WIN_HEIGHT        800

#define INITIAL_CTR               (-0.75 + 0.0*I)

#define ZOOM_STEP                 .1   // must be a submultiple of 1
#define LAST_ZOOM                 (MAX_ZOOM-1)

#define WAVELEN_FIRST             400
#define WAVELEN_LAST              700
#define WAVELEN_START_DEFAULT     400
#define WAVELEN_SCALE_DEFAULT     2

#define DISPLAY_SELECT_MBS        1
#define DISPLAY_SELECT_HELP       2
#define DISPLAY_SELECT_COLOR_LUT  3

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
static double zoom_step(double z, bool dir_is_incr);
static void init_color_lut(int wavelen_start, int wavelen_scale, unsigned int *color_lut);
static void set_alert(int color, char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void display_alert(rect_t *pane);
static void display_info(rect_t *pane, double lcl_zoom, int wavelen_start, int wavelen_scale,
                         unsigned long update_intvl_ms);

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

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event)
{
    struct {
        texture_t     texture;
        unsigned int *pixels;
        complex       lcl_ctr;
        double        lcl_zoom;
        int           auto_zoom;
        int           auto_zoom_last;
        unsigned long last_update_time_us;
        bool          full_screen;
        bool          display_info;
        int           display_select;
        bool          force;
        int           wavelen_start;
        int           wavelen_scale;
        unsigned int  color_lut[65536];
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    #define SDL_EVENT_CENTER   (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_PAN      (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_ZOOM     (SDL_EVENT_USER_DEFINED + 2)

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        INFO("PANE x,y,w,h  %d %d %d %d\n", pane->x, pane->y, pane->w, pane->h);

        vars = pane_cx->vars = calloc(1,sizeof(*vars));

        vars->texture             = sdl_create_texture(pane->w, pane->h);
        vars->pixels              = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        vars->lcl_ctr             = INITIAL_CTR;
        vars->lcl_zoom            = 0;
        vars->auto_zoom           = 0;
        vars->auto_zoom_last      = 1;    //xxx needs defines
        vars->last_update_time_us = microsec_timer();
        vars->full_screen         = false;
        vars->display_info        = true;
        vars->display_select      = DISPLAY_SELECT_MBS;
        vars->force               = false;
        vars->wavelen_start       = WAVELEN_START_DEFAULT;
        vars->wavelen_scale       = WAVELEN_SCALE_DEFAULT;

        init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);

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
    }

    if (request == PANE_HANDLER_REQ_RENDER && vars->display_select == DISPLAY_SELECT_HELP) {
        FILE *fp;
        char s[200];
        int row, len;

        if ((fp = fopen("mbs_help.txt", "r")) != NULL) {
            for (row = 0; fgets(s,sizeof(s),fp); row++) {
                len = strlen(s);
                if (len > 0 && s[len-1] == '\n') s[len-1] = '\0';
                sdl_render_printf(pane, 0, ROW2Y(row,20), 20, WHITE, BLACK, "%s", s);
            }
            fclose(fp);
        }

        return PANE_HANDLER_RET_NO_ACTION;
    }

    if (request == PANE_HANDLER_REQ_RENDER && vars->display_select == DISPLAY_SELECT_COLOR_LUT) {
        int i, x, y, x_start;
        unsigned char r,g,b;
        char title[100];

        x_start = (pane->w - MBSVAL_IN_SET) / 2;
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            PIXEL_TO_RGB(vars->color_lut[i],r,g,b);           
            x = x_start + i;
            y = pane->h/2 - 400/2;
            sdl_define_custom_color(20, r,g,b);
            sdl_render_line(pane, x, y, x, y+400, 20);  // xxx y should be centered
        }

        sprintf(title,"COLOR MAP - START=%d nm  SCALE=%d",
                vars->wavelen_start, vars->wavelen_scale);
        x   = pane->w/2 - COL2X(strlen(title),30)/2;
        sdl_render_printf(pane, x, 0, 30,  WHITE, BLACK, "%s", title);

        return PANE_HANDLER_RET_NO_ACTION;
    }

    if (request == PANE_HANDLER_REQ_RENDER && vars->display_select == DISPLAY_SELECT_MBS) {
        int            idx = 0, pixel_x, pixel_y;
        unsigned int * pixels = vars->pixels;
        unsigned long  time_now_us = microsec_timer();
        unsigned long  update_intvl_ms;

        // debug
        update_intvl_ms = (time_now_us - vars->last_update_time_us) / 1000;
        vars->last_update_time_us = time_now_us;

        // if the texture hasn't been allocated yet, or the size of the
        // texture doesn't match the size of the pane then
        // re-allocate the texture and the pixels array
        int new_texture_width, new_texture_height;
        if ((vars->texture == NULL) ||
            ((sdl_query_texture(vars->texture, &new_texture_width, &new_texture_height), true) &&
             (new_texture_width != pane->w || new_texture_height != pane->h)))
        {
            DEBUG("ALLOCATING TEXTURE AND PIXELS w=%d h=%d\n", pane->w, pane->h);
            sdl_destroy_texture(vars->texture);
            free(vars->pixels);
            vars->texture = sdl_create_texture(pane->w, pane->h);
            vars->pixels = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
            pixels = vars->pixels;
        }

        // xxx
        if (vars->auto_zoom != 0) {
            vars->lcl_zoom = zoom_step(vars->lcl_zoom, vars->auto_zoom == 1);
            if (vars->lcl_zoom == 0) {
                vars->auto_zoom = 0;
            }
            if (vars->lcl_zoom == LAST_ZOOM) {
                vars->auto_zoom = 0;
            }
        }

        // inform mandelbrot set cache of the current ctr and zoom
        if (vars->force) {
            INFO("debug force cache_thread to run\n");
        }
        cache_param_change(vars->lcl_ctr, floor(vars->lcl_zoom), win_width, win_height, vars->force);
        vars->force = false;

        // get the cached mandelbrot set values; and
        // convert them to pixel color values
        unsigned short * mbsval = malloc(win_height*win_width*2);
        cache_get_mbsval(mbsval);
        for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
            for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
                pixels[idx] = vars->color_lut[mbsval[idx]];
                idx++;
            }
        }
        free(mbsval);

        // copy the pixels to the texture and render the texture
        sdl_update_texture(vars->texture, (void*)pixels, pane->w*BYTES_PER_PIXEL);

        // xxx
        rect_t dst = {0,0,pane->w,pane->h};
        rect_t src;
        //if changed then do prints
        static double lcl_zoom_last;
        bool do_print = vars->lcl_zoom != lcl_zoom_last;
        lcl_zoom_last = vars->lcl_zoom;
        do_print = false; //xxx

        double xxx = pow(2, -(vars->lcl_zoom - floor(vars->lcl_zoom)));

        src.w = pane->w * xxx;
        src.h = pane->h * xxx;
        src.x = (pane->w - src.w) / 2;
        src.y = (pane->h - src.h) / 2;

        if (do_print) INFO("xxx = %.25lf  xywh = %d %d %d %d\n", 
                            xxx, src.x, src.y, src.w, src.h);

        sdl_render_scaled_texture_ex(pane, &src, &dst, vars->texture);

        // display info in upper left corner
        if (vars->display_info) {
            display_info(pane, 
                         vars->lcl_zoom, 
                         vars->wavelen_start, 
                         vars->wavelen_scale,
                         update_intvl_ms);
        }

        // display alert messages in the center of the pane
        display_alert(pane);

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

        // return
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT && vars->display_select == DISPLAY_SELECT_HELP) {
        switch (event->event_id) {
        case SDL_EVENT_KEY_ESC:
            vars->display_select = DISPLAY_SELECT_MBS;
            break;
        }

        return PANE_HANDLER_RET_NO_ACTION;
    }

    if (request == PANE_HANDLER_REQ_EVENT && vars->display_select == DISPLAY_SELECT_COLOR_LUT) {
        switch (event->event_id) {
        case SDL_EVENT_KEY_ESC:
            vars->display_select = DISPLAY_SELECT_MBS;
            break;
        case 'R':
            vars->wavelen_start = WAVELEN_START_DEFAULT;
            vars->wavelen_scale = WAVELEN_SCALE_DEFAULT;
            init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);
            break;
        case SDL_EVENT_KEY_UP_ARROW: case SDL_EVENT_KEY_DOWN_ARROW:
            vars->wavelen_scale = vars->wavelen_scale +
                                  (event->event_id == SDL_EVENT_KEY_UP_ARROW ? 1 : -1);
            if (vars->wavelen_scale < 0) vars->wavelen_scale = 0;
            if (vars->wavelen_scale > 8) vars->wavelen_scale = 8;
            init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);
            break;
        case SDL_EVENT_KEY_LEFT_ARROW: case SDL_EVENT_KEY_RIGHT_ARROW:
            vars->wavelen_start = vars->wavelen_start +
                                  (event->event_id == SDL_EVENT_KEY_RIGHT_ARROW ? 1 : -1);
            if (vars->wavelen_start < WAVELEN_FIRST) vars->wavelen_start = WAVELEN_LAST;
            if (vars->wavelen_start > WAVELEN_LAST) vars->wavelen_start = WAVELEN_FIRST;
            init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);
            break;
        }

        return PANE_HANDLER_RET_NO_ACTION;
    }

    if (request == PANE_HANDLER_REQ_EVENT && vars->display_select == DISPLAY_SELECT_MBS) {
        switch (event->event_id) {

        // --- GENERAL ---
        case '?':
            vars->display_select = DISPLAY_SELECT_HELP;
            break;
        case 'q':
            return PANE_HANDLER_RET_PANE_TERMINATE;
            break;
        case 'i':
            vars->display_info = !vars->display_info;
            break;
        case 'c':
            vars->display_select = DISPLAY_SELECT_COLOR_LUT;
            break;
        case 'r':
            vars->lcl_ctr  = INITIAL_CTR;
            vars->lcl_zoom = 0;
            break;
        case 'R':
            vars->wavelen_start = WAVELEN_START_DEFAULT;
            vars->wavelen_scale = WAVELEN_SCALE_DEFAULT;
            init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);
            break;

        // --- DEBUG ---
        case SDL_EVENT_KEY_F(1):
            debug_enabled = !debug_enabled;
            break;
        case SDL_EVENT_KEY_F(2):
            vars->force = true;
            break;

        // --- FULL SCREEN ---
        case 'f': {
            vars->full_screen = !vars->full_screen;
            DEBUG("set full_screen to %d\n", vars->full_screen);
            sdl_full_screen(vars->full_screen);
            break; }

        // --- COLOR CONTROLS ---
        case SDL_EVENT_KEY_UP_ARROW: case SDL_EVENT_KEY_DOWN_ARROW:
            vars->wavelen_scale = vars->wavelen_scale +
                                  (event->event_id == SDL_EVENT_KEY_UP_ARROW ? 1 : -1);
            if (vars->wavelen_scale < 0) vars->wavelen_scale = 0;
            if (vars->wavelen_scale > 8) vars->wavelen_scale = 8;
            init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);
            break;
        case SDL_EVENT_KEY_LEFT_ARROW: case SDL_EVENT_KEY_RIGHT_ARROW:
            vars->wavelen_start = vars->wavelen_start +
                                  (event->event_id == SDL_EVENT_KEY_RIGHT_ARROW ? 1 : -1);
            if (vars->wavelen_start < WAVELEN_FIRST) vars->wavelen_start = WAVELEN_LAST;
            if (vars->wavelen_start > WAVELEN_LAST) vars->wavelen_start = WAVELEN_FIRST;
            init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);
            break;

        // --- CENTER ---
        case SDL_EVENT_PAN: {
            double pixel_size = pixel_size_at_zoom0 * pow(2,-vars->lcl_zoom);
            vars->lcl_ctr += -(event->mouse_motion.delta_x * pixel_size) + 
                             -(event->mouse_motion.delta_y * pixel_size) * I;
            break; }
        case SDL_EVENT_CENTER: {
            double pixel_size = pixel_size_at_zoom0 * pow(2,-vars->lcl_zoom);
            vars->lcl_ctr += ((event->mouse_click.x - (pane->w/2)) * pixel_size) + 
                             ((event->mouse_click.y - (pane->h/2)) * pixel_size) * I;
            break; }

        // --- ZOOM ---
        case '+': case '=': case '-':
            vars->lcl_zoom = zoom_step(vars->lcl_zoom, 
                                       event->event_id == '+' || event->event_id == '=');
            break;
        case SDL_EVENT_ZOOM:
            if (event->mouse_wheel.delta_y > 0) {
                vars->lcl_zoom = zoom_step(vars->lcl_zoom, true);
            } else if (event->mouse_wheel.delta_y < 0) {
                vars->lcl_zoom = zoom_step(vars->lcl_zoom, false);
            }
            break;
        case 'z':
            if (vars->lcl_zoom == LAST_ZOOM) {
                vars->lcl_zoom = 0;
            } else {
                vars->lcl_zoom = LAST_ZOOM;
            }
            break;

        // --- AUTO ZOOM ---
        case 'a':
            // xxx comment
            if (vars->auto_zoom != 0) {
                vars->auto_zoom_last = vars->auto_zoom;
                vars->auto_zoom = 0;
            } else {
                if (vars->lcl_zoom == 0) {
                    vars->auto_zoom = 1;
                } else if (vars->lcl_zoom == LAST_ZOOM) {
                    vars->auto_zoom = 2;
                } else {
                    vars->auto_zoom = vars->auto_zoom_last;
                }
            }
            break;
        case 'A': 
            // flip dir of autozoom
            if (vars->auto_zoom == 1) {
                vars->auto_zoom = 2;
            } else if (vars->auto_zoom == 2) {
                vars->auto_zoom = 1;
            } else {
                vars->auto_zoom_last = (vars->auto_zoom_last == 1 ? 2 : 1);
            }
            break;

        // --- READ AND WRITE FILES ---
        case '0'...'9': {
            complex new_ctr;
            double  new_zoom;
            int     new_wavelen_start;
            int     new_wavelen_scale;
            bool    succ;
            int     file_id = (event->event_id - '0');
            char    file_name[100];
            sprintf(file_name, "mbs_%d.dat", file_id);
            succ = cache_read(file_name, &new_ctr, &new_zoom, 
                              &new_wavelen_start, &new_wavelen_scale);
            if (succ) {
                set_alert(GREEN, "Read %s Okay", file_name);
            } else {
                set_alert(RED, "Read %s Failed", file_name);
                break;
            }
            vars->lcl_ctr = new_ctr;
            vars->lcl_zoom = new_zoom;
            vars->wavelen_start = new_wavelen_start;
            vars->wavelen_scale = new_wavelen_scale;
            init_color_lut(vars->wavelen_start, vars->wavelen_scale, vars->color_lut);
            break; }
        case SDL_EVENT_KEY_CTRL+'0'...SDL_EVENT_KEY_CTRL+'9': 
        case SDL_EVENT_KEY_ALT+'0'...SDL_EVENT_KEY_ALT+'9': {
            bool require_cache_thread_finished = 
                           event->event_id >= SDL_EVENT_KEY_CTRL+'0' &&
                           event->event_id <= SDL_EVENT_KEY_CTRL+'9';
            int file_id = (require_cache_thread_finished
                           ? (event->event_id - (SDL_EVENT_KEY_CTRL+'0'))
                           : (event->event_id - (SDL_EVENT_KEY_ALT+'0')));
            char file_name[100];
            bool succ;
            sprintf(file_name, "mbs_%d.dat", file_id);
            succ = cache_write(file_name, vars->lcl_ctr, vars->lcl_zoom, 
                               vars->wavelen_start, vars->wavelen_scale,
                               require_cache_thread_finished);
            if (succ) {
                set_alert(GREEN, "Write %s Okay", file_name);
            } else {
                set_alert(RED, "Write %s Failed", file_name);
            }
            break; }
        }

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ---------------------------
    // -------- TERMINATE --------
    // ---------------------------

    if (request == PANE_HANDLER_REQ_TERMINATE) {
        sdl_destroy_texture(vars->texture);
        free(vars->pixels);
        free(vars);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

static double zoom_step(double z, bool dir_is_incr)
{
    z += (dir_is_incr ? ZOOM_STEP : -ZOOM_STEP);

    if (fabs(z - nearbyint(z)) < 1e-6) {
        z = nearbyint(z);
    }

    if (z < 0) z = 0;
    if (z > LAST_ZOOM) z = LAST_ZOOM;

    return z;
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

struct {
    char          str[200];
    int           color;
    unsigned long expire_us;
} alert;

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
    static int alert_font_ptsize = 30;
    int x, y;

    if (microsec_timer() > alert.expire_us) {
        return;
    }

    x = pane->w / 2 - COL2X(strlen(alert.str), alert_font_ptsize) / 2;
    y = pane->h / 2 - ROW2Y(1,alert_font_ptsize) / 2;
    sdl_render_printf(pane, x, y, alert_font_ptsize, alert.color, BLACK, "%s", alert.str);
}

static void display_info(rect_t *pane,
                         double lcl_zoom,
                         int wavelen_start,
                         int wavelen_scale,
                         unsigned long update_intvl_ms)
{
    char line[20][50];
    int  line_len[20];
    int  n=0, max_len=0, i;
    int  phase, percent_complete, zoom_lvl_inprog;

    // print info to line[] array
    sprintf(line[n++], "Window: %d %d", win_width, win_height);
    sprintf(line[n++], "Zoom:   %0.2f", lcl_zoom);
    sprintf(line[n++], "Color:  %d %d", wavelen_start, wavelen_scale);   
    cache_status(&phase, &percent_complete, &zoom_lvl_inprog);
    if (phase == 0) {
        sprintf(line[n++], "Cache:  Idle");
    } else {
        sprintf(line[n++], "Cache:  Phase%d %d%% Zoom=%d", phase, percent_complete, zoom_lvl_inprog);
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
