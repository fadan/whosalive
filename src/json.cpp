
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
        token->parent = -1;
    }
    return token;
}   

static i32 json_parse(JsonParser *parser, char *json_string, u32 json_string_length)
{
    i32 num_tokens_found = parser->num_tokens;
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
                        parser->parent = parser->token_array[parser->parent].parent;

                        for (i32 token_index = parser->num_tokens - 1; token_index >= 0; --token_index)
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
                        token->parent = parser->parent;
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
                if (parser->num_tokens > 0)
                {
                    JsonToken *token = &parser->token_array[parser->num_tokens - 1];
                    for ( ; ; )
                    {
                        if (token->start != -1 && token->end == -1)
                        {
                            if (token->type == type)
                            {
                                token->end = parser->at + 1;
                                parser->parent = token->parent;    
                            }
                            else
                            {
                                parser->status = JsonParserStatus_InvalidCharacter;
                                json_string_length = parser->at; // NOTE(dan): break out from the loop
                            }
                            break;
                        }

                        if (token->parent == -1)
                        {
                            break;
                        }

                        token = &parser->token_array[token->parent];
                    }
                }
                else
                {
                    parser->status = JsonParserStatus_InvalidCharacter;
                    json_string_length = parser->at; // NOTE(dan): break out from the loop
                }
            } break;

            case '\"':
            {
                i32 start = parser->at;
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
                            token->parent = parser->parent;
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
                i32 start = parser->at;
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
                        token->parent = parser->parent;

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

// NOTE(dan): iterator

static b32 json_string_token_equals(char *json_string, JsonToken *token, char *string)
{
    b32 equals = false;
    u32 length = token->end - token->start;
    char *test_string = json_string + token->start;
    if (test_string && string)
    {
        while (length && *test_string && *string && *test_string == *string)
        {
            ++test_string;
            ++string;
            --length;
        }
        equals = (!length && *string == 0);
    }
    return equals;
}

inline JsonIterator json_iterator_get(JsonParser *parser, JsonToken *from)
{
    JsonIterator iterator = {0};
    iterator.tokens = parser->token_array;
    iterator.one_past_last = parser->token_array + parser->num_tokens;

    iterator.at = iterator.one_past_last;

    i32 parent_index = 0;
    if (from)
    {
        parent_index = (i32)(from - parser->token_array);
        assert((parent_index >= 0) && ((u32)parent_index < parser->num_tokens));
    }

    for (JsonToken *token = iterator.tokens; token < iterator.one_past_last; ++token)
    {
        if (token->parent == parent_index)
        {
            iterator.at = token;
            break;
        }
    }

    iterator.parent_index = parent_index;
    return iterator;
}

inline b32 json_iterator_valid(JsonIterator iterator)
{
    b32 valid = (iterator.at && (iterator.at < iterator.one_past_last));
    return valid;
}

inline JsonIterator json_iterator_next(JsonIterator iterator)
{
    for (JsonToken *token = ++iterator.at; token < iterator.one_past_last; ++token, ++iterator.at)
    {
        if (token->parent == iterator.parent_index)
        {
            break;
        }
    }
    return iterator;
}

inline i32 json_get_token_index(JsonParser *parser, JsonToken *token)
{
    i32 index = (i32)(token - parser->token_array);    
    assert((index >= 0) && ((u32)index < parser->num_tokens));
    return index;
}

inline JsonToken *json_get_token(JsonIterator iterator)
{
    JsonToken *token = iterator.at;
    return token;
}

inline JsonToken *json_peek_next_token(JsonIterator iterator)
{
    JsonToken *token = 0;
    if ((iterator.at + 1) < iterator.one_past_last)
    {
        token = iterator.at + 1;
    }
    return token;
}
