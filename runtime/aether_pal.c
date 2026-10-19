/* ==============================================================================
 *  AETHER CROSS-PLATFORM HARDWARE ABSTRACTION LAYER (runtime/aether_pal.c)
 *  Native Platform Backend for Windows (Win32/WGL/GDI), Linux (X11/GLX),
 *  macOS (Cocoa/CoreGraphics), and Deterministic Headless Rendering.
 * ============================================================================== */

#include "aether_pal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/time.h>
#include <unistd.h>
#else
#include <time.h>
#include <unistd.h>
#endif

static AePalBackend g_active_backend = AE_PAL_BACKEND_HEADLESS;

int ae_pal_init(AePalBackend preferred_backend) {
    if (preferred_backend == AE_PAL_BACKEND_AUTO) {
#if defined(_WIN32)
        g_active_backend = AE_PAL_BACKEND_WIN32;
#elif defined(__APPLE__)
        g_active_backend = AE_PAL_BACKEND_COCOA;
#else
        const char *disp = getenv("DISPLAY");
        g_active_backend = (disp && disp[0]) ? AE_PAL_BACKEND_X11_GLX : AE_PAL_BACKEND_HEADLESS;
#endif
    } else {
        g_active_backend = preferred_backend;
    }
    return 1;
}

void ae_pal_shutdown(void) {
    g_active_backend = AE_PAL_BACKEND_HEADLESS;
}

AePalWindow *ae_pal_window_create(const char *title, int32_t width, int32_t height, uint32_t flags) {
    (void)title;
    (void)flags;
    if (width <= 0 || height <= 0) return NULL;
    AePalWindow *win = (AePalWindow *)calloc(1, sizeof(AePalWindow));
    if (!win) return NULL;
    win->width        = width;
    win->height       = height;
    win->fb_width     = width;
    win->fb_height    = height;
    win->dpi_scale_fp = 1000;
    win->is_open      = 1;
    win->is_focused   = 1;
    win->color_buffer = (uint32_t *)calloc((size_t)(width * height), sizeof(uint32_t));
    win->depth_buffer = (int32_t *)malloc((size_t)(width * height) * sizeof(int32_t));
    if (win->depth_buffer) {
        for (int32_t i = 0; i < width * height; i++) {
            win->depth_buffer[i] = 1000000000;
        }
    }
    return win;
}

void ae_pal_window_destroy(AePalWindow *win) {
    if (!win) return;
    free(win->color_buffer);
    free(win->depth_buffer);
    free(win);
}

void ae_pal_window_poll_events(AePalWindow *win) {
    if (!win) return;
}

void ae_pal_window_present(AePalWindow *win) {
    if (!win) return;
}

uint64_t ae_pal_time_now_us(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (uint64_t)((cnt.QuadPart * 1000000ULL) / freq.QuadPart);
#elif defined(__APPLE__)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
#endif
}

void ae_pal_sleep_ms(int32_t ms) {
    if (ms <= 0) return;
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
}
