#include "q_platform.h"
#include "q_common.cpp"
#include "q_model.cpp"
#include "q_render.cpp"

Model *g_worldModel;

void AllocRenderBuffer(RenderBuffer *renderBuffer, GameOffScreenBuffer *offscreenBuffer)
{
    renderBuffer->width = offscreenBuffer->width;
    renderBuffer->height = offscreenBuffer->height;
    renderBuffer->bytesPerPixel = offscreenBuffer->bytesPerPixel;
    renderBuffer->bytesPerRow = offscreenBuffer->bytesPerRow;

    int pixelBufferSize = renderBuffer->bytesPerRow * renderBuffer->height;

    int zBufferSize = renderBuffer->width * sizeof(*renderBuffer->zbuffer) * renderBuffer->height;
    
    renderBuffer->backbuffer = (U8 *)HunkHighAlloc(pixelBufferSize, "renderbuffer");
    renderBuffer->zbuffer = (float *)HunkHighAlloc(zBufferSize, "zbuffer");

    offscreenBuffer->memory = renderBuffer->backbuffer;
}

extern "C" GAME_INIT(GameInit)
{
    g_platformAPI = memory->platformAPI;

    MemoryInit(memory->gameMemory, memory->gameMemorySize);
    
    FileSystemInit(memory->gameAssetDir);

    AllocRenderBuffer(&g_renderBuffer, &memory->offscreenBuffer);

    g_renderBuffer.colorPalette = FileLoadToLowHunk("gfx/palette.lmp");
    g_platformAPI.SysSetPalette(g_renderBuffer.colorPalette);

    TextureCreateDefault(g_defaultTexture);

    ModelInit();

    g_worldModel = ModelLoadForName("maps/e1m3.bsp");

    CvarSet("moveforward", 0.0f);
    CvarSet("movebackward", 0.0f);

    g_camera.position = {-735.968750f, -1591.96875f, 110.031250f};
    // x right, y forward, z up
    g_camera.angles = {0, 0.0f, -90.0f};
    AngleVectors(g_camera.angles, &g_camera.rotx, &g_camera.roty, &g_camera.rotz);

    Recti screenRect = {0, 0, 640, 480};
    float fovx = 90.0f;

    g_renderdata.worldModel = g_worldModel;

    ResetCamera(&g_camera, screenRect, fovx);

    RenderInit();
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Vec3f forward = g_camera.roty;
    //forward.z = 0;
    forward = Vec3Normalize(forward);
    Vec3f right = g_camera.rotx;
    //right.z = 0;
    right = Vec3Normalize(right);
    for (I32 i = 0; i < game_input->kevt_count; ++i)
    {
        KeyState key = game_input->key_events[i];
        if (key.key == 'w' && key.is_down)
        {
            g_camera.position += forward * 3.0f;
        }
        else if (key.key == 's' && key.is_down)
        {
            g_camera.position -= forward * 3.0f;
        }
        else if (key.key == 'a' && key.is_down)
        {
            g_camera.position -= right * 3.0f;
        }
        else if (key.key == 'd' && key.is_down)
        {
            g_camera.position += right * 3.0f;
        }
    }

    float degree = g_camera.angles.z;
    degree += game_input->mouse.delta_x * 0.05f;
    if (degree > 360.0f)
    {
        degree = degree - 360.0f;
    }
    if (degree < -360.0f)
    {
        degree = degree + 360.0f;
    }
    g_camera.angles.z = degree;
    
    degree = g_camera.angles.x;
    degree -= game_input->mouse.delta_y * 0.05f;
    if (degree > 85.0f)
    {
        degree = 85.0f;
    }
    if (degree < -85.0f)
    {
        degree = -85.0f;
    }

    g_camera.angles.x = degree;
    AngleVectors(g_camera.angles, &g_camera.rotx, &g_camera.roty, &g_camera.rotz);

    RenderView();
}
