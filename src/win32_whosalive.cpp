#include "platform.h"
#include "win32_whosalive.h"

#include "whosalive.cpp"

#define UPDATE_THREAD_TIMER_ID      1
#define FADER_TIMER_ID              2
#define UPDATE_THREAD_INTERVAL_MS   (10 * 1000)
#define MAX_DOWNLOAD_SIZE           (1*MB)
#define TRAY_ICON_MESSAGE           (WM_USER + 1)

static Win32State global_win32_state_;
static Win32State *global_win32_state = &global_win32_state_;

static HANDLE global_update_event;
static HICON global_tray_icon;
static HFONT global_font_22;
static HFONT global_font_16;
static Win32DibSection global_dib_section;

Platform platform;

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

static void win32_clear(Win32DibSection *dib_section, u32 color)
{
    for (u32 y = 0; y < dib_section->height; ++y)
    {
        for (u32 x = 0; x < dib_section->width; ++x)
        {
            i32 y_col = y * dib_section->stride;
            u32 x_col = x * dib_section->bytes_per_pixel;

            u32 *pixel = (u32 *)(dib_section->top_left_corner + y_col + x_col);

            *pixel = color;
        }
    }
}

static PLATFORM_SHOW_NOTIFICATION(win32_show_notification)
{
    Win32DibSection dib_section = global_dib_section;
    Win32Window *window = &global_win32_state->overlay;

    RECT text_rect = {0};
    text_rect.top = 15;
    text_rect.left = 80;
    text_rect.right = global_dib_section.width - 10;
    text_rect.bottom = global_dib_section.height - 15;

    SelectObject(dib_section.dc, dib_section.bitmap);

    win32_clear(&dib_section, 0xFFFFFFFF);

    SetBkMode(dib_section.dc, TRANSPARENT);

    SelectObject(dib_section.dc, global_font_22);
    SetTextColor(dib_section.dc, 0x003C3C3C);
    DrawText(dib_section.dc, title, string_length(title), &text_rect, DT_LEFT);

    TEXTMETRIC text_metric;
    GetTextMetrics(dib_section.dc, &text_metric);

    text_rect.top += text_metric.tmHeight;

    SelectObject(dib_section.dc, global_font_16);
    SetTextColor(dib_section.dc, 0x00666666);
    DrawText(dib_section.dc, message, string_length(message), &text_rect, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);

    BLENDFUNCTION blend_func = {0};
    blend_func.BlendOp = AC_SRC_OVER;
    blend_func.SourceConstantAlpha = 255;
    blend_func.AlphaFormat = AC_SRC_ALPHA;

    RECT client_rect;
    GetClientRect(window->hwnd, &client_rect);
    
    POINT position = {};

    POINT origin = {0};
    SIZE size = { client_rect.right, client_rect.bottom };

    HDC dc = GetDC(0);
    UpdateLayeredWindow(window->hwnd, dc, 0, &size, dib_section.dc, &origin, 0, &blend_func, ULW_ALPHA);
    ReleaseDC(0, dc);

    win32_fade_in(&global_win32_state->overlay);
}

enum TrayIconMenuID
{
    TrayIconMenuID_Close,

    TrayIconMenuID_Count
};

static void win32_open_in_browser(i32 stream_index)
{
    Stream *stream = streams + stream_index;

    char url[256];
    wsprintf(url, "https://www.twitch.tv/%s", stream->name);

    ShellExecute(0, "open", url, 0, 0, SW_SHOWNORMAL);
}

static void win32_create_tray_menu(HWND window)
{
    HMENU menu = CreatePopupMenu();

    POINT mouse;
    GetCursorPos(&mouse);

    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;
        if (stream->online)
        {
            i32 cmd_id = TrayIconMenuID_Count + stream_index;
            AppendMenu(menu, MF_CHECKED, cmd_id, stream->name);
        }
    }

    AppendMenu(menu, MF_SEPARATOR, 0, 0);

    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;
        if (!stream->online)
        {
            i32 cmd_id = TrayIconMenuID_Count + stream_index;
            AppendMenu(menu, MF_UNCHECKED, cmd_id, stream->name);
        }
    }

    AppendMenu(menu, MF_SEPARATOR, 0, 0);
    AppendMenu(menu, MF_UNCHECKED, TrayIconMenuID_Close, "Close");

    i32 cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window, 0);
    switch (cmd)
    {
        case TrayIconMenuID_Close:
        {
            global_win32_state->quit_requested = true;
        } break;

        default:
        {
            if (cmd >= TrayIconMenuID_Count)
            {
                u32 stream_index = cmd - TrayIconMenuID_Count;
                win32_open_in_browser(stream_index);
            }
        } break;
    }

    DestroyMenu(menu);
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

        case TRAY_ICON_MESSAGE:
        {
            if (lparam == WM_RBUTTONUP || lparam == WM_LBUTTONUP)
            {
                win32_create_tray_menu(wnd);
            }
        } break;

        default:
        {
            result = DefWindowProcA(wnd, message, wparam, lparam);
        } break;
    }
    return result;
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

static Win32DibSection win32_create_dib_section(Win32Window *window)
{
    Win32DibSection dib_section = {0};
    dib_section.width = window->width;
    dib_section.height = window->height;

    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info);
    bitmap_info.bmiHeader.biWidth = dib_section.width;
    bitmap_info.bmiHeader.biHeight = -(long)dib_section.height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    dib_section.dc = CreateCompatibleDC(0);

    void *image;
    HDC dc = GetDC(0);
    dib_section.bitmap = CreateDIBSection(dc, &bitmap_info, DIB_RGB_COLORS, &image, 0, 0);

    if (dib_section.bitmap)
    {

        dib_section.bytes_per_pixel = 4;
        dib_section.bytes_per_line = dib_section.width * dib_section.bytes_per_pixel;
        dib_section.stride = -dib_section.bytes_per_line;
        dib_section.total_size = dib_section.height * dib_section.bytes_per_line;
        dib_section.top_left_corner = (u8 *)image + dib_section.total_size - dib_section.bytes_per_line;
    }

    ReleaseDC(0, dc);

    return dib_section;
}

static HFONT win32_create_font(char *font_name, i32 height)
{
    HFONT font = CreateFont(height, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, 
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FW_DONTCARE, font_name);
    return font;
}

static void win32_build_filename(char *pathname, u32 pathname_size,
                                 char *filename, u32 filename_size, 
                                 char *out, u32 max_out_size)
{
    if ((pathname_size + filename_size + 1) < max_out_size)
    {
        u32 out_index = 0;
        for (u32 char_index = 0; char_index < pathname_size; ++char_index)
        {
            out[out_index++] = pathname[char_index];
        }
        for (u32 char_index = 0; char_index < filename_size; ++char_index)
        {
            out[out_index++] = filename[char_index];
        }
        out[out_index] = '\0';
    }
}

static void win32_init_exe_path(Win32State *state)
{
    state->exe_filename_length = GetModuleFileNameA(0, state->exe_filename, sizeof(state->exe_filename));
    state->exe_path_length = state->exe_filename_length;

    for (u32 char_index = state->exe_path_length - 1; char_index > 0; --char_index)
    {
        if (*(state->exe_filename + char_index) == '\\')
        {
            state->exe_path_length = char_index + 1;
            break;
        }
    }

    char streams_filename[] = "streams.txt";
    win32_build_filename(state->exe_filename, state->exe_path_length,
                         streams_filename, array_count(streams_filename),
                         state->streams_filename, MAX_FILENAME_SIZE);
}

static PLATFORM_UNLOAD_FILE(win32_unload_file)
{
    if (file.contents)
    {
        VirtualFree(file.contents, 0, MEM_RELEASE);
        file.contents = 0;
    }
}

static PLATFORM_LOAD_FILE(win32_load_file)
{
    LoadedFile file = {0};
    HANDLE handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (handle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER large_file_size;
        if (GetFileSizeEx(handle, &large_file_size))
        {
            u32 file_size = (u32)large_file_size.QuadPart;
            file.contents = win32_allocate(file_size);
            if (file.contents)
            {
                u32 bytes_read;
                if (ReadFile(handle, file.contents, file_size, (DWORD *)&bytes_read, 0) && file_size == bytes_read)
                {
                    file.size = file_size;
                }
                else
                {
                    win32_unload_file(file);
                }
            }
        }
    }
    return file;
}

static void win32_init_tray_icon(Win32Window *window)
{
    NOTIFYICONDATA notify_icon = {0};
    notify_icon.cbSize = sizeof(notify_icon);
    notify_icon.hWnd = window->hwnd;
    notify_icon.uFlags = NIF_ICON | NIF_MESSAGE;
    notify_icon.uCallbackMessage = TRAY_ICON_MESSAGE;
    notify_icon.hIcon = LoadIcon(GetModuleHandleA(0), MAKEINTRESOURCE(101));
    notify_icon.szTip[0] = '\0';

    Shell_NotifyIcon(NIM_ADD, &notify_icon);
}

int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, char *cmd_line, int cmd_show)
{
    Win32State *state = global_win32_state;

    platform.show_notification = win32_show_notification;
    platform.load_file = win32_load_file;
    platform.unload_file = win32_unload_file;

    state->window.class_name = "WhosAliveWindowClassName";
    state->window.title = "WhosAlive";
    state->window.wnd_proc = win32_window_proc;
    win32_init_window(&state->window);

    POINT zero = {0};
    HMONITOR monitor = MonitorFromPoint(zero, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitor_info = { sizeof(monitor_info) };
    GetMonitorInfo(monitor, &monitor_info);

    state->overlay.width = 350;
    state->overlay.height = 80;
    state->overlay.x = monitor_info.rcWork.right - state->overlay.width - 20;
    state->overlay.y = monitor_info.rcWork.bottom - state->overlay.height - 20;
    state->overlay.style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    state->overlay.class_name = "WhosAliveOverlayClassName";
    state->overlay.wnd_proc = win32_overlay_window_proc;
    state->overlay.popup = true;
    win32_init_window(&state->overlay);

    global_dib_section = win32_create_dib_section(&state->overlay);
    global_font_22 = win32_create_font("Arial", 22);
    global_font_16 = win32_create_font("Arial", 16);

    win32_init_tray_icon(&state->window);

    win32_init_exe_path(state);

    init_streams(state->streams_filename);

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
