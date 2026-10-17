/* The game window on Windows, shared by the RSX backends that own one
 * (rsx_null_backend.c, which live draw presents into, and rsx_d3d12_backend.c).
 *
 *   PS3_PARENT_HWND=<hwnd>   render inside another program's window (a
 *                            launcher), as a child filling its client area
 *   PS3_WINDOW_SIZE=<w>x<h>  the window's client size (the picture is
 *                            stretched to it); default the render size
 *   PS3_FULLSCREEN=1         borderless full screen; F11 or Alt+Enter toggles
 *   PS3_WINDOW_ICON=<.ico>   the window's icon
 *   PS3_ESC_QUITS=0          Escape does not close the game
 *
 * Embedded, full screen applies to the host's top-level window, the host
 * window's size is followed, and its closing ends the game. */
#ifndef RSX_HOST_WINDOW_H
#define RSX_HOST_WINDOW_H
#ifdef _WIN32
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static HWND s_hw_parent;
static int  s_hw_fullscreen;
static RECT s_hw_restore;
static LONG s_hw_restore_style;

static void rsx_host_window_set_fullscreen(HWND hwnd, int on)
{
    if (s_hw_parent) hwnd = GetAncestor(s_hw_parent, GA_ROOT);
    if (!hwnd || on == s_hw_fullscreen) return;
    if (on) {
        MONITORINFO mi = { sizeof(mi) };
        GetWindowRect(hwnd, &s_hw_restore);
        s_hw_restore_style = GetWindowLongA(hwnd, GWL_STYLE);
        GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, s_hw_restore_style ? s_hw_restore_style
                                                           : (LONG)(WS_OVERLAPPEDWINDOW | WS_VISIBLE));
        SetWindowPos(hwnd, NULL, s_hw_restore.left, s_hw_restore.top,
                     s_hw_restore.right - s_hw_restore.left, s_hw_restore.bottom - s_hw_restore.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
    }
    s_hw_fullscreen = on;
}

static HWND rsx_host_window_create(const char* cls, unsigned width, unsigned height, const char* title)
{
    const char* e = getenv("PS3_PARENT_HWND");
    HWND parent = e && *e ? (HWND)(uintptr_t)strtoull(e, NULL, 0) : NULL;
    HWND hwnd = NULL;
    if (parent && IsWindow(parent)) {
        RECT pr; GetClientRect(parent, &pr);
        hwnd = CreateWindowExA(0, cls, title, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                               pr.right > 0 ? pr.right : (int)width, pr.bottom > 0 ? pr.bottom : (int)height,
                               parent, NULL, GetModuleHandle(NULL), NULL);
        if (hwnd) {
            s_hw_parent = parent;
            printf("[window] rendering inside window 0x%llX\n", (unsigned long long)(uintptr_t)parent);
            SetFocus(hwnd);
            return hwnd;
        }
        printf("[window] could not render inside 0x%llX (%lu): own window\n",
               (unsigned long long)(uintptr_t)parent, GetLastError());
    }
    unsigned cw = width, ch = height;
    e = getenv("PS3_WINDOW_SIZE");
    { unsigned ew = 0, eh = 0;
      if (e && sscanf(e, "%ux%u", &ew, &eh) == 2 && ew >= 320 && eh >= 180 && ew <= 16384 && eh <= 16384) {
          cw = ew; ch = eh; } }
    RECT wr = {0, 0, (LONG)cw, (LONG)ch};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowExA(0, cls, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                           NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hwnd) return NULL;
    e = getenv("PS3_WINDOW_ICON");
    if (e && *e) {
        HICON ico_b = (HICON)LoadImageA(NULL, e, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
        HICON ico_s = (HICON)LoadImageA(NULL, e, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        if (ico_b) SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)ico_b);
        if (ico_s) SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ico_s);
    }
    e = getenv("PS3_FULLSCREEN");
    if (e && e[0] == '1') rsx_host_window_set_fullscreen(hwnd, 1);
    return hwnd;
}

/* The window messages handled here: F11 / Alt+Enter, and a click that gives
 * an embedded window the keyboard. Returns 1 when the message is consumed. */
static int rsx_host_window_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN && (lp & (1 << 29))) { rsx_host_window_set_fullscreen(hwnd, !s_hw_fullscreen); return 1; }
        break;
    case WM_KEYDOWN:
        if (wp == VK_F11) { rsx_host_window_set_fullscreen(hwnd, !s_hw_fullscreen); return 1; }
        break;
    case WM_LBUTTONDOWN:
        if (s_hw_parent) SetFocus(hwnd);
        break;
    }
    return 0;
}

static int rsx_host_window_esc_quits(void)
{
    const char* e = getenv("PS3_ESC_QUITS");
    return !(e && e[0] == '0');
}

/* Embedded: follow the host window's size. Returns 1 once the host is gone. */
static int rsx_host_window_follow(HWND hwnd)
{
    if (!s_hw_parent || !hwnd) return 0;
    if (!IsWindow(s_hw_parent)) return 1;
    RECT pr, cr;
    GetClientRect(s_hw_parent, &pr);
    GetClientRect(hwnd, &cr);
    if (pr.right > 0 && pr.bottom > 0 && (pr.right != cr.right || pr.bottom != cr.bottom))
        MoveWindow(hwnd, 0, 0, pr.right, pr.bottom, TRUE);
    return 0;
}

#endif /* _WIN32 */
#endif /* RSX_HOST_WINDOW_H */
