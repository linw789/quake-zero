#pragma once

#include "q_math.h"

struct RenderBuffer
{
    I32 width;
    I32 height;
    I32 bytesPerPixel;
    I32 bytesPerRow;
    U8 *colorPalette;
    U8 *colormap;
    U8 *backbuffer;
    float *zbuffer;
};

struct ClipPlane
{
    ClipPlane *next;
    Vec3f normal;
    float distance;
    U8 leftEdge;
    U8 rightEdge;
    U8 reserved[2];
};

struct Recti
{
    I32 x, y; // top-left corner
    I32 width, height;
};

struct Camera
{
    Vec2f screenCenter;
    Vec2f screenMin;
    Vec2f screenMax;

    float nearZ;
    // float farZ;
    // float half_fovy; // half of field of view in y axis, in redian
    // float aspect; // width / height

    /*
    x_w / scaleZ = tan(fovx * 0.5); 
    x_w equals the width of the screen and scaleZ is the corresponding z value.
    x_s / x_v = scaleZ / z_v => x_s = scaleZ / z_v * x_v
    x_v and z_v are the values in view space, x_s is the projected value of x_v
    on the plane whose width is x_w, essentially x_s is the value in screen space.
    */
    float scaleZ;
    float scaleInvZ;

    Vec3f position; // in world space
    Vec3f angles;

    /*
    T * R is camera transform matrix, 
    Pv, Pw are points in view and world space respectively 

    (T*R) * Pv = Pw 
    --> inverse(T*R) * (T*R) * Pv = inverse(T*R) * Pw
    --> Pv = inverse(T*R) * Pw 
    --> Pv = inverse(R) * inverse(T) * Pw

    R = |transpose(rotx), transpose(roty), transpose(rotz)|
    */
    Vec3f rotx, roty, rotz;

    // left, right, top, bottom
    Plane frustumPlanes[4];
    // frustum planes transformed in world space
    ClipPlane worldFrustumPlanes[4];
    I32 frustumIndices[4 * 6];

    U32 dirty;
};

struct ESpan
{
    ESpan *next;
    I32 x_start, y;
    I32 count; // pixel count
    I32 padding;
};

// intermediate edge data for span drawing
struct IEdge
{
    IEdge *prev;
    IEdge *next;
    IEdge *nextRemove;
    Edge *owner;

    fixed20 x_start; // in screen space
    fixed20 x_step;
    // isurfaceOffsets[0] is set for trailing(right) edge, 
    // isurfaceOffsets[1] is set for leading(left) edge
    U32 isurfaceOffsets[2];
    float nearInvZ;
};

// intermediate surface data for span drawing
struct ISurface
{
    ISurface *next;
    ISurface *prev;
    ESpan *spans;
    
    void *data;
    Entity *entity;

    // We are using span-base drawing, no need to walk bsp tree from back to
    // front. It's actually being walked from front to back, and therefore 
    // smaller keys are in front. 
    I32 key; 
    I32 x_last;
    // safe guard to ensure that trailing edge only comes after leading edge
    I32 spanState;
    I32 flags;
    float nearestInvZ;
    B32 isInSubmodel;
    // used for calculating 1/z in screen space
    float zi_stepx, zi_stepy, zi_d;
};

#define MAX_PIXEL_HEIGHT 1024

// imtermediate data for drawing
struct RenderData
{
    IEdge *newIEdges[MAX_PIXEL_HEIGHT];
    IEdge *removeIEdges[MAX_PIXEL_HEIGHT];

    IEdge *iedges;
    IEdge *currentIEdge;
    IEdge *endIEdge;

    ISurface *isurfaces;
    ISurface *currentISurface;
    ISurface *endISurface;

    Leaf *oldViewLeaf;
    Leaf *currentViewLeaf;

    Model *worldModel;

    float nearestInvZ; // for surface

    I32 currentKey;

    I32 frameCount;
    I32 updateCountPVS;

    I32 outOfIEdges;
    I32 surfaceCount;
};
