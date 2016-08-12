#pragma once

// Remove this after implementing math functions ourselves.
#include <math.h> 

#define PI32 3.1415926535897932f

inline float Clamp(float min, float max, float x)
{
    if (x < min)
        x = min;
    if (x > max)
        x = max;
    return x;
}

inline Fixed16 Clamp(Fixed16 min, Fixed16 max, Fixed16 x)
{
    if (x < min)
        x = min;
    if (x > max)
        x = max;
    return x;
}

inline float Absf(float x)
{
    float result = fabsf(x);
    return result;
}

inline float DegreeToRadian(float angle)
{
    float result = angle / 180.0f * PI32;
    return result;
}

inline float SquareRoot(float x)
{
    float result = sqrtf(x);
    return result;
}

float InvSquareRoot(float x)
{
    float result = 1.0f / SquareRoot(x);
    return result;
}

float Sine(float x)
{
    float result = sinf(x);
    return result;
}

float Cosine(float x)
{
    float result = cosf(x);
    return result;
}

float Tangent(float x)
{
    float result = tanf(x);
    return result;
}

/*
    Linear Math 
*/
struct Vec2f
{
    float & operator[](int index)
    {
        ASSERT(index >= 0);
        ASSERT(index < 2);
        return ((float *)this)[index];
    }

    union
    {
        struct {float x, y;};
        struct {float s, t;};
        struct {float u, v;};
    };
};

struct Vec3f
{
    float & operator[](int index)
    {
        ASSERT(index >= 0);
        ASSERT(index < 3);
        return ((float *)this)[index];
    }

    void operator+=(const Vec3f &rhv)
    {
        x += rhv.x;
        y += rhv.y;
        z += rhv.z;
    }

    void operator-=(const Vec3f &rhv)
    {
        x -= rhv.x;
        y -= rhv.y;
        z -= rhv.z;
    }

    union
    {
        struct {float x, y, z;};
        struct {float r, g, b;};
    };
};

struct Vec4f
{
    float & operator[](int index)
    {
        ASSERT(index >= 0);
        ASSERT(index < 4);
        return ((float *)this)[index];
    }

    union
    {
        struct {float x, y, z, w;};
        struct {float r, g, b, a;};
    };
};

inline float Vec3Dot(const Vec3f &lhv, const Vec3f &rhv)
{
    float result = lhv.x * rhv.x + lhv.y * rhv.y + lhv.z * rhv.z;
    return result;
}

inline float Vec3Length(const Vec3f& v)
{
    float result = SquareRoot(Vec3Dot(v, v));
    return result;
}

inline Vec3f Vec3Normalize(const Vec3f &v)
{
    float invlen = InvSquareRoot(Vec3Dot(v, v));
    Vec3f result = {v.x * invlen, v.y * invlen, v.z * invlen};
    return result;
}

B32 operator==(const Vec3f &lhv, const Vec3f &rhv)
{
    B32 result = (lhv.x == rhv.x && lhv.y == rhv.y && lhv.z == rhv.z);
    return result;
}

inline Vec3f operator+(const Vec3f &lhv, const Vec3f &rhv)
{
    Vec3f result = {lhv.x + rhv.x, lhv.y + rhv.y, lhv.z + rhv.z};
    return result;
}

inline Vec3f operator-(const Vec3f &lhv, const Vec3f &rhv)
{
    Vec3f result = {lhv.x - rhv.x, lhv.y - rhv.y, lhv.z - rhv.z};
    return result;
}

inline Vec3f operator*(float scalar, const Vec3f &rhv)
{
    Vec3f result = {scalar * rhv.x, scalar * rhv.y, scalar * rhv.z};
    return result;
}

inline Vec3f operator*(const Vec3f &lhv, float scalar)
{
    Vec3f result = scalar * lhv;
    return result;
}

/*
   TODO lw: better explanation of matrix 

   Matrix is laid out in memory as row major, meaning 
   matrix[0-3] is the first row, 
   matrix[4-7] is the second row,
   matrix[8-11] is the third row, 
   matrix[12-15] is the fourth row.

   Note, memory layout has nothing to do with the way matrices multiply with
   vectors. It depends on your coordinate system.
*/

struct Mat3
{
    const float *operator[](int index) const
    {
        ASSERT(index >= 0);
        ASSERT(index < 3);
        return &(values[index][0]);
    }
    
    float *operator[](int index)
    {
        ASSERT(index >= 0);
        ASSERT(index < 3);
        return &(values[index][0]);
    }

    float values[3][3];
};

struct Mat4
{
    float values[4][4];
};

inline Vec3f operator*(const Mat3 m3, const Vec3f v3)
{
    Vec3f result = {
        m3[0][0] * v3.x + m3[0][1] * v3.y + m3[0][2] * v3.z,
        m3[1][0] * v3.x + m3[1][1] * v3.y + m3[1][2] * v3.z,
        m3[2][0] * v3.x + m3[2][1] * v3.y + m3[2][2] * v3.z
    };
    return result;
}

void AngleVectors(Vec3f angles, Vec3f *vx, Vec3f *vy, Vec3f *vz)
{
    float radian = 0;
    radian = DegreeToRadian(angles[0]);
    float sinx = Sine(radian);
    float cosx = Cosine(radian);
    radian = DegreeToRadian(angles[1]);
    float siny = Sine(radian);
    float cosy = Cosine(radian);
    radian = DegreeToRadian(angles[2]);
    float sinz = Sine(radian);
    float cosz = Cosine(radian);

    /*
     Simple trigonometry could be used to build rotation matrices around the X, 
     Y, Z axes in world space. And since we are always rotating around the world  
     Z axis, we could rotate around the local x axis first which is coincident 
     with world x axis initially, then rotate around world axis.
    */
    *vx = {cosz, -sinz, 0};
    *vy = {sinz * cosx, cosz * cosx, sinx};
    *vz = {-sinz * sinx, -cosz * sinx, cosx};
}
