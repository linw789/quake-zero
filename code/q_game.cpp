#include "q_platform.h"
#include "q_common.cpp"
#include "q_sky.cpp"
#include "q_model.cpp"
#include "q_render.cpp"

float g_target_dt; // target seconds per frame

struct MapInfo
{
    Model *model;
    Vec3f spawn_pos;
    LightStyle light_styles[MAX_LIGHT_STYLE_NUM];
};

MapInfo g_mapinfos[20];

void FillLightStyles(MapInfo *mapinfo, I32 index, const char *wave)
{
    I32 len = StringCopy(mapinfo->light_styles[index].wave, 64, wave);
    mapinfo->light_styles[index].length = len;
}

void FillMapInfos()
{
    MapInfo *mapinfo = g_mapinfos + 0;
    mapinfo->model = ModelLoadForName("maps/start.bsp");
    mapinfo->spawn_pos = {544.6f, 290.0f, 50.0f};
    FillLightStyles(mapinfo, 0, "m");
    FillLightStyles(mapinfo, 1, "mmnmmommommnonmmonqnmmo");
    FillLightStyles(mapinfo, 2, "mmnmmommommnonmmonqnmmo");
    FillLightStyles(mapinfo, 3, "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba");
    FillLightStyles(mapinfo, 4, "mmmmmaaaaammmmmaaaaaabcdefgabcdefg");
    FillLightStyles(mapinfo, 5, "mamamamamama");
    FillLightStyles(mapinfo, 7, "jklmnopqrstuvwxyzyxwvutsrqponmlkj");
    FillLightStyles(mapinfo, 8, "nmonqnmomnmomomno");
    FillLightStyles(mapinfo, 9, "mmmaaaabcdefgmmmmaaaammmaamm");
    FillLightStyles(mapinfo, 10, "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa");
    FillLightStyles(mapinfo, 11, "aaaaaaaazzzzzzzz");
    FillLightStyles(mapinfo, 12, "mmamammmmammamamaaamammma");
    FillLightStyles(mapinfo, 13, "abcdefghijklmnopqrrqponmlkjihgfedcba");
    FillLightStyles(mapinfo, 32, "m");
    FillLightStyles(mapinfo, 33, "a");
    FillLightStyles(mapinfo, 34, "a");
    FillLightStyles(mapinfo, 35, "a");
    FillLightStyles(mapinfo, 36, "a");
    FillLightStyles(mapinfo, 63, "a");

    mapinfo = g_mapinfos + 1;
    mapinfo->model = ModelLoadForName("maps/e1m1.bsp");
    mapinfo->spawn_pos = {472.281250, -352.218750, 110.031250};
    
    mapinfo = g_mapinfos + 2;
    mapinfo->model = ModelLoadForName("maps/e1m3.bsp");
    mapinfo->spawn_pos = {-735.968750f, -1591.96875f, 110.031250f};
}

void SetLightStyle(LightStyle dest[MAX_LIGHT_STYLE_NUM], LightStyle src[MAX_LIGHT_STYLE_NUM])
{
    for (I32 i = 0; i < MAX_LIGHT_STYLE_NUM; ++i)
    {
        if (src[i].length)
        {
            dest[i].length = src[i].length;
            StringCopy(dest[i].wave, 64, src[i].wave);
        }
    }
}

void SetMapInfo(MapInfo *mapinfo)
{
    g_renderdata.worldModel = mapinfo->model;
    g_camera.position = mapinfo->spawn_pos;
    g_camera.angles = {0, 0.0f, -90.0f};

    SetLightStyle(g_lightsystem.styles, g_mapinfos[0].light_styles);

    ModelInit();
}

void AllocRenderBuffer(RenderBuffer *renderBuffer, GameOffScreenBuffer *offscreenBuffer)
{
    renderBuffer->width = offscreenBuffer->width;
    renderBuffer->height = offscreenBuffer->height;
    renderBuffer->bytesPerPixel = offscreenBuffer->bytesPerPixel;
    renderBuffer->bytes_per_row = offscreenBuffer->bytesPerRow;

    I32 pixel_buffer_size = renderBuffer->bytes_per_row * renderBuffer->height;

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

    g_renderbuffer.colorPalette = FileLoadToLowHunk("gfx/palette.lmp");
    g_renderbuffer.colormap = FileLoadToLowHunk("gfx/colormap.lmp");
    RemapColorMap(g_renderbuffer.colorPalette, g_renderbuffer.colormap);

    {
        U8 new_palette[256 * 3];
        U8 gamma_table[256];

        BuildGammaTable(gamma_table, 1);
        GammaCorrect(gamma_table, new_palette, g_renderbuffer.colorPalette);
        g_platformAPI.SysSetPalette(new_palette);
    }

    g_defaultTexture = TextureCreateDefault();

    FillMapInfos();

    SetMapInfo(g_mapinfos + 0);

    // x right, y forward, z up
    AngleVectors(g_camera.angles, &g_camera.rotx, &g_camera.roty, &g_camera.rotz);

    Recti screenRect = {0, 0, 
                        memory->offscreenBuffer.width,
                        memory->offscreenBuffer.height};
    float fovx = 90.0f;

    ResetCamera(&g_camera, screenRect, fovx);

    RenderInit();

    g_target_dt = memory->targetSecondsPerFrame;
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Vec3f forward = g_camera.roty;
    //forward.z = 0;
    forward = Vec3Normalize(forward);
    Vec3f right = g_camera.rotx;
    //right.z = 0;
    right = Vec3Normalize(right);

    float move_speed = 7.0f;

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

    const float ROTATE_EPSILON = 1;

    float degree = g_camera.angles.z;
    float delta = game_input->mouse.delta_x * 0.05f;

    degree += delta;
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
    delta = game_input->mouse.delta_y * 0.05f;

    degree -= delta;
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

    RenderView(g_target_dt);
}
