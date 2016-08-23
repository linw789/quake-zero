#include "q_platform.h"
#include "q_common.cpp"
#include "q_model.cpp"
#include "q_render.cpp"

Model *g_worldModel;
float g_targetSecondsPerFrame;

void AllocRenderBuffer(RenderBuffer *renderBuffer, GameOffScreenBuffer *offscreenBuffer)
{
    renderBuffer->width = offscreenBuffer->width;
    renderBuffer->height = offscreenBuffer->height;
    renderBuffer->bytesPerPixel = offscreenBuffer->bytesPerPixel;
    renderBuffer->bytesPerRow = offscreenBuffer->bytesPerRow;

    I32 pixel_buffer_size = renderBuffer->bytesPerRow * renderBuffer->height;

    I32 zbuffer_size = renderBuffer->width * sizeof(*renderBuffer->zbuffer) * renderBuffer->height;
    
    renderBuffer->backbuffer = (U8 *)HunkHighAlloc(pixel_buffer_size, "renderbuffer");
    renderBuffer->zbuffer = (float *)HunkHighAlloc(zbuffer_size, "zbuffer");

    offscreenBuffer->memory = renderBuffer->backbuffer;

    // allocate for surface caching
    I32 surface_cache_size = SurfaceCacheGetSizeForResolution(renderBuffer->width, renderBuffer->height);
    void *surface_cache = HunkHighAlloc(surface_cache_size, "surfacecache");
    SurfaceCacheInit(surface_cache, surface_cache_size);
}

extern "C" GAME_INIT(GameInit)
{
    g_platformAPI = memory->platformAPI;

    MemoryInit(memory->gameMemory, memory->gameMemorySize);
    
    FileSystemInit(memory->gameAssetDir);

    AllocRenderBuffer(&g_renderbuffer, &memory->offscreenBuffer);

    // TODO lw: gamma correction
    g_renderbuffer.colorPalette = FileLoadToLowHunk("gfx/palette.lmp");
    g_renderbuffer.colormap = FileLoadToLowHunk("gfx/colormap.lmp");
    RemapColorMap(g_renderbuffer.colorPalette, g_renderbuffer.colormap);

    g_platformAPI.SysSetPalette(g_renderbuffer.colorPalette);

    TextureCreateDefault(g_defaultTexture);

    ModelInit();

    /*
    g_worldModel = ModelLoadForName("maps/e1m3.bsp");
    g_camera.position = {-735.968750f, -1591.96875f, 110.031250f};
    g_camera.angles = {0, 0.0f, -90.0f};
    */

    g_worldModel = ModelLoadForName("maps/start.bsp");
    g_camera.position = {544.6f, 290.0f, 50.0f};
    g_camera.angles = {0, 0.0f, -90.0f};

    // x right, y forward, z up
    AngleVectors(g_camera.angles, &g_camera.rotx, &g_camera.roty, &g_camera.rotz);

    Recti screenRect = {0, 0, 320, 240};
    float fovx = 90.0f;

    g_renderdata.worldModel = g_worldModel;

    ResetCamera(&g_camera, screenRect, fovx);

    RenderInit();

    g_targetSecondsPerFrame = memory->targetSecondsPerFrame;
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Vec3f forward = g_camera.roty;
    //forward.z = 0;
    forward = Vec3Normalize(forward);
    Vec3f right = g_camera.rotx;
    //right.z = 0;
    right = Vec3Normalize(right);

    float move_speed = 5.0f;

    for (I32 i = 0; i < game_input->kevt_count; ++i)
    {
        KeyState key = game_input->key_events[i];
        if (key.key == 'w' && key.is_down)
        {
            g_camera.position += forward * move_speed;
        }
        else if (key.key == 's' && key.is_down)
        {
            g_camera.position -= forward * move_speed;
        }
        else if (key.key == 'a' && key.is_down)
        {
            g_camera.position -= right * move_speed;
        }
        else if (key.key == 'd' && key.is_down)
        {
            g_camera.position += right * move_speed;
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

    RenderView(g_targetSecondsPerFrame);
}
