#include "platform.h"
#include "json.h"

#include "json.cpp"

#include <windows.h>
#include <wininet.h>

#define UPDATE_THREAD_TIMER_ID      1
#define FADER_TIMER_ID              2
#define UPDATE_THREAD_INTERVAL_MS   (10 * 1000)
#define MAX_DOWNLOAD_SIZE           (1*MB)

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

static Win32State global_win32_state_;
static Win32State *global_win32_state = &global_win32_state_;

static HANDLE global_update_event;
static HICON global_tray_icon;

static void win32_init_window(Win32Window *window)
{
    WNDCLASSEXA window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window->wnd_proc;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.lpszClassName = window->class_name;

    if (RegisterClassExA(&window_class))
    {
        u32 style = (window->popup) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
        u32 width = (window->width) ? window->width : CW_USEDEFAULT;
        u32 height = (window->height) ? window->height : CW_USEDEFAULT;

        window->hwnd = CreateWindowExA(window->style, window_class.lpszClassName, window->title, 
                                       style, window->x, window->y, width, height, 0, 0, 
                                       window_class.hInstance, 0);
    }
}

static intptr __stdcall win32_window_proc(HWND wnd, unsigned int message, uintptr wparam, intptr lparam)
{
    intptr result = 0;
    switch (message)
    {
        case WM_DESTROY:
        {
            global_win32_state->quit_requested = true;
        } break;

        case WM_TIMER:
        {
            if (wparam == UPDATE_THREAD_TIMER_ID)
            {
                SetEvent(global_update_event);
            }
        } break;

        default:
        {
            result = DefWindowProcA(wnd, message, wparam, lparam);
        } break;
    }
    return result;
}

inline b32 strings_equal(char *a, char *b)
{
    b32 equal = (a == b);
    if (a && b)
    {
        while (*a && *b && (*a == *b))
        {
            ++a;
            ++b;
        }
        equal = ((*a == 0) && (*b == 0));
    }
    return equal;
}

inline void copy_string(char *src, char *dest)
{
    u32 length = 1;
    for (char *at = src; *at; ++at)
    {
        ++length;
    }
    for (u32 char_index = 0; char_index < length; ++char_index)
    {
        dest[char_index] = src[char_index];
    }
}

inline void copy_string_and_null_terminate(char *src, char *dest, i32 length)
{
    for (i32 char_index = 0; char_index < length; ++char_index)
    {
        dest[char_index] = src[char_index];
    }
    dest[length] = 0;
}

static char check_for_streams[2][128] =
{
    "venruki",
    "handmadehero",
};

struct Stream
{
    char name[128];
    char game[128];

    b32 online;
    b32 was_online;
};

static u32 num_streams;
static Stream streams[64];

inline u32 string_length(char *string)
{
    u32 length = 0;
    while (*string++)
    {
        ++length;
    }
    return length;
}

char url[4096];

static void init_url()
{
    char *base = "https://api.twitch.tv/kraken/streams?channel=";
    
    copy_string(base, url);

    u32 at = string_length(base);

    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;

        copy_string(stream->name, url + at);

        at += string_length(stream->name);

        if ((stream_index + 1) < num_streams)
        {
            url[at++] = ',';
        }
    }
    url[at] = 0;
}

static void init_streams()
{
    num_streams = array_count(check_for_streams);
    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;
        stream->game[0] = 0;
        stream->online = false;
        stream->was_online = false;

        char *name = check_for_streams[stream_index];
        copy_string(name, stream->name);
    }
}

inline Stream *get_stream_by_name(char *name)
{
    Stream *stream = 0;
    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *test_stream = streams + stream_index;
        if (strings_equal(test_stream->name, name))
        {
            stream = test_stream;
            break;
        }
    }
    return stream;
}

static void pre_update_streams()
{
    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;
        stream->online = false;
    }
}
static void win32_show_notification(char *title, char *message)
{
    
}

static void notify_or_update_online_stream(char *name, char *game)
{
    Stream *stream = get_stream_by_name(name);
    if (stream)
    {
        stream->online = true;
        if (!stream->was_online)
        {
            // TODO(dan): this garbage is only for test
            char title[64];
            u32 name_length = string_length(stream->name);
            copy_string(stream->name, title);

            copy_string(" started streaming", title + name_length);

            char message[256];
            copy_string("Playing: ", message);
            copy_string(game, message + 9);

            win32_show_notification(title, message);
        }
    }
}

static void post_update_streams()
{
    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;
        stream->was_online = stream->online;
    }
}

static void update_streams(void *data, unsigned int data_size)
{
    char *json_string = (char *)data;
    JsonToken tokens[1024] = {};
    JsonParser parser;

    json_init_parser(&parser, tokens, array_count(tokens));
    json_parse(&parser, (char *)data, data_size);

    for (JsonIterator root_iterator = json_iterator_get(&parser, 0); json_iterator_valid(root_iterator); root_iterator = json_iterator_next(root_iterator))
    {
        JsonToken *identifier = json_get_token(root_iterator);
        JsonToken *value = json_peek_next_token(root_iterator);
        
        if (value && value->type == JsonType_Array && json_string_token_equals(json_string, identifier, "streams"))
        {
            for (JsonIterator streams_iterator = json_iterator_get(&parser, value); json_iterator_valid(streams_iterator); streams_iterator = json_iterator_next(streams_iterator))
            {
                JsonToken *stream = json_get_token(streams_iterator);
                char name[256];
                char game[256];

                name[0] = 0;
                game[0] = 0;

                for (JsonIterator stream_iterator = json_iterator_get(&parser, stream); json_iterator_valid(stream_iterator); stream_iterator = json_iterator_next(stream_iterator))
                {
                    JsonToken *ident = json_get_token(stream_iterator);
                    JsonToken *val = json_peek_next_token(stream_iterator);

                    if (val && val->type == JsonType_String && json_string_token_equals(json_string, ident, "game"))
                    {
                        char *game_src = json_string + val->start;
                        i32 game_length = val->end - val->start;
                        copy_string_and_null_terminate(game_src, game, game_length);
                    }
                    else if (val && val->type == JsonType_Object && json_string_token_equals(json_string, ident, "channel"))
                    {
                        for (JsonIterator channel_iterator = json_iterator_get(&parser, val); json_iterator_valid(channel_iterator); channel_iterator = json_iterator_next(channel_iterator))
                        {
                            JsonToken *i = json_get_token(channel_iterator);
                            JsonToken *v = json_peek_next_token(channel_iterator);

                            if (v && v->type == JsonType_String && json_string_token_equals(json_string, i, "name"))
                            {
                                char *name_src = json_string + v->start;
                                i32 name_length = v->end - v->start;
                                copy_string_and_null_terminate(name_src, name, name_length);
                            }
                        }
                    }
                }

                notify_or_update_online_stream(name, game);
            }
        }
    }
}

static void win32_update(HINTERNET internet, void *data, unsigned int data_size)
{
    char *headers = "Client-ID: j6dzqx92ht08vnyr1ghz0a1fdw6oss";

    HINTERNET connection = InternetOpenUrlA(internet, url, headers, (unsigned int)-1, INTERNET_FLAG_EXISTING_CONNECT | INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
    if (connection)
    {
        unsigned int total_bytes_read = 0;
        unsigned int bytes_to_read = data_size;
        unsigned int bytes_read;

        while (InternetReadFile(connection, (char *)data + total_bytes_read, bytes_to_read, (DWORD *)&bytes_read))
        {
            if (!bytes_read)
            {
                break;
            }
            total_bytes_read += bytes_read;
        }

        pre_update_streams();
        update_streams(data, total_bytes_read);
        post_update_streams();

        InternetCloseHandle(connection);
    }
}

static void *win32_allocate(usize size)
{
    void *memory = VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return memory;
}

static DWORD __stdcall win32_update_thread_proc(void *data)
{
    HINTERNET internet = InternetOpenA("WhosAlive", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
    if (internet)
    {
        void *data = win32_allocate(MAX_DOWNLOAD_SIZE);
        while (WaitForSingleObject(global_update_event, 0xFFFFFFFF) == WAIT_OBJECT_0)
        {
            win32_update(internet, data, MAX_DOWNLOAD_SIZE);
        }
    }
    return 0;
}

static void win32_init_update_thread(Win32State *state)
{
    global_update_event = CreateEventA(0, 0, 0, 0);
    if (global_update_event)
    {    
        uintptr timer = SetTimer(state->window.hwnd, UPDATE_THREAD_TIMER_ID, UPDATE_THREAD_INTERVAL_MS, 0);
        if (timer)
        {
            SetEvent(global_update_event);

            HANDLE update_thread = CreateThread(0, 0, win32_update_thread_proc, 0, 0, 0);
            if (update_thread)
            {
            }
        }
    }
}

static void win32_change_opacity(Win32Window *window, i32 alpha)
{
    BLENDFUNCTION blend_func = {0};
    blend_func.BlendOp = AC_SRC_OVER;
    blend_func.SourceConstantAlpha = (BYTE)alpha;
    blend_func.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(window->hwnd, 0, 0, 0, 0, 0, 0, &blend_func, ULW_ALPHA);
}

static void win32_fading(Win32Window *window)
{
    win32_change_opacity(window, window->alpha);

    SetTimer(window->hwnd, FADER_TIMER_ID, 5, 0);
}

static void win32_fade_in(Win32Window *window)
{
    window->alpha = 0;
    window->state = Win32WindowState_FadingIn;

    win32_change_opacity(window, 0);

    ShowWindow(window->hwnd, SW_SHOW);
    SetTimer(window->hwnd, FADER_TIMER_ID, 5, 0);
}

static void win32_fade_out(Win32Window *window)
{
    window->state = Win32WindowState_FadingOut;

    SetTimer(window->hwnd, FADER_TIMER_ID, 5000, 0);
}

static void win32_update_window(Win32Window *window)
{
    switch (window->state)
    {
        case Win32WindowState_FadingIn:
        {
            if (window->alpha == 255)
            {
                window->state = Win32WindowState_Shown;
            }
            else
            {
                // TODO(dan): delta_alpha, float alpha
                ++window->alpha;
                if (window->alpha > 255)
                {
                    window->alpha = 255;
                }
            }

            win32_fading(window);
        } break;

        case Win32WindowState_FadingOut:
        {
            --window->alpha;
            if (window->alpha < 1)
            {
                window->state = Win32WindowState_Hidden;
            }

            win32_fading(window);
        } break;

        case Win32WindowState_Shown:
        {
            win32_fade_out(window);
        } break;

        case Win32WindowState_Hidden:
        {
            ShowWindow(window->hwnd, SW_HIDE);
            KillTimer(window->hwnd, FADER_TIMER_ID);
        } break;
    }
}

static intptr __stdcall win32_overlay_window_proc(HWND wnd, unsigned int message, uintptr wparam, intptr lparam)
{
    intptr result = 0;
    switch (message)
    {
        case WM_TIMER:
        {
            win32_update_window(&global_win32_state->overlay);
        } break;

        default:
        {
            result = DefWindowProcA(wnd, message, wparam, lparam);
        } break;
    }
    return result;
}

static void win32_create_dib_section(Win32Window *window)
{
    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info);
    bitmap_info.bmiHeader.biWidth = window->width;
    bitmap_info.bmiHeader.biHeight = -(long)window->height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void *image;
    HDC dc = GetDC(0);
    HBITMAP bitmap = CreateDIBSection(dc, &bitmap_info, DIB_RGB_COLORS, &image, 0, 0);
    ReleaseDC(0, dc);

    if (image)
    {
        i32 bytes_per_pixel = 4;
        i32 bytes_per_line = window->width * bytes_per_pixel;

        i32 stride = -bytes_per_line;
        i32 total_size = window->height * bytes_per_line;

        u8 *top_left_corner = (u8 *)image + total_size - bytes_per_line;

        for (u32 y = 0; y < window->height; ++y)
        {
            for (u32 x = 0; x < window->width; ++x)
            {
                i32 y_col = y * stride;
                u32 x_col = x * bytes_per_pixel;

                u32 *pixel = (u32 *)(top_left_corner + y_col + x_col);

                *pixel = 0xFFFFFFFF;
            }
        }

        HDC dc = GetDC(0);
        HDC compatible_dc = CreateCompatibleDC(dc);
        HBITMAP old = (HBITMAP)SelectObject(compatible_dc, bitmap);

        BLENDFUNCTION blend_func = {0};
        blend_func.BlendOp = AC_SRC_OVER;
        blend_func.SourceConstantAlpha = 255;
        blend_func.AlphaFormat = AC_SRC_ALPHA;

        RECT client_rect;
        GetClientRect(window->hwnd, &client_rect);
        
        POINT position = {};

        POINT origin = {0};
        SIZE size = { client_rect.right, client_rect.bottom };

        UpdateLayeredWindow(window->hwnd, dc, 0, &size, compatible_dc, &origin, 0, &blend_func, ULW_ALPHA);

        ReleaseDC(0, dc);
    }
}

int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, char *cmd_line, int cmd_show)
{
    Win32State *state = global_win32_state;

    state->window.class_name = "WhosAliveWindowClassName";
    state->window.title = "WhosAlive";
    state->window.wnd_proc = win32_window_proc;
    win32_init_window(&state->window);

    POINT zero = {0};
    HMONITOR monitor = MonitorFromPoint(zero, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitor_info = { sizeof(monitor_info) };
    GetMonitorInfo(monitor, &monitor_info);

    state->overlay.width = 300;
    state->overlay.height = 80;
    state->overlay.x = monitor_info.rcWork.right - state->overlay.width - 20;
    state->overlay.y = monitor_info.rcWork.bottom - state->overlay.height - 20;
    state->overlay.style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    state->overlay.class_name = "WhosAliveOverlayClassName";
    state->overlay.wnd_proc = win32_overlay_window_proc;
    state->overlay.popup = true;
    win32_init_window(&state->overlay);

    win32_create_dib_section(&state->overlay);
    win32_fade_in(&state->overlay);

    init_streams();
    init_url();

    win32_init_update_thread(state);

    while (!state->quit_requested)
    {
        MSG msg;
        if (!GetMessageA(&msg, 0, 0, 0))
        {
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}
