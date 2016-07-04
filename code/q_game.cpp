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
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    U8 *row = g_renderBuffer.backbuffer;
    for (int y = 0; y < g_renderBuffer.height; ++y)
    {
        for (int x = 0; x < g_renderBuffer.width; ++x)
        {
            row[x] = 0;
        }
        row += g_renderBuffer.bytesPerRow;
    }

    g_camera.nearZ = 0.1f;
    g_camera.position = {-735.968750f, -1591.96875f, 110.031250f};
    // x right, y forward, x up
    g_camera.angles = {0, -90.0f, 0};
    AngleVectors(g_camera.angles, &g_camera.rotx, &g_camera.roty, &g_camera.rotz);

    Recti screenRect = {0, 0, 640, 480};
    float fovx = 90.0f;

    g_renderdata.worldModel = g_worldModel;

    ResetCamera(&g_camera, screenRect, fovx);

    RenderView();
}
