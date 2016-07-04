#include "math.h" // ceil, tan

#include "q_model.h"
#include "q_render.h"

#define	CONTENTS_EMPTY		-1
#define	CONTENTS_SOLID		-2
#define	CONTENTS_WATER		-3
#define	CONTENTS_SLIME		-4
#define	CONTENTS_LAVA		-5
#define	CONTENTS_SKY		-6
#define	CONTENTS_ORIGIN		-7		// removed at csg time
#define	CONTENTS_CLIP		-8		// changed to contents_solid

#define	CONTENTS_CURRENT_0		-9
#define	CONTENTS_CURRENT_90		-10
#define	CONTENTS_CURRENT_180	-11
#define	CONTENTS_CURRENT_270	-12
#define	CONTENTS_CURRENT_UP		-13
#define	CONTENTS_CURRENT_DOWN	-14

// 0-2 are axial planes
#define	PLANE_X			0
#define	PLANE_Y			1
#define	PLANE_Z			2
// 3-5 are non-axial planes snapped to the nearest
#define	PLANE_ANYX		3
#define	PLANE_ANYY		4
#define	PLANE_ANYZ		5

#define BACKFACE_EPSILON 0.01

// used for edge caching mechanism
#define EDGE_FULLY_CLIPPED	    0x80000000
#define FRAMECOUNT_MASK			0x7fffffff
#define EDGE_PARTIALLY_CLIPPED  FRAMECOUNT_MASK

// for data alignment on function stack
#define CACHE_SIZE 64

Camera g_camera;

void ResetCamera(Camera *camera, Recti screenRect, float fovx)
{
    // Subtracting 0.5f to make center.x sits between pixels if screen width is 
    // an even number, on the center pixel if an odd number.
    camera->screenCenter.x = screenRect.x + screenRect.width / 2.0f - 0.5f;
    camera->screenCenter.y = screenRect.y + screenRect.height / 2.0f - 0.5f;

    camera->screenMin.x = screenRect.x - 0.5f;
    camera->screenMin.y = screenRect.y - 0.5f;
    camera->screenMax.x = screenRect.x + screenRect.width - 0.5f;
    camera->screenMax.y = screenRect.y + screenRect.height - 0.5f;

    float tanx = Tangent(DegreeToRadian(fovx * 0.5f));
    camera->scaleZ = screenRect.width * 0.5f / tanx;
    camera->scaleInvZ = 1.0f / camera->scaleZ;

    float invAspect = (float)(screenRect.height) / (float)(screenRect.width);
    float tany = tanx * invAspect;

    // left side frustum clip
    camera->frustumPlanes[0].normal = Vec3Normalize({1.0f / tanx, 0, 1.0f});
    camera->frustumPlanes[0].type = PLANE_ANYZ;
    // right side frustum clip
    camera->frustumPlanes[1].normal = Vec3Normalize({-1.0f / tanx, 0, 1.0f});
    camera->frustumPlanes[1].type = PLANE_ANYZ;
    // top side frustum clip
    camera->frustumPlanes[2].normal = Vec3Normalize({0, -1.0f / tany, 1.0f});
    camera->frustumPlanes[2].type = PLANE_ANYZ;
    // bottom side frustum clip
    camera->frustumPlanes[3].normal = Vec3Normalize({0, 1.0f / tany, 1.0f});
    camera->frustumPlanes[3].type = PLANE_ANYZ;
}

inline Vec3f TransformPointToView(const Camera *camera, Vec3f point)
{
    Vec3f result;
    
    Vec3f pt = point - camera->position;
    result.x = Vec3Dot(camera->rotx, pt);
    // swap y and z, same reason as in TransformDirectionToView
    result.y = Vec3Dot(camera->rotz, pt);
    result.z = Vec3Dot(camera->roty, pt);

    return result;
}

inline Vec3f TransformDirectionToView(const Camera *camera, Vec3f dir)
{
    Vec3f result;
    /*
    Nw, Nv are normals of the plane in world and view space respectively.
    Pw, Pv are points on the plane in world and view space respectively.
    rot is the rotation matrix of the camera

    Nw * Pw = 0, Nv * Pv = 0, Pv = inverse(rot) * Pw
    Nw * inverse(inverse(rot)) * inverse(rot) * Pw = 0
    --> Nv = Nw * inverse(inverse(rot))
    
    quake's world coordinate system
            ^ z+  
            |    ^ y+
            |   /
            |  /
            | /
            |--------> x+

	quake's view coordinate system
            ^ y+  
            |    ^ z+
            |   /
            |  /
            | /
            |--------> x+

    It's not the transformation of coordinate system but just swap of axis 
    notation
    
    To transform points from world space to view space, we first transform 
    points normally, then swap y and z values.
    */
    result.x = Vec3Dot(dir, camera->rotx);
    result.y = Vec3Dot(dir, camera->rotz);
    result.z = Vec3Dot(dir, camera->roty);

    return result;
}

void TransformFrustum(Camera *camera)
{
    Vec3f normal = {0};
    Vec3f normal_world = {0};
    /*
    Nw, Nv are normals of the plane in world and view space respectively.
    Pw, Pv are points on the plane in world and view space respectively.
    rot is the rotation matrix of the camera

    Nw * Pw = 0, Nv * Pv = 0, Pw = rot * Pv
    Nv * inverse(rot) * (rot) * Pv = 0
    --> Nw = Nv * inverse(rot)

    */
    for (int i = 0; i < 4; ++i)
    {
        normal = camera->frustumPlanes[i].normal;

        // swap y and z
        normal_world.x = normal.x * camera->rotx[0] + normal.z * camera->roty[0] + normal.y * camera->rotz[0]; 
        normal_world.y = normal.x * camera->rotx[1] + normal.z * camera->roty[1] + normal.y * camera->rotz[1]; 
        normal_world.z = normal.x * camera->rotx[2] + normal.z * camera->roty[2] + normal.y * camera->rotz[2];

        camera->worldFrustumPlanes[i].normal = normal_world;
        
        // camera position is on every frustum plane
        camera->worldFrustumPlanes[i].distance = Vec3Dot(camera->position, normal_world);
    }

    camera->worldFrustumPlanes[0].leftEdge = 1;
    camera->worldFrustumPlanes[1].rightEdge = 1;
    camera->worldFrustumPlanes[2].reserved[0] = 1;
    camera->worldFrustumPlanes[3].reserved[0] = 0;
}

/* 
Set up indices for bounding box culling against planes. To detemine if the 
bounding box is completely on the opposite side of the plane that the normal 
points to, we can test if the furthest point along the direction of the 
normal is on the opposite side. Test succeeding means the bounding box is 
completely on the opposite side. 
example in one dimension:
<---------A---p0-------p1-------   normal goes from right to left
To determine is the line p0p1 is completely on right side of A, we test if p0 
is on the right of A.
*/ 
void SetupFrustumIndices(Camera *camera)
{
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            /*
            Assume we want to reject points that are on the opposite side of the 
            plane the normal points to. Reject Point should be the one that's 
            the furthest along the normal and Accpet Point the furthest along 
            the opposite normal direction. And we can calculate 2 points 
            dimension by dimension. Define the minmax of a bounding box as 
            minmax[6]. If normal.x < 0 then Reject Point's x should be 
            minmax[0], Accept Point's x should be minmax[3]. If normal.y > 0 
            then Reject Point's y should minmax[4], Accpet Point's y should be
            minmax[2].
            */
            if (camera->worldFrustumPlanes[i].normal[j] < 0)
            {
                camera->frustumIndices[i * 6 + j] = j; // reject point
                camera->frustumIndices[i * 6 + j + 3] = j + 3; // accept point
            }
            else
            {
                camera->frustumIndices[i * 6 + j] = j + 3; // reject point
                camera->frustumIndices[i * 6 + j + 3] = j; // accept point
            }
        }
    }
}

void UpdateVisibleLeaves(RenderData *renderdata)
{
    if (renderdata->oldViewLeaf == renderdata->currentViewLeaf)
    {
        return ;
    }

    renderdata->updateCountPVS++;

    Model *worldModel = renderdata->worldModel;

    U8 *visibility = ModelGetDecompressedPVS(renderdata->currentViewLeaf, worldModel);
    Leaf *leafHead = worldModel->leaves + 1;

    for (int i = 0; i < worldModel->numLeaf; ++i)
    {
        if (visibility[i >> 3] & (1 << (i & 7)))
        {
            Node *node = (Node *)(leafHead + i);
            do 
            {
                if (node->visibleFrame == renderdata->updateCountPVS)
                {
                    break;
                }
                node->visibleFrame = renderdata->updateCountPVS;
                node = node->parent;
            } while (node != NULL);
        }
    }
}

struct LastVertData
{
    float screenX1;
    float screenY1;
    float viewInvZ1; // inverse z in view space
    int ceilScreenY1;
    U32 isValid;
};

U32 EmitIEdge(Vec3f v0, Vec3f v1, Camera *camera, LastVertData *lastVertData, 
                 B32 onlyNearInvZ, U32 *needCache, Edge *edgeLord, 
                 RenderData *renderdata)
{
    float screenX0 = 0, screenX1 = 0; 
    float screenY0 = 0, screenY1 = 0;
    float viewInvZ0 = 0, viewInvZ1 = 0;
    I32 ceilScreenY0 = 0, ceilScreenY1 = 0;

    if (lastVertData->isValid)
    {
        screenX0 = lastVertData->screenX1;
        screenY0 = lastVertData->screenY1;
        viewInvZ0 = lastVertData->viewInvZ1;
        ceilScreenY0 = lastVertData->ceilScreenY1;
    }
    else
    {
        Vec3f vertView0 = TransformPointToView(camera, v0);

        if (vertView0.z < camera->nearZ)
        {
            vertView0.z = camera->nearZ;
        }

        viewInvZ0 = 1.0f / vertView0.z;
        float scale = camera->scaleZ * viewInvZ0;
        screenX0 = camera->screenCenter.x + scale * vertView0.x;
        screenY0 = camera->screenCenter.y - scale * vertView0.y;

        ASSERT(screenY0 > (camera->screenMin.y - 0.5f) 
               && screenY0 < (camera->screenMax.y + 0.5f));

        screenX0 = Clamp(camera->screenMin.x, camera->screenMax.x, screenX0);
        screenY0 = Clamp(camera->screenMin.y, camera->screenMax.y, screenY0);

        ceilScreenY0 = (I32)ceilf(screenY0);
    }

    Vec3f vertView1 = TransformPointToView(camera, v1);

    // TODO lw: why this works?
    if (vertView1.z < camera->nearZ)
    {
        vertView1.z = camera->nearZ;
    }

    viewInvZ1 = 1.0f / vertView1.z;
    float scale = camera->scaleZ * viewInvZ1;
    screenX1 = camera->screenCenter.x + scale * vertView1.x;
    screenY1 = camera->screenCenter.y - scale * vertView1.y;

    ASSERT(screenY1 > (camera->screenMin.y - 0.5f)
           && screenY1 < (camera->screenMax.y + 0.5f));

    screenX1 = Clamp(camera->screenMin.x, camera->screenMax.x, screenX1);
    screenY1 = Clamp(camera->screenMin.y, camera->screenMax.y, screenY1);

    ceilScreenY1 = (I32)ceilf(screenY1);

    // find minimum z value
    if (viewInvZ1 > viewInvZ0)
    {
        viewInvZ0 = viewInvZ1;
    }
    if (viewInvZ0 > renderdata->nearestInvZ) 
    {
        renderdata->nearestInvZ = viewInvZ0;
    }

    // backup v1 data for reuse
    lastVertData->screenX1 = screenX1;
    lastVertData->screenY1 = screenY1;
    lastVertData->viewInvZ1 = viewInvZ1;
    lastVertData->ceilScreenY1 = ceilScreenY1;

    // For right edges made of clipped points, we only need nearest z value, we
    // don't need stepping infomation because it's on right screen edge.
    if (onlyNearInvZ)
    {
        return 0;
    }

    if (ceilScreenY0 == ceilScreenY1)
    {
        // cache unclipped horizontal edges as fully clipped
        if (*needCache != EDGE_PARTIALLY_CLIPPED)
        {
            *needCache = EDGE_FULLY_CLIPPED | (renderdata->frameCount & FRAMECOUNT_MASK);
        }
        return 1;
    }

    IEdge *iedge = renderdata->currentIEdge++;

    iedge->owner = edgeLord;
    iedge->nearInvZ = viewInvZ0;

    // the screen origin is at top-left corner, thus topY has smaller value
    I32 topY, bottomY;
    float x_start, x_step;

    // points are passed in clock-wise, trailing(right) edge
    if (ceilScreenY0 < ceilScreenY1)
    {   
        topY = ceilScreenY0;
        bottomY = ceilScreenY1 - 1; // floor(screenY1)
        x_step = (screenX1 - screenX0) / (screenY1 - screenY0);
        x_start = screenX0 + (ceilScreenY0 - screenY0) * x_step;

        iedge->isurfaceOffsets[0] = 
            (U32)(renderdata->currentISurface - renderdata->isurfaces);
        iedge->isurfaceOffsets[1] = 0;
    }
    else // ceilY0 > ceilY1, leading(left) edge
    {   
        topY = ceilScreenY1;
        bottomY = ceilScreenY0 - 1; // floor(screenY0)
        x_step = (screenX0 - screenX1) / (screenY0 - screenY1);
        x_start = screenX1 + (ceilScreenY1 - screenY1) * x_step;

        iedge->isurfaceOffsets[1] = 
            (U32)(renderdata->currentISurface - renderdata->isurfaces);
        iedge->isurfaceOffsets[0] = 0;
    }

    iedge->x_step = FloatToFixed20(x_step);
    // ensure x_start don't have fraction, on a whole pixel
    iedge->x_start = FloatToFixed20(x_start) + 0xfffff; 

    // clamping edge->x_start

    // sort the iedge in ascending order of x value
    fixed20 x_check = iedge->x_start;
    // trailing edge
    if (iedge->isurfaceOffsets[0])
    {   
        // if a leading and a trailing have the same x_start, sort leading edge
        // in front
        x_check++; 
    }

    if (renderdata->newIEdges[topY] == NULL 
        || x_check < renderdata->newIEdges[topY]->x_start)
    {   
        iedge->next = renderdata->newIEdges[topY];
        renderdata->newIEdges[topY] = iedge;
    }
    else
    {
        IEdge *temp = renderdata->newIEdges[topY];
        while (temp->next != NULL && temp->next->x_start < x_check)
        {
            temp = temp->next;
        }
        // insert edge inbetween temp and temp->next
        iedge->next = temp->next;
        temp->next = iedge;
    }

    // insert in front, the edge will be removed when scanline reaches the bottomY
    iedge->nextRemove = renderdata->removeIEdges[bottomY];
    renderdata->removeIEdges[bottomY] = iedge;

    return 1;
}

U32 ReEmitIEdge(Edge *edge, RenderData *renderdata)
{
    IEdge *iedge = renderdata->iedges + edge->cachedIEdgeOffset;

    // If the edge was used as leading edge, now it must be trailing edge.
    if (iedge->isurfaceOffsets[0] == NULL)
    {
        iedge->isurfaceOffsets[0] = 
            (U32)(renderdata->currentISurface - renderdata->isurfaces);
    }
    else
    {
        iedge->isurfaceOffsets[1] = 
            (U32)(renderdata->currentISurface - renderdata->isurfaces);
    }

    if (iedge->nearInvZ > renderdata->nearestInvZ)
    {
        renderdata->nearestInvZ = iedge->nearInvZ;
    }

    return 1;
}

// If the start point of the edge is inside the frustum and the end point of the
// edge is outside, the clipped point is an exit point. Otherwise it's an enter
// point. We want to make a vertical edge from exit point to enter point, so the
// new edge following the same clock-wise winding. 
struct SurfaceClipResult
{
    bool leftEdgeClipped;
    bool rightEdgeClipped;
    Vec3f vertLeftEnter;
    Vec3f vertLeftExit;
    Vec3f vertRightEnter;
    Vec3f vertRightExit;
};

struct EdgeClipResult
{
    Vec3f v0, v1;
    U32 isV0Unclipped;
    U32 isFullyClipped;
};

EdgeClipResult ClipEdge(Vec3f v0, Vec3f v1, ClipPlane *clipPlane, 
                        SurfaceClipResult *scr, U32 *needCache, I32 frameCount)
{
    EdgeClipResult result = {0};
    result.isV0Unclipped = 1;

    while (clipPlane != NULL)
    {
        float d0 = Vec3Dot(v0, clipPlane->normal) - clipPlane->distance;
        float d1 = Vec3Dot(v1, clipPlane->normal) - clipPlane->distance;

        if (d0 >= 0) // v0 is not clipped
        {   
            if (d1 < 0) // v1 is clipped
            {   
                // don't cache partially clipped edge
                *needCache = EDGE_PARTIALLY_CLIPPED;

                float t = d0 / (d0 - d1);
                Vec3f newPoint = v0 + t * (v1 - v0);

                v1 = newPoint;
                if (clipPlane->leftEdge)
                {
                    scr->leftEdgeClipped = true;
                    scr->vertLeftExit = newPoint;
                }
                else if (clipPlane->rightEdge)
                {
                    scr->rightEdgeClipped = true;
                    scr->vertRightExit = newPoint;
                }
            }
            // else, both points are unclipped, try next clip plane
        }
        else // d0 < 0, v0 is clipped
        {   
            result.isV0Unclipped = 0;

            if (d1 < 0) // both points are clipped
            {   
                result.isFullyClipped = 1;
                // TODO lw: why this check?
                if (!scr->leftEdgeClipped)
                {
                    *needCache = EDGE_FULLY_CLIPPED | (frameCount & FRAMECOUNT_MASK);
                }
                break;
            }
            else // only v0 is clipped
            {
                *needCache = EDGE_PARTIALLY_CLIPPED;

                float t = d0 / (d0 - d1);
                Vec3f newPoint = v0 + t * (v1 - v0);

                v0 = newPoint;
                if (clipPlane->leftEdge)
                {
                    scr->leftEdgeClipped = true;
                    scr->vertLeftEnter = newPoint;
                }
                else if (clipPlane->rightEdge)
                {
                    scr->rightEdgeClipped = true;
                    scr->vertRightEnter = newPoint;
                }
            }
        }

        clipPlane = clipPlane->next;
    }

    result.v0 = v0;
    result.v1 = v1;

    return result;
}

void RenderFace(Surface *surface, RenderData *renderdata, Camera *camera, 
                B32 isInSubmodel, I32 clipflag)
{
    // no more surface
    if (renderdata->currentISurface >= renderdata->endISurface)
    {
        return ;
    }
    // no more edge
    if ((renderdata->currentIEdge + surface->numEdge + 4) >= renderdata->endIEdge)
    {
        renderdata->outOfIEdges += surface->numEdge;
        return ;
    }

    ClipPlane *clipPlane = NULL;
    U32 mask = 0x08;
    for (int i = 3; i >= 0; --i, mask >>= 1)
    {
        if (clipflag & mask)
        {
            camera->worldFrustumPlanes[i].next = clipPlane;
            clipPlane = &camera->worldFrustumPlanes[i];
        }
    }

    Vertex *vertices = renderdata->worldModel->vertices;
    I32 *surfaceEdges = renderdata->worldModel->surfaceEdges;
    Edge *edges = renderdata->worldModel->edges;

    U32 edgeEmitted = 0;
    SurfaceClipResult scr = {0};
    EdgeClipResult ecr = {0};
    U32 iedgeOffset = 0;
    LastVertData lastVertData = {0};
    renderdata->nearestInvZ = 0;

    // A surface is convex, so one clip plane will at most generate one pair of 
    // enter and exit clip points
    for (int i = 0; i < surface->numEdge; ++i)
    {
        // TODO lw: why needs negative edgeIndex?
        int edgeIndex = surfaceEdges[surface->firstEdge + i];
        int startVertIndex = 0;
        int endVertIndex = 1;
        if (edgeIndex <=0)
        {
            edgeIndex = -edgeIndex;
            startVertIndex = 1;
            endVertIndex = 0;
        }

        // get the edge
        Edge *edge = edges + edgeIndex;

        if (isInSubmodel == false)
        {
            if (edge->cachedIEdgeOffset & EDGE_FULLY_CLIPPED)
            {
                if ((edge->cachedIEdgeOffset & FRAMECOUNT_MASK) == (U32)renderdata->frameCount)
                {
                    lastVertData.isValid = 0;
                    // If iedge is fully clipped or horizontal fully accepted 
                    // and we are still in the same frame, meaning we already 
                    // done clipping on this edge, so we can skip ClipEdge() 
                    // and EmitIEdge().
                    continue;
                }
            }
            else
            {
                iedgeOffset = (U32)(renderdata->currentIEdge - renderdata->iedges);
                IEdge *tempIEdge = (IEdge *)(renderdata->iedges + edge->cachedIEdgeOffset);

                // If this iedge's owner was completely inside and emitted 
                // before, but now is used for another surface, we re-emit it.
                if ((iedgeOffset > edge->cachedIEdgeOffset) 
                    && (tempIEdge->owner == edge)) 
                {
                    edgeEmitted |= ReEmitIEdge(edge, renderdata);
                    lastVertData.isValid = 0;
                    continue;
                }
            }
        }
        iedgeOffset = (U32)(renderdata->currentIEdge - renderdata->iedges);

        Vec3f startVert = vertices[edge->vertIndex[startVertIndex]].position;
        Vec3f endVert = vertices[edge->vertIndex[endVertIndex]].position;

        ecr = ClipEdge(startVert, endVert, clipPlane, &scr, &iedgeOffset, renderdata->frameCount);
        lastVertData.isValid &= ecr.isV0Unclipped;
        if (!ecr.isFullyClipped)
        {
            edgeEmitted |= EmitIEdge(ecr.v0, ecr.v1, camera, &lastVertData, false, 
                                     &iedgeOffset, edge, renderdata);
        }

        edge->cachedIEdgeOffset = iedgeOffset;
        lastVertData.isValid = 1;
    }

    if (scr.leftEdgeClipped)
    {
		// Based on how clip plane list is set up, left clip plane must be the 
        // first one, namely pclip. Passing pclip->next will exlucde the left 
        // clip plane
        ecr = ClipEdge(scr.vertLeftExit, scr.vertLeftEnter, clipPlane->next, 
                       &scr, &iedgeOffset, renderdata->frameCount);
        lastVertData.isValid = 0;
        if (!ecr.isFullyClipped)
        {
            edgeEmitted |= EmitIEdge(ecr.v0, ecr.v1, camera, &lastVertData, false, 
                                     &iedgeOffset, NULL, renderdata);
        }
    }
    if (scr.rightEdgeClipped)
    {
        // view_clipplanes[1] is the right clip plane, passing 
        // view_clipplanes[1].next will exclude the right clip plane.
        ecr = ClipEdge(scr.vertRightExit, scr.vertRightEnter, 
                       camera->worldFrustumPlanes[1].next, &scr, &iedgeOffset,
                       renderdata->frameCount);
        lastVertData.isValid = 0;
        if (!ecr.isFullyClipped)
        {
            edgeEmitted |= EmitIEdge(ecr.v0, ecr.v1, camera, &lastVertData, true, 
                                     &iedgeOffset, NULL, renderdata);
        }
    }

    if (!edgeEmitted)
    {
        return ;
    }

    renderdata->surfaceCount++;

    ISurface *isurface = renderdata->currentISurface;

    isurface->data = (void *)surface;
    isurface->nearestInvZ = renderdata->nearestInvZ;
    isurface->flags = surface->flags;
    isurface->isInSubmodel = isInSubmodel;
    isurface->spanState = 0;
    // isurface->entity = ;
    isurface->key = renderdata->currentKey++;
    isurface->spans = NULL;

    Vec3f normal_view = TransformDirectionToView(camera, surface->plane->normal);

    /* 
    Affine transformation won't change the distance.
    Q is a point on the plane, O is the world orign, P is camera position
    N is the normal of the plane
    distance_world = (Q - O) * N  and distance_view = (Q - P) * N
    distance_view = ((Q - O) - (P - O)) * N = distance_world - (P - O) * N
    */
    float distanceInv = 1.0f / (surface->plane->distance - Vec3Dot(camera->position, surface->plane->normal));

    /* wl:
	Instead of calculating 1/z by interpolating between edges,
	we use plane equation to get 1/z.

	a*x + b*y + c*z - d = 0                 eq.1
	x/x_s = z/z_s --> x = (z/z_s) * x_s     eq.2
	y/-y_s = z/z_s --> y = (z/z_s) * -y_s   eq.3

	put eq.2 and eq.3 into eq.1, we get
	1/z = ((a/z_s) * x_s - (b/z_s) * y_s + c) / d
		= ((a/z_s)/d * x_s) - ((b/z_s)/d * y_s) + c/d

	Note: Above calculation assumes the origin is at the center of the view,
	however, screen space has the origin at top-left corner. There is some
	translation work needs to be done.
	*/
    isurface->zi_stepx = normal_view.x * camera->scaleInvZ * distanceInv;
    isurface->zi_stepy = normal_view.y * camera->scaleInvZ * distanceInv;
    isurface->zi_d = normal_view.z * distanceInv
                   - camera->screenCenter.x * isurface->zi_stepx
                   - camera->screenCenter.y * isurface->zi_stepy;

    renderdata->currentISurface++;
}

void RecurseWorldNode(Node *node, Camera *camera, RenderData *renderdata, int clipflag)
{
    if (node->contents == CONTENTS_SOLID)
    {
        return ;
    }
    if (node->visibleFrame != renderdata->updateCountPVS)
    {
        return ;
    }

    if (clipflag)
    {
        Vec3f rejectPoint = {0};
        Vec3f acceptPoint = {0};
        double d = 0;
        int *index = NULL;
        for (int i = 0; i < 4; ++i) // 4 clip planes
        {
            if (clipflag & (1<<i))
            {
                // see SetupFrustumIndices for information
                index = &(camera->frustumIndices[i * 6]);
                rejectPoint[0] = node->minmax[index[0]];
                rejectPoint[1] = node->minmax[index[1]];
                rejectPoint[2] = node->minmax[index[2]];

                d = Vec3Dot(rejectPoint, camera->worldFrustumPlanes[i].normal)
                    - camera->worldFrustumPlanes[i].distance;

                if (d <= 0)
                {   // bounding box is completely outside
                    return ;
                }

                index = &(camera->frustumIndices[i * 6 + 3]);
                acceptPoint[0] = node->minmax[index[0]];
                acceptPoint[1] = node->minmax[index[1]];
                acceptPoint[2] = node->minmax[index[2]];

                d = Vec3Dot(acceptPoint, camera->worldFrustumPlanes[i].normal)
                    - camera->worldFrustumPlanes[i].distance;

                if (d >= 0)
                {   // bounding box is completely inside
                    clipflag &= ~(1<<i);
#if 0
                    Vec3f min = {(float)node->minmax[0], (float)node->minmax[1], (float)node->minmax[2]};
                    Vec3f max = {(float)node->minmax[3], (float)node->minmax[4], (float)node->minmax[5]};
                    double min_d = Vec3Dot(min, camera->worldFrustumPlanes[i].normal)
                                 - camera->worldFrustumPlanes[i].distance;
                    double max_d = Vec3Dot(max, camera->worldFrustumPlanes[i].normal)
                                 - camera->worldFrustumPlanes[i].distance;

                    ASSERT(min_d >= 0 && max_d >= 0);
#endif
                }
            }
        }
    }

    // if the node is leaf, draw it
    if (node->contents < 0)
    {
        Leaf *leaf = (Leaf *)node;
        Surface **mark = leaf->firstMarksurface;
        int count = leaf->numMarksurface;
        while (count)
        {
            (*mark)->visibleFrame = renderdata->frameCount;
            mark++;
            count--;
        }
        if (leaf->efrags)
        {
            // store efrags
        }
        leaf->key = renderdata->currentKey;
        renderdata->currentKey++;
    }
    // if it's a node, decide which way to go down
    else
    {
        double d = 0;
        // These cases are so unnecessary!
        switch (node->plane->type)
        {
            case PLANE_X:
            {
                d = camera->position.x - node->plane->distance;
            } break;

            case PLANE_Y:
            {
                d = camera->position.y - node->plane->distance;
            } break;

            case PLANE_Z:
            {
                d = camera->position.z - node->plane->distance;
            } break;

            default:
            {
                d = Vec3Dot(camera->position, node->plane->normal) 
                    - node->plane->distance;
            } break;
        }

        int side = 0;
        if (d < 0)
        {
            side = 1;
        }

        RecurseWorldNode(node->children[side], camera, renderdata, clipflag);

        // draw surface
        int count = node->numSurface;
        if (count)
        {
            Surface *surface = renderdata->worldModel->surfaces + node->firstSurface;

            if (d < -BACKFACE_EPSILON)
            {
                while (count)
                {
                    if ((surface->flags & SURF_PLANE_BACK) 
                        && (surface->visibleFrame == renderdata->frameCount))
                    {
                        RenderFace(surface, renderdata, camera, false, clipflag);
                    }
                    surface++;
                    count--;
                }
            }
            else if (d > BACKFACE_EPSILON)
            {
                while (count)
                {
                    if (!(surface->flags & SURF_PLANE_BACK) 
                        && (surface->visibleFrame == renderdata->frameCount))
                    {
                        RenderFace(surface, renderdata, camera, false, clipflag);
                    }
                    surface++;
                    count--;
                }
            }

            renderdata->currentKey++;
        }

        RecurseWorldNode(node->children[!side], camera, renderdata, clipflag);
    }
}

void RenderWorld(Node *nodes, Camera *camera, RenderData *renderdata)
{
    RecurseWorldNode(nodes, camera, renderdata, 15);
}

void InsertNewIEdges(IEdge *edgesToAdd, IEdge *edgeList)
{
    IEdge *next_edge;
    while (edgesToAdd != NULL)
    {
        next_edge = edgesToAdd->next;
        // unroll the loop a bit
        for(;;) 
        {
            if (edgeList->x_start >= edgesToAdd->x_start)
            {
                break;
            }
            edgeList = edgeList->next;
            if (edgeList->x_start >= edgesToAdd->x_start)
            {
                break;
            }
            edgeList = edgeList->next;
            if (edgeList->x_start >= edgesToAdd->x_start)
            {
                break;
            }
            edgeList = edgeList->next;
            if (edgeList->x_start >= edgesToAdd->x_start)
            {
                break;
            }
            edgeList = edgeList->next;
        } 

        // insert 'edgesToAdd' intween 'edgelist->prev' and 'edgelist'
		edgesToAdd->next = edgeList;
		edgesToAdd->prev = edgeList->prev;
		edgeList->prev->next = edgesToAdd;
		edgeList->prev = edgesToAdd;

        edgesToAdd = next_edge;
    }
}

void LeadingEdge(IEdge *iedge, ISurface *isurfaces, int y, ESpan **currentSpan)
{
    ISurface *topISurf;

    if (!iedge->isurfaceOffsets[1])
    {
        return ;
    }

    // get the isurface this iedge belongs to
    ISurface *isurf = &isurfaces[iedge->isurfaceOffsets[1]];

    ASSERT(isurf->spanState == 0);

    // '->' has higher precedence than '++a', add parenthesis anyway
    if (++(isurf->spanState) == 1)
    {
        /*
        if (surf->isInSubmodel)
        {
            r_submodelactive++;
        }
        */

        topISurf = isurfaces[1].next;

        if (isurf->key < topISurf->key)
        {
            goto newtop;
        }

        // If 2 isurfaces are on the same plane, the one that's already active
        // is in front. But if isurf is in submodel, we compare their z value to
        // decide if isurf is in front.
        if ((isurf->key == topISurf->key) && isurf->isInSubmodel )
        {
            float x = Fixed20ToFloat(iedge->x_start - 0xfffff);
            float newInvZ = isurf->zi_d + isurf->zi_stepx * x + isurf->zi_stepy * y;
            float newInvZBottom = newInvZ * 0.99f; // TODO lw: ???
            float currentTopInvZ = topISurf->zi_d + topISurf->zi_stepx * x + topISurf->zi_stepy * y;
            if (newInvZBottom >= currentTopInvZ)
            {
                goto newtop;
            }

            // TODO lw: ???
            float newInvZTop = newInvZ * 1.01f;
            if (newInvZTop >= currentTopInvZ
                && isurf->zi_stepx >= topISurf->zi_stepx)
            {
                goto newtop;
            }
        }

continuesearch:
        do
        {
            topISurf = topISurf->next;
        } while (isurf->key > topISurf->key);

        if (isurf->key == topISurf->key)
        {
            if (isurf->isInSubmodel)
            {
                float x = Fixed20ToFloat(iedge->x_start - 0xfffff);
                float newInvZ = isurf->zi_d + isurf->zi_stepx * x + isurf->zi_stepy * y;
                float newInvZBottom = newInvZ * 0.99f; 
                float currentTopInvZ = topISurf->zi_d + topISurf->zi_stepx * x + topISurf->zi_stepy * y;
                if (newInvZBottom >= currentTopInvZ)
                {
                    goto gotposition;
                }

                float newInvZTop = newInvZ * 1.01f;
                if (newInvZTop >= currentTopInvZ
                    && isurf->zi_stepx >= topISurf->zi_stepx)
                {
                    goto gotposition;
                }

                goto continuesearch;
            }
            else
            {
                goto continuesearch;
            }
        }

        goto gotposition;

newtop: 
        // emit a span
        int px = iedge->x_start >> 20;
        if (px > topISurf->x_last)
        {
            ESpan *span = *currentSpan;
            (*currentSpan)++;
            span->x_start = topISurf->x_last;
            span->count = px - span->x_start;
            span->y = y;
            // insert in front
            span->next = topISurf->spans;
            topISurf->spans = span;
        }
        isurf->x_last = px;

gotposition:
        // insert isurf in front of topISurf
        isurf->next = topISurf;
        isurf->prev = topISurf->prev;
        topISurf->prev->next = isurf;
        topISurf->prev = isurf;
    }
}

void TrailingEdge(IEdge *iedge, ISurface *isurfaces, int y, ESpan **currentSpan)
{
    ISurface *isurf = &isurfaces[iedge->isurfaceOffsets[0]];
    
    if (--(isurf->spanState) == 0)
    {
        /*
        if (isurf->isInSubmodel)
        {
            r_submodelactive--;
        }
        */
        // only when it's the top isurface
        if (isurf == isurfaces[1].next)
        {
            // emit span
            int px = iedge->x_start >> 20;
            if (px > isurf->x_last)
            {
                ESpan *span = *currentSpan;
                (*currentSpan)++;
                span->x_start = isurf->x_last;
                span->count = px - span->x_start;
                span->y = y;
                // insert in front
                span->next = isurf->spans;
                isurf->spans = span;
            }
            isurf->next->x_last = px;
        }
        // remove this isurface
        isurf->prev->next = isurf->next;
        isurf->next->prev = isurf->prev;
    }
}

// After we iterate all iedge on the scaneline, we still need to take care of 
// the span between the last edge and the right screen edge
void CleanupSpan(ISurface *isurfaces, int screenEndX, int y, ESpan **currentSpan)
{
    ISurface *isurf = isurfaces[1].next;
    if (isurf->x_last < screenEndX)
    {
        ESpan *span = *currentSpan;
        (*currentSpan)++;
        span->x_start = isurf->x_last;
        span->count = screenEndX - span->x_start;
        span->y = y;
        // insert in front
        span->next = isurf->spans;
        isurf->spans = span;
    }
    // reset all span states
    do
    {
        isurf->spanState = 0;
        isurf = isurf->next;
    } while (isurf != &isurfaces[1]);
}

void GenerateSpan(ISurface *isurfaces, int screenStartX, int screenEndX, 
                  int scanliney, ESpan **currentSpan, IEdge *iedgeHead, IEdge *iedgeTail)
{
    // clear active isurfaces
    isurfaces[1].next = &isurfaces[1];
    isurfaces[1].prev = &isurfaces[1];
    isurfaces[1].x_last = screenStartX;

    for (IEdge *iedge = iedgeHead->next; iedge != iedgeTail; iedge = iedge->next)
    {
        if (iedge->isurfaceOffsets[0])
        {
            TrailingEdge(iedge, isurfaces, scanliney, currentSpan);
        }
        if (iedge->isurfaceOffsets[1])
        {
            LeadingEdge(iedge, isurfaces, scanliney, currentSpan);
        }
    }

    CleanupSpan(isurfaces, screenEndX, scanliney, currentSpan);
}

void RemoveEdges(IEdge *iedge)
{
    do
    {
        iedge->prev->next = iedge->next;
        iedge->next->prev = iedge->prev;
        iedge = iedge->nextRemove;
    } while (iedge != NULL);
}

// step x as scanline goes down 1 unit in y
void StepActiveIEdgeX(IEdge *iedge, IEdge *iedgeTail, IEdge *iedgeAfterTail)
{
    do
    {
        for (;;)
        {
            iedge->x_start += iedge->x_step;
            if (iedge->x_start < iedge->prev->x_start)
                break;
            iedge = iedge->next;

            iedge->x_start += iedge->x_step;
            if (iedge->x_start < iedge->prev->x_start)
                break;
            iedge = iedge->next;

            iedge->x_start += iedge->x_step;
            if (iedge->x_start < iedge->prev->x_start)
                break;
            iedge = iedge->next;

            iedge->x_start += iedge->x_step;
            if (iedge->x_start < iedge->prev->x_start)
                break;
            iedge = iedge->next;
        }

        if (iedge == iedgeAfterTail)
            return;

        IEdge *next = iedge->next;

        // pull the edge out and insert it back into the right place
        iedge->prev->next = iedge->next;
        iedge->next->prev = iedge->prev;

        IEdge *temp = iedge->prev->prev;
        while (temp->x_start > iedge->x_start)
        {
            temp = temp->prev;
        }
        iedge->next = temp->next;
        iedge->prev = temp;
        iedge->next->prev = iedge;
        temp->next = iedge;

        iedge = next;
    } while (iedge != iedgeTail);
}

void DrawSolidSurfaces(ISurface *isurf, U8 *buffer, I32 bytesPerRow)
{
    U8 pixelColor = (size_t)isurf->data & 0xff;

    for (ESpan *span = isurf->spans; span != NULL; span = span->next)
    {
        U8 *pixel = buffer + bytesPerRow * span->y + span->x_start;
        I32 startX = span->x_start;
        // don't drawing trailing edges
        I32 endX = startX + span->count - 1;

        for (I32 i = startX; i < endX; ++i)
        {
            *pixel++ = (U8)pixelColor;
        }
    }
}

void DrawZBuffer(float zi_stepx, float zi_stepy, float zi_d, ESpan *span, 
                 float *zbuffer, I32 width)
{
    do 
    {
        float *zpixel = zbuffer + width * span->y + span->x_start;

        float invz = zi_stepx * span->x_start + zi_stepy * span->y + zi_d;
        int startX = span->x_start;
        int endX = span->x_start + span->count - 1;

        for (int i = startX; i < endX; ++i)
        {
            *zpixel++ = invz;
            invz += zi_stepx;
        }

        span = span->next;
    } while (span != NULL);
}

void DrawSurfaces(ISurface *isurfaces, ISurface *endISurf, 
                  U8 *pbuffer, I32 bytesPerRow, float *zbuffer, I32 width)
{
    for (ISurface *isurf = &isurfaces[1]; isurf < endISurf; ++isurf)
    {
        if (!isurf->spans)
        {
            continue;
        }
        DrawSolidSurfaces(isurf, pbuffer, bytesPerRow);
        DrawZBuffer(isurf->zi_stepx, isurf->zi_stepy, isurf->zi_d, 
                    isurf->spans, zbuffer, width);
    }
}

#define MAX_SPAN_NUM 5120

void ScanEdge(Recti rect, IEdge **iedges, IEdge **removeIEdges, 
              ISurface *isurfaces, ISurface *endISurf,
              U8 *pbuffer, I32 bytesPerRow, float *zbuffer, I32 bufferWidth)
{
    U8 basespans[MAX_SPAN_NUM * sizeof(ESpan) + CACHE_SIZE];
    ESpan *spanlist = (ESpan *)((size_t)(basespans + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
    ESpan *maxSpan = &spanlist[MAX_SPAN_NUM - rect.width]; // TODO lw: ???
    ESpan *currentSpan = spanlist;

    IEdge iedgeHead = {0}; 
    IEdge iedgeTail = {0}; 
    IEdge iedgeAfterTail = {0};
    IEdge iedgeSentinel = {0};

    I32 screenStartX = rect.x;
    I32 screenEndX = rect.x + rect.width;

    iedgeHead.x_start = rect.x << 20;
    screenStartX = iedgeHead.x_start >> 20; // TODO lw: ???
    iedgeHead.x_step = 0;
    iedgeHead.prev = NULL;
    iedgeHead.next = &iedgeTail;
    iedgeHead.isurfaceOffsets[0] = 0;
    iedgeHead.isurfaceOffsets[1] = 1;

    // NOTE lw: operator '+' precedes operator '<<'
    iedgeTail.x_start = ((rect.x + rect.width) << 20) + 0xfffff; // TODO lw: why 0xfffff?
    screenEndX = iedgeTail.x_start >> 20; // TODO lw: ???
    iedgeTail.x_step = 0;
    iedgeTail.prev = &iedgeHead;
    iedgeTail.next = &iedgeAfterTail;
    iedgeTail.isurfaceOffsets[0] = 1;
    iedgeTail.isurfaceOffsets[1] = 0;

    iedgeAfterTail.x_start = -1; // force a move // TODO lw: ???
    iedgeAfterTail.x_step = 0;
    iedgeAfterTail.prev = &iedgeTail;
    iedgeAfterTail.next = &iedgeSentinel;

    iedgeSentinel.x_start = 2000 << 24; // make sure nothing sorts past this
    iedgeSentinel.prev = &iedgeAfterTail;

    pbuffer = pbuffer + rect.y * bytesPerRow + rect.x;
    zbuffer = zbuffer + rect.y * bufferWidth + rect.x;

    I32 bottomY = rect.y + rect.height - 1;
    I32 scanliney = 0;
    for (scanliney = rect.y; scanliney < bottomY; ++scanliney)
    {
        // background is pre-included
        isurfaces[1].spanState = 1;
        // sort iedges in a list in order of ascending x
        if (iedges[scanliney])
        {
            InsertNewIEdges(iedges[scanliney], iedgeHead.next);
        }
        GenerateSpan(isurfaces, screenStartX, screenEndX, scanliney, &currentSpan, 
                     &iedgeHead, &iedgeTail);

        // If we run out of spans, draw the image and flush span list.
        if (currentSpan >= maxSpan)
        {
            DrawSurfaces(isurfaces, endISurf, pbuffer, bytesPerRow, zbuffer, bufferWidth);
            
            // clear surface span list
            for (ISurface *surf = &isurfaces[1]; surf < endISurf; ++surf)
            {
                surf->spans = NULL;
            }
            currentSpan = spanlist;
        }

        if (removeIEdges[scanliney])
        {
            RemoveEdges(removeIEdges[scanliney]);
        }
        if (iedgeHead.next != &iedgeTail)
        {
            StepActiveIEdgeX(iedgeHead.next, &iedgeTail, &iedgeAfterTail);
        }
    }

    // last scan, x has already been stepped
    isurfaces[1].spanState = 1;
    if (iedges[scanliney])
    {
        InsertNewIEdges(iedges[scanliney], iedgeHead.next);
    }
    GenerateSpan(isurfaces, screenStartX, screenEndX, scanliney, &currentSpan, 
                 &iedgeHead, &iedgeTail);

    DrawSurfaces(isurfaces, endISurf, pbuffer, bytesPerRow, zbuffer, bufferWidth);
}

#define NUM_STACK_EDGE 2400
#define NUM_STACK_SURFACE 800

void SetupEdgeDrawingFrame(RenderData *renderdata, IEdge *iedgeStack, ISurface *isurfaceStack)
{
    // start on CACHE_SIZE aligned address
    renderdata->iedges = (IEdge *)((size_t)(&iedgeStack[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
    renderdata->isurfaces= (ISurface *)((size_t)(&isurfaceStack[0] + CACHE_SIZE -1) & ~(CACHE_SIZE - 1));

    renderdata->endISurface = &(renderdata->isurfaces[NUM_STACK_SURFACE]);
    
    // surface[0] is a dummy representing the surface with no edge
    renderdata->isurfaces--;

    renderdata->currentIEdge = renderdata->iedges;
    renderdata->endIEdge = &(renderdata->iedges[NUM_STACK_EDGE]);

    // surface[0] is a dummy, surface[1] is background
    renderdata->currentISurface = &(renderdata->isurfaces[2]);
    renderdata->isurfaces[1].spans = NULL;
    renderdata->isurfaces[1].flags = SURF_DRAW_BACKGROUND;
    renderdata->isurfaces[1].key = 0x7fffffff;

    renderdata->currentKey = 0;

    for (int i = 0; i < MAX_PIXEL_HEIGHT; ++i)
    {
        renderdata->newIEdges[i] = NULL;
        renderdata->removeIEdges[i] = NULL;
    }
}

void EdgeDrawing(RenderData *renderdata, Camera *camera, RenderBuffer *renderbuffer)
{
    // TODO lw: why put these data on stack? easy to clear up every frame?
    IEdge iedgeStack[NUM_STACK_EDGE + (CACHE_SIZE - 1)/sizeof(IEdge) + 1];
    ISurface isurfaceStack[NUM_STACK_SURFACE + (CACHE_SIZE - 1)/sizeof(ISurface) + 1];

    SetupEdgeDrawingFrame(renderdata, iedgeStack, isurfaceStack);

    // TODO lw: how does it make sense in the case where the camera is facing 
    // away the root node, 
    RenderWorld(renderdata->worldModel->nodes, camera, renderdata);

    Recti rect = {0, 0, 640, 480}; // TODO lw: remove this
    ScanEdge(rect, renderdata->newIEdges, renderdata->removeIEdges, 
             renderdata->isurfaces, renderdata->currentISurface,
             renderbuffer->backbuffer, renderbuffer->bytesPerRow,
             renderbuffer->zbuffer, renderbuffer->width);
}

void SetupFrame(RenderData *renderdata, Camera *camera)
{
    renderdata->frameCount++;

    renderdata->oldViewLeaf = renderdata->currentViewLeaf;
    renderdata->currentViewLeaf = ModelFindViewLeaf(camera->position, renderdata->worldModel);

    TransformFrustum(camera);
    SetupFrustumIndices(camera);

    UpdateVisibleLeaves(renderdata);
}

RenderBuffer g_renderBuffer;
RenderData g_renderdata;

void RenderView()
{
    SetupFrame(&g_renderdata, &g_camera);

    EdgeDrawing(&g_renderdata, &g_camera, &g_renderBuffer);
}
