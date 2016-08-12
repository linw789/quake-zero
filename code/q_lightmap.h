#pragma once

#define MAX_LIGHT_STYLE_NUM 64
#define MAX_LIGHT_NUM 32

struct LightStyle
{
    I32 length;
    // Each char represents the brightness every 0.1 second, 'a' means no light,
    // 'm' normal bright, 'z' double birght. 
    char wave[64];
    I32 cur_value;
};

struct Light 
{
    Vec3f position;
    float radius;
    float decay;
    float minlight;
    I32 key;
    float duration;
    float time_passed;
};

struct LightSystem
{
    I32 light_framecount;

    LightStyle styles[MAX_LIGHT_STYLE_NUM];
    Light lights[MAX_LIGHT_NUM];

    Fixed8 blocklights[18][18];
};
