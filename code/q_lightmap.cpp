#include "q_model.h"
#include "q_lightmap.h"

#define SURFACE_CACHE_GUARD 0x4c575343 // 'LWSC'
#define SURFACE_CACHE_GUARD_SIZE 4

#define COLOR_SHADE_BITS 6
#define COLOR_SHADE_GRADE (1 << COLOR_SHADE_BITS)

LightSystem g_lightsystem;

//======================================
// Memory management for surface caching
//======================================

struct SurfaceCacheMemory
{
    SurfaceCache *base;
    SurfaceCache *rover;
    I32 size;
};

SurfaceCacheMemory g_surfcache_memory;

I32 SurfaceCacheGetSizeForResolution(I32 width, I32 height)
{
    I32 size = width * height * 16;
    return size;
}

void SurfaceCacheCheckCacheGuard()
{
    U8 *s = (U8 *)g_surfcache_memory.base + g_surfcache_memory.size;
    if (*((I32 *)s) != SURFACE_CACHE_GUARD)
    {
        g_platformAPI.SysError("Surface cache is corrupted!");
    }
}

void SurfaceCacheSetCacheGuard()
{
    U8 *s = (U8 *)g_surfcache_memory.base + g_surfcache_memory.size;
    *((I32 *)s) = SURFACE_CACHE_GUARD;
}

void SurfaceCacheFlush()
{
    if (!g_surfcache_memory.base)
    {
        return ;
    }
    for (SurfaceCache *sc = g_surfcache_memory.base; sc != NULL; sc = sc->next)
    {
        if (sc->owner)
        {
            *(sc->owner) = NULL;
        }
    }
    g_surfcache_memory.base->next = NULL;
    g_surfcache_memory.base->owner = NULL;
    g_surfcache_memory.base->size = g_surfcache_memory.size;
    g_surfcache_memory.rover = g_surfcache_memory.base;
}

void SurfaceCacheInit(void *buffer, I32 size)
{
    g_surfcache_memory.size = size - SURFACE_CACHE_GUARD_SIZE;
    g_surfcache_memory.base = (SurfaceCache *)buffer;
    g_surfcache_memory.rover = g_surfcache_memory.base;

    g_surfcache_memory.base->next = NULL;
    g_surfcache_memory.base->owner = NULL;
    g_surfcache_memory.base->size = g_surfcache_memory.size;

    SurfaceCacheSetCacheGuard();
}

SurfaceCache *SurfaceCacheAlloc(I32 width, I32 size)
{
    if (width < 0 || width > 256)
    {
        ASSERT(width < 256);
        ASSERT(width > 0);
        g_platformAPI.SysError("SurfaceCacheAlloc: bad width");
    }
    if (size <= 0 || size > 0x10000)
    {
        g_platformAPI.SysError("SurfaceCacheAlloc: bad size");
    }
    // SurfaceCache *tempcache = 0;
    //size_t total_size = (size_t)(&tempcache->data[size]);
    I32 total_size = size + sizeof(SurfaceCache) - 4;
    total_size = (total_size + 3) & ~3;
    if (total_size > g_surfcache_memory.size)
    {
        g_platformAPI.SysError("SurfaceCacheAlloc: %d > surface cache size", total_size);
    }

    // if there is not enough memory left, we go back to base
    if (!g_surfcache_memory.rover 
        || (U8 *)g_surfcache_memory.rover - (U8 *)g_surfcache_memory.base > g_surfcache_memory.size - total_size)
    {
        g_surfcache_memory.rover = g_surfcache_memory.base;
    }

    // find a memory block large enough
    SurfaceCache *new_cache = g_surfcache_memory.rover;
    if (new_cache->owner)
    {
        // unlink (surface->cachespots) from this surfacecache
        *(new_cache->owner) = NULL; 
    }
    while (new_cache->size < total_size)
    {
        g_surfcache_memory.rover = g_surfcache_memory.rover->next;
        if (!g_surfcache_memory.rover)
        {
            g_platformAPI.SysError("SurfaceCacheAlloc: not enough memory!");
        }
        new_cache->next = g_surfcache_memory.rover->next;
        new_cache->size += g_surfcache_memory.rover->size; 
    }

    // if new_cache is too big, carve out the rest
    if (new_cache->size - total_size > sizeof(SurfaceCache) + 256)
    {
        g_surfcache_memory.rover = (SurfaceCache *)((U8 *)new_cache + total_size);
        g_surfcache_memory.rover->size = new_cache->size - total_size;
        g_surfcache_memory.rover->next = new_cache->next;
        g_surfcache_memory.rover->owner = NULL;
        new_cache->next = g_surfcache_memory.rover;
        new_cache->size = total_size;
    }
    else
    {
        g_surfcache_memory.rover = new_cache->next;
    }

    new_cache->width = width;

    SurfaceCacheCheckCacheGuard();

    return new_cache;
}

struct LightSurface
{
    Surface *surface;
    Texture *texture;

    U8 *surface_cache_data;
    Fixed8 bright_adjusts[MAX_LIGHT_MAPS];

    I32 mip_level;
    I32 mip_width_in_texel; 
    I32 mip_height_in_texel;

    I32 lightblocks_width;
    I32 lightblocks_height;
};

void AddDynamicLights(LightSurface *lightsurf, LightSystem *lightsystem)
{
    Surface *surface = lightsurf->surface;
    TextureInfo *texinfo = surface->tex_info;

    for (I32 light_i = 0; light_i < MAX_LIGHT_NUM; ++light_i)
    {
        if ((surface->lightbits & (1 << light_i)) == 0)
        {
            continue; // not lit by this light
        }

        Light *light = lightsystem->lights + light_i;

        float light_to_surf_dist = Vec3Dot(light->position, surface->plane->normal) 
                                 - surface->plane->distance;

        // TODO lw: ???
        float dist_delta = light->radius - Absf(light_to_surf_dist);
        if (dist_delta < light->minlight)
        {
            continue;
        }
        float minlight = dist_delta - light->minlight;

        Vec3f light_on_surface_pos = light->position 
                                   - surface->plane->normal * light_to_surf_dist;

        float light_u = Vec3Dot(light_on_surface_pos, texinfo->u_axis) 
                      + texinfo->u_offset - surface->uv_min[0];

        float light_v = Vec3Dot(light_on_surface_pos, texinfo->v_axis) 
                      + texinfo->v_offset - surface->uv_min[1];

        I32 u_delta = 0, v_delta = 0;
        float dist = 0;
        // uv starts at surface->uv_min
        for (I32 v_i = 0; v_i < lightsurf->lightblocks_height; ++v_i)
        {
            v_delta = (I32)(light_v - v_i * 16);
            if (v_delta < 0)
            {
                v_delta = -v_delta;
            }

            for (I32 u_i = 0; u_i < lightsurf->lightblocks_width; ++u_i)
            {
                u_delta = (I32)(light_u - u_i * 16);
                if (u_delta < 0)
                {
                    u_delta = -u_delta;
                }
                // distance approximation?
                if (u_delta < v_delta)
                {
                    dist = (float)(u_delta + (v_delta >> 1));
                }
                else
                {
                    dist = (float)(v_delta + (u_delta >> 1));
                }

                if (dist < minlight)
                {
                    // TODO lw: ?
                    ((Fixed8 *)lightsystem->blocklights)[v_i * lightsurf->lightblocks_width + u_i] = 
                        (Fixed8)((dist_delta - dist) * 256);
                }
            }
        }
    }
}

// add calculate both static and dynamic lights
void BuildLightMap(LightSurface *lightsurf, LightSystem *lightsystem, I32 framecount)
{
    Surface *surface = lightsurf->surface;

    I32 lightsample_size = lightsurf->lightblocks_width * lightsurf->lightblocks_height;

    U8 *lightsamples = surface->samples;

    // Cvar *ambient_light = CvarGet("ambientlight");

    Fixed8 *blocklights = (Fixed8 *)lightsystem->blocklights;

    // clear to ambient
    for (I32 i = 0; i < lightsample_size; ++i)
    {
        // blocklights[i] = (U32)ambient_light->val;
        blocklights[i] = 0 << 8;
    }
    if (lightsamples)
    {
        for (I32 lightmap = 0; 
             lightmap < MAX_LIGHT_MAPS && surface->light_styles[lightmap] != 255;
             ++lightmap)
        {
            U32 bright_adjust = lightsurf->bright_adjusts[lightmap];
            for (I32 i = 0; i < lightsample_size; ++i)
            {
                // blocklights[i] += lightsamples[i] * bright_adjust;
                blocklights[i] += lightsamples[i] << 8;
            }
            lightsamples += lightsample_size;
        }
    }
    if (surface->lightframe == framecount)
    {
        AddDynamicLights(lightsurf, lightsystem);
    }

    for (I32 i = 0; i < lightsample_size; ++i)
    {
        // TODO lw: ?
        I32 t = ((255 << 8) - (I32)blocklights[i]) >> (8 - COLOR_SHADE_BITS);
        if (t < (1 << COLOR_SHADE_BITS))
        {
            t = 1 << COLOR_SHADE_BITS;
        }
        blocklights[i] = t;
    }
}

void LightTextureSurface(LightSurface *lightsurf, LightSystem *lightsystem, 
                         I32 framecount, U8 *colormap)
{
    BuildLightMap(lightsurf, lightsystem, framecount);

    Texture *texture = lightsurf->texture;
    U8 *miptex = (U8 *)texture + texture->offsets[lightsurf->mip_level];

    // sample at every 16 texel
    I32 lightsample_width = lightsurf->lightblocks_width;

    // width equals height
    I32 lightblocksize_in_texel = 16 >> lightsurf->mip_level;

    I32 mip_divshift = 4 - lightsurf->mip_level;
    // lightblock_num_h = lightsurf->mip_texel_width / lightblocksize_in_texel
    I32 lightblock_num_h = lightsurf->mip_width_in_texel >> mip_divshift;
    I32 lightblock_num_v = lightsurf->mip_height_in_texel >> mip_divshift;

    // dimensions of mipmapped texture
    I32 miptex_width = texture->width >> lightsurf->mip_level;
    I32 miptex_height = texture->height >> lightsurf->mip_level;
    I32 miptex_size = miptex_width * miptex_height;

    I32 tex_offset_u = lightsurf->surface->uv_min[0];
    I32 tex_offset_v = lightsurf->surface->uv_min[1];

    // wrap tex_offset_u around texture boundary
    // (miptex_width << 16) guarantees positive value for %
    tex_offset_u = ((tex_offset_u >> lightsurf->mip_level) + (miptex_width << 16)) % miptex_width;
    tex_offset_v = ((tex_offset_v >> lightsurf->mip_level) + (miptex_height << 16)) % miptex_height;

    U8 *tex_src = &miptex[tex_offset_v * miptex_width];
    I32 tex_src_stepback = miptex_width * miptex_height;
    U8 *tex_src_max = miptex + tex_src_stepback;

    U8 *surfcache_row = lightsurf->surface_cache_data;

    Fixed8 *blocklights = (Fixed8 *)lightsystem->blocklights;

    for (I32 u = 0; u < lightblock_num_h; ++u)
    {
        Fixed8 *lightsample_row = blocklights + u;
        U8 *surfcache_dest = surfcache_row;
        U8 *tex_src_row = tex_src + tex_offset_u;

        for (I32 v = 0; v < lightblock_num_v; ++v)
        {
            Fixed8 lightsample_left = lightsample_row[0];
            Fixed8 lightsample_right = lightsample_row[1];
            lightsample_row += lightsample_width;

            I32 lightsample_left_vstep = (lightsample_row[0] - lightsample_left) >> mip_divshift;
            I32 lightsample_right_vstep = (lightsample_row[1] - lightsample_right) >> mip_divshift;

            // bilinearly interpolate texels within the lightblock
            for (I32 y = 0; y < lightblocksize_in_texel; ++y)
            {
                I32 light_step = (lightsample_right - lightsample_left) >> mip_divshift;
                Fixed8 light_val = lightsample_left;

                for (I32 x = 0; x < lightblocksize_in_texel; ++x)
                {
                    U8 texel = tex_src_row[x];
                    // first 0-255 bits determines the color of the pixel, and 
                    //(light_val & 0xff00) detemines the shade.
                    I32 color_index = (light_val & 0xff00) + texel;
                    surfcache_dest[x] = colormap[color_index];
                    light_val += light_step;
                }

                lightsample_left += lightsample_left_vstep;
                lightsample_right += lightsample_right_vstep;
                tex_src_row += miptex_width; // move up one row
                surfcache_dest += lightsurf->mip_width_in_texel; // move up one row
            }

            if (tex_src_row >= tex_src_max)
            {
                tex_src_row -= tex_src_stepback;
            }
        }

        tex_offset_u += lightblocksize_in_texel;
        if (tex_offset_u > miptex_width)
        {
            tex_offset_u = 0;
        }

        surfcache_row += lightblocksize_in_texel;
    }
}

Texture *AnimateTexture(Texture *base_tex)
{
    return base_tex;
}

SurfaceCache *CacheSurface(Surface *surface, I32 miplevel, LightSystem *lightsystem, 
                           I32 framecount, U8 *colormap)
{
    LightSurface lightsurf;

    // if the surface is animating or flashing, flush the code
    lightsurf.texture = AnimateTexture(surface->tex_info->texture);

    lightsurf.bright_adjusts[0] = lightsystem->styles[surface->light_styles[0]].cur_value;
    lightsurf.bright_adjusts[1] = lightsystem->styles[surface->light_styles[1]].cur_value;
    lightsurf.bright_adjusts[2] = lightsystem->styles[surface->light_styles[2]].cur_value;
    lightsurf.bright_adjusts[3] = lightsystem->styles[surface->light_styles[3]].cur_value;

    // light sample at every 16 texel
    lightsurf.lightblocks_width = (surface->uv_extents[0] >> 4) + 1;
    lightsurf.lightblocks_height = (surface->uv_extents[1] >> 4) + 1;

    SurfaceCache *surface_cache = surface->cachespots[miplevel];

    // check if cache is still valid
    // TODO lw: why surface->lightframe != frameount?
    if (surface_cache && !surface_cache->dlight && surface->lightframe != framecount 
        && surface_cache->bright_adjusts[0] == lightsurf.bright_adjusts[0]
        && surface_cache->bright_adjusts[1] == lightsurf.bright_adjusts[1]
        && surface_cache->bright_adjusts[2] == lightsurf.bright_adjusts[2]
        && surface_cache->bright_adjusts[3] == lightsurf.bright_adjusts[3])
    {
        return surface_cache;
    }

    float surf_scale = 1.0f / (1 << miplevel);
    lightsurf.mip_level = miplevel;
    // surface cache's width and height are mip adjusted and cropped to surface size
    lightsurf.mip_width_in_texel = surface->uv_extents[0] >> miplevel;
    lightsurf.mip_height_in_texel = surface->uv_extents[1] >> miplevel;

    if (!surface_cache)
    {
        I32 total_size = lightsurf.mip_width_in_texel * lightsurf.mip_height_in_texel;
        surface_cache = SurfaceCacheAlloc(lightsurf.mip_width_in_texel, total_size);

        surface_cache->height = lightsurf.mip_height_in_texel;
        surface->cachespots[miplevel] = surface_cache;
        surface_cache->owner = &(surface->cachespots[miplevel]);
        // surface_cache->mipscale = surf_scale;
    }

    surface_cache->dlight = surface->lightframe == framecount ? 1 : 0; // TODO lw: ?
    lightsurf.surface_cache_data = surface_cache->data;

    surface_cache->bright_adjusts[0] = lightsurf.bright_adjusts[0];
    surface_cache->bright_adjusts[1] = lightsurf.bright_adjusts[1];
    surface_cache->bright_adjusts[2] = lightsurf.bright_adjusts[2];
    surface_cache->bright_adjusts[3] = lightsurf.bright_adjusts[3];

    lightsurf.surface = surface;
    LightTextureSurface(&lightsurf, lightsystem, framecount, colormap);

    return surface_cache;
}

void AnimateLights(LightSystem *lightsystem, double time)
{
    I32 t = (I32)(time * 10.0);
    for (I32 i = 0; i < MAX_LIGHT_STYLE_NUM; ++i)
    {
        LightStyle *style = lightsystem->styles + i;
        if (style->length)
        {
            I32 k = t % style->length;
            k = style->wave[k] - 'a';
            k *= 22; // TODO lw: ?
            style->cur_value = k;
        }
        else
        {
            style->cur_value = 256;
        }
    }
}

void MarkLight(Light *light, I32 lightbit, Node *node, Surface *allsurfaces, I32 light_framecount)
{
    // skip if contents is a leaf ?
    if (node->contents < 0)
    {
        return ;
    }

    Plane *splitplane = node->plane;
    float d = Vec3Dot(light->position, splitplane->normal) - splitplane->distance;

    // light is at normal side, but too far fram the plane
    if (d > light->radius)
    {
        MarkLight(light, lightbit, node->children[1], allsurfaces, light_framecount);
        return ;
    }
    // light is at opposite normal side, but too far from the plane
    if (d < -light->radius)
    {
        MarkLight(light, lightbit, node->children[0], allsurfaces, light_framecount);
        return ;
    }

    // mark all surfaces on this plane as being affected by the light
    Surface *surface = allsurfaces + node->firstsurface;
    for (I32 i = 0; i < node->numsurface; ++i)
    {
        // Clear lightbits from previous frame first
        if (surface->lightframe != light_framecount)
        {
            surface->lightbits = 0;
            surface->lightframe = light_framecount;
        }
        surface->lightbits |= lightbit;
    }

    MarkLight(light, lightbit, node->children[1], allsurfaces, light_framecount);
    MarkLight(light, lightbit, node->children[0], allsurfaces, light_framecount);
}

void PushLights(LightSystem *lightsystem, float dt, Node *world_nodes, 
                I32 frame_count, Surface *allsurfaces)
{
    lightsystem->light_framecount = frame_count;
    Light *lights = lightsystem->lights;

    for (I32 i = 0; i < MAX_LIGHT_NUM; ++i)
    {
        Light *light = lights + i;

        light->time_passed += dt;
        if ((light->time_passed < light->duration) && light->radius != 0)
        {
            MarkLight(light, 1 << i, world_nodes, allsurfaces, lightsystem->light_framecount);
        }
    }
}
