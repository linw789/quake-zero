#pragma once

#include "q_math.h"

#define MAX_MAP_HULLS 4

#define MIP_LEVELS 4
#define MAX_LIGHT_MAPS 4

#define TEX_SPECIAL 1 // sky or slime, no lightmap or 256 subdivision

#define SURF_PLANE_BACK 2
#define SURF_DRAW_SKY 4
#define SURF_DRAW_SPRITE 8
#define SURF_DRAW_TURB 0x10
#define SURF_DRAW_TILED 0x20
#define SURF_DRAW_BACKGROUND 0x40

#define NUM_AMBIENT_SOUND 4 // automatic ambient sound


struct Vertex
{
    Vec3f position;
};

struct Edge
{
    U16 vertIndex[2];
    U32 iedge_cache_state;
};

struct Texture
{
    char name[16];
    U32 width;
    U32 height;
    I32 animTotal; // total tenths in sequence (0 = no)
    Texture *animNext; // in the animation sequence
    Texture *alternateAnims; // bmodels in frame 1 use this
    U32 offsets[MIP_LEVELS]; // 4 mip maps stored
};

struct MipTexture
{
    char name[16];
    U32 width;
    U32 height;
    U32 offsets[MIP_LEVELS];
};

struct TextureInfo
{
    Vec3f u_axis;
    float u_offset;
    Vec3f v_axis;
    float v_offset;

    Texture *texture;
    float mip_adjust;
    I32 flags;
};

struct Plane
{
    Vec3f normal;
    float distance;
    U8 type;
    
    // the signness of z, y, z component of the normal precalculated while 
    // loading from disk. Help speed up BOX_ON_PLANE_SIDE routine.
    // sign_x + sign_y << 1 + sign_z << 2
    U8 signBits;

    U8 padding[2];
};

struct SurfaceCache
{
    SurfaceCache *next;
    SurfaceCache **owner;
    Texture *texture;
    I32 bright_adjusts[MAX_LIGHT_MAPS];
    I32 dlight; // ?
    I32 size; // including the header
    U32 width;
    float mipscale;
    U8 data[4]; // &data[0] is the starting address of cache data
};

struct Surface
{
    Plane *plane;
    TextureInfo *tex_info;

    SurfaceCache *cachespots[MIP_LEVELS];

    I32 visibleframe;

    I32 lightframe;
    // every 1-bit represents a light affectting this surface
    I32 lightbits;

    I32 flags;

    I32 firstEdge;
    I32 numEdge;

    I16 uv_min[2];
    I16 uv_extents[2];


    U32 light_styles[MAX_LIGHT_MAPS];
    U8 *samples;
};

// BSP node
struct Node
{
    // common with leaf
    Node *parent;
    I32 contents; // 0, to differentiate from leaves
    I32 visibleframe;
    I16 minmax[6]; // for bounding box culling

    // node specific
    U16 firstsurface;
    U16 numsurface;
    Plane *plane;
    Node *children[2];
};

struct ClipNode
{
    I32 planeOffset;
    // negative means leaf content, positive means node offset
    I16 children[2];
};

struct Leaf
{
    // common with node
    Node *parent;
    I32 contents; // negative value means leaf
    I32 visibleFrame; // not used for leaf
    I16 minmax[6]; // not used for leaf

    // leaf specific
    I32 numMarksurface; // TODO lw: what is this?
    I32 key; // BSP sequence number for leaf's content
    U8 ambientSoundLevel[NUM_AMBIENT_SOUND];

    U8 *visibilityCompressed; // run-length compressed
    struct EFrag *efrags;

    Surface **firstMarksurface; // surfaces that this leaf contains
};

struct Hull
{
    ClipNode *clipNodes;
    Plane *planes;
    I32 firstClipNode;
    I32 lastClipNode;
    Vec3f clipMin;
    Vec3f clipMax;
};

// TODO lw: Entity Fragment???
struct EFrag
{
    Leaf *leaf;
    EFrag *nextLeaf; 

    struct Entity *entity;
    EFrag *nextEntity;
};

struct Submodel
{
    Vec3f min;
    Vec3f max;
    Vec3f origin;
    I32 headNodes[MAX_MAP_HULLS];
    I32 visibleLeaves; // not including the solid leaf 0
    I32 firstFace; // offset
    I32 numFace;
};

enum ModelType
{
    BRUSH, // wall, building, etc.
    SPRITE, // particle effect etc.
    ALIAS // monster, weapon, player etc.
};

enum ModelLoadStatus
{
    PRESENT,
    NEEDLOAD,
    UNUSED
};

struct Model
{
    char name[MAX_PACK_FILE_PATH];

// brush model data
    
    I32 firstModelSurface;
    I32 numModelSurface;

    Submodel *submodels;
    Vertex *vertices;
    Edge *edges;
    Node *nodes;
    Plane *planes;
    // edge indices stored sequentially for each surface,
    // queried by firstEdge and numEdge in Surface struct
    I32 *surfaceEdges;
    Texture **textures;
    TextureInfo *tex_info;
    Surface *surfaces;
    ClipNode *clipNodes;
    Surface **marksurfaces;
    // leaf 0 is the generic SOLID leaf used for all solid area, all other 
    // leaves need visibility info
    Leaf *leaves;

    // run-length encoded visibility data for all leaves
    U8 *visibility;

    U8 *lightData;
    char *entities;

    // additional model data, only access through Mod_Extradata
    CacheUser cache;

    Hull hulls[MAX_MAP_HULLS];

    // bounding volume
    Vec3f max;
    Vec3f min;
    float radius;

    I32 numSubmodel;
    I32 numVert;
    I32 numEdge;
    I32 numNode;
    I32 numPlane;
    I32 numSurfaceEdge;
    I32 numTexture;
    I32 numTexInfo;
    I32 numSurface;
    I32 numClipNode;
    I32 numMarksurface;
    I32 numLeaf;

    I32 numFrame;
    I32 flags;

    ModelType type;
    ModelLoadStatus loadStatus;
};
