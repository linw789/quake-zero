#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "q_platform.h"
#include "sys_win32.h"

#define GLOBAL_VARIABLE static 
#define INTERNAL_LINKAGE static 

INTERNAL_LINKAGE bool g_running;
INTERNAL_LINKAGE Win32State g_win32_state;
INTERNAL_LINKAGE Win32ScreenBuffer g_screenBuffer;
INTERNAL_LINKAGE GameInput g_game_input; 

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
Win32CatString(char *src0, int src0Count, 
               char *src1, int src1Count,
               char *dest, int destCount)
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
    Win32CatString(state->exeFilePath, 
                   (unsigned int)(state->onePastLastExeFilePathSlash - state->exeFilePath),
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
Win32DisplayBufferInWindow(HDC deviceContext, Win32ScreenBuffer *screenBuffer, Win32WindowSize window_size)
{
    int result = 0;

    int offsetX = 10;
    int offsetY = 10;

#if 0
    // top
    PatBlt(deviceContext, 0, 0, window_size.width, offsetY, BLACKNESS);
    // left
    PatBlt(deviceContext, 0, 0, offsetX, window_size.height, BLACKNESS);
    // bottom
    PatBlt(deviceContext, 0, screenBuffer->height + offsetY, 
            windowSize.width, windowSize.height, BLACKNESS);
    // right
    PatBlt(deviceContext, screenBuffer->width + offsetX, 0, 
            windowSize.width, windowSize.height, BLACKNESS);

#endif
    result = StretchDIBits(deviceContext, 
                           0, 0, window_size.width, window_size.height,
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
    // Negative height means top-left corner is the origin
    screenBuffer->bitmapInfo.bmiHeader.biHeight = -screenBuffer->height;
    screenBuffer->bitmapInfo.bmiHeader.biPlanes = 1;
    screenBuffer->bitmapInfo.bmiHeader.biBitCount = (WORD)(offscreenBuffer->bytesPerPixel * 8);
    screenBuffer->bitmapInfo.bmiHeader.biCompression = BI_RGB;

    screenBuffer->memory = offscreenBuffer->memory;
}

INTERNAL_LINKAGE U32
Win32MapKey(LPARAM lparam)
{
    U32 key = (lparam >> 16) & 255;
    if (key > 127)
    {
        return 0;
    }
    key = g_scantokey[key];
    return key;
}

INTERNAL_LINKAGE void
Win32PostKeyEvent(U32 key, U32 is_down)
{
    if (key == K_ESCAPE)
    {
        g_running = false;
    }
    g_game_input.key_events[g_game_input.kevt_count].key = (U8)key;
    g_game_input.key_events[g_game_input.kevt_count].is_down = (U8)is_down;
    g_game_input.kevt_count++;
}

INTERNAL_LINKAGE void 
Win32ProcessPendingMessages()
{
    g_game_input.kevt_count = 0;

    MSG msg;
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
    {
        switch (msg.message)
        {
            case WM_QUIT:
            {
                g_running = false;
            } break;

            case WM_SYSKEYUP:
            case WM_KEYUP:
            {
                Win32PostKeyEvent(Win32MapKey(msg.lParam), 0);
            } break;
            case WM_SYSKEYDOWN:
            case WM_KEYDOWN:
            {
                Win32PostKeyEvent(Win32MapKey(msg.lParam), 1);
            } break;

            default:
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
}

INTERNAL_LINKAGE void
Win32ProcessMouseMove(bool has_focus, RECT win_rect, MouseState *mouse)
{
    if (has_focus)
    {
        POINT mouse_point = {0};
        GetCursorPos(&mouse_point);

        int win_center_x = (win_rect.left + win_rect.right) / 2;
        int win_center_y = (win_rect.top + win_rect.bottom) / 2;

        mouse->delta_x = mouse_point.x - win_center_x;
        mouse->delta_y = mouse_point.y - win_center_y;

        // TODO lw: this is a hack because somehow calling GetCursorPos 
        // immediately after SetCursorPos returns a positions that's one pixel
        // off on either x or y from what's been set.
        if (mouse->delta_x ==1 || mouse->delta_x == -1)
        {
            mouse->delta_x = 0;
        }
        if (mouse->delta_y ==1 || mouse->delta_y == -1)
        {
            mouse->delta_y = 0;
        }

        SetCursorPos(win_center_x, win_center_y);
        // GetCursorPos(&mouse_point); // one pixel off

        mouse->old_x = mouse_point.x;
        mouse->old_y = mouse_point.y;
    }
    else
    {
        mouse->delta_x = 0;
        mouse->delta_y = 0;
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
         g_running = false;
      } break;

      case WM_ACTIVATEAPP:
      {
         // TODO
      } break;

      case WM_SIZE:
      {
          switch (wParam)
          {
              case SIZE_MINIMIZED:
              {
                  g_win32_state.has_focus = 1;

              } break;

              case SIZE_MAXIMIZED:
              {
                  g_win32_state.has_focus = 0;
              } break; 
          }
      } break;

      case WM_SETFOCUS:
      {
          g_win32_state.has_focus = 1;
      } break;

      case WM_KILLFOCUS:
      {
          g_win32_state.has_focus = 0;
      } break;

      case WM_DESTROY:
      {
         g_running = false;
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
    windowClass.lpszClassName = "Quake Zero";

    HWND hwnd;
    if (RegisterClassA(&windowClass))
    {
       hwnd = CreateWindowExA(0,
                              windowClass.lpszClassName,
                              "Quake Zero",
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


    Win32GetExeFileName(&g_win32_state);

    char sourceGameDLLPath[WIN32_MAX_FILE_PATH_LENGTH];
    Win32BuildGameFilePath(&g_win32_state, "q_game.dll", 
            sourceGameDLLPath, sizeof(sourceGameDLLPath));

    char tempGameDLLPath[WIN32_MAX_FILE_PATH_LENGTH];
    Win32BuildGameFilePath(&g_win32_state, "q_temp_game.dll", 
            tempGameDLLPath, sizeof(tempGameDLLPath));

    char gameCodeLockPath[WIN32_MAX_FILE_PATH_LENGTH];
    Win32BuildGameFilePath(&g_win32_state, "lock.tmp",
            gameCodeLockPath, sizeof(gameCodeLockPath));

    GameMemory gameMemory = {};

    gameMemory.targetSecondsPerFrame = targetSecondsPerFrame;

    gameMemory.gameMemory = malloc(MEGA_BYTES(64));
    gameMemory.gameMemorySize = MEGA_BYTES(64);

    gameMemory.platformAPI.SysError = Win32SysError;
    gameMemory.platformAPI.SysSetPalette = Win32SetPalette;

    Win32BuildGameFilePath(&g_win32_state, "..\\assets\\", 
            gameMemory.gameAssetDir, sizeof(gameMemory.gameAssetDir));

    Win32GameCode gameCode = Win32LoadGameCode(sourceGameDLLPath,
                                               tempGameDLLPath,
                                               gameCodeLockPath);

    GameInput gameInput[2] = {0};

    U64 startCounter = Win32GetWallClock();

    // set offscreen buffer size
    gameMemory.offscreenBuffer.width = 320;
    gameMemory.offscreenBuffer.height = 240;
    gameMemory.offscreenBuffer.bytesPerPixel = 1;
    int widthbytes = gameMemory.offscreenBuffer.width * gameMemory.offscreenBuffer.bytesPerPixel;
    gameMemory.offscreenBuffer.bytesPerRow = (widthbytes + sizeof(LONG) - 1) & ~(sizeof(LONG) -1);

    gameCode.GameInit(&gameMemory);

    Win32ResizeDIBSection(&g_screenBuffer, &gameMemory.offscreenBuffer);

    // move the window to the center of the screen
    {
        int window_width = 640; 
        int window_height = 480; 
        RECT rect = {0};
        const HWND hDesktop = GetDesktopWindow();
        GetWindowRect(hDesktop, &rect);
        int left = (rect.right - window_width) / 2;
        int top = (rect.bottom - window_height) / 2;
        rect.left = left;
        rect.top = top;
        rect.right = window_width + rect.left - 1;
        rect.bottom = window_height + rect.top - 1;
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        // TODO lw: We could make the window resizable, because the frame buffer
        // and the z-buffer are allocated at high hunk. Resize them won't affect
        // other memory.
        SetWindowPos(hwnd, HWND_TOP, rect.left, rect.top, 
                     rect.right - rect.left, rect.bottom - rect.top, 
                     SWP_SHOWWINDOW);

        RECT window_rect = {0};

        GetClientRect(hwnd, &rect);
        POINT point = {0};
        ClientToScreen(hwnd, &point);

        window_rect.left = point.x;
        window_rect.top = point.y;

        point = {rect.right, rect.bottom};
        ClientToScreen(hwnd, &point);

        window_rect.right = point.x;
        window_rect.bottom = point.y;

        ShowCursor(FALSE);
        // TODO lw: after switching windows, cursor won't be clipped
        ClipCursor(&window_rect);
        SetCursorPos((window_rect.left + window_rect.right) / 2,
                     (window_rect.top + window_rect.bottom) / 2);

        g_game_input.mouse.old_x = (window_rect.left + window_rect.right) / 2;
        g_game_input.mouse.old_y = (window_rect.top + window_rect.bottom) / 2;

        g_win32_state.window_size = window_rect;
    }

    g_win32_state.has_focus = 1;

    g_running = true;
    while (g_running)
    {
        Win32ProcessPendingMessages();
        Win32ProcessMouseMove(g_win32_state.has_focus, g_win32_state.window_size, &g_game_input.mouse);

        U64 counter = Win32GetWallClock();

        gameCode.GameUpdateAndRender(&g_game_input);

        float secondsElapsed = Win32GetSecondsElapsed(startCounter, counter, counterFrequency);
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
            DWORD sleepMilliseconds = (DWORD)(1000.0f * (targetSecondsPerFrame - secondsElapsed)) - 4;

            // t <= 0 will cause the app to sleep forever
            if (sleepMilliseconds)
            {
                static char tempbuffer[64];
                sprintf_s(tempbuffer, 64, "Sleep for %d milliseconds\n", sleepMilliseconds);
                OutputDebugStringA(tempbuffer);

                // TODO lw: find a better way to do this
                Sleep(sleepMilliseconds);
            }
            OutputDebugStringA("Wake up\n");

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
