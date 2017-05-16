#include "platform.h"

#include <windows.h>
#include <wininet.h>

#define UPDATE_THREAD_TIMER_ID      1
#define UPDATE_THREAD_INTERVAL_MS   (10 * 1000)
#define MAX_DOWNLOAD_SIZE           (1*MB)

struct Win32Window
{
    char *title;
    char *class_name;
    HWND hwnd;
    WNDPROC wnd_proc;
};

struct Win32State
{
    Win32Window window;
    b32 initialized;

    b32 quit_requested;
};

static Win32State global_win32_state_;
static Win32State *global_win32_state = &global_win32_state_;

static HANDLE global_update_event;

static void win32_init_window(Win32Window *window)
{
    WNDCLASSEXA window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = GetModuleHandleA(0);
    window_class.lpfnWndProc = window->wnd_proc;
    window_class.lpszClassName = (window->class_name) ? window->class_name : window->title;

    if (RegisterClassExA(&window_class))
    {
        window->hwnd = CreateWindowExA(0, window_class.lpszClassName, window->title, WS_OVERLAPPEDWINDOW, 
                                       CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                       0, 0, window_class.hInstance, 0);
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

//

enum JsonParserStatus
{
    JsonParserStatus_Uninitialized,
    JsonParserStatus_Initialized,

    JsonParserStatus_NotEnoughTokens,
    JsonParserStatus_InvalidCharacter,

    JsonParserStatus_Success,
};

enum JsonType
{
    JsonType_Undefined,

    JsonType_Object,
    JsonType_Array,
    JsonType_String,
    JsonType_Primitive,
};

struct JsonToken
{
    JsonType type;
    int start;
    int end;
    int size;
};

struct JsonParser
{
    unsigned int num_tokens;
    unsigned int token_array_count;
    JsonToken *token_array;

    unsigned int at;
    int parent;

    JsonParserStatus status;
};

inline void json_init_parser(JsonParser *parser, JsonToken *token_array, unsigned int token_array_count)
{
    parser->num_tokens = 0;
    parser->token_array_count = token_array_count;
    parser->token_array = token_array;

    parser->at = 0;
    parser->parent = -1;

    parser->status = JsonParserStatus_Initialized;
}

inline JsonToken *json_new_token(JsonParser *parser)
{
    JsonToken *token = 0;
    if (parser->num_tokens < parser->token_array_count)
    {
        token = &parser->token_array[parser->num_tokens++];
        token->start = token->end = -1;
        token->size = 0;
    }
    return token;
}   

static int json_parse(JsonParser *parser, char *json_string, unsigned int json_string_length)
{
    int num_tokens_found = parser->num_tokens;
    for ( ; parser->at < json_string_length && json_string[parser->at]; ++parser->at)
    {
        char c = json_string[parser->at];
        switch (c)
        {
            case '\t':
            case '\r':
            case '\n':
            case ' ':
            {
                // NOTE(dan): skip whitespace
            } break;

            case ':':
            {
                parser->parent = parser->num_tokens - 1;
            } break;

            case ',':
            {
                if (parser->parent != -1)
                {
                    JsonToken *parent_token = &parser->token_array[parser->parent];
                    if (parent_token->type != JsonType_Array && parent_token->type != JsonType_Object)
                    {
                        for (int token_index = parser->num_tokens - 1; token_index >= 0; --token_index)
                        {
                            JsonToken *token = &parser->token_array[token_index];
                            if (token->type == JsonType_Array || token->type == JsonType_Object)
                            {
                                if (token->start != -1 && token->end == -1)
                                {
                                    parser->parent = token_index;
                                    break;
                                }
                            }
                        }
                    }
                }
            } break;

            case '{':
            case '[':
            {
                ++num_tokens_found;

                JsonToken *token = json_new_token(parser);
                if (token)
                {
                    if (parser->parent != -1)
                    {
                        ++parser->token_array[parser->parent].size;
                    }

                    token->type = (c == '{' ? JsonType_Object : JsonType_Array);
                    token->start = parser->at;

                    parser->parent = parser->num_tokens - 1;
                }
                else
                {
                    parser->status = JsonParserStatus_NotEnoughTokens;
                    json_string_length = parser->at; // NOTE(dan): break out from the loop
                }
            } break;

            case '}':
            case ']':
            {
                JsonType type = (c == '}' ? JsonType_Object : JsonType_Array);
                int token_index;
                for (token_index = parser->num_tokens - 1; token_index >= 0; --token_index)
                {
                    JsonToken *token = &parser->token_array[token_index];
                    if (token->start != -1 && token->end == -1)
                    {
                        if (token->type == type)
                        {
                            parser->parent = -1;
                            token->end = parser->at + 1;
                        }
                        else
                        {
                            parser->status = JsonParserStatus_InvalidCharacter;
                            json_string_length = parser->at; // NOTE(dan): break out from the loop
                        }
                        break;
                    }
                }

                if (token_index < 0) // NOTE(dan): unmatched closing bracket
                {
                    parser->status = JsonParserStatus_InvalidCharacter;
                    json_string_length = parser->at; // NOTE(dan): break out from the loop
                }

                for ( ; token_index >= 0; --token_index)
                {
                    JsonToken *token = &parser->token_array[token_index];
                    if (token->start != -1 && token->end == -1)
                    {
                        parser->parent = token_index;
                        break;
                    }
                }
            } break;

            case '\"':
            {
                int start = parser->at;
                ++parser->at;

                for ( ; parser->at < json_string_length && json_string[parser->at]; ++parser->at)
                {
                    char c = json_string[parser->at];

                    if (c == '\"')
                    {
                        JsonToken *token = json_new_token(parser);
                        if (token)
                        {
                            token->type = JsonType_String;
                            token->start = start + 1;
                            token->end = parser->at;
                            token->size = 0;
                            break;
                        }
                        else
                        {
                            parser->status = JsonParserStatus_NotEnoughTokens;
                            json_string_length = parser->at; // NOTE(dan): break out from the loop
                        }
                    }

                    // TODO(dan): escape sequences
                }

                ++num_tokens_found;

                if (parser->parent != -1)
                {
                    ++parser->token_array[parser->parent].size;
                }
            } break;

            default:
            {
                // NOTE(dan): if not object, array or string, it should be primitive
                int start = parser->at;
                for ( ; parser->at < json_string_length && json_string[parser->at]; ++parser->at)
                {
                    switch (json_string[parser->at])
                    {
                        case '\t':
                        case '\r':
                        case '\n':
                        case ' ':
                        case ',':
                        case ']':
                        case '}':
                        {
                            goto found;
                        } break;
                    }

                    if (json_string[parser->at] < 32 || json_string[parser->at] >= 127)
                    {
                        parser->status = JsonParserStatus_InvalidCharacter;
                        json_string_length = parser->at; // NOTE(dan): break out from the loop
                    }
                }

            found:          
                if (parser->status != JsonParserStatus_InvalidCharacter)
                {
                    JsonToken *token = json_new_token(parser);
                    if (token)
                    {
                        token->type = JsonType_Primitive;
                        token->start = start;
                        token->end = parser->at;
                        token->size = 0;

                        --parser->at;
                        ++num_tokens_found;

                        if (parser->parent != -1)
                        {
                            ++parser->token_array[parser->parent].size;
                        }
                    }
                    else
                    {
                        parser->status = JsonParserStatus_NotEnoughTokens;
                        json_string_length = parser->at; // NOTE(dan): break out from the loop
                    }
                }
            } break;
        }
    }
    return num_tokens_found;
}

//

static void win32_parse_json(void *data, unsigned int data_size)
{
    JsonParser parser;
    JsonToken tokens[64] = {};

    json_init_parser(&parser, tokens, array_count(tokens));
    json_parse(&parser, (char *)data, data_size);


}

static void win32_update(HINTERNET internet, void *data, unsigned int data_size)
{
    char *url = "https://api.twitch.tv/kraken/streams?channel=nothings2";
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

        win32_parse_json(data, total_bytes_read);

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

int __stdcall WinMain(HINSTANCE instance, HINSTANCE prev_instance, char *cmd_line, int cmd_show)
{
    Win32State *state = global_win32_state;

    state->window.title = "WhosAlive";
    state->window.wnd_proc = win32_window_proc;
    win32_init_window(&state->window);

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
