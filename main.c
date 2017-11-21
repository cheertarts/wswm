
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>
#include <wlc/wlc.h>

static struct {
    struct {
        wlc_handle view;
        struct wlc_point grab;
        uint32_t edges;
    } action;
} compositor;

static bool start_interactive_action(wlc_handle view,
                                     const struct wlc_point *origin) {
    if (compositor.action.view) {
        return false;
    }

    compositor.action.view = view;
    compositor.action.grab = *origin;
    wlc_view_bring_to_front(view);
    return true;
}

static void start_interactive_move(wlc_handle view,
                                   const struct wlc_point *origin) {
    start_interactive_action(view, origin);
}

static void start_interactive_resize(wlc_handle view, uint32_t edges,
                                     const struct wlc_point *origin) {
    const struct wlc_geometry *g;
    if (!(g = wlc_view_get_geometry(view)) ||
        !start_interactive_action(view, origin)) {
        return;
    }

    const int32_t halfw = g->origin.x + g->size.w / 2;
    const int32_t halfh = g->origin.y + g->size.h / 2;

    if (!(compositor.action.edges = edges)) {
        uint32_t x, y;
        
        if (origin->x < halfw) {
            x = WLC_RESIZE_EDGE_LEFT;
        } else if (origin->x > halfw) {
            x = WLC_RESIZE_EDGE_RIGHT;
        } else {
            x = 0;
        }
        if (origin->y < halfh) {
            y = WLC_RESIZE_EDGE_TOP;
        } else if (origin->y > halfh) {
            y = WLC_RESIZE_EDGE_BOTTOM;
        } else {
            y = 0;
        }
        
        compositor.action.edges = x | y;
    }

    wlc_view_set_state(view, WLC_BIT_RESIZING, true);
}

static void stop_interactive_action(void) {
    if (!compositor.action.view) {
        return;
    }

    wlc_view_set_state(compositor.action.view, WLC_BIT_RESIZING, false);
    memset(&compositor.action, 0, sizeof(compositor.action));
}

static bool view_created(wlc_handle view) {
    wlc_view_set_mask(view, wlc_output_get_mask(wlc_view_get_output(view)));
    wlc_view_bring_to_front(view);
    wlc_view_focus(view);
    return true;
}

static void view_focus(wlc_handle view, bool focus) {
    wlc_view_set_state(view, WLC_BIT_ACTIVATED, focus);
}

static void view_request_move(wlc_handle view, const struct wlc_point *origin) {
    start_interactive_move(view, origin);
}

static void view_request_resize(wlc_handle view, uint32_t edges,
                                const struct wlc_point *origin) {
    start_interactive_resize(view, edges, origin);
}

static void view_request_geometry(wlc_handle view, const struct wlc_geometry *g) {
    (void)view, (void)g;
    /* TODO: Do something here */
}

static bool keyboard_key(wlc_handle view, uint32_t time,
                         const struct wlc_modifiers *modifiers, uint32_t key,
                         enum wlc_key_state state) {
    (void)time, (void)key;
    const uint32_t sym = wlc_keyboard_get_keysym_for_key(key, NULL);

    if (view) {
        if (modifiers->mods & WLC_BIT_MOD_LOGO && sym == XKB_KEY_grave) {
            if (state == WLC_KEY_STATE_PRESSED) {
                wlc_view_close(view);
            }
            return true;
        }
    }

    if (modifiers->mods & WLC_BIT_MOD_LOGO &&
          modifiers->mods & WLC_BIT_MOD_SHIFT && sym == XKB_KEY_grave) {
        if (state == WLC_KEY_STATE_PRESSED) {
            wlc_terminate();
        }
        return true;
    } else if (modifiers ->mods & WLC_BIT_MOD_LOGO && sym == XKB_KEY_Return) {
        if (state == WLC_KEY_STATE_PRESSED) {
            wlc_exec("xterm", (char *const[]){"xterm", NULL});
        }
        return true;
    }
    return false;
}

static bool pointer_button(wlc_handle view, uint32_t time,
                           const struct wlc_modifiers *modifiers,
                           uint32_t button, enum wlc_button_state state,
                           const struct wlc_point *position) {
    (void)button, (void)time, (void)modifiers;

    if (state == WLC_BUTTON_STATE_PRESSED) {
        wlc_view_focus(view);
        if (view) {
            if (modifiers->mods & WLC_BIT_MOD_LOGO && button == BTN_LEFT) {
                start_interactive_move(view, position);
            } else if (modifiers->mods & WLC_BIT_MOD_LOGO &&
                       button == BTN_RIGHT) {
                start_interactive_resize(view, 0, position);
            }
        }
    } else {
        stop_interactive_action();
    }

    if (compositor.action.view) {
        return true;
    }
    return false;
}

static bool pointer_motion(wlc_handle handle, uint32_t time, double x,
                           double y) {
    (void)handle, (void)time;

    if (compositor.action.view) {
        const int32_t dx = x - compositor.action.grab.x;
        const int32_t dy = y - compositor.action.grab.y;
        struct wlc_geometry g = *wlc_view_get_geometry(compositor.action.view);

        if (compositor.action.edges) {
            const struct wlc_size min = {80, 40};
            struct wlc_geometry n = g;

            if (compositor.action.edges & WLC_RESIZE_EDGE_LEFT) {
                n.size.w -= dx;
                n.origin.x += dx;
            } else if (compositor.action.edges & WLC_RESIZE_EDGE_RIGHT) {
                n.size.w += dx;
            }

            if (compositor.action.edges & WLC_RESIZE_EDGE_TOP) {
                n.size.h -= dy;
                n.origin.y += dy;
            } else if (compositor.action.edges & WLC_RESIZE_EDGE_BOTTOM) {
                n.size.h += dy;
            }

            if (n.size.w >= min.w) {
                g.origin.x = n.origin.x;
                g.size.w = n.size.w;
            }

            if (n.size.h >= min.h) {
                g.origin.y = n.origin.y;
                g.size.h = n.size.h;
            }

            wlc_view_set_geometry(compositor.action.view,
                                  compositor.action.edges, &g);
        } else {
            g.origin.x += dx;
            g.origin.y += dy;
            wlc_view_set_geometry(compositor.action.view, 0, &g);
        }

        compositor.action.grab.x = x;
        compositor.action.grab.y = y;
    }

    wlc_pointer_set_position_v2(x, y);
    if (compositor.action.view) {
        return true;
    }
    return false;
}

static void cb_data_source(void *data, const char *type, int fd) {
    ((void) type);
    const char *str = data;
    write(fd, str, strlen(str));
    close(fd);
}

static void cb_log(enum wlc_log_type type, const char *str) {
    (void)type;
    printf("%s\n", str);
}

int main(int argc, char **argv) {
    wlc_log_set_handler(cb_log);

    wlc_set_view_created_cb(view_created);
    wlc_set_view_focus_cb(view_focus);
    wlc_set_view_request_move_cb(view_request_move);
    wlc_set_view_request_resize_cb(view_request_resize);
    wlc_set_view_request_geometry_cb(view_request_geometry);
    wlc_set_keyboard_key_cb(keyboard_key);
    wlc_set_pointer_button_cb(pointer_button);
    wlc_set_pointer_motion_cb_v2(pointer_motion);

    if (!wlc_init()) {
        return EXIT_FAILURE;
    }

    const char *type = "text/plain;charset=utf-8";
    wlc_set_selection("wlc", &type, 1, &cb_data_source);
    
    wlc_run();
    return EXIT_SUCCESS;
}
