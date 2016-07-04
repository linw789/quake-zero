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
    U32 cachedIEdgeOffset;
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
    Vec3f uAxis;
    float uOffset;
    Vec3f vAxis;
    float vOffset;

    Texture *texture;
    float mipAdjust;
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

struct Surface
{
    Plane *plane;
    TextureInfo *texInfo;

    I32 visibleFrame;

    I32 dlightFrame;
    I32 dlightBits;

    I32 flags;

    I32 firstEdge;
    I32 numEdge;

    // SurfaceCache *cacheSpots[MIP_LEVELS];

    I16 texCoordMin[2];
    I16 texCoordExtents[2];


    U32 styles[MAX_LIGHT_MAPS];
    U8 *samples;
};

// BSP node
struct Node
{
    // common with leaf
    Node *parent;
    I32 contents; // 0, to differentiate from leaves
    I32 visibleFrame;
    I16 minmax[6]; // for bounding box culling

    // node specific
    U16 firstSurface;
    U16 numSurface;
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
    TextureInfo *texInfo;
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
