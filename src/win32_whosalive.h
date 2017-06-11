#include <windows.h>
#include <wininet.h>

enum Win32WindowState
{
    Win32WindowState_Hidden,
    Win32WindowState_FadingIn,
    Win32WindowState_Shown,
    Win32WindowState_FadingOut,
};

struct Win32Window
{
    b32 popup;
    char *title;
    char *class_name;
    u32 style;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    HWND hwnd;
    WNDPROC wnd_proc;

    Win32WindowState state;
    i32 alpha;
};

struct Win32State
{
    Win32Window window;
    Win32Window overlay;
    b32 initialized;

    b32 quit_requested;
};

struct Win32DibSection
{
    HDC dc;
    HBITMAP bitmap;

    u32 width;
    u32 height;

    i32 bytes_per_pixel;
    i32 bytes_per_line;
    i32 stride;
    i32 total_size;
    u8 *top_left_corner;
};
