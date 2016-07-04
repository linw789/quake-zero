#pragma once

struct Win32BitmapInfo
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[256];
};

struct Win32ScreenBuffer
{
    Win32BitmapInfo bitmapInfo;
    int width;
    int height;
    int pixelBytes;
    int rowBytes;
    void *memory;
};

struct Win32WindowSize
{
    int width;
    int height;
};

#define WIN32_MAX_FILE_PATH_LENGTH 256
struct Win32State
{
    char exeFilePath[WIN32_MAX_FILE_PATH_LENGTH];
    char *onePastLastExeFilePathSlash; 
};

struct Win32GameCode
{
    HMODULE gameCodeDLL;
    FILETIME lastWriteTime;

    GameInit_t *GameInit;
    GameUpdateAndRender_t *GameUpdateAndRender;

    bool isValid;
};
