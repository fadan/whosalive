
#define PLATFORM_UNDEFINED  0
#define PLATFORM_WINDOWS    1
#define PLATFORM_UNIX       2
#define PLATFORM_OSX        3

#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM    PLATFORM_WINDOWS
#elif defined(__unix__)
    #define PLATFORM    PLATFORM_UNIX
#elif defined(__APPLE__) && defined(__MACH__)
    #define PLATFORM    PLATFORM_OSX
#else
    #define PLATFORM    PLATFORM_UNDEFINED
#endif

#define COMPILER_UNDEFINED  0
#define COMPILER_MSVC       1
#define COMPILER_GCC        2
#define COMPILER_CLANG      3

#if defined(_MSC_VER)
    #define COMPILER    COMPILER_MSVC
#elif defined(__GNUC__)
    #define COMPILER    COMPILER_GCC
#elif defined(__clang__)
    #define COMPILER    COMPILER_CLANG
#else
    #define COMPILER    COMPILER_UNDEFINED
#endif

#define ARCH_32_BIT    0
#define ARCH_64_BIT    1

#if defined(_WIN64) || defined(__x86_64__) || defined(__ia64__) || defined(__LP64__)
    #define ARCH    ARCH_64_BIT
#else
    #define ARCH    ARCH_32_BIT
#endif

typedef unsigned char    u8;
typedef   signed char    i8;
typedef unsigned short  u16;
typedef   signed short  i16;
typedef unsigned int    u32;
typedef   signed int    i32;

#if COMPILER == COMPILER_MSVC
    typedef unsigned __int64    u64;
    typedef          __int64    i64;
#else
    typedef unsigned long long  u64;
    typedef          long long  i64;
#endif

typedef u8             uchar;
typedef u16            ushort;
typedef u32            uint;
typedef unsigned long  ulong;

typedef i32    b32;
typedef float  f32;
typedef double f64;

#if ARCH == ARCH_64_BIT
    typedef u64     usize;
    typedef i64     isize;
    typedef u64     uintptr;
    typedef i64     intptr;
    typedef u64     ulongptr;
    typedef i64     longptr;
#else
    typedef u32     usize;
    typedef i32     isize;
    typedef u32     uintptr;
    typedef i32     intptr;
    typedef ulong   ulongptr;
    typedef long    longptr;
#endif

typedef char test_size_u8[sizeof(u8)   == 1 ? 1 : -1];
typedef char test_size_u16[sizeof(u16) == 2 ? 1 : -1];
typedef char test_size_u32[sizeof(u32) == 4 ? 1 : -1];
typedef char test_size_u64[sizeof(u64) == 8 ? 1 : -1];
typedef char test_size_usize[sizeof(usize) == sizeof(char *) ? 1 : -1];

#if INTERNAL_BUILD
    #if COMPILER == COMPILER_MSVC
        #define assert(e) do \
            { \
                if (!(e)) \
                { \
                    __debugbreak(); \
                } \
            } while (0)
    #else
        #define assert(e) do \
            { \
                if (!(e)) \
                { \
                    *(i32 *)0 = 0; \
                } \
            } while (0)
    #endif
#else
    #define assert(e)
#endif

#define array_count(a)              (sizeof(a) / sizeof((a)[0]))

#define KB  (1024LL)
#define MB  (1024LL * KB)
#define GB  (1024LL * MB)
#define TB  (1024LL * GB)

