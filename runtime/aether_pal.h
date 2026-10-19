/* ==============================================================================
 *  AETHER CROSS-PLATFORM HARDWARE ABSTRACTION LAYER (runtime/aether_pal.h)
 *  Native Platform Backend Interface for Windows (Win32/WGL), Linux (X11/GLX),
 *  macOS (Cocoa/NSOpenGL), and Headless Software/CI Execution.
 * ============================================================================== */

#ifndef AETHER_PAL_H
#define AETHER_PAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AE_PAL_BACKEND_AUTO     = 0,
    AE_PAL_BACKEND_X11_GLX  = 1,
    AE_PAL_BACKEND_WIN32    = 2,
    AE_PAL_BACKEND_COCOA    = 3,
    AE_PAL_BACKEND_HEADLESS = 4
} AePalBackend;

typedef struct {
    int32_t   width;
    int32_t   height;
    int32_t   fb_width;
    int32_t   fb_height;
    int32_t   dpi_scale_fp;
    int32_t   is_open;
    int32_t   is_focused;
    int32_t   is_minimized;
    int32_t   is_fullscreen;
    uint32_t *color_buffer;
    int32_t  *depth_buffer;
    void     *native_display;
    uint64_t  native_window;
    void     *native_gl_ctx;
} AePalWindow;

int          ae_pal_init(AePalBackend preferred_backend);
void         ae_pal_shutdown(void);
AePalWindow *ae_pal_window_create(const char *title, int32_t width, int32_t height, uint32_t flags);
void         ae_pal_window_destroy(AePalWindow *win);
void         ae_pal_window_poll_events(AePalWindow *win);
void         ae_pal_window_present(AePalWindow *win);
uint64_t     ae_pal_time_now_us(void);
void         ae_pal_sleep_ms(int32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_PAL_H */
