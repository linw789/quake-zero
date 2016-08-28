#include "q_platform.h"
#include "q_model.h"

/*
 * floor, ceil
 */
#include <math.h>

#define BSPVERSION 29

#define IDSPRITEHEADER	(('P'<<24)+('S'<<16)+('D'<<8)+'I')
#define IDPOLYHEADER	(('O'<<24)+('P'<<16)+('D'<<8)+'I')

#define MAX_MAP_LEAVES 8192

struct VertexDisk
{
    Vec3f position;
};

struct EdgeDisk
{
    U16 vertIndex[2];
};

struct PlaneDisk
{
    Vec3f normal;
    float distance;
    int type;
};

struct MipTexLump
{
    int numMipTex;
    int dataOffsets[1]; // [numMipTex], serve as starting pointer
};

struct TextureInfoDisk
{
    float vecs[2][4];
    int mipTexIndex;
    int flags;
};

struct MipTexture
{
    char name[16];
    U32 width;
    U32 height;
    U32 offsets[MIP_LEVELS];
};

struct FaceDisk
{
    I16 planeOffset;
    I16 side;

    int firstEdge;
    I16 numEdge;
    I16 texInfoOffset;

    U8 light_styles[MAX_LIGHT_MAPS];
    int lightOffset;
};

struct LeafDisk
{
    int contents;
    int visibilityOffset; // -1 = no visibility info

    // for frustum culling
    I16 mins[3];
    I16 maxs[3];

    U16 firstMarksurface;
    U16 numMarksurface;

    U8 ambientLevel[NUM_AMBIENT_SOUND];
};

struct NodeDisk
{
    I32 planeOffset;
    I16 childOffsets[2]; // negative numbers are -(leaves + 1), not node
    I16 mins[3];
    I16 maxs[3];
    U16 firstFace;
    U16 numFace; // counting both sides
};

// used to reference data in files in pack
struct Lump
{
    int offset;
    int length;
};

enum ModelLump
{
    ENTITY = 0,
    PLANE,
    TEXTURE,
    VERTEX,
    VISIBILITY,
    NODE,
    TEXTUREINFO,
    FACE,
    LIGHTING,
    CLIPNODE,
    LEAF,
    MARKSURFACE,
    EDGE,
    SURFACEEDGE,
    SUBMODEL,
    COUNT
};

struct ModelHeaderDisk
{
    int version;
    Lump lumps[ModelLump::COUNT];
};

Texture *g_defaultTexture;

void TextureCreateDefault(Texture *tx)
{
    // create a simple checkerboard texture

    int size = sizeof(*tx) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2;
    tx = (Texture *)HunkLowAlloc(size, "defaulttexture");
    StringCopy(tx->name, 16, "default");

    tx->width = 16;
    tx->height = 16;
    tx->offsets[0] = sizeof(*tx);
    tx->offsets[1] = sizeof(*tx) + 16 * 16;
    tx->offsets[2] = sizeof(*tx) + 16 * 16 + 8 * 8;
    tx->offsets[3] = sizeof(*tx) + 16 * 16 + 8 * 8 + 4 * 4;

    for (int i = 0; i < 4; ++i)
    {
        U8 *dest = (U8 *)tx + tx->offsets[i];

        for (int y = 0; y < (16 >> i); ++y)
        {
            for (int x = 0; x < (16 >> i); ++x)
            {
                if ((y < (8 >> i)) ^ (x < (8 >> i)))
                {
                    *dest = 0;
                }
                else
                {
                    *dest = 0xff;
                }
                dest++;
            }
        }
    }
}

void 
ModelLoadVertices(Model *model, U8 *base, Lump lump)
{
    VertexDisk *vertDisk = (VertexDisk *)(base + lump.offset);
    if (lump.length % sizeof(*vertDisk))
    {
        g_platformAPI.SysError("incorrect lump size for vertex");
    }
    
    int vertCount = lump.length / sizeof(*vertDisk);
    Vertex *vert = NULL;
    vert = (Vertex *)HunkLowAlloc(vertCount * sizeof(*vert), model->name);
    
    model->vertices = vert;
    model->numVert = vertCount;

    for (int i = 0; i < vertCount; ++i, ++vert, ++vertDisk)
    {
        vert->position = vertDisk->position;
    }
}

void 
ModelLoadEdges(Model *model, U8 *base, Lump lump)
{
    EdgeDisk *edgeDisk = (EdgeDisk *)(base + lump.offset);
    if (lump.length % sizeof(*edgeDisk))
    {
        g_platformAPI.SysError("incorrect lump size for edges");
    }
    
    int edgeCount = lump.length / sizeof(*edgeDisk);
    Edge *edge = NULL; 
    edge = (Edge *)HunkLowAlloc(edgeCount * sizeof(*edge), model->name);
    MemSet(edge, 0, edgeCount * sizeof(*edge));

    model->edges = edge;
    model->numEdge = edgeCount;

    for (int i = 0; i < edgeCount; ++i, ++edge, ++edgeDisk)
    {
        edge->vertIndex[0] = edgeDisk->vertIndex[0];
        edge->vertIndex[1] = edgeDisk->vertIndex[1];
    }
}

void 
ModelLoadSurfaceEdges(Model *model, U8 *base, Lump lump)
{
    int *surfaceEdgeDisk = (int *)(base + lump.offset);
    if (lump.length % sizeof(*surfaceEdgeDisk))
    {
        g_platformAPI.SysError("incorrect lump size for surface edge");
    }

    int surfaceEdgeCount = lump.length / sizeof(*surfaceEdgeDisk);
    int *surfaceEdge = NULL;
    surfaceEdge = (int *)HunkLowAlloc(surfaceEdgeCount * sizeof(*surfaceEdge), model->name);

    model->surfaceEdges = surfaceEdge;
    model->numSurfaceEdge = surfaceEdgeCount;

    for (int i = 0; i < surfaceEdgeCount; ++i)
    {
        surfaceEdge[i] = surfaceEdgeDisk[i];
    }
}

void 
ModelLoadPlanes(Model *model, U8 *base, Lump lump)
{
    PlaneDisk *planeDisk = (PlaneDisk *)(base + lump.offset);
    if (lump.length % sizeof(*planeDisk))
    {
        g_platformAPI.SysError("incorrect lump size for planes");
    }

    I32 planeCount = lump.length / sizeof(*planeDisk);
    Plane *plane = NULL;
    
    // TODO lw: why allocated twice amount of memory?
    plane = (Plane *)HunkLowAlloc(planeCount * 2 * sizeof(*plane), model->name);

    model->planes = plane;
    model->numPlane = planeCount;

    for (I32 i = 0; i < planeCount; ++i, ++plane, ++planeDisk)
    {
        U8 bits = 0;
        for (I32 j = 0; j < 3; ++j)
        {
            plane->normal[j] = planeDisk->normal[j];
            if (plane->normal[j] < 0)
            {
                bits |= 1 << j;
            }
        }
        
        plane->distance = planeDisk->distance;
        plane->type = (U8)planeDisk->type;
        plane->signBits = bits;
    }
}

void 
ModelLoadTextures(Model *model, U8 *base, Lump lump)
{
    if (!lump.length)
    {
        model->textures = NULL;
    }

    MipTexLump *miptexLump = (MipTexLump *)(base + lump.offset);
    MipTexture *mipTex = NULL;

    model->numTexture = miptexLump->numMipTex;
    model->textures = (Texture **)HunkLowAlloc(
            miptexLump->numMipTex * sizeof(*model->textures), model->name);

    if ((miptexLump->numMipTex + 1) * sizeof(int) != miptexLump->dataOffsets[0])
    {
        g_platformAPI.SysError("texture data is corrupted!");
    }

    Texture *tx = NULL;
    for (int i = 0; i < miptexLump->numMipTex; ++i)
    {
        if (miptexLump->dataOffsets[i] == -1)
        {
            continue;
        }

        mipTex = (MipTexture *)((I8 *)miptexLump + miptexLump->dataOffsets[i]);

        if (mipTex->width & 15 || mipTex->height & 15)
        {
            g_platformAPI.SysError("Texture %s is 16-byte aligned");
        }

        // w*h + w/2*h/2 + w/4*h/4 + w/8*h/8
        int pixelCount = mipTex->width * mipTex->height * 85 / 64;
        tx = (Texture *)HunkLowAlloc(sizeof(*tx) + pixelCount, model->name);
        model->textures[i] = tx;

        StringCopy(tx->name, sizeof(tx->name), mipTex->name);
        tx->width = mipTex->width;
        tx->height = mipTex->height;
        for (int j = 0; j < MIP_LEVELS; ++j)
        {
            tx->offsets[j] = mipTex->offsets[j] - sizeof(*mipTex) + sizeof(*tx);
        }
        MemCpy(tx + 1, mipTex + 1, pixelCount);

        if (StringNCompare(tx->name, "sky", 3) == 0)
        {
            SkyInit(&g_skycanvas, tx);
        }
    }

    // TODO lw: load animations?
}

void
ModelLoadLighting(Model *model, U8 *base, Lump lump)
{
    if (lump.length == 0)
    {
        model->light_data = NULL;
    }
    else
    {
        model->light_data = (U8 *)HunkLowAlloc(lump.length, model->name);
        MemCpy(model->light_data, base + lump.offset, lump.length);
    }
}

void
ModelLoadTextureInfo(Model *model, U8 *base, Lump lump)
{
    TextureInfoDisk *texInfoDisk = (TextureInfoDisk *)(base + lump.offset);
    if (lump.length % sizeof(*texInfoDisk))
    {
        g_platformAPI.SysError("incorrect lump size for texture info");
    }
    
    int count = lump.length / sizeof(*texInfoDisk);
    TextureInfo *tex_info = NULL;
    tex_info = (TextureInfo *)HunkLowAlloc(count * sizeof(*tex_info), model->name);

    model->numTexInfo = count;
    model->tex_info = tex_info;

    for (int i = 0; i < count; ++i, ++texInfoDisk, ++tex_info)
    {
        MemCpy(tex_info, texInfoDisk, 8 * sizeof(float));

        float u_length = Vec3Length(tex_info->u_axis);
        float v_length = Vec3Length(tex_info->v_axis);
        float length = (u_length + v_length) / 2.0f;
        
        // decide the mipmap level
        // TODO lw: why use length as as reference
        if (length < 0.32f)
        {
            tex_info->mip_adjust = 4;
        }
        else if (length < 0.49f)
        {
            tex_info->mip_adjust = 3;
        }
        else if (length < 0.99f)
        {
            tex_info->mip_adjust = 2;
        }
        else
        {
            tex_info->mip_adjust = 1;
        }
         
        tex_info->flags = texInfoDisk->flags;

        if (!model->textures)
        {
            tex_info->texture = g_defaultTexture; 
            tex_info->flags = 0;
        }
        else
        {
            if (texInfoDisk->mipTexIndex >= model->numTexture)
            {
                g_platformAPI.SysError("mip texture index too big");
            }

            tex_info->texture = model->textures[texInfoDisk->mipTexIndex];
            if (!tex_info->texture)
            {
                tex_info->texture = g_defaultTexture;
                tex_info->flags = 0;
            }
        }
    }
}

void
CalcTexCoordExtents(Model *model, Surface *surface)
{
    Vec2f min = {9999, 9999};
    Vec2f max = {-9999, -9999};

    for (int i = 0; i < surface->numEdge; ++i)
    {
        int edgeIndex = model->surfaceEdges[surface->firstEdge + i];

        Vertex vert;
        if (edgeIndex >= 0)
        {
            vert = model->vertices[model->edges[edgeIndex].vertIndex[0]];
        }
        else
        {
            vert = model->vertices[model->edges[-edgeIndex].vertIndex[1]];
        }

        float u = Vec3Dot(surface->tex_info->u_axis, vert.position) + surface->tex_info->u_offset;
        float v = Vec3Dot(surface->tex_info->v_axis, vert.position) + surface->tex_info->v_offset;

        if (u < min.u)
        {
            min.u = u;
        }
        if (u > max.u)
        {
            max.u = u;
        }

        if (v < min.v)
        {
            min.v = v;
        }
        if (v > max.v)
        {
            max.v = v;
        }
    }

    int bmins[2];
    int bmaxs[2];
    for (int i = 0; i < 2; ++i)
    {
        // make bmins and bmaxs discrete at multiples of 16
        bmins[i] = (int)floor(min[i] / 16);
        bmaxs[i] = (int)ceil(max[i] / 16);
        
        surface->uv_min[i] = (I16)(bmins[i] * 16);
        surface->uv_extents[i] = (I16)((bmaxs[i] - bmins[i]) * 16);

        ASSERT(surface->uv_extents[i] > 0);
            
        if (!(surface->tex_info->flags & TEX_SPECIAL) && surface->uv_extents[i] > 256)
        {
            g_platformAPI.SysError("bad surface uv_extents");
        }
    }
}

void 
ModelLoadFaces(Model *model, U8 *base, Lump lump)
{
    FaceDisk * faceDisk= (FaceDisk *)(base + lump.offset);
    if (lump.length % sizeof(*faceDisk))
    {
        g_platformAPI.SysError("incorrect lump size for surface");
    }

    I32 count = lump.length / sizeof(*faceDisk);

    Surface *surface = NULL;
    surface = (Surface *)HunkLowAlloc(count * sizeof(*surface), model->name);

    MemSet(surface, 0, count * sizeof(*surface));

    model->surfaces = surface;
    model->numSurface = count; 

    for (I32 i = 0; i < count; ++i, ++faceDisk, ++surface)
    {
        surface->firstEdge = faceDisk->firstEdge;
        surface->numEdge = faceDisk->numEdge;
        surface->flags = 0;

        if (faceDisk->side)
        {
            surface->flags |= SURF_PLANE_BACK;
        }

        surface->plane = model->planes + faceDisk->planeOffset;
        surface->tex_info = model->tex_info + faceDisk->texInfoOffset;

        CalcTexCoordExtents(model, surface);

        // load lighting info
        for (I32 j = 0; j < MAX_LIGHT_MAPS; ++j)
        {
            surface->light_styles[j] = faceDisk->light_styles[j];
        }

        if (faceDisk->lightOffset == -1)
        {
            surface->samples = NULL;
        }
        else
        {
            surface->samples = model->light_data + faceDisk->lightOffset;
        }

        // set drawing flags
        if (StringNCompare(surface->tex_info->texture->name, "sky", 3) == 0)
        {
            surface->flags |= SURF_DRAW_SKY | SURF_DRAW_TILED;
        }
        else if (StringNCompare(surface->tex_info->texture->name, "*", 1) == 0)
        {
            surface->flags |= SURF_DRAW_TURB | SURF_DRAW_TILED;
            for (I32 j = 0; j < 2; ++j)
            {
                surface->uv_min[j] = 16384;
                surface->uv_extents[j] = -8192;
            }
        }
    }
}

void 
ModelLoadMarkSurfaces(Model *model, U8 *base, Lump lump)
{
    I16 *markSurfOffset = (I16 *)(base + lump.offset);
    if (lump.length % sizeof(*markSurfOffset))
    {
        g_platformAPI.SysError("incorrect lump size for mark surface offsets");
    }

    int count = lump.length / sizeof(*markSurfOffset);
    Surface **marksurface = NULL;
    marksurface = (Surface **)HunkLowAlloc(count * sizeof(*marksurface), model->name);

    model->marksurfaces = marksurface;
    model->numMarksurface = count;

    for (int i = 0; i < count; ++i)
    {
        if (markSurfOffset[i] >= model->numSurface)
        {
            g_platformAPI.SysError("ModelLoadMarkSurfaces: bad marksurface");
        }
        marksurface[i] = model->surfaces + markSurfOffset[i];
    }
}

void 
ModelLoadVisibility(Model *model, U8 *base, Lump lump)
{
    if (!lump.length)
    {
        model->visibility = NULL;
        return ;
    }

    model->visibility = (U8 *)HunkLowAlloc(lump.length, model->name);
    MemCpy(model->visibility, base + lump.offset, lump.length);
}

void
ModelLoadLeaves(Model *model, U8 *base, Lump lump)
{
    LeafDisk *leafDisk = (LeafDisk *)(base + lump.offset);
    if (lump.length % sizeof(*leafDisk))
    {
        g_platformAPI.SysError("incorrect lump size for leaf");
    }

    int count = lump.length / sizeof(*leafDisk);

    Leaf *leaf = NULL;
    leaf = (Leaf *)HunkLowAlloc(count * sizeof(*leaf), model->name);
    MemSet(leaf, 0, count * sizeof(*leaf));

    model->leaves = leaf;
    model->numLeaf = count;

    for (int i = 0; i < count; ++i, ++leafDisk, ++leaf)
    {
        for (int j = 0; j < 3; ++j)
        {
            leaf->minmax[j] = leafDisk->mins[j];
            leaf->minmax[3 + j] = leafDisk->maxs[j];
        }

        int temp = leafDisk->contents;
        leaf->contents = temp;

        leaf->firstMarksurface = model->marksurfaces + leafDisk->firstMarksurface;
        leaf->numMarksurface = leafDisk->numMarksurface;

        temp = leafDisk->visibilityOffset;
        if (temp == -1)
        {
            leaf->visibilityCompressed = NULL;
        }
        else
        {
            leaf->visibilityCompressed = model->visibility + temp;
        }

        leaf->efrags = NULL;

        for (int j = 0; j < 4; ++j)
        {
            leaf->ambientSoundLevel[j] = leafDisk->ambientLevel[j];
        }
    }
}

void ModelSetNodeParent(Node *node, Node *parent)
{
    node->parent = parent;
    if (node->contents >= 0)
    {
        ModelSetNodeParent(node->children[0], node);
        ModelSetNodeParent(node->children[1], node);
    }
}

void ModelLoadNodes(Model *model, U8 *base, Lump lump)
{
    NodeDisk *nodeDisk = (NodeDisk *)(base + lump.offset);
    if (lump.length % sizeof(*nodeDisk))
    {
        g_platformAPI.SysError("incorrect lump size for nodes");
    }

    int count = lump.length / sizeof(*nodeDisk);
    Node *node = NULL;
    node = (Node *)HunkLowAlloc(count * sizeof(*node), model->name);
    MemSet(node, 0, count * sizeof(*node));

    model->nodes = node;
    model->numNode = count;

    for (int i = 0; i < count; ++i, ++nodeDisk, ++node)
    {
        for (int j = 0; j < 3; ++j)
        {
            node->minmax[j] = nodeDisk->mins[j];
            node->minmax[j + 3] = nodeDisk->maxs[j];
        }
        node->plane = model->planes + nodeDisk->planeOffset;
        node->firstsurface = nodeDisk->firstFace;
        node->numsurface = nodeDisk->numFace;
        node->contents = 0;

        for (int j = 0; j < 2; ++j)
        {
            int p = nodeDisk->childOffsets[j];
            if (p >= 0)
            {
                node->children[j] = model->nodes + p;
            }
            else
            {
                node->children[j] = (Node *)(model->leaves + (-1 - p));
            }
        }
    }

    ModelSetNodeParent(model->nodes, NULL);
}

void ModelLoadClipNodes(Model *model, U8 *base, Lump lump)
{
    ClipNode *clipNodeDisk = (ClipNode *)(base + lump.offset);
    ClipNode *clipNode = NULL;

    if (lump.length % sizeof(*clipNodeDisk))
    {
        g_platformAPI.SysError("incorrect lump size for clip node");
    }
    int count = lump.length / sizeof(*clipNodeDisk);
    clipNode = (ClipNode *)HunkLowAlloc(count * sizeof(*clipNode), model->name);

    model->clipNodes = clipNode;
    model->numClipNode = count;

    Hull *hull = &model->hulls[1]; // TODO lw: why 1 not 0?
    hull->clipNodes = clipNode;
    hull->firstClipNode = 0;
    hull->lastClipNode = count - 1;
    hull->planes = model->planes;
    hull->clipMin = {-16, -16, -24};
    hull->clipMax = {16, 16, 32};

    hull = &model->hulls[2];
    hull->clipNodes = clipNode;
    hull->firstClipNode = 0;
    hull->lastClipNode = count - 1;
    hull->planes = model->planes;
    hull->clipMin = {-32, -32, -24};
    hull->clipMax = {32, 32, 64};

    for (int i = 0; i < count; ++i, ++clipNode, ++clipNodeDisk)
    {
        clipNode->planeOffset = clipNodeDisk->planeOffset;
        clipNode->children[0] = clipNodeDisk->children[0];
        clipNode->children[1] = clipNodeDisk->children[1];
    }
}

void ModelLoadEntities(Model *model, U8 *base, Lump lump)
{
    if (lump.length == 0)
    {
        model->entities = NULL;
    }

    model->entities = (char *)HunkLowAlloc(lump.length, model->name);
    MemCpy(model->entities, base + lump.offset, lump.length);
}

void ModelLoadSubmodels(Model *model, U8 *base, Lump lump)
{
    Submodel *submodelDisk = (Submodel *)(base + lump.offset);
    if (lump.length % sizeof(*submodelDisk))
    {
        g_platformAPI.SysError("incorrect lump size for submodels");
    }
    int count = lump.length / sizeof(*submodelDisk);
    Submodel *submodel = NULL;
    submodel = (Submodel *)HunkLowAlloc(count * sizeof(*submodel), model->name);

    model->submodels = submodel;
    model->numSubmodel = count;

    for (int i = 0; i < count; ++i, ++submodelDisk, ++submodel)
    {
        Vec3f one = {1, 1, 1};
        submodel->min = submodelDisk->min - one;
        submodel->max = submodelDisk->max + one;
        submodel->origin = submodelDisk->origin;

        for (int j = 0; j < MAX_MAP_HULLS; ++j)
        {
            submodel->headNodes[j] = submodelDisk->headNodes[j];
        }
        submodel->visibleLeaves = submodelDisk->visibleLeaves;
        submodel->firstFace = submodelDisk->firstFace;
        submodel->numFace = submodelDisk->numFace;
    }
}

void ModelMakeHull(Model *model)
{
    Hull *hull = &model->hulls[0];

    Node *node = model->nodes;
    ClipNode *clipNode = (ClipNode *)HunkLowAlloc(
            model->numNode * sizeof(ClipNode), model->name);

    hull->clipNodes = clipNode;
    hull->planes = model->planes;
    hull->firstClipNode = 0;
    hull->lastClipNode = model->numNode - 1;

    for (int i = 0; i < model->numNode; ++i, node++, clipNode++)
    {
        clipNode->planeOffset = (I32)(node->plane - model->planes);
        for (int j = 0; j < 2; ++j)
        {
            Node *child = node->children[j];
            if (child->contents < 0)
            {
                clipNode->children[j] = (I16)child->contents;
            }
            else
            {
                clipNode->children[j] = (I16)(child - model->nodes);
            }
        }
    }
}

#define MAX_KNOWN_MODEL 256
Model g_knownModels[MAX_KNOWN_MODEL];
int g_numKnownModel;

// Try to find a loaded model that's matching the name, else return an unused
// mode in g_knownModels[MAX_KNOWN_MODEL]
Model *ModelFindForName(char *name)
{
    if (name == NULL || name[0] == '\0')
    {
        g_platformAPI.SysError("no model name");
    }

    Model *unusedModel = NULL;
    Model *model = g_knownModels;
    int modelIndex = 0;
    for (modelIndex = 0; modelIndex < g_numKnownModel; ++modelIndex, ++model)
    {
        if (StringCompare(model->name, name) == 0)
        {
            break ;
        }
        if (model->loadStatus == ModelLoadStatus::UNUSED)
        {
            // TODO lw: find out why the test: model->type != ModelType::Alias
            if (unusedModel == NULL || model->type != ModelType::ALIAS)
            {
                unusedModel = model;
            }
        }
    }

    // no existing model matches the name, need to load one
    if (modelIndex == g_numKnownModel)
    {
        if (unusedModel)
        {
            model = unusedModel;
            // if cache is still in memory, eject it
            if (model->type == ModelType::ALIAS)
            {
                if (CacheCheck(&model->cache))
                {
                    CacheFree(&model->cache);
                }
            }
        }
        else if (g_numKnownModel == MAX_KNOWN_MODEL)
        {
            g_platformAPI.SysError("Exceeds maximum model number!");
        }
        else
        {
            g_numKnownModel++;
        }

        StringCopy(model->name, sizeof(model->name), name);
        model->loadStatus = ModelLoadStatus::NEEDLOAD;
    }

    return model;
}

float ModelRadiusFromBounds(Vec3f min, Vec3f max)
{
    Vec3f extreme = {0};
    for (int i = 0; i < 3; ++i)
    {
        extreme[i] = Absf(min[i]) > Absf(max[i]) ? Absf(min[i]) : Absf(max[i]);
    }

    float result = Vec3Length(extreme);
    return result;
}

/*
Type Submodel is used for describing data, the real data of each submodel are 
stored as an instance of type of Model too. Submodel includes the room model as
well as torche and other small models. 
*/
void ModelSetupSubmodel(Model *model)
{
    Model *submodel = model;
    for (int i = 0; i < model->numSubmodel; ++i)
    {
        Submodel *spec = &model->submodels[i];
        submodel->hulls[0].firstClipNode = spec->headNodes[0];
        for (int j = 0; j < MAX_MAP_HULLS; ++j)
        {
            submodel->hulls[j].firstClipNode = spec->headNodes[j];
            submodel->hulls[j].lastClipNode = model->numClipNode - 1;
        }

        submodel->firstModelSurface = spec->firstFace;
        submodel->numModelSurface = spec->numFace;

        submodel->min = spec->min;
        submodel->max = spec->max;
        submodel->radius = ModelRadiusFromBounds(submodel->min, submodel->max);

        submodel->numLeaf = spec->visibleLeaves;

        if (i < model->numSubmodel - 1)
        {
            char submodelname[MAX_PACK_FILE_PATH];
            snprintf(submodelname, MAX_PACK_FILE_PATH, "%s*%d", model->name, i);
            Model *nextSubmodel = ModelFindForName(submodelname);
            // duplicate the basic information
            *nextSubmodel = *model;
            StringCopy(nextSubmodel->name, MAX_PACK_FILE_PATH, submodelname, 0);
            submodel = nextSubmodel;
        }
    }

}

void ModelLoadBrushModel(Model *model, void *buffer)
{
    model->type = ModelType::BRUSH;

    ModelHeaderDisk *headerDisk = (ModelHeaderDisk *)buffer;

    if (headerDisk->version != BSPVERSION)
    {
        g_platformAPI.SysError("ModelLoadBrushModel: %s has wrong version number", model->name);
    }

    // load into low hunk
    U8 *base = (U8 *)buffer;
    ModelLoadVertices(model, base, headerDisk->lumps[ModelLump::VERTEX]);
    ModelLoadEdges(model, base, headerDisk->lumps[ModelLump::EDGE]);
    ModelLoadSurfaceEdges(model, base, headerDisk->lumps[ModelLump::SURFACEEDGE]);
    ModelLoadTextures(model, base, headerDisk->lumps[ModelLump::TEXTURE]);
    ModelLoadLighting(model, base, headerDisk->lumps[ModelLump::LIGHTING]);
    ModelLoadPlanes(model, base, headerDisk->lumps[ModelLump::PLANE]);
    ModelLoadTextureInfo(model, base, headerDisk->lumps[ModelLump::TEXTUREINFO]);
    ModelLoadFaces(model, base, headerDisk->lumps[ModelLump::FACE]);
    ModelLoadMarkSurfaces(model, base, headerDisk->lumps[ModelLump::MARKSURFACE]);
    ModelLoadVisibility(model, base, headerDisk->lumps[ModelLump::VISIBILITY]);
    ModelLoadLeaves(model, base, headerDisk->lumps[ModelLump::LEAF]);
    ModelLoadNodes(model, base, headerDisk->lumps[ModelLump::NODE]);
    ModelLoadClipNodes(model, base, headerDisk->lumps[ModelLump::CLIPNODE]);
    ModelLoadEntities(model, base, headerDisk->lumps[ModelLump::ENTITY]);
    ModelLoadSubmodels(model, base, headerDisk->lumps[ModelLump::SUBMODEL]);

    ModelMakeHull(model);

    model->numFrame = 2; // regular and alternate animation TODO lw: ???
    model->flags = 0;

    ModelSetupSubmodel(model);
}

void ModelLoad(Model *model)
{
    void *buffer = FileLoad(model->name, ALLocType::TEMPHUNK);

    model->loadStatus = ModelLoadStatus::PRESENT;

    switch (*((U32 *)buffer))
    {
        case IDPOLYHEADER:
        {

        } break; 

        case IDSPRITEHEADER:
        {

        } break;

        default:
        {
            ModelLoadBrushModel(model, buffer);
        } break;
    }
}

Model *ModelLoadForName(char *name)
{
    // find a model. if it's an existing one, load it.
    Model *result = ModelFindForName(name);

    if (result->loadStatus == ModelLoadStatus::PRESENT)
    {
        if (result->type == ModelType::ALIAS)
        {
            // make sure cache has not been evicted
            if (CacheCheck(&result->cache))
            {
                return result;
            }
        }
        else
        {
            return result;
        }
    }

    ModelLoad(result);

    return result;
}

U8 g_allVisible[MAX_MAP_LEAVES / 8];

U8 *ModelDecompressVisibility(U8 *visibility, int numLeaf)
{
    // the n-th bit is set if the n-th leaf if visible
    // MAX_MAP_LEAVES is defined to be multiple of 8, no need to ceil
    static U8 decompressed[MAX_MAP_LEAVES / 8];

    // ensure to have enough bits
    int numBytes = (numLeaf + 7) >> 3;

    U8 *result = decompressed;

    // no visibility info, make all leaves visible
    if (visibility == NULL)
    {
        result = g_allVisible;
    }
    else
    {
        do 
        {
            if (*visibility)
            {   // if the byte is not zero, write it
                *result++ = *visibility++;
                continue;
            }
            else
            {   // if the byte is zero, get the next byte to see how many zeroes 
                // following it, then write all zeroes.
                int count = visibility[1];
                visibility += 2;
                while (count)
                {
                    *result++ = 0;
                    count--;
                }
            }
        } while (result - decompressed < numBytes);
    }


    return decompressed;
}

U8 *ModelGetDecompressedPVS(Leaf *leaf, Model *model)
{
    if (leaf == model->leaves) 
    {
        return g_allVisible;
    }
    else
    {
        U8 *result = ModelDecompressVisibility(
                leaf->visibilityCompressed, model->numLeaf);

        return result;
    }
}

Leaf *ModelFindViewLeaf(Vec3f pos, Model *worldModel)
{
    if (worldModel == NULL || worldModel->nodes == NULL)
    {
        g_platformAPI.SysError("ModelFindViewingLeaf: bad model!");
    }

    Node *node = worldModel->nodes;
    for (;;)
    {
        if (node->contents < 0)
        {
            return (Leaf *)node;
        }
        // (P - O) * N - (Q - O) * N = (P - Q) * N 
        // P is pos, N is normal, Q is any point on the plane, O is the origin
        float d = Vec3Dot(pos, node->plane->normal) - node->plane->distance;
        if (d > 0)
        {   // same side the normal points to
            node = node->children[0];
        }
        else
        {   // opposite side the normal points to, or on the plane
            // TODO lw: why on the plane is considered back facing???
            node = node->children[1];
        }
    }
}

void ModelInit()
{
    MemSet(g_allVisible, 0xff, sizeof(g_allVisible));
}
