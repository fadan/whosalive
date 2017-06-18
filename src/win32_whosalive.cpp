#include "platform.h"
#include "win32_whosalive.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT assert
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#pragma warning(disable: 4702)
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_ASSERT assert
#include "stb_image_resize.h"

#pragma warning(disable: 4996)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT assert
#include "stb_image_write.h"

#include "whosalive.cpp"

#define UPDATE_THREAD_TIMER_ID      1
#define FADER_TIMER_ID              2
#define UPDATE_THREAD_INTERVAL_MS   (60 * 1000)
#define MAX_DOWNLOAD_SIZE           (1*MB)
#define TRAY_ICON_MESSAGE           (WM_USER + 1)

#define LOGO_EXPIRES_DAYS           7
#define LOGO_EXPIRES_SECS           (LOGO_EXPIRES_DAYS * 24 * 60 * 60)
#define LOGO_EXPIRES_NS             (LOGO_EXPIRES_SECS * 10000000LL)

static char *global_headers = "Client-ID: j6dzqx92ht08vnyr1ghz0a1fdw6oss";

static Win32State global_win32_state_;
static Win32State *global_win32_state = &global_win32_state_;

static HINTERNET global_internet;
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

enum TrayIconMenuID
{
    TrayIconMenuID_CloseMenu,
    TrayIconMenuID_Exit,

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
    AppendMenu(menu, MF_UNCHECKED, TrayIconMenuID_Exit, "Exit");

    i32 cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window, 0);
    switch (cmd)
    {
        case TrayIconMenuID_Exit:
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
    HINTERNET connection = InternetOpenUrlA(internet, url, global_headers, (unsigned int)-1, INTERNET_FLAG_EXISTING_CONNECT | INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
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
    global_internet = InternetOpenA("WhosAlive", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
    if (global_internet)
    {
        void *data = win32_allocate(MAX_DOWNLOAD_SIZE);
        while (WaitForSingleObject(global_update_event, 0xFFFFFFFF) == WAIT_OBJECT_0)
        {
            win32_update(global_internet, data, MAX_DOWNLOAD_SIZE);
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

static void win32_init_paths(Win32State *state)
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

    state->temp_path_length = GetTempPath(MAX_FILENAME_SIZE, state->temp_path);

    char whosalive_dir[] = "whosalive\\";
    for (char *at = whosalive_dir; *at; ++at)
    {
        state->temp_path[state->temp_path_length++] = *at;
    }
    state->temp_path[state->temp_path_length] = '\0';

    CreateDirectory(state->temp_path, 0);
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

static PLATFORM_SHOW_NOTIFICATION(win32_show_notification)
{
    Win32DibSection dib_section = global_dib_section;
    Win32Window *window = &global_win32_state->overlay;

    RECT text_rect = {0};
    text_rect.top = 10;
    text_rect.left = 80;
    text_rect.right = global_dib_section.width - 10;
    text_rect.bottom = global_dib_section.height - 15;

    SelectObject(dib_section.dc, dib_section.bitmap);

    win32_clear(&dib_section, 0xFFFFFFFF);

    // NOTE(dan): logo

    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info);
    bitmap_info.bmiHeader.biWidth = 60;
    bitmap_info.bmiHeader.biHeight = -(long)60;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    char filename[32];
    wsprintf(filename, "%u.bmp", logo_hash);

    char path_to_file[MAX_FILENAME_SIZE];
    win32_build_filename(global_win32_state->temp_path, global_win32_state->temp_path_length,
                         filename, string_length(filename), 
                         path_to_file, array_count(path_to_file));

    LoadedFile logo = platform.load_file(path_to_file);

    HDC screen_dc = GetDC(0);
    HDC logo_dc = CreateCompatibleDC(screen_dc);
    HBITMAP logo_bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, (void **)&logo.contents, 0, 0);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(logo_dc, logo_bitmap);

    BitBlt(dib_section.dc, 10, 10, 60, 60, logo_dc, 0, 0, SRCCOPY);

    SelectObject(logo_dc, old_bitmap);
    DeleteDC(logo_dc);
    ReleaseDC(0, screen_dc);

    platform.unload_file(logo);

    // NOTE(dan): text

    SelectObject(dib_section.dc, global_font_22);
    SetTextColor(dib_section.dc, 0xFF3C3C3C);
    DrawText(dib_section.dc, title, string_length(title), &text_rect, DT_LEFT);

    TEXTMETRIC text_metric;
    GetTextMetrics(dib_section.dc, &text_metric);

    text_rect.top += text_metric.tmHeight;

    SelectObject(dib_section.dc, global_font_16);
    SetBkMode(dib_section.dc, OPAQUE);
    // SetTextColor(dib_section.dc, 0xFF666666);
    SetTextColor(dib_section.dc, 0xFFFFFF00);
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

static void win32_free(void *memory)
{
    VirtualFree(memory, 0, MEM_RELEASE);
}

static PLATFORM_UNLOAD_FILE(win32_unload_file)
{
    if (file.contents)
    {
        win32_free(file.contents);
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

static void *win32_create_logo_if_not_exists(char *path_to_file, b32 *created)
{
    *created = false;
    WIN32_FILE_ATTRIBUTE_DATA data;
    u32 creation_flags = OPEN_EXISTING;
    if (!GetFileAttributesEx(path_to_file, GetFileExInfoStandard, &data))
    {
        creation_flags = CREATE_NEW;
        *created = true;
    }
    else
    {
        FILETIME current_time;
        GetSystemTimeAsFileTime(&current_time);

        u64 last_write = ((u64)data.ftLastWriteTime.dwHighDateTime << 32) + data.ftLastWriteTime.dwLowDateTime;
        u64 now = ((u64)current_time.dwHighDateTime << 32) + current_time.dwLowDateTime;
        u64 expires = last_write + LOGO_EXPIRES_NS;

        if (expires < now)
        {
            *created = true;
            creation_flags = TRUNCATE_EXISTING;
        }
    }
    void *handle = CreateFileA(path_to_file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, creation_flags, 0, 0);
    return handle;
}

static void *global_download_buffer;

static PLATFORM_CACHE_LOGO(win32_cache_logo)
{
    char filename[32];
    wsprintf(filename, "%u.bmp", logo_hash);

    char path_to_file[MAX_FILENAME_SIZE];
    win32_build_filename(global_win32_state->temp_path, global_win32_state->temp_path_length,
                         filename, string_length(filename), 
                         path_to_file, array_count(path_to_file));

    b32 created;
    HANDLE logo = win32_create_logo_if_not_exists(path_to_file, &created);
    assert(logo);
    CloseHandle(logo);

    if (created)
    {
        HINTERNET connection = InternetOpenUrlA(global_internet, url, global_headers, (unsigned int)-1, INTERNET_FLAG_EXISTING_CONNECT | INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
        if (connection)
        {
            void *data = global_download_buffer;
            u32 total_bytes_read = 0;
            u32 bytes_to_read = MAX_DOWNLOAD_SIZE;
            u32 bytes_read;

            while (InternetReadFile(connection, (char *)data + total_bytes_read, bytes_to_read, (DWORD *)&bytes_read))
            {
                if (!bytes_read)
                {
                    break;
                }
                total_bytes_read += bytes_read;
            }
     
            int x, y, n;
            unsigned char *image = stbi_load_from_memory((unsigned char *)data, total_bytes_read, &x, &y, &n, 0);
            if (image)
            {
                unsigned char *resized_image = (unsigned char *)win32_allocate(total_bytes_read);
                stbir_resize_uint8(image, x, y, 0,
                                   resized_image, 60, 60, 0,
                                   n);

                stbi_write_bmp(path_to_file, 60, 60, n, resized_image);
                stbi_image_free(image);
                win32_free(resized_image);
            }

            InternetCloseHandle(connection);
        }
    }
}

// static PLATFORM_OPEN_TEMP_FILE(win32_open_temp_file)
// {
//     PlatformFile temp_file = {0};

//     char path_to_file[MAX_FILENAME_SIZE];
//     win32_build_filename(global_win32_state->temp_path, global_win32_state->temp_path_length,
//                          filename, string_length(filename), 
//                          path_to_file, array_count(path_to_file));

//     WIN32_FILE_ATTRIBUTE_DATA data;
//     u32 creation_flags = OPEN_EXISTING;
//     if (!GetFileAttributesEx(path_to_file, GetFileExInfoStandard, &data))
//     {
//         creation_flags = CREATE_NEW;
//         temp_file.created = true;
//     }
//     else
//     {
//         FILETIME current_time;
//         GetSystemTimeAsFileTime(&current_time);

//         u64 last_write = ((u64)data.ftLastWriteTime.dwHighDateTime << 32) + data.ftLastWriteTime.dwLowDateTime;
//         u64 now = ((u64)current_time.dwHighDateTime << 32) + current_time.dwLowDateTime;
//         u64 expires = last_write + LOGO_EXPIRES_NS;

//         if (expires < now)
//         {
//             temp_file.created = true;
//             creation_flags = TRUNCATE_EXISTING;
//         }
//     }

//     temp_file.reserved_for_platform = CreateFileA(path_to_file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, creation_flags, 0, 0);
//     assert(temp_file.reserved_for_platform != INVALID_HANDLE_VALUE);

//     return temp_file;
// }

// static void *global_download_buffer;

// static PLATFORM_DOWNLOAD_URL_TO_FILE(win32_download_url_to_file)
// {
//     b32 downloaded = false;

//     if (!global_download_buffer)
//     {
//         global_download_buffer = win32_allocate(MAX_DOWNLOAD_SIZE);
//     }

//     HINTERNET connection = InternetOpenUrlA(global_internet, url, global_headers, (unsigned int)-1, INTERNET_FLAG_EXISTING_CONNECT | INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
//     if (connection)
//     {
//         void *data = global_download_buffer;
//         u32 total_bytes_read = 0;
//         u32 bytes_to_read = MAX_DOWNLOAD_SIZE;
//         u32 bytes_read;

//         while (InternetReadFile(connection, (char *)data + total_bytes_read, bytes_to_read, (DWORD *)&bytes_read))
//         {
//             if (!bytes_read)
//             {
//                 break;
//             }
//             total_bytes_read += bytes_read;
//         }

//         int x, y, n;
//         unsigned char *image = stbi_load_from_memory((unsigned char *)data, total_bytes_read, &x, &y, &n, 0);
//         if (image)
//         {


//             // u32 bytes_written;
//             // WriteFile(file.reserved_for_platform, image, x * n * n, (DWORD *)&bytes_written, 0);

//             downloaded = true;

//             stbi_image_free(image);
//         }

//         InternetCloseHandle(connection);
//     }

//     return downloaded;
// }


int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, char *cmd_line, int cmd_show)
{
    Win32State *state = global_win32_state;

    platform.show_notification = win32_show_notification;
    platform.load_file = win32_load_file;
    platform.unload_file = win32_unload_file;
    platform.cache_logo = win32_cache_logo;

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
    global_download_buffer = win32_allocate(MAX_DOWNLOAD_SIZE);

    win32_init_tray_icon(&state->window);

    win32_init_paths(state);

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

    MessageBox(state->window.hwnd, "Exit", "Exit", MB_OK);
}
