#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "q_platform.h"
#include "sys_win32.h"

#define GLOBAL_VARIABLE static 
#define INTERNAL_LINKAGE static 

GLOBAL_VARIABLE bool g_isRunning;
GLOBAL_VARIABLE Win32ScreenBuffer g_screenBuffer;

SYS_ERROR(Win32SysError)
{
    char error[1024];
    va_list vl;
    va_start(vl, format);
    vsprintf_s(error, 1024, format, vl);
    va_end(vl);

    MessageBox(NULL, error, "Quake Error", MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
}

SYS_SET_PALETTE(Win32SetPalette)
{
    for (int i = 0; i < 256; ++i)
    {
        g_screenBuffer.bitmapInfo.bmiColors[i].rgbRed = palette[i * 3 + 0];
        g_screenBuffer.bitmapInfo.bmiColors[i].rgbGreen = palette[i * 3 + 1];
        g_screenBuffer.bitmapInfo.bmiColors[i].rgbBlue = palette[i * 3 + 2];
        g_screenBuffer.bitmapInfo.bmiColors[i].rgbReserved = 0; // must be zero
    }
}

inline U64 
Win32GetWallClock()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

inline float 
Win32GetSecondsElapsed(U64 startCounter, U64 endCounter, U64 counterFrequency)
{
    float result = (float)(endCounter - startCounter) / (float)counterFrequency;
    return result;
}

INTERNAL_LINKAGE void
Win32GetExeFileName(Win32State *state)
{
    // TODO lw: why using MAX_PATH in user facing code is a bad idea
    DWORD filePathLength = GetModuleFileNameA(0, state->exeFilePath, sizeof(state->exeFilePath));
    state->onePastLastExeFilePathSlash = state->exeFilePath;
    for (char *scan = state->exeFilePath; *scan != '\0'; ++scan)
    {
        if (*scan == '\\')
        {
            state->onePastLastExeFilePathSlash = scan + 1;
        }
    }
}

INTERNAL_LINKAGE void
CatString(char *src0, size_t src0Count, 
          char *src1, size_t src1Count,
          char *dest, size_t destCount)
{
    int destIndex = 0;

    for (int i = 0; i < src0Count; ++i)
    {
        if (destIndex == destCount - 1 || src0[i] == '\0')
        {
            dest[destIndex] = '\0';
            return ;
        }
        else 
        {
            dest[destIndex++] = src0[i];
        }
    }

    for (int i = 0; i < src1Count; ++i)
    {
        if (destIndex == destCount - 1 || src1[i] == '\0')
        {
            dest[destIndex] = '\0';
            return ;
        }
        else
        {
            dest[destIndex++] = src1[i];
        }
    }

	dest[destIndex] = '\0';
}

INTERNAL_LINKAGE int
StringLength(char *src)
{
    int length = 0;
    char *scan = src;
    while (*scan != '\0')
    {
        length++;
        scan++;
    }
    return length;
}

INTERNAL_LINKAGE void
Win32BuildGameFilePath(Win32State *state, char *filename, 
                       char *dest, int destSize)
{
    CatString(state->exeFilePath, 
              state->onePastLastExeFilePathSlash - state->exeFilePath,
              filename, StringLength(filename),
              dest, destSize);
}

INTERNAL_LINKAGE Win32WindowSize 
Win32GetWindowSize(HWND window)
{
    RECT clientRect;
    GetClientRect(window, &clientRect);
    Win32WindowSize result = 
        {clientRect.right -clientRect.left, 
            clientRect.bottom -clientRect.top};
    return result;
}

inline FILETIME
Win32GetLastWriteTime(char *filename)
{
    FILETIME lastTime = { };
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesEx(filename, GetFileExInfoStandard, &data))
    {
        lastTime = data.ftLastWriteTime;
    }

    return lastTime;
}

INTERNAL_LINKAGE Win32GameCode
Win32LoadGameCode(char *sourceDLLName, char *tempDLLName, char *lockFileName)
{
    Win32GameCode result = { };

    WIN32_FILE_ATTRIBUTE_DATA dummy;
    if (!GetFileAttributesEx(lockFileName, GetFileExInfoStandard, &dummy))
    {
        result.lastWriteTime = Win32GetLastWriteTime(sourceDLLName);

        if (!CopyFile(sourceDLLName, tempDLLName, FALSE))
        {
            Win32SysError("Copy DLL failed!");
        }

        result.gameCodeDLL = LoadLibraryA(tempDLLName);
        if (result.gameCodeDLL)
        {
            result.GameInit = (GameInit_t *)
                GetProcAddress(result.gameCodeDLL, "GameInit");

            result.GameUpdateAndRender = (GameUpdateAndRender_t *)
                GetProcAddress(result.gameCodeDLL, "GameUpdateAndRender");

            result.isValid = (result.GameInit && result.GameUpdateAndRender);
        }
        else
        {
            Win32SysError("Can't Load Game DLL!");
        }

        if (!result.isValid)
        {
            result.GameInit = GameInit_stub;
            result.GameUpdateAndRender = GameUpdateAndRender_stub;

            Win32SysError("Can't Load Game Functions!");
        }
    }

    return result;
}

INTERNAL_LINKAGE void
Win32UnloadGameCode(Win32GameCode *gameCode)
{
    if (gameCode->gameCodeDLL)
    {
        FreeLibrary(gameCode->gameCodeDLL);
        gameCode->gameCodeDLL = NULL;
    }

    gameCode->isValid = false;
    gameCode->GameInit = NULL;
    gameCode->GameUpdateAndRender = NULL;
}

INTERNAL_LINKAGE int 
Win32DisplayBufferInWindow(HDC deviceContext, Win32ScreenBuffer *screenBuffer, Win32WindowSize windowSize)
{
    int result = 0;

    int offsetX = 10;
    int offsetY = 10;

#if 0
    // top
    PatBlt(deviceContext, 0, 0, windowSize.width, offsetY, BLACKNESS);
    // left
    PatBlt(deviceContext, 0, 0, offsetX, windowSize.height, BLACKNESS);
    // bottom
    PatBlt(deviceContext, 0, screenBuffer->height + offsetY, 
            windowSize.width, windowSize.height, BLACKNESS);
    // right
    PatBlt(deviceContext, screenBuffer->width + offsetX, 0, 
            windowSize.width, windowSize.height, BLACKNESS);

#endif
    // drawing pixel 1-to-1 without stretch
    result = StretchDIBits(deviceContext, 
                           0, 0, screenBuffer->width, screenBuffer->height,
                           0, 0, screenBuffer->width, screenBuffer->height, 
                           screenBuffer->memory,
                           (BITMAPINFO *)&screenBuffer->bitmapInfo, 
                           DIB_RGB_COLORS, SRCCOPY);

    return result;
}

INTERNAL_LINKAGE void 
Win32ResizeDIBSection(Win32ScreenBuffer *screenBuffer, 
                      GameOffScreenBuffer *offscreenBuffer)
{
    screenBuffer->width = offscreenBuffer->width;
    screenBuffer->height = offscreenBuffer->height;
    screenBuffer->pixelBytes = offscreenBuffer->bytesPerPixel;
    screenBuffer->rowBytes = offscreenBuffer->bytesPerRow;

    screenBuffer->bitmapInfo.bmiHeader = {0};

    screenBuffer->bitmapInfo.bmiHeader.biSize = sizeof(screenBuffer->bitmapInfo.bmiHeader);
    screenBuffer->bitmapInfo.bmiHeader.biWidth = screenBuffer->width;
    screenBuffer->bitmapInfo.bmiHeader.biHeight = screenBuffer->height;
    // Negative height means top-left corner is the origin
    screenBuffer->bitmapInfo.bmiHeader.biHeight = -screenBuffer->height;
    screenBuffer->bitmapInfo.bmiHeader.biPlanes = 1;
    screenBuffer->bitmapInfo.bmiHeader.biBitCount = (WORD)(offscreenBuffer->bytesPerPixel * 8);
    screenBuffer->bitmapInfo.bmiHeader.biCompression = BI_RGB;

    screenBuffer->memory = offscreenBuffer->memory;
}

INTERNAL_LINKAGE void 
Win32ProcessPendingMessages()
{
    MSG msg;
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
    {
        switch (msg.message)
        {
            case WM_QUIT:
            {
                g_isRunning = false;
            } break;

            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            {
                U32 VKCode = (U32)msg.wParam;
                bool wasDown = (msg.lParam & (1 << 30)) != 0;
                bool isDown = (msg.lParam & (1 << 31)) == 0;
                // we do not differentiate the time of keys being holding down, 
                // only record transition
                if (wasDown != isDown)
                {
                    if (VKCode == 'W')
                    {
                    }
                    else if (VKCode == 'A')
                    {
                    }
                    else if (VKCode == 'S')
                    {
                    }
                    else if (VKCode == 'D')
                    {
                    }
                    else if (VKCode == 'I')
                    {
                    }
                    else if (VKCode == 'K')
                    {
                    }
                    else if (VKCode == 'J')
                    {
                    }
                    else if (VKCode == 'L')
                    {
                    }

                    if (isDown)
                    {
                        I32 altKeyIsDown = (msg.lParam & (1 << 29));
                        if (VKCode == VK_F4 && altKeyIsDown)
                        {
                            g_isRunning = false;
                        }

                        if (VKCode == VK_RETURN && altKeyIsDown)
                        {
                            if (msg.hwnd)
                            {
                                // Win32ToggleFullScreen(msg.hwnd);
                            }
                        }
                    }
                }
            } break;

            default:
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
}

INTERNAL_LINKAGE LRESULT CALLBACK
Win32MainWindowCallback(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   LRESULT result = 0;

   switch (message)
   {
      case WM_CLOSE:
      {
         g_isRunning = false;
      } break;

      case WM_ACTIVATEAPP:
      {
         // TODO
      } break;

      case WM_DESTROY:
      {
         g_isRunning = false;
      } break;

      case WM_SETCURSOR:
      {
         result = DefWindowProcA(hwnd, message, wParam, lParam);
      } break;

      case WM_SYSKEYDOWN:
      case WM_SYSKEYUP:
      case WM_KEYDOWN:
      case WM_KEYUP:
      {
         ASSERT(!"Keyboard input come in through a non-dispatch messsage");
      } break;

      // PeekMessage does not remve WM_PAINT itself, have to handle this.
      // see MSDN page on PeekMessage
      case WM_PAINT:
      {
         PAINTSTRUCT paint;
         HDC dc = BeginPaint(hwnd, &paint);
         Win32WindowSize size = Win32GetWindowSize(hwnd);
         Win32DisplayBufferInWindow(dc, &g_screenBuffer, size);
         EndPaint(hwnd, &paint);
      } break;

      default:
      {
         result = DefWindowProcA(hwnd, message, wParam, lParam);
      } break;
   }

   return result;
}

int CALLBACK
WinMain(HINSTANCE instance, HINSTANCE preInstance, LPSTR cmdline, int showCode)
{
    WNDCLASSA windowClass = { };
    
    /* 
    For applications like games which need to draw frequently, we can specify
    CS_OWNDC to create a private Device Context only used by this application 
    window, which could avoid the overhead of retrieving DC every draw.
    
    alse see: 
       MSDN: GetDC / ReleaseDC
       Raymond Chen: What does the CS_OWNDC class style do
    */
    
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = Win32MainWindowCallback;
    // windowClass.cbClsExtra;
    // windowClass.cbWndExtra;
    windowClass.hInstance = instance;
    // windowClass.hIcon;
    windowClass.hCursor = LoadCursor(0, IDC_ARROW);
    // windowClass.hbrBackground;
    // windowClass.lpszMenuName;
    windowClass.lpszClassName = "Quake Remake";

    HWND hwnd;
    if (RegisterClassA(&windowClass))
    {
       hwnd = CreateWindowExA(0,
                              windowClass.lpszClassName,
                              "Quake Remake",
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              0,
                              0,
                              instance,
                              0);
    }
    else
    {
       // TODO lw: logging
       return 0;
    }

    if (!hwnd)
    {
       // TODO lw: logging
       return 0;
    }

    U64 counterFrequency;

    {
        LARGE_INTEGER cf;
        QueryPerformanceFrequency(&cf);
        counterFrequency = cf.QuadPart;
    }

    // TODO find out pros and cons of high system schedule resolution
    UINT timerResolution = 1; // in millisecond
    bool isSleepGranular = (timeBeginPeriod(timerResolution) == TIMERR_NOERROR);

    HDC privateDC = GetDC(hwnd);

    float targetSecondsPerFrame = 0;

    // get pfs of the screen
    {
       int refreshRate = GetDeviceCaps(privateDC, VREFRESH);
       // default to 60fps
       refreshRate = (refreshRate > 0) ? refreshRate : 60;
 
       // set the target frame rate to half of the screen frame rate
       targetSecondsPerFrame = 1.0f / (float)refreshRate * 2.0f;
    }


    Win32State win32State = { };
    Win32GetExeFileName(&win32State);

    char sourceGameDLLPath[WIN32_MAX_FILE_PATH_LENGTH];
    Win32BuildGameFilePath(&win32State, "q_game.dll", 
            sourceGameDLLPath, sizeof(sourceGameDLLPath));

    char tempGameDLLPath[WIN32_MAX_FILE_PATH_LENGTH];
    Win32BuildGameFilePath(&win32State, "q_temp_game.dll", 
            tempGameDLLPath, sizeof(tempGameDLLPath));

    char gameCodeLockPath[WIN32_MAX_FILE_PATH_LENGTH];
    Win32BuildGameFilePath(&win32State, "lock.tmp",
            gameCodeLockPath, sizeof(gameCodeLockPath));

    GameMemory gameMemory = {};

    gameMemory.gameMemory = malloc(MegaBytes(64));
    gameMemory.gameMemorySize = MegaBytes(64);

    gameMemory.platformAPI.SysError = Win32SysError;
    gameMemory.platformAPI.SysSetPalette = Win32SetPalette;

    Win32BuildGameFilePath(&win32State, "..\\assets\\", 
            gameMemory.gameAssetDir, sizeof(gameMemory.gameAssetDir));

    Win32GameCode gameCode = Win32LoadGameCode(sourceGameDLLPath,
                                               tempGameDLLPath,
                                               gameCodeLockPath);

    GameInput gameInput[2] = {0};

    U64 startCounter = Win32GetWallClock();

    // set offscreen buffer size
    gameMemory.offscreenBuffer.width = 640;
    gameMemory.offscreenBuffer.height = 480;
    gameMemory.offscreenBuffer.bytesPerPixel = 1;
    int widthbytes = gameMemory.offscreenBuffer.width * gameMemory.offscreenBuffer.bytesPerPixel;
    gameMemory.offscreenBuffer.bytesPerRow = (widthbytes + sizeof(LONG) - 1) & ~(sizeof(LONG) -1);

    gameCode.GameInit(&gameMemory);

    Win32ResizeDIBSection(&g_screenBuffer, &gameMemory.offscreenBuffer);

    // move the window to the center of the screen
    {
        RECT rect = {0};
        const HWND hDesktop = GetDesktopWindow();
        GetWindowRect(hDesktop, &rect);
        int left = (rect.right - g_screenBuffer.width) / 2;
        int top = (rect.bottom - g_screenBuffer.height) / 2;
        rect.left = left;
        rect.top = top;
        rect.right = g_screenBuffer.width + rect.left;
        rect.bottom = g_screenBuffer.height + rect.top;
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        // TODO lw: We could make the window resizable, because the frame buffer
        // and the z-buffer are allocated at high hunk. Resize them won't affect
        // other memory.
        SetWindowPos(hwnd, HWND_TOP, rect.left, rect.top, 
                     rect.right - rect.left, rect.bottom - rect.top, 
                     SWP_SHOWWINDOW);
    }

    g_isRunning = true;
    while (g_isRunning)
    {
        Win32ProcessPendingMessages();

        U64 counter = Win32GetWallClock();
        float secondsElapsed = Win32GetSecondsElapsed(startCounter, counter, counterFrequency);

        gameCode.GameUpdateAndRender(&gameInput[0]);

#ifdef QUAKEREMAKE_INTERNAL

        // TODO lw: Game DLL stores many global variables which will be zeroed 
        // out after reloading the DLL. So hot reloading cpp won't work properly
        // for now.
        FILETIME newFileTime = Win32GetLastWriteTime(sourceGameDLLPath);
        if (CompareFileTime(&newFileTime, &gameCode.lastWriteTime) != 0)
        {
            Win32UnloadGameCode(&gameCode);
            for (int loadTryIndex = 0; !gameCode.isValid && (loadTryIndex < 100); ++loadTryIndex)
            {
                gameCode = Win32LoadGameCode(sourceGameDLLPath, tempGameDLLPath, gameCodeLockPath);
                Sleep(100);
            }
        }

#endif

        if (secondsElapsed < targetSecondsPerFrame)
        {
            DWORD sleepMilliseconds = (DWORD)(1000.0f * (targetSecondsPerFrame - secondsElapsed));

            if (sleepMilliseconds)
            {
                // TODO lw: find a better way to do this
                Sleep(sleepMilliseconds - 4);
            }

            U64 testCounter = Win32GetWallClock();
            float testSecondsElapsed = Win32GetSecondsElapsed(startCounter, testCounter, counterFrequency);

            if (testSecondsElapsed > targetSecondsPerFrame)
            {
                OutputDebugStringA("Slept over one frame!\n");
            }

            while (testSecondsElapsed < targetSecondsPerFrame)
            {
                testCounter = Win32GetWallClock();
                testSecondsElapsed = Win32GetSecondsElapsed(startCounter, testCounter, counterFrequency);
            }
        }

        U64 endCounter = Win32GetWallClock();
        float millisecPerFrame = 1000.0f * Win32GetSecondsElapsed(startCounter, endCounter, counterFrequency);
        startCounter = endCounter;

        Win32WindowSize windowSize = Win32GetWindowSize(hwnd);
        Win32DisplayBufferInWindow(privateDC, &g_screenBuffer, windowSize);
    }

   return 0;
}
