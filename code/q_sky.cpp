#include "q_platform.h"
#include "q_model.h"
#include "q_render.h"

#define SKY_SIZE 128
#define SKY_SIZE_MASK SKY_SIZE - 1
#define SKY_TEXTURE_WIDTH SKY_SIZE * 2
#define SKY_WEIRD_NUMBER 131

struct SkyCanvas
{
    /* 
     newsky and topsky both pack in here, 128 bytes of newsky on the left of 
     each scan, 128 bytes of topsky on the right, because the low-level drawers 
     need 256-byte scan widths
    */
    U8 new_sky[SKY_SIZE * SKY_TEXTURE_WIDTH];

    /*
     Sky texture must be size of 256*128, and stores 2 skies of 128*128. In 
     front is the right sky where the black pixel represent transparency.
    */
    U8 left_sky[SKY_SIZE * SKY_WEIRD_NUMBER];
    U8 left_mask[SKY_SIZE * SKY_WEIRD_NUMBER];

    float sky_shift; // sky moving speed
};

SkyCanvas g_skycanvas;

void SkyInit(SkyCanvas *sky, Texture *texture)
{
    U8 *newsky = sky->new_sky + SKY_SIZE;
    U8 *tex_src = (U8 *)texture + texture->offsets[0] + SKY_SIZE;

    for (I32 y = 0; y < SKY_SIZE; ++y)
    {
        U8 *sky_pixel = newsky;
        U8 *texel = tex_src;
        for (I32 x = 0; x < SKY_SIZE; ++x)
        {
            *sky_pixel++ = *texel++;
        }
        newsky += SKY_TEXTURE_WIDTH;
        tex_src += SKY_TEXTURE_WIDTH;
    }

    newsky = sky->new_sky;
    tex_src = (U8 *)texture + texture->offsets[0];

    for (I32 y = 0; y < SKY_SIZE; ++y)
    {
        for (I32 x = 0; x < SKY_WEIRD_NUMBER; ++x)
        {
            U8 color = tex_src[y * SKY_TEXTURE_WIDTH + (x & SKY_SIZE_MASK)];
            sky->left_sky[y * SKY_WEIRD_NUMBER + x] = color;
            sky->left_mask[y * SKY_WEIRD_NUMBER + x] = color ? 0 : 0xff;
        }
    }
}

void SkyAnimate(SkyCanvas *sky)
{
    I32 right_sky_row = 0;
    I32 right_sky_offset = 0;
    U8 *sky_texel = sky->new_sky;

    for (I32 y = 0; y < SKY_SIZE; ++y)
    {
        right_sky_row = ((y + (I32)sky->sky_shift) & SKY_SIZE_MASK) * SKY_WEIRD_NUMBER;
        for (I32 x = 0; x < SKY_SIZE; ++x)
        {
            right_sky_offset = right_sky_row + ((x + (I32)sky->sky_shift) & SKY_SIZE_MASK);

            U8 left_sky_texel = *(sky_texel + SKY_SIZE);
            U8 right_sky_texel = sky->left_sky[right_sky_offset];
            U8 right_sky_mask = sky->left_mask[right_sky_offset];
            *sky_texel++ = (left_sky_texel & right_sky_mask) | right_sky_texel;
        }
        sky_texel += SKY_SIZE;
    }
}

void SkySetupFrame(SkyCanvas *sky)
{
    // TODO lw: floating-point, especially 0.5f, makes sky movement jerky.
    sky->sky_shift += 0.6f;
}

Vec2i SkyGetTextureUV(I32 x, I32 y, I32 screen_half_width, I32 screen_half_height, 
                      float sky_shift, Camera *camera)
{
    // Find the direction from origin to the pixel on the screen plane in view 
    // space
    float wu = (float)(x - screen_half_width);
    float wv = (float)(screen_half_height - y);
    float wz = camera->scale_z;

    Vec3f dir_world = {0};
    dir_world.x = wz * camera->roty[0] + wu * camera->rotx[0] + wv * camera->rotz[0];
    dir_world.y = wz * camera->roty[1] + wu * camera->rotx[1] + wv * camera->rotz[1];
    dir_world.z = wz * camera->roty[2] + wu * camera->rotx[2] + wv * camera->rotz[2];
    dir_world.z *= 3;
    dir_world = Vec3Normalize(dir_world);

    float sky_multiplier = 320.0f;

    Vec2i result = {
        (I32)((sky_shift + sky_multiplier * dir_world.x) * 0x10000), 
        (I32)((sky_shift + sky_multiplier * dir_world.y) * 0x10000)
    };

    return result;
}
