#include "json.h"

#include "json.cpp"

struct Stream
{
    char name[128];
    char game[128];

    b32 online;
    b32 was_online;
};

static u32 num_streams;
static Stream streams[64];

static char *url_base = "https://api.twitch.tv/kraken/streams?channel=";
static char url[4096];

static char *check_for_streams[] =
{
    "belezogep",
    "handmadehero",
    "towelliee",
};

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

static void notify_or_update_online_stream(char *name, char *game, char *display_name)
{
    Stream *stream = get_stream_by_name(name);
    if (stream)
    {
        stream->online = true;
        if (!stream->was_online)
        {
            char title[64];
            wsprintf(title, "%s started streaming", display_name);

            char message[256];
            wsprintf(message, "Playing: %s", game);

            platform.show_notification(title, message);
        }
    }
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

static void init_url()
{
    copy_string(url_base, url);

    u32 at = string_length(url_base);

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

static void pre_update_streams()
{
    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;
        stream->online = false;
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

static void update_streams(void *data, u32 data_size)
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
                char display_name[256];

                name[0] = 0;
                game[0] = 0;
                display_name[0] = 0;

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

                            if (v && v->type == JsonType_String)
                            {
                                if (json_string_token_equals(json_string, i, "name"))
                                {
                                    char *name_src = json_string + v->start;
                                    i32 name_length = v->end - v->start;
                                    copy_string_and_null_terminate(name_src, name, name_length);
                                }
                                else if (json_string_token_equals(json_string, i, "display_name"))
                                {
                                    char *display_name_src = json_string + v->start;
                                    i32 display_name_length = v->end - v->start;
                                    copy_string_and_null_terminate(display_name_src, display_name, display_name_length);
                                }
                            }
                        }
                    }
                }

                notify_or_update_online_stream(name, game, display_name);
            }
        }
    }
}
