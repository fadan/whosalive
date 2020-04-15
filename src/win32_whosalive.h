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

#define MAX_FILENAME_SIZE 260

struct Win32State
{
    Win32Window window;
    Win32Window overlay;
    b32 initialized;

    u32 exe_filename_length;
    u32 exe_path_length;
    u32 temp_path_length;
    char exe_filename[MAX_FILENAME_SIZE];
    char streams_filename[MAX_FILENAME_SIZE];
    char temp_path[MAX_FILENAME_SIZE];

    b32 quit_requested;
};

struct Win32Overlay
{
    int width;
    int height;
    
    int stride;
    unsigned char *top_left_corner;

    HDC draw_dc;
    HBITMAP bitmap;
    HFONT header_font;
    HFONT message_font;
};

void win32_message_box(char *message, char *title);
