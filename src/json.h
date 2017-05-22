
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
    i32 start;
    i32 end;
    i32 size;
    i32 parent;
};

struct JsonParser
{
    JsonToken *token_array;
    u32 token_array_count;
    u32 num_tokens;

    u32 at;
    i32 parent;

    JsonParserStatus status;
};

struct JsonIterator
{
    JsonToken *tokens;
    JsonToken *at;
    JsonToken *one_past_last;
    i32 parent_index;
};
