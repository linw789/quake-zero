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
    RECT window_size;
    bool has_focus;
};

struct Win32GameCode
{
    HMODULE gameCodeDLL;
    FILETIME lastWriteTime;

    GameInit_t *GameInit;
    GameUpdateAndRender_t *GameUpdateAndRender;

    bool isValid;
};

INTERNAL_LINKAGE 
U8 g_scantokey[128] = {
	0  ,    K_ESCAPE,     '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, 9, // 0 
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',    13 ,    K_CTRL,'a',  's',      // 1 
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'' ,    '`',    K_SHIFT,'\\',  'z',    'x',    'c',    'v',      // 2 
	'b',    'n',    'm',    ',',    '.',    '/',    K_SHIFT,'*', 
	K_ALT,' ',   0  ,    K_F1, K_F2, K_F3, K_F4, K_F5,   // 3 
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0  , K_HOME, 
	K_UPARROW,K_PGUP,'-',K_LEFTARROW,'5',K_RIGHTARROW,'+',K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11, 
	K_F12,0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 5
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 6 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0         // 7 
}; 
