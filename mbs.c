#include <common.h>

#include <util_sdl.h>

//
// defines
//

#define DEFAULT_WIN_WIDTH   1200   // 1600   xxx or something else
#define DEFAULT_WIN_HEIGHT  800    // 900

//#define INITIAL_CTR    (-0.75 + 0.0*I)
#define INITIAL_CTR      (0.27808593632993183764 -0.47566405952660278933*I)

// xxx don't define these
#define PIXEL_BLACK ((  0 << 0) | (  0 << 8) | (  0 << 16) | (255 << 24))
#define PIXEL_BLUE  ((  0 << 0) | (  0 << 8) | (255 << 16) | (255 << 24))

//xxx comment on being am multiple
#define ZOOM_STEP .1
#define LAST_ZOOM  (MAX_ZOOM-1)

//
// typedefs
//

//
// variables
//

static int    win_width   = DEFAULT_WIN_WIDTH;
static int    win_height  = DEFAULT_WIN_HEIGHT;
static double pixel_size_at_zoom0;

//
// prototypes
//

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static double zoom_step(double z, bool dir_is_incr);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    int requested_win_width;
    int requested_win_height;

    // get and process options
    // -g NNNxNNN  : window size
    // -v          : verbose
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
        case 'v':
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

    // run the pane manger xxx describe what does this do
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
        unsigned int  color_lut[65536];  // xxx maxshort
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

        vars->texture   = sdl_create_texture(pane->w, pane->h);
        vars->pixels    = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        vars->lcl_ctr   = INITIAL_CTR;
        vars->lcl_zoom  = 0;
        vars->auto_zoom = 0;
        vars->auto_zoom_last = 1;    //xxx needs defines
        vars->last_update_time_us = microsec_timer();
        vars->full_screen = false;
        vars->display_info = true;

        // xxx later vars->color_lut[65535]         = PIXEL_BLUE;

        // xxx routine to do this
        vars->color_lut[MBSVAL_IN_SET] = PIXEL_BLACK;  // xxx need a macro to make the pixel
        int i;
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            unsigned char r,g,b;
            double wavelen = 400. + i * ((700. - 400.) / (MBSVAL_IN_SET-1));
            sdl_wavelen_to_rgb(wavelen, &r, &g, &b);
            vars->color_lut[i] = (r << 0) | (  g << 8) | (b << 16) | (255 << 24); // xxx mavro
        }

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        int            idx = 0, pixel_x, pixel_y;
        unsigned int * pixels = vars->pixels;
        unsigned long  time_now_us = microsec_timer();
        unsigned long  update_intvl_ms;

        // debug
        update_intvl_ms = (time_now_us - vars->last_update_time_us) / 1000;
        vars->last_update_time_us = time_now_us;
        //DEBUG("*** INTVL=%ld ms ***\n", update_intvl_ms);

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
        cache_param_change(vars->lcl_ctr, floor(vars->lcl_zoom), win_width, win_height);

        // get the cached mandelbrot set values; and
        // convert them to pixel color values
        short * mbsval = malloc(win_height*win_width*2);  //xxx make this unsigned
        cache_get_mbsval(mbsval);
        for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
            for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
                pixels[idx] = (mbsval[idx] == MBSVAL_NOT_COMPUTED 
                               ? PIXEL_BLUE
                               : vars->color_lut[mbsval[idx]]);
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

        // status  xxx clean up
        if (vars->display_info) {
            int row = 0;
            int phase, percent_complete, zoom_lvl_inprog;

            sdl_render_printf(pane, 0, ROW2Y(row++,20), 20,  WHITE, BLACK, 
                              "Window: %d %d",
                              win_width, win_height);
            sdl_render_printf(pane, 0, ROW2Y(row++,20), 20,  WHITE, BLACK, 
                              "Zoom:   %0.2f",
                              vars->lcl_zoom);   // xxx also autozoom status
            sdl_render_printf(pane, 0, ROW2Y(row++,20), 20,  WHITE, BLACK, 
                              "Intvl:  %ld ms",
                              update_intvl_ms);
            cache_status(&phase, &percent_complete, &zoom_lvl_inprog);
            if (phase == 0) {
                sdl_render_printf(pane, 0, ROW2Y(row++,20), 20,  WHITE, BLACK, 
                                  "Cache:  Idle");
            } else {
                sdl_render_printf(pane, 0, ROW2Y(row++,20), 20,  WHITE, BLACK, 
                                  "Cache:  Phase%d %d%% Zoom=%d",
                                  phase, percent_complete, zoom_lvl_inprog);
            }
            sdl_render_printf(pane, 0, ROW2Y(row++,20), 20,  WHITE, BLACK, 
                              "Debug:  %s",
                              debug_enabled ? "True" : "False");
        }

        // xxx square ?
        if (debug_enabled) {
            rect_t loc = { pane->w/2-100, pane->h/2-100, 200, 200};
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

    // xxx reorder
    if (request == PANE_HANDLER_REQ_EVENT) {
        switch (event->event_id) {
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
        case 't': {
            vars->full_screen = !vars->full_screen;
            DEBUG("set full_screen to %d\n", vars->full_screen);
            sdl_full_screen(vars->full_screen);
            break; }
        case '+': case '=': case '-':
            vars->lcl_zoom = zoom_step(vars->lcl_zoom, 
                                       event->event_id == '+' || event->event_id == '=');
            break;
        case SDL_EVENT_ZOOM:  // xxx combine with above
            if (event->mouse_wheel.delta_y > 0) {
                vars->lcl_zoom = zoom_step(vars->lcl_zoom, true);
            } else if (event->mouse_wheel.delta_y < 0) {
                vars->lcl_zoom = zoom_step(vars->lcl_zoom, false);
            }
            break;
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
        case 'r':
            vars->lcl_ctr  = INITIAL_CTR;
            vars->lcl_zoom = 0;
            break;
        case 'z':
            if (vars->lcl_zoom == LAST_ZOOM) {
                vars->lcl_zoom = 0;
            } else {
                vars->lcl_zoom = LAST_ZOOM;
            }
            break;
        case '?':
            // xxx help
            break;
        case 'i':
            vars->display_info = !vars->display_info;
            break;
        case 'd': 
            debug_enabled = !debug_enabled;
            break;
        case 'q':
            return PANE_HANDLER_RET_PANE_TERMINATE;
            break;
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

