#include "json.h"

#include "json.cpp"

struct Stream
{
    u32 logo_hash;

    char channel_id[128];

    char name[128];
    char game[128];

    b32 online;
    b32 was_online;

    b32 not_exists_on_twitch;
};

static u32 num_streams;
static Stream streams[64];

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

static u32 djb2_hash(char *string)
{
    u32 hash = 5381;
    for (char *at = string; *at; ++at)
    {
        hash = ((hash << 5) + hash) + *at;
    }
    return hash;
}

static void notify_or_update_online_stream(char *name, char *game, char *display_name, char *logo_url)
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

            stream->logo_hash = djb2_hash(logo_url);
            platform.cache_logo(logo_url, stream->logo_hash);

            platform.show_notification(title, message, stream->logo_hash);
        }
    }
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
                char logo[512];
                char display_name[256];

                name[0] = 0;
                game[0] = 0;
                logo[0] = 0;
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
                                else if (json_string_token_equals(json_string, i, "logo"))
                                {
                                    char *logo_src = json_string + v->start;
                                    i32 logo_length = v->end - v->start;
                                    copy_string_and_null_terminate(logo_src, logo, logo_length);
                                }
                            }
                        }
                    }
                }

                notify_or_update_online_stream(name, game, display_name, logo);
            }
        }
    }
}

static void add_stream(char *name, u32 name_length)
{
    assert(num_streams < array_count(streams));
    Stream *stream = streams + num_streams++;
    stream->game[0] = 0;
    stream->online = false;
    stream->was_online = false;

    copy_string_and_null_terminate(name, stream->name, name_length);
}

static void load_streams(char *filename)
{
    LoadedFile file = platform.load_file(filename);
    char *data = (char *)file.contents;
    u32 begin = 0;

    while (begin < file.size)
    {
        u32 end = begin;
        while (end < file.size && data[end] != '\n' && data[end] != '\r')
        {
            ++end;
        }

        char *stream_name = data + begin;
        u32 stream_name_length = end - begin;

        if (stream_name_length > 0)
        {
            add_stream(stream_name, stream_name_length);
        }

        while (end < file.size && (data[end] == '\n' || data[end] == '\r'))
        {
            ++end;
        }

        begin = end;
    }

    platform.unload_file(file);
}

static void init_users_url(char *base_url, char *url)
{
    copy_string(base_url, url);

    u32 at = string_length(base_url);
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

static void store_id_for_stream(char *name, char *id)
{
    Stream *stream = get_stream_by_name(name);
    assert(stream);

    if (stream)
    {
        u32 id_length = string_length(id);

        assert((id_length + 1) < array_count(stream->channel_id));
        copy_string_and_null_terminate(id, stream->channel_id, id_length);
    }
}

static void query_user_ids(void *data, u32 data_size)
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
        
        if (value && value->type == JsonType_Array && json_string_token_equals(json_string, identifier, "users"))
        {
            for (JsonIterator users_iterator = json_iterator_get(&parser, value); json_iterator_valid(users_iterator); users_iterator = json_iterator_next(users_iterator))
            {
                JsonToken *user = json_get_token(users_iterator);

                char name[256];
                char id[256];

                name[0] = 0;
                id[0]   = 0;

                for (JsonIterator user_iterator = json_iterator_get(&parser, user); json_iterator_valid(user_iterator); user_iterator = json_iterator_next(user_iterator))
                {
                    JsonToken *ident = json_get_token(user_iterator);
                    JsonToken *val   = json_peek_next_token(user_iterator);

                    if (val && val->type == JsonType_String && json_string_token_equals(json_string, ident, "name"))
                    {
                        char *name_src  = json_string + val->start;
                        i32 name_length = val->end - val->start;
                        copy_string_and_null_terminate(name_src, name, name_length);
                    }
                    else if (val && val->type == JsonType_String && json_string_token_equals(json_string, ident, "_id"))
                    {
                        char *id_src  = json_string + val->start;
                        i32 id_length = val->end - val->start;
                        copy_string_and_null_terminate(id_src, id, id_length);
                    }
                }

                store_id_for_stream(name, id);
            }
        }
    }
}

static void init_streams_url(char *base_url, char *url)
{
    copy_string(base_url, url);

    u32 at = string_length(base_url);
    u32 valid_stream_count = 0;

    for (u32 stream_index = 0; stream_index < num_streams; ++stream_index)
    {
        Stream *stream = streams + stream_index;

        u32 channel_id_length = string_length(stream->channel_id);
        if (channel_id_length)
        {
            if (valid_stream_count++ != 0)
            {
                url[at++] = ',';
            }

            copy_string(stream->channel_id, url + at);
            at += channel_id_length;
        }
        else
        {
            stream->not_exists_on_twitch = true;
        }
    }
    url[at] = 0;
}
