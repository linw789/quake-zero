#pragma once

#include <stdint.h>

typedef int8_t I8;
typedef int16_t I16;
typedef int32_t I32;
typedef int64_t I64;
typedef int B32;

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef I32 Fixed8; // 8 bits for mantissa
typedef I32 Fixed16; // 16 bits for mantissa
typedef I32 Fixed20; // 20 bits for mantissa

#define INTERNAL_LINKAGE static

#define MAX_OS_PATH_LENGTH 256

#if QUAKEREMAKE_SLOW
#define ASSERT(expression) if (!(expression)) { *((int *)0) = 0; }
#else
#define ASSERT(expression)
#endif

#define ARRAY_COUNT(array) sizeof(array) / sizeof(array[0])

#define KILO_BYTES(val) ((val) * 1024LL)
#define MEGA_BYTES(val) (KILO_BYTES(val) * 1024LL)
#define GIGA_BYTES(val) (MEGA_BYTES(val) * 1024LL)
#define TERA_BYTES(val) (GIGA_BYTES(val) * 1024LL)

inline Fixed20 FloatToFixed20(float v)
{
    // 0x100000 = 2^20
    Fixed20 result = (I32)(v * 0x100000);
    return result;
}

inline float Fixed20ToFloat(Fixed20 f)
{
    float r = (float)f / 0x100000;
    return r;
}

inline Fixed16 FloatToFixed16(float v)
{
    Fixed16 r = (I32)(v * 0x10000);
    return r;
}

inline float Fixed16ToFloat(Fixed16 v)
{
    float r = (float)v / 0x10000;
    return r;
}

struct ThreadContext
{
    int placeHolder;
};

//=== Services the platform layer provides to the game ===
   
#if QUAKEREMAKE_INTERNAL 

struct DebugReadFileResult
{
    int contentSize;
    void *content;
};

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(ThreadContext *thread, DebugReadFileResult *file)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(DebugPlatformFreeFileMemory_t);

#define DEBUG_PLATFORM_READ_WHOLE_FILE(name) DebugReadFileResult name(ThreadContext *thread, const char *filename)

typedef DEBUG_PLATFORM_READ_WHOLE_FILE(DebugPlatformReadWholeFile_t);

#define DEBUG_PLATFORM_WRITE_WHOLE_FILE(name) bool name(ThreadContext *thread, const char *filename, void *memory, U32 memorySize)

typedef DEBUG_PLATFORM_WRITE_WHOLE_FILE(DebugPlatformWriteWholeFile_t);

#endif


//========================================================

//=== Services that the game provides to the platform layer
struct GameOffScreenBuffer
{
    I32 width;
    I32 height;
    I32 bytesPerPixel;
    I32 bytesPerRow;
    void *memory;
    U8 *palette;
};

struct GameSoundOutputBuffer
{
    I32 samplesPerSecond;
    I32 sampleCount;
    I16 *samples;
};

//
// these are the key numbers that should be passed to Key_Event
//
#define	K_TAB			9
#define	K_ENTER			13
#define	K_ESCAPE		27
#define	K_SPACE			32

// normal keys should be passed as lowercased ascii

#define	K_BACKSPACE		127
#define	K_UPARROW		128
#define	K_DOWNARROW		129
#define	K_LEFTARROW		130
#define	K_RIGHTARROW	131

#define	K_ALT			132
#define	K_CTRL			133
#define	K_SHIFT			134
#define	K_F1			135
#define	K_F2			136
#define	K_F3			137
#define	K_F4			138
#define	K_F5			139
#define	K_F6			140
#define	K_F7			141
#define	K_F8			142
#define	K_F9			143
#define	K_F10			144
#define	K_F11			145
#define	K_F12			146
#define	K_INS			147
#define	K_DEL			148
#define	K_PGDN			149
#define	K_PGUP			150
#define	K_HOME			151
#define	K_END			152

#define K_PAUSE			255

struct KeyState
{
    U32 key : 8;
    U32 is_down: 8;
};

struct MouseState
{
    I32 delta_x;
    I32 delta_y;
};

//     |  player makes input  |   we process input  |   inputs take effect
//  frame0                  frame1                frame2
//  so, input is always one frame behind
struct GameInput
{
    // bits 0-8: key code, bits 9-15: 1 is key down, 0 is key up
    KeyState key_events[32];
    I32 kevt_count;

    MouseState mouse;
};

#define SYS_ERROR(name) void name(char *format, ...)
typedef SYS_ERROR(SysError_t);

#define SYS_SET_PALETTE(name) void name(U8 *palette)
typedef SYS_SET_PALETTE(SysSetPalette_t);

struct PlatformAPI
{
    SysError_t *SysError;
    SysSetPalette_t *SysSetPalette;
};

PlatformAPI g_platformAPI;

struct GameMemory
{
    void *gameMemory;
    I32 gameMemorySize;

    PlatformAPI platformAPI;

    char gameAssetDir[MAX_OS_PATH_LENGTH];

    GameOffScreenBuffer offscreenBuffer;
    float targetSecondsPerFrame;
};

#define GAME_INIT(name) void name(GameMemory *memory)
typedef GAME_INIT(GameInit_t);

GAME_INIT(GameInit_stub) { }

#define GAME_UPDATE_AND_RENDER(name) \
    void name(GameInput *game_input)

typedef GAME_UPDATE_AND_RENDER(GameUpdateAndRender_t);

GAME_UPDATE_AND_RENDER(GameUpdateAndRender_stub) { }

#define GAME_GET_SOUND_SAMPLES(name) \
    void name(ThreadContext *thread, GameMemory *gameMemory, GameSoundOutputBuffer *soundBuffer)

typedef GAME_GET_SOUND_SAMPLES(GameGetSoundSamples_t);

// GAME_GET_SOUND_SAMPLES(GameGetSoundSamples_stub) { }
