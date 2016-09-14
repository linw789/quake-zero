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
#define EDGE_FRAMECOUNT_MASK	0x7fffffff
#define EDGE_PARTIALLY_CLIPPED  EDGE_FRAMECOUNT_MASK

#define SKY_SPAN_SHIFT 5
#define SKY_SPAN_MAX (1 << SKY_SPAN_SHIFT)

// for data alignment on function stack
#define CACHE_SIZE 64

#include "q_lightmap.cpp"

// TODO lw: why these numbers, empirical?
float g_base_mip[MIP_NUM - 1] = {1.0f, 0.5f * 0.8f, 0.25f * 0.8f};

Camera g_camera;

void ResetCamera(Camera *camera, Recti screen_rect, float fovx)
{
    camera->screen_rect = screen_rect;

    // Subtracting 0.5f to make center.x sits between pixels if screen width is 
    // an even number, on the center pixel if an odd number.
    camera->screen_center.x = screen_rect.x + screen_rect.width / 2.0f - 0.5f;
    camera->screen_center.y = screen_rect.y + screen_rect.height / 2.0f - 0.5f;

    camera->screen_clamp_min.x = screen_rect.x - 0.5f;
    camera->screen_clamp_min.y = screen_rect.y - 0.5f;
    camera->screen_clamp_max.x = screen_rect.x + screen_rect.width - 0.5f;
    camera->screen_clamp_max.y = screen_rect.y + screen_rect.height - 0.5f;

    camera->near_z = 0.01f;

    float tanx = Tangent(DegreeToRadian(fovx * 0.5f));
    camera->scale_z = screen_rect.width * 0.5f / tanx;
    camera->scale_invz = 1.0f / camera->scale_z;

    float inv_aspect = (float)(screen_rect.height) / (float)(screen_rect.width);
    float tany = tanx * inv_aspect;

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
    
    /*
     transform_matrix = inverse(rotation) * inverse(translation)
    */
    Vec3f pt = point - camera->position;
    result.x = Vec3Dot(camera->rotx, pt);
    // swap y and z, same reason as in TransformDirectionToView
    result.y = Vec3Dot(camera->rotz, pt);
    result.z = Vec3Dot(camera->roty, pt);

    return result;
}

inline Vec3f TransformDirectionToView(const Camera *camera, Vec3f dir_world)
{
    Vec3f result_view = {0};
    /*
    Nw, Nv are normals of the plane in world and view space respectively.
    rot is the rotation matrix of the camera

    Nv = inverse(rot) * Nw
    
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

    It's not the transformation of coordinate system but just swapping of axis 
    notation
    
    To transform points from world space to view space, we first transform 
    points normally, then swap y and z values.
    */
    result_view.x = Vec3Dot(dir_world, camera->rotx);
    result_view.y = Vec3Dot(dir_world, camera->rotz);
    result_view.z = Vec3Dot(dir_world, camera->roty);

    return result_view;
}

void TransformFrustum(Camera *camera)
{
    Vec3f n_view = {0};
    Vec3f n_world = {0};
    /*
    Nw, Nv are normals of the plane in world and view space respectively.
    rot is the rotation matrix of the camera

    Nw = rot * Nv

    */
    for (int i = 0; i < 4; ++i)
    {
        n_view = camera->frustumPlanes[i].normal;
    
        // Swap normal.y and normal.z because in quake world space z-axis is the 
        // one pointing up, but in view space z-axis points inwards.
        n_world.x = n_view.x * camera->rotx[0] + n_view.z * camera->roty[0] + n_view.y * camera->rotz[0]; 
        n_world.y = n_view.x * camera->rotx[1] + n_view.z * camera->roty[1] + n_view.y * camera->rotz[1]; 
        n_world.z = n_view.x * camera->rotx[2] + n_view.z * camera->roty[2] + n_view.y * camera->rotz[2];

        camera->worldFrustumPlanes[i].normal = n_world;
        
        // camera position is on every frustum plane
        camera->worldFrustumPlanes[i].distance = Vec3Dot(camera->position, n_world);
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
                if (node->visibleframe == renderdata->updateCountPVS)
                {
                    break;
                }
                node->visibleframe = renderdata->updateCountPVS;
                node = node->parent;
            } while (node != NULL);
        }
    }
}

struct LastVertex
{
    float screen_x1;
    float screen_y1;
    float view_invz1; // inverse z in view space
    int ceil_screen_y1;
    U32 is_valid;
};

// If the start point of the edge is inside the frustum and the end point of the
// edge is outside, the clipped point is an exit point. Otherwise it's an enter
// point. We want to make a vertical edge from exit point to enter point, so the
// new edge following the same clock-wise winding. 
struct SurfaceClipResult
{
    Vec3f left_enter_vert;
    Vec3f left_exit_vert;
    Vec3f right_enter_vert;
    Vec3f right_exit_vert;
};

struct EmitIEdgeResult
{
    B32 left_edge_clipped;
    B32 right_edge_clipped;
    B32 edge_emitted;
};

EmitIEdgeResult EmitIEdge(Vec3f v0, Vec3f v1, B32 onlyNearInvZ, U32 *iedge_cache_state,
                          Camera *camera, RenderData *renderdata, ClipPlane *clip_plane, 
                          Edge *edgeOwner, LastVertex *last_vert, SurfaceClipResult *scr)
{
    //
    // clip the edge against the frustum planes in world space
    //

    EmitIEdgeResult result = {0};

    while (clip_plane != NULL)
    {
        float d0 = Vec3Dot(v0, clip_plane->normal) - clip_plane->distance;
        float d1 = Vec3Dot(v1, clip_plane->normal) - clip_plane->distance;

        if (d0 >= 0) // v0 is not clipped
        {
            if (d1 < 0) // v1 is clipped
            {   
                // don't cache partially clipped edge
                *iedge_cache_state = EDGE_PARTIALLY_CLIPPED;

                float t = d0 / (d0 - d1);
                v1 = v0 + t * (v1 - v0);

                if (clip_plane->leftEdge)
                {
                    result.left_edge_clipped = 1;
                    scr->left_exit_vert = v1;
                }
                else if (clip_plane->rightEdge)
                {
                    result.right_edge_clipped = 1;
                    scr->right_exit_vert = v1;
                }
            }
            else
            {
                // both points are unclipped, try next clip plane
                // don't change iedge_cache_state
            }
        }
        else // d0 < 0, v0 is clipped
        {   
            // v0 is clipped, so it's not the same as the last v1
            last_vert->is_valid = 0;

            if (d1 < 0) // both points are clipped
            {   
                // TODO lw: why this check?
                if (!result.left_edge_clipped)
                {
                    *iedge_cache_state = EDGE_FULLY_CLIPPED | (renderdata->framecount & EDGE_FRAMECOUNT_MASK);
                }

                return result;
            }
            else // only v0 is clipped
            {
                *iedge_cache_state = EDGE_PARTIALLY_CLIPPED;

                float t = d0 / (d0 - d1);
                v0 = v0 + t * (v1 - v0);

                if (clip_plane->leftEdge)
                {
                    result.left_edge_clipped = 1;
                    scr->left_enter_vert = v0;
                }
                else if (clip_plane->rightEdge)
                {
                    result.right_edge_clipped = 1;
                    scr->right_enter_vert = v0;
                }
            }
        }

        clip_plane = clip_plane->next;
    }
#if 0
    for (I32 i = 0; i < 4; ++i)
    {
        float dd0 = Vec3Dot(v0, camera->worldFrustumPlanes[i].normal) - camera->worldFrustumPlanes[i].distance;
        float dd1 = Vec3Dot(v1, camera->worldFrustumPlanes[i].normal) - camera->worldFrustumPlanes[i].distance;
        if (dd0 < 0.0f || dd1 < 0.0f)
        {
            I32 test = 0;
        }
    }
    Vec3f debug_point0 = TransformPointToView(camera, v0);
    ASSERT(debug_point0.z > -0.01f);
    Vec3f debug_point1 = TransformPointToView(camera, v1);
    ASSERT(debug_point1.z > -0.01f);
#endif

    // 
    // emit iedge
    // 

    float screen_x0 = 0, screen_x1 = 0; 
    float screen_y0 = 0, screen_y1 = 0;
    float view_invz0 = 0, view_invz1 = 0;
    I32 ceil_screen_y0 = 0, ceil_screen_y1 = 0;

    if (last_vert->is_valid)
    {
        screen_x0 = last_vert->screen_x1;
        screen_y0 = last_vert->screen_y1;
        view_invz0 = last_vert->view_invz1;
        ceil_screen_y0 = last_vert->ceil_screen_y1;
    }
    else
    {
        Vec3f view_vert0 = TransformPointToView(camera, v0);
        for (I32 i = 0; i < 4; ++i)
        {
            float d0 = Vec3Dot(view_vert0, g_camera.frustumPlanes[i].normal) - g_camera.frustumPlanes[i].distance;
            if (d0 < -0.01f)
            {
                d0 = 1;
            }
        }

        if (view_vert0.z < camera->near_z)
        {
            view_vert0.z = camera->near_z;
        }

        view_invz0 = 1.0f / view_vert0.z;
        float scale = camera->scale_z * view_invz0;
        screen_x0 = camera->screen_center.x + scale * view_vert0.x;
        screen_y0 = camera->screen_center.y - scale * view_vert0.y;

        //ASSERT(screen_y0 > (camera->screen_clamp_min.y - 0.5f) 
               //&& screen_y0 < (camera->screen_clamp_max.y + 0.5f));

        screen_x0 = Clamp(camera->screen_clamp_min.x, camera->screen_clamp_max.x, screen_x0);
        screen_y0 = Clamp(camera->screen_clamp_min.y, camera->screen_clamp_max.y, screen_y0);

        ceil_screen_y0 = (I32)ceilf(screen_y0);
    }

    Vec3f view_vert1 = TransformPointToView(camera, v1);
    for (I32 i = 0; i < 4; ++i)
    {
        float d1 = Vec3Dot(view_vert1, g_camera.frustumPlanes[i].normal) - g_camera.frustumPlanes[i].distance;
        if (d1 < -0.01f)
        {
            d1 = 1;
        }
    }

    // TODO lw: why this works?
    if (view_vert1.z < camera->near_z)
    {
        view_vert1.z = camera->near_z;
    }

    view_invz1 = 1.0f / view_vert1.z;
    float scale = camera->scale_z * view_invz1;
    screen_x1 = camera->screen_center.x + scale * view_vert1.x;
    // Transform y from view space(y axis pointing up) to screen space(y axis 
    // pointing down).
    screen_y1 = camera->screen_center.y - scale * view_vert1.y;

    //ASSERT(screen_y1 > (camera->screen_clamp_min.y - 0.5f)
           //&& screen_y1 < (camera->screen_clamp_max.y + 0.5f));

    screen_x1 = Clamp(camera->screen_clamp_min.x, camera->screen_clamp_max.x, screen_x1);
    screen_y1 = Clamp(camera->screen_clamp_min.y, camera->screen_clamp_max.y, screen_y1);

    // TODO lw: explain why ceiling?
    ceil_screen_y1 = (I32)ceilf(screen_y1);

    // find minimum z value
    if (view_invz1 > view_invz0)
    {
        view_invz0 = view_invz1;
    }
    if (view_invz0 > renderdata->nearest_invz) 
    {
        renderdata->nearest_invz = view_invz0;
    }

    // backup v1 data for reuse
    last_vert->screen_x1 = screen_x1;
    last_vert->screen_y1 = screen_y1;
    last_vert->view_invz1 = view_invz1;
    last_vert->ceil_screen_y1 = ceil_screen_y1;

    // For right edges made of clipped points, we only need nearest z value, we
    // don't need stepping infomation because it's on right screen edge.
    if (onlyNearInvZ)
    {
        return result;
    }

    result.edge_emitted = 1;

    if (ceil_screen_y0 == ceil_screen_y1)
    {
        // cache unclipped horizontal edges as fully clipped
        if (*iedge_cache_state != EDGE_PARTIALLY_CLIPPED)
        {
            *iedge_cache_state = EDGE_FULLY_CLIPPED | (renderdata->framecount & EDGE_FRAMECOUNT_MASK);
        }
        return result;
    }

    IEdge *iedge = renderdata->currentIEdge++;

    iedge->owner = edgeOwner;
    iedge->nearInvZ = view_invz0;

    // the screen origin is at top-left corner, thus top_y has smaller value
    I32 top_y, bottom_y;
    float x_start, x_step;

    // points are passed in clock-wise, trailing(right) edge
    if (ceil_screen_y0 < ceil_screen_y1)
    {   
        top_y = ceil_screen_y0;
        bottom_y = ceil_screen_y1 - 1; // floor(screen_y1)
        x_step = (screen_x1 - screen_x0) / (screen_y1 - screen_y0);
        x_start = screen_x0 + (ceil_screen_y0 - screen_y0) * x_step;

        iedge->isurfaceOffsets[0] = 
            (U32)(renderdata->currentISurface - renderdata->isurfaces);
        iedge->isurfaceOffsets[1] = 0;
    }
    else // ceilY0 > ceilY1, leading(left) edge
    {   
        top_y = ceil_screen_y1;
        bottom_y = ceil_screen_y0 - 1; // floor(screen_y0)
        x_step = (screen_x0 - screen_x1) / (screen_y0 - screen_y1);
        x_start = screen_x1 + (ceil_screen_y1 - screen_y1) * x_step;

        iedge->isurfaceOffsets[0] = 0;
        iedge->isurfaceOffsets[1] = 
            (U32)(renderdata->currentISurface - renderdata->isurfaces);
    }

    iedge->x_step = FloatToFixed20(x_step);
    // ensure x_start don't have fraction, on a whole pixel
    iedge->x_start = FloatToFixed20(x_start) + 0xfffff; 

    // clamping edge->x_start

    // sort the iedge in ascending order of x value
    Fixed20 x_check = iedge->x_start;
    // trailing edge
    if (iedge->isurfaceOffsets[0])
    {   
        // if a leading and a trailing have the same x_start, sort leading edge
        // in front
        x_check++; 
    }

    if (renderdata->newIEdges[top_y] == NULL 
        || x_check < renderdata->newIEdges[top_y]->x_start)
    {   
        iedge->next = renderdata->newIEdges[top_y];
        renderdata->newIEdges[top_y] = iedge;
    }
    else
    {
        IEdge *temp = renderdata->newIEdges[top_y];
        while (temp->next != NULL && temp->next->x_start < x_check)
        {
            temp = temp->next;
        }
        // insert edge inbetween temp and temp->next
        iedge->next = temp->next;
        temp->next = iedge;
    }

    // insert in front, the edge will be removed when scanline reaches the bottom_y
    iedge->nextRemove = renderdata->removeIEdges[bottom_y];
    renderdata->removeIEdges[bottom_y] = iedge;

    return result;
}

U32 ReEmitIEdge(Edge *edge, RenderData *renderdata)
{
    IEdge *iedge = renderdata->iedges + edge->iedge_cache_state;

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

    if (iedge->nearInvZ > renderdata->nearest_invz)
    {
        renderdata->nearest_invz = iedge->nearInvZ;
    }

    return 1;
}

void RenderFace(Surface *surface, RenderData *renderdata, Camera *camera, 
                B32 in_submodel, I32 clipflag)
{
    // no more surface
    if (renderdata->currentISurface >= renderdata->endISurface)
    {
        return ;
    }
    // no more edge. a face has a least 4 edges?
    if ((renderdata->currentIEdge + surface->numEdge + 4) >= renderdata->endIEdge)
    {
        renderdata->outOfIEdges += surface->numEdge;
        return ;
    }

    ClipPlane *clip_plane = NULL;
    U32 mask = 0x08;
    for (int i = 3; i >= 0; --i, mask >>= 1)
    {
        if (clipflag & mask)
        {
            camera->worldFrustumPlanes[i].next = clip_plane;
            clip_plane = &camera->worldFrustumPlanes[i];
        }
    }

    Vertex *vertices = renderdata->worldModel->vertices;
    I32 *surfaceEdges = renderdata->worldModel->surfaceEdges;
    Edge *edges = renderdata->worldModel->edges;

    B32 edge_emitted = 0;
    B32 make_left_edge = 0;
    B32 make_right_edge = 0;
    U32 iedge_offset = 0;
    EmitIEdgeResult emit_result = {0};
    SurfaceClipResult scr = {0};
    LastVertex last_vert = {0};
    renderdata->nearest_invz = 0;

    // A surface is convex, so one clip plane will at most generate one pair of 
    // enter and exit clip points
    for (I32 i = 0; i < surface->numEdge; ++i)
    {
        // TODO lw: why needs negative edgeIndex?
        I32 edge_index = surfaceEdges[surface->firstEdge + i];
        I32 start_vert_index = 0;
        I32 end_vert_index = 1;
        if (edge_index <= 0)
        {
            edge_index = -edge_index;
            start_vert_index = 1;
            end_vert_index = 0;
        }

        // get the edge
        Edge *edge = edges + edge_index;

        if (in_submodel == false)
        {
            if (edge->iedge_cache_state & EDGE_FULLY_CLIPPED)
            {
                if ((edge->iedge_cache_state & EDGE_FRAMECOUNT_MASK) == (U32)renderdata->framecount)
                {
                    last_vert.is_valid = 0;
                    // If iedge is fully clipped or horizontal fully accepted 
                    // and we are still in the same frame, meaning we already 
                    // done clipping on this edge, so we can skip ClipEdge() 
                    // and EmitIEdge().
                    continue;
                }
            }
            else
            {
                iedge_offset = (U32)(renderdata->currentIEdge - renderdata->iedges);
                IEdge *temp_iedge = (IEdge *)(renderdata->iedges + edge->iedge_cache_state);

                // If this iedge's owner was completely inside and emitted 
                // before, but now is used for another surface, we re-emit it.
                if ((iedge_offset > edge->iedge_cache_state) 
                    && (temp_iedge->owner == edge)) 
                {
                    edge_emitted += ReEmitIEdge(edge, renderdata);
                    last_vert.is_valid = 0;
                    continue;
                }
            }
        }
        iedge_offset = (U32)(renderdata->currentIEdge - renderdata->iedges);

        Vec3f start_vert = vertices[edge->vertIndex[start_vert_index]].position;
        Vec3f end_vert = vertices[edge->vertIndex[end_vert_index]].position;

        emit_result = EmitIEdge(start_vert, end_vert, false, &iedge_offset, camera, 
                                renderdata, clip_plane, edge, &last_vert, &scr);
        edge_emitted += emit_result.edge_emitted;
        make_left_edge += emit_result.left_edge_clipped;
        make_right_edge += emit_result.right_edge_clipped;


        edge->iedge_cache_state = iedge_offset;
        last_vert.is_valid = 1;
    }

    if (make_left_edge)
    {
        last_vert.is_valid = 0;
		// Based on how clip plane list is set up, left clip plane must be the 
        // first one, namely clip_plane. Passing clip_plane->next will exlucde 
        // the left clip plane
        emit_result = EmitIEdge(scr.left_exit_vert, scr.left_enter_vert, false, 
                                &iedge_offset, camera, renderdata, clip_plane->next, 
                                NULL, &last_vert, &scr);
        edge_emitted |= emit_result.edge_emitted;
    }
    if (make_right_edge)
    {
        last_vert.is_valid = 0;
        // view_clipplanes[1] is the right clip plane, passing 
        // view_clipplanes[1].next will exclude the right clip plane.
        emit_result = EmitIEdge(scr.right_exit_vert, scr.right_enter_vert, true, 
                                &iedge_offset, camera, renderdata, 
                                camera->worldFrustumPlanes[1].next, NULL, &last_vert, &scr);
        edge_emitted |= emit_result.edge_emitted;
    }

    if (!edge_emitted)
    {
        return ;
    }

    renderdata->surfaceCount++;

    ISurface *isurface = renderdata->currentISurface;

    isurface->data = (void *)surface;
    isurface->nearest_invz = renderdata->nearest_invz;
    isurface->flags = surface->flags; // sky, water, normal plane and etc.
    isurface->in_submodel = in_submodel;
    isurface->spanState = 0;
    // isurface->entity = ;
    isurface->key = renderdata->currentKey++;
    isurface->spans = NULL;

    Vec3f n_view = TransformDirectionToView(camera, surface->plane->normal);

    /* 
    Affine transformation won't change the distance.
    Q is a point on the plane, O is the world orign, P is camera position
    N is the normal of the plane
    distance_world = (Q - O) * N  and distance_view = (Q - P) * N
    distance_view = ((Q - O) - (P - O)) * N = distance_world - (P - O) * N
    */
    float inv_dist = 1.0f / (surface->plane->distance - Vec3Dot(camera->position, surface->plane->normal));

    /* 
	Instead of calculating 1/z by interpolating between edges,
	we use plane equation to get 1/z. 

    normal = (a, b, c)
    z_s is the z value of the projecting plane that's perpendicular to z axis, ...
    x_s and y_s are values the projecting plane in view space

	a*x + b*y + c*z - d = 0                 eq.1
	x/x_s = z/z_s --> x = (z/z_s) * x_s     eq.2
	y/y_s = z/z_s --> y = (z/z_s) * y_s   eq.3

	put eq.2 and eq.3 into eq.1, we get
	1/z = ((a/z_s) * x_s + (b/z_s) * y_s + c) / d
		= ((a/z_s)/d * x_s) + ((b/z_s)/d * y_s) + c/d

    screen_origin is at top-left corner

    x_ss = screen_center_x + x_s (x_ss is in screen space)
    y_ss = screen_center_y - y_s (y_ss is in screen space)

	1/z = ((a/z_s)/d * (x_ss - screen_center_x)) 
        + ((b/z_s)/d * (screen_center_y - y_ss)) 
        + c/d

	Note: Above calculation assumes the origin is at the center of the view,
	however, screen space has the origin at top-left corner. There is some
	translation work needs to be done.
	*/
    isurface->zi_stepx = n_view.x * camera->scale_invz * inv_dist;
    // y axis is pointing up in view space
    isurface->zi_stepy = -n_view.y * camera->scale_invz * inv_dist;
    // move to top-left corner
    isurface->zi_start = n_view.z * inv_dist
                       - camera->screen_center.x * isurface->zi_stepx
                       - camera->screen_center.y * isurface->zi_stepy;

    renderdata->currentISurface++;
}

void RecurseWorldNode(Node *node, Camera *camera, RenderData *renderdata, int clipflag)
{
    if (node->contents == CONTENTS_SOLID)
    {
        return ;
    }
    if (node->visibleframe != renderdata->updateCountPVS)
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
            (*mark)->visibleframe = renderdata->framecount;
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
        int count = node->numsurface;
        if (count)
        {
            Surface *surface = renderdata->worldModel->surfaces + node->firstsurface;

            if (d < -BACKFACE_EPSILON)
            {
                while (count)
                {
                    if ((surface->flags & SURF_PLANE_BACK) 
                        && (surface->visibleframe == renderdata->framecount))
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
                        && (surface->visibleframe == renderdata->framecount))
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

void InsertNewIEdges(IEdge *edges_to_add, IEdge *edge_list)
{
    IEdge *next_edge;
    while (edges_to_add != NULL)
    {
        for(;;) 
        {
            // unroll the loop a bit

            if (edge_list->x_start >= edges_to_add->x_start)
            {
                break;
            }
            edge_list = edge_list->next;

            if (edge_list->x_start >= edges_to_add->x_start)
            {
                break;
            }
            edge_list = edge_list->next;

            if (edge_list->x_start >= edges_to_add->x_start)
            {
                break;
            }
            edge_list = edge_list->next;

            if (edge_list->x_start >= edges_to_add->x_start)
            {
                break;
            }
            edge_list = edge_list->next;
        } 
        next_edge = edges_to_add->next;

        // insert 'edges_to_add' intween 'edgelist->prev' and 'edgelist'
		edges_to_add->next = edge_list;
		edges_to_add->prev = edge_list->prev;
		edge_list->prev->next = edges_to_add;
		edge_list->prev = edges_to_add;

        edges_to_add = next_edge;
    }
}

void LeadingEdge(IEdge *iedge, ISurface *isurfaces, I32 y, ESpan **currentSpan)
{
    ISurface *topISurf;

    if (!iedge->isurfaceOffsets[1])
    {
        return ;
    }

    // get the isurface this iedge belongs to
    ISurface *isurf = &isurfaces[iedge->isurfaceOffsets[1]];

    //ASSERT(isurf->spanState == 0);

    // '->' has higher precedence than '++a', add parenthesis anyway
    if (++(isurf->spanState) == 1)
    {
        /*
        if (surf->in_submodel)
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
        if ((isurf->key == topISurf->key) && isurf->in_submodel )
        {
            float x = Fixed20ToFloat(iedge->x_start - 0xfffff);
            float newInvZ = isurf->zi_start + isurf->zi_stepx * x + isurf->zi_stepy * y;
            float newInvZBottom = newInvZ * 0.99f; // TODO lw: ???
            float currentTopInvZ = topISurf->zi_start + topISurf->zi_stepx * x + topISurf->zi_stepy * y;
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
            if (isurf->in_submodel)
            {
                float x = Fixed20ToFloat(iedge->x_start - 0xfffff);
                float newInvZ = isurf->zi_start + isurf->zi_stepx * x + isurf->zi_stepy * y;
                float newInvZBottom = newInvZ * 0.99f; 
                float currentTopInvZ = topISurf->zi_start + topISurf->zi_stepx * x + topISurf->zi_stepy * y;
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

void TrailingEdge(IEdge *iedge, ISurface *isurfaces, I32 y, ESpan **currentSpan)
{
    ISurface *isurf = &isurfaces[iedge->isurfaceOffsets[0]];
    
    //ASSERT(isurf->spanState == 1);

    if (--(isurf->spanState) == 0)
    {
        /*
        if (isurf->in_submodel)
        {
            r_submodelactive--;
        }
        */
        // only when it's the top isurface
        if (isurf == isurfaces[1].next)
        {
            // emit span
            I32 px = iedge->x_start >> 20;
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
void CleanupSpan(ISurface *isurfaces, I32 screenEndX, I32 y, ESpan **currentSpan)
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

void GenerateSpan(ISurface *isurfaces, I32 screen_start_x, I32 screen_end_x, 
                  I32 scanliney, ESpan **currentSpan, IEdge *iedgeHead, IEdge *iedgeTail)
{
    // clear active isurfaces
    isurfaces[1].next = &isurfaces[1];
    isurfaces[1].prev = &isurfaces[1];
    isurfaces[1].x_last = screen_start_x;

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

    CleanupSpan(isurfaces, screen_end_x, scanliney, currentSpan);
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

I32 GetMipLevelForScale(float miplevels[MIP_NUM -1], I32 mip_min, float scale)
{
    /*
     z_scale = (1/near_z) * sz
     scale = z_scale * some_adjust
     where sz is the z value projecting plane (screen) in view space.
     If z_scale is greater than or equal to 1, that means the nearest point of 
     the surface is front of the projecting plane and we should use the largest 
     texture.
    */

    I32 miplevel = 3;
    if (scale >= miplevels[0])
    {
        miplevel = 0;
    }
    else if (scale >= miplevels[1])
    {
        miplevel = 1;
    }
    else if (scale >= miplevels[2])
    {
        miplevel = 2;
    }

    if (miplevel < mip_min)
    {
        miplevel = mip_min;
    }
    return miplevel;
}

struct TextureGradient
{
    float uinvz_step_x;
    float vinvz_step_x;
    float uinvz_step_y;
    float vinvz_step_y;
    float uinvz_origin;
    float vinvz_origin;

    Fixed16 u_adjust;
    Fixed16 v_adjust;
    Fixed16 u_extent;
    Fixed16 v_extent;
};

/*
Calculate perspective-correct texture stepping variables.
*/
TextureGradient CalcGradients(Surface *surface, I32 miplevel, Camera *camera)
{
    TextureGradient result = {0};

    Plane *plane = surface->plane;

    // mipscale = 1, 1/2, 1/4, 1/8
    float mipscale = 1.0f / (float)(1 << miplevel);

    Vec3f u_axis = TransformDirectionToView(camera, surface->tex_info->u_axis);
    Vec3f v_axis = TransformDirectionToView(camera, surface->tex_info->v_axis);

    /* 
     u_axis = u_axis * mipscale
     u = vec3dot(p, u_axis) + offset_ignore_for_now
	   = p.x * u_axis.x + p.y * u_axis.y + p.z * u_axis.z
 
     u/z = (p.x * u_axis.x / p.z) + (p.y * u_axis.y / p.z) + u_axis.z
 
     x_ss = screen_center_x + p.x (x_ss is in screen space)
     y_ss = screen_center_y - p.y (y_ss is in screen space)
 
     u/z = ((x_ss - screen_center_x) * u_axis.x / p.z) 
         + ((screen_center_y - y_ss) * u_axis.y / p.z) 
         + u_axis.z

	 uinvz_step_x = u_axis.x / p.z --> stepping of u/z along x axis
	 uinvz_step_y = -u_axis.y / p.z --> stepping of u/z along y axis
	*/   

    float temp = camera->scale_invz * mipscale; // pre-scale by mip size
    result.uinvz_step_x = u_axis.x * temp;
    result.vinvz_step_x = v_axis.x * temp;
    // y axis is pointing up, but we scan from top down.
    result.uinvz_step_y = -u_axis.y * temp;
    result.vinvz_step_y = -v_axis.y * temp;

    // move to top-left corner
    result.uinvz_origin = -camera->screen_center.x * result.uinvz_step_x
                        - camera->screen_center.y * result.uinvz_step_y
                        + u_axis.z * mipscale;
    result.vinvz_origin = -camera->screen_center.x * result.vinvz_step_x
                        - camera->screen_center.y * result.vinvz_step_y
                        + v_axis.z * mipscale;

    /*
     U_view = inverse(R) * U_world
     P_view = (inverse(R) * inverse(T)) * P_world

     U_view * P_view = inverse(R) * U_world * (inverse(R) * inverse(T)) * P_world
                     = U_world * transpose(inverse(R)) * (inverse(R) * inverse(T)) * P_world
                     = U_world * R * inverse(R) * inverse(T) * P_world
                     = U_world * (inverse(T) * P_world)
                     = U_world * (P_world - camera_position)
                     = U_world * P_world - U_world * camera_position

     U_world * P_world = U_view * P_view + U_world * camera_position

     => camera_adjust_u = U_world * camera_position
     quake did it this way : camera_adjust_u = (U_world * R) * (inverse(R) * camera_position)
    */

    temp = 0x10000 * mipscale; // to Fixed16

    Fixed16 camera_adjust_u = (Fixed16)(Vec3Dot(camera->position, surface->tex_info->u_axis) * temp + 0.5f);

    // surface->uv_min[0] << 16 is promoted to I32, even surface->uv_min[0] is I16
    result.u_adjust = camera_adjust_u
                    - (Fixed16)((surface->uv_min[0] << 16) >> miplevel)
                    + (Fixed16)(surface->tex_info->u_offset * temp);

    Fixed16 camera_adjust_v = (Fixed16)(Vec3Dot(camera->position, surface->tex_info->v_axis) * temp + 0.5f);

    result.v_adjust = camera_adjust_v
                    - (Fixed16)((surface->uv_min[1] << 16) >> miplevel)
                    + (Fixed16)(surface->tex_info->v_offset * temp);

    // TODO lw: ? -1 (-epsilon) so we never wander off the edge of the texture
    result.u_extent = ((surface->uv_extents[0] << 16) >> miplevel) - 1;
    result.v_extent = ((surface->uv_extents[1] << 16) >> miplevel) - 1;

    return result;
}

void DrawSpan8(ISurface *isurf, TextureGradient tex_grad, float zi_start, 
               float zi_stepx, float zi_stepy, U8 *surfcache, I32 cachewidth, 
               U8 *pixelbuffer, I32 bytes_per_row)
{
    ESpan *span = isurf->spans;

    // perspective-correctly interpolate at every 8 unit
    float uinvz_step_x8 = tex_grad.uinvz_step_x * 8.0f;
    float vinvz_step_x8 = tex_grad.vinvz_step_x * 8.0f;
    float invz_step_x8 = zi_stepx * 8.0f;

    // interpolating texels across the span
    while (span)
    {
        U8 *pixel = pixelbuffer + span->y * bytes_per_row + span->x_start;

        I32 span_pixel_count = span->count;

        // calculate values of the starting pixel
        float uinvz = tex_grad.uinvz_origin 
                    + span->x_start * tex_grad.uinvz_step_x
                    + span->y * tex_grad.uinvz_step_y;

        float vinvz = tex_grad.vinvz_origin
                    + span->x_start * tex_grad.vinvz_step_x
                    + span->y * tex_grad.vinvz_step_y;

        float invz = zi_start + span->x_start * zi_stepx + span->y * zi_stepy;

        float z = (float)0x10000 / invz; // prescale to 16.16 fixed-point

        Fixed16 u = (Fixed16)(uinvz * z) + tex_grad.u_adjust;
        u = Clamp(0, tex_grad.u_extent, u);
        Fixed16 v = (Fixed16)(vinvz * z) + tex_grad.v_adjust;
        v = Clamp(0, tex_grad.v_extent, v);
        
        Fixed16 u_step = 0, v_step = 0;
        Fixed16 u_next8 = 0, v_next8 = 0;

        while (span_pixel_count)
        {
            I32 count = 8;
            if (span_pixel_count < 8)
            {
                count = span_pixel_count;
            }
            span_pixel_count -= count;

            // calculate stepping variables at multiples of 8
            if (count == 8)
            {
                uinvz += uinvz_step_x8;
                vinvz += vinvz_step_x8;
                invz += invz_step_x8;
                z = (float)0x10000 / invz; // prescale to 16.16 fixed-point

                u_next8 = (Fixed16)(uinvz * z) + tex_grad.u_adjust;
                u_next8 = Clamp(8, tex_grad.u_extent, u_next8);

                v_next8 = (Fixed16)(vinvz * z) + tex_grad.v_adjust;
                v_next8 = Clamp(8, tex_grad.v_extent, v_next8);

                u_step = (u_next8 - u) >> 3;
                v_step = (v_next8 - v) >> 3;
            }
            else
            {
                I32 steps = (count - 1);
                uinvz += tex_grad.uinvz_step_x * steps;
                vinvz += tex_grad.vinvz_step_x * steps;
                invz += zi_stepx * steps;
                z = (float)0x10000 / invz;

                // u_next8 doesn't mean next 8 steps, but next count - 1 steps
                u_next8 = (Fixed16)(uinvz * z) + tex_grad.u_adjust;
                u_next8 = Clamp(8, tex_grad.u_extent, u_next8);

                v_next8 = (Fixed16)(vinvz * z) + tex_grad.v_adjust;
                v_next8 = Clamp(8, tex_grad.v_extent, v_next8);

                if (count > 1)
                {
                    u_step = (u_next8 - u) / steps;
                    v_step = (v_next8 - v) / steps;
                }
            }

            while (count--)
            {
                *pixel++ = *(surfcache + (v >> 16) * cachewidth + (u >> 16));
                u += u_step;
                v += v_step;
            }

            u = u_next8;
            v = v_next8;
        }

        span = span->next;
    }
}

void DrawSkySpan(ISurface *isurf, U8 *pixelbuffer, I32 bytes_per_row, float sky_shift, 
                 U8 *sky_src, Camera *camera)
{
    ESpan *span = isurf->spans;
    I32 screen_half_width = camera->screen_rect.width >> 1;
    I32 screen_half_height = camera->screen_rect.height >> 1;

    while (span)
    {
        U8 *pixel = pixelbuffer + span->y * bytes_per_row + span->x_start;
        I32 span_pixelcount = span->count;
        I32 x = span->x_start;
        I32 y = span->y;
        Vec2i tex_uv = SkyGetTextureUV(x, y, screen_half_width, screen_half_height, sky_shift, camera);
        Vec2i tex_uv_next = {0};
        I32 stepu = 0, stepv = 0;

        while (span_pixelcount)
        {
            I32 count = SKY_SPAN_MAX;
            if (span_pixelcount < SKY_SPAN_MAX)
            {
                count = span_pixelcount;
            }
            span_pixelcount -= count;

            if (count == SKY_SPAN_MAX)
            {
                x += SKY_SPAN_MAX;
                tex_uv_next = SkyGetTextureUV(x, y, screen_half_width, screen_half_height, sky_shift, camera);
                stepu = (tex_uv_next.u - tex_uv.u) >> SKY_SPAN_SHIFT;
                stepv = (tex_uv_next.v - tex_uv.v) >> SKY_SPAN_SHIFT;
            }
            else
            {
                I32 count_minus1 = count - 1;
                if (count_minus1)
                {
                    x += count_minus1;
                    tex_uv_next = SkyGetTextureUV(x, y, screen_half_width, screen_half_height, sky_shift, camera);
                    stepu = (tex_uv_next.u - tex_uv.u) / count_minus1;
                    stepv = (tex_uv_next.v - tex_uv.v) / count_minus1;
                }
            }

            while (count--)
            {
                // width = 128, (width - 1) = 0x7f
                // ((v >> 16) & 0x7f) * width + ((u >> 16) & 0x7f)
                *pixel++ = sky_src[((tex_uv.v & 0x007f0000) >> 8) + ((tex_uv.u & 0x007f0000) >> 16)];
                tex_uv.u += stepu;
                tex_uv.v += stepv;
            }

            tex_uv = tex_uv_next;
        }

        span = span->next;
    }
}

void DrawTurbulentSpan16(ISurface *isurf, TextureGradient tex_grad, float zi_start, 
                         float zi_stepx, float zi_stepy, U8 *surfcache, I32 cachewidth,
                         U8 *pixelbuffer, I32 bytes_per_row, I32 *sine_table, I32 framecount)
{
    ESpan *span = isurf->spans;

    sine_table = sine_table + (framecount & (SINE_SAMPLE_SIZE - 1));

    // perspective-correct at every 16 pixel
    float uinvz_step_x16 = tex_grad.uinvz_step_x * 16.0f;
    float vinvz_step_x16 = tex_grad.vinvz_step_x * 16.0f;
    float invz_step_x16 = zi_stepx * 16.0f;

    while (span)
    {

        U8 *pixel = pixelbuffer + span->y * bytes_per_row + span->x_start;

        I32 span_pixel_count = span->count;

        // calculate values of the starting pixel
        float uinvz = tex_grad.uinvz_origin 
                    + span->x_start * tex_grad.uinvz_step_x
                    + span->y * tex_grad.uinvz_step_y;

        float vinvz = tex_grad.vinvz_origin
                    + span->x_start * tex_grad.vinvz_step_x
                    + span->y * tex_grad.vinvz_step_y;

        float invz = zi_start + span->x_start * zi_stepx + span->y * zi_stepy;

        float z = (float)0x10000 / invz; // prescale to 16.16 fixed-point

        Fixed16 u = (Fixed16)(uinvz * z) + tex_grad.u_adjust;
        u = Clamp(0, tex_grad.u_extent, u);
        Fixed16 v = (Fixed16)(vinvz * z) + tex_grad.v_adjust;
        v = Clamp(0, tex_grad.v_extent, v);

        Fixed16 u_step = 0, v_step = 0;
        Fixed16 u_next16 = 0, v_next16 = 0;

        while (span_pixel_count)
        {
            I32 count = 16;
            if (span_pixel_count < 16)
            {
                count = span_pixel_count;
            }
            span_pixel_count -= count;

            // calculate stepping variables at multiples of 8
            if (count == 16)
            {
                uinvz += uinvz_step_x16;
                vinvz += vinvz_step_x16;
                invz += invz_step_x16;
                z = (float)0x10000 / invz; // prescale to 16.16 fixed-point

                u_next16 = (Fixed16)(uinvz * z) + tex_grad.u_adjust;
                u_next16 = Clamp(16, tex_grad.u_extent, u_next16);

                v_next16 = (Fixed16)(vinvz * z) + tex_grad.v_adjust;
                v_next16 = Clamp(16, tex_grad.v_extent, v_next16);

                u_step = (u_next16 - u) >> 4;
                v_step = (v_next16 - v) >> 4;
            }
            else
            {
                I32 steps = (count - 1);
                uinvz += tex_grad.uinvz_step_x * steps;
                vinvz += tex_grad.vinvz_step_x * steps;
                invz += zi_stepx * steps;
                z = (float)0x10000 / invz;

                // u_next8 doesn't mean next 16 steps
                u_next16 = (Fixed16)(uinvz * z) + tex_grad.u_adjust;
                u_next16 = Clamp(16, tex_grad.u_extent, u_next16);

                v_next16 = (Fixed16)(vinvz * z) + tex_grad.v_adjust;
                v_next16 = Clamp(16, tex_grad.v_extent, v_next16);

                if (count > 1)
                {
                    u_step = (u_next16 - u) / steps;
                    v_step = (v_next16 - v) / steps;
                }
            }

            // clamping
            u = u & ((SINE_SAMPLE_SIZE << 16) - 1);
            v = v & ((SINE_SAMPLE_SIZE << 16) - 1);

            while (count--)
            {
                I32 sine_scale = 8;
                I32 sine_u = sine_table[v >> 16] * sine_scale;
                I32 sine_v = sine_table[u >> 16] * sine_scale;
                I32 turb_u = ((u + sine_u) >> 16) & 63;
                I32 turb_v = ((v + sine_v) >> 16) & 63;

                *pixel++ = *(surfcache + turb_v * cachewidth + turb_u);
                // *pixel++ = *(surfcache + ((v >> 16) & 63) * cachewidth + ((u >> 16) & 63));
                u += u_step;
                v += v_step;
            }

            u = u_next16;
            v = v_next16;
        }

        span = span->next;
    }
}

void DrawSolidSurfaces(ISurface *isurf, U8 *buffer, I32 bytes_per_row)
{
    U8 color = (size_t)isurf->data & 0xff;

    for (ESpan *span = isurf->spans; span != NULL; span = span->next)
    {
        U8 *pixel = buffer + bytes_per_row * span->y + span->x_start;
        I32 start_x = span->x_start;
        // don't drawing trailing(right) edges, so minus 1
        I32 end_x = start_x + span->count - 1;

        for (I32 i = start_x; i <= end_x; ++i)
        {
            *pixel++ = (U8)color;
        }
    }
}

void DrawZBuffer(float zi_stepx, float zi_stepy, float zi_start, ESpan *span, 
                 float *zbuffer, I32 width)
{
    do 
    {
        float *zpixel = zbuffer + width * span->y + span->x_start;

        float invz = zi_stepx * span->x_start + zi_stepy * span->y + zi_start;
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

void Debug_DrawTexture(U8 *pixelbuffer, I32 bytes_per_row, U8 *tex_src, I32 tex_width, I32 tex_height)
{
    U8 *pixel_y = pixelbuffer;
    U8 *texel = tex_src;
    for (I32 y = 0; y < tex_height; ++y)
    {
        U8 *pixel_x = pixel_y;
        for (I32 x = 0; x < tex_width; ++x)
        {
            *pixel_x++ = *texel++;
        }
        pixel_y += bytes_per_row;
    }
}

void Debug_DrawTurbTexture(U8 *pixelbuffer, I32 bytes_per_row, U8 *tex_src, I32 tex_width, 
                           I32 tex_height, I32 *sine_table, I32 framecount)
{
    sine_table = sine_table + (framecount & (SINE_SAMPLE_SIZE - 1));

    for (I32 y = 0; y < tex_height; ++y)
    {
        for (I32 x = 0; x < tex_width; ++x)
        {
            I32 cy = (y + (sine_table[x & (SINE_SAMPLE_SIZE - 1)] >> 16)) & 63;
            I32 cx = (x + (sine_table[y & (SINE_SAMPLE_SIZE - 1)] >> 16)) & 63;
            pixelbuffer[y * bytes_per_row + x] = tex_src[cy * tex_width + cx];
        }
    }
}

void Debug_DrawColorMap(U8 *pixelbuffer, I32 bytes_per_row, U8 *colormap)
{
    U8 *pixel_y = pixelbuffer;
    U8 *color = colormap;
    for (I32 y = 0; y < 64; ++y)
    {
        U8 *pixel = pixel_y;
        for (I32 x = 0; x < 256; ++x)
        {
            *pixel++ = *color++;
        }
        pixel_y += bytes_per_row;
    }
}

void Debug_DrawSkyTexture(U8 *pixelbuffer, I32 bytes_per_row, SkyCanvas *sky)
{
    // draw original left sky texture
    U8 *pixel_y = pixelbuffer;
    U8 *sky_src_y = sky->left_sky;
    for (I32 y = 0; y < SKY_SIZE; ++y)
    {
        U8 *pixel = pixel_y;
        U8 *sky_src = sky_src_y;
        for (I32 x = 0; x < SKY_SIZE; ++x)
        {
            *pixel++ = *sky_src++;
        }
        sky_src_y += SKY_WEIRD_NUMBER;
        pixel_y += bytes_per_row;
    }

#if 0
    // draw original right sky texture
    pixel_y = pixelbuffer + SKY_SIZE;
    sky_src_y = sky->new_sky + SKY_SIZE;
    for (I32 y = 0; y < SKY_SIZE; ++y)
    {
        U8 *pixel = pixel_y;
        U8 *sky_src = sky_src_y;
        for (I32 x = 0; x < SKY_SIZE; ++x)
        {
            *pixel++ = *sky_src++;
        }
        pixel_y += bytes_per_row;
        sky_src_y += SKY_TEXTURE_WIDTH;
    }
#else
    // draw synthesized sky
    pixel_y = pixelbuffer + SKY_SIZE;
    sky_src_y = sky->new_sky;
    for (I32 y = 0; y < SKY_SIZE; ++y)
    {
        U8 *pixel = pixel_y;
        U8 *sky_src = sky_src_y;
        for (I32 x = 0; x < SKY_SIZE; ++x)
        {
            *pixel++ = *sky_src++;
        }
        pixel_y += bytes_per_row;
        sky_src_y += SKY_TEXTURE_WIDTH;
    }
#endif
}

void DrawSurfaces(ISurface *isurfaces, ISurface *endISurf, U8 *pbuffer, 
                  I32 bytes_per_row, float *zbuffer, I32 zbuffer_width, U8 *colormap,
                  RenderData *renderdata, SkyCanvas *sky, Camera *camera)
{
    Cvar *cvar_drawflat = CvarGet("drawflat");
    if (cvar_drawflat->val)
    {
        for (ISurface *isurf = &isurfaces[1]; isurf < endISurf; ++isurf)
        {
            if (!isurf->spans)
            {
                continue;
            }
            DrawSolidSurfaces(isurf, pbuffer, bytes_per_row);
            DrawZBuffer(isurf->zi_stepx, isurf->zi_stepy, isurf->zi_start, 
                        isurf->spans, zbuffer, zbuffer_width);
        }
    }
    else
    {
#if 1
        for (ISurface *isurf = &isurfaces[1]; isurf < endISurf; ++isurf)
        {
            if (!isurf->spans)
            {
                continue;
            }
            if (isurf->flags & SURF_DRAW_SKY)
            {
                //DrawSolidSurfaces(isurf, pbuffer, bytes_per_row);
                DrawSkySpan(isurf, pbuffer, bytes_per_row, sky->sky_shift, sky->new_sky, camera);

                DrawZBuffer(isurf->zi_stepx, isurf->zi_stepy, isurf->zi_start, 
                            isurf->spans, zbuffer, zbuffer_width);
            }
            else if (isurf->flags & SURF_DRAW_BACKGROUND)
            {
                DrawSolidSurfaces(isurf, pbuffer, bytes_per_row);
            }
            else if (isurf->flags & SURF_DRAW_TURB) // water, lava
            {
                if (isurf->in_submodel)
                {

                }

                Surface *surface = (Surface *)isurf->data;
                TextureGradient tex_grad = CalcGradients(surface, 0, camera);

#if 1
                // use original texture as surface cache, no lighting
                U8 *surfcache = (U8 *)surface->tex_info->texture 
                              + surface->tex_info->texture->offsets[0];
                I32 surfcache_width = 64;
#else // use default checker-board texture

                U8 *surfcache = (U8 *)g_defaultTexture + g_defaultTexture->offsets[0];
                I32 surfcache_width = 64;
#endif

                DrawTurbulentSpan16(isurf, tex_grad, isurf->zi_start, isurf->zi_stepx,
                                    isurf->zi_stepy, surfcache, surfcache_width, pbuffer, 
                                    bytes_per_row, renderdata->sine_table, renderdata->framecount);

                DrawZBuffer(isurf->zi_stepx, isurf->zi_stepy, isurf->zi_start, 
                            isurf->spans, zbuffer, zbuffer_width);
            }
            else
            {
                if (isurf->in_submodel)
                {

                }

                Surface *surface = (Surface *)isurf->data;

                // scale = (screen_z / nearest_z), the smaller nearest_z is the
                // larger scale is
                float scale = isurf->nearest_invz 
                            * camera->scale_z * surface->tex_info->mip_adjust;

                I32 mip_level = GetMipLevelForScale(renderdata->scaled_mip, 
                                                    renderdata->mip_min, scale);

                TextureGradient tex_grad = CalcGradients(surface, mip_level, camera);

                SurfaceCache *surfcache = CacheSurface(surface, mip_level, &g_lightsystem, 
                                                       renderdata->framecount, colormap);

                DrawSpan8(isurf, tex_grad, isurf->zi_start, isurf->zi_stepx,
                          isurf->zi_stepy, surfcache->data, surfcache->width, 
                          pbuffer, bytes_per_row);
                // DrawSolidSurfaces(isurf, pbuffer, bytes_per_row);

                DrawZBuffer(isurf->zi_stepx, isurf->zi_stepy, isurf->zi_start, 
                            isurf->spans, zbuffer, zbuffer_width);
            }
        }
#else
        // Debug_DrawSkyTexture(pbuffer, bytes_per_row, sky);

        // U8 *tex_src = (U8 *)g_defaultTexture + g_defaultTexture->offsets[0];
        /*
        Debug_DrawTexture(pbuffer, bytes_per_row, tex_src, 
                          g_defaultTexture->width, g_defaultTexture->height);
        */
        /*
        Debug_DrawTurbTexture(pbuffer, bytes_per_row, tex_src, 
                              g_defaultTexture->width, g_defaultTexture->height,
                              renderdata->sine_table, renderdata->framecount);
        */
        Debug_DrawColorMap(pbuffer, bytes_per_row, colormap);
#endif
    }
}

#define MAX_SPAN_NUM 5120

void ScanEdge(RenderData *renderdata, RenderBuffer *renderbuffer, SkyCanvas *sky,
              Camera *camera)
{
    Recti rect = camera->screen_rect;

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

    I32 bytes_per_row = renderbuffer->bytes_per_row;
    I32 zbuffer_width = renderbuffer->width;
    U8 *pbuffer = renderbuffer->backbuffer + rect.y * bytes_per_row + rect.x;
    float *zbuffer = renderbuffer->zbuffer + rect.y * zbuffer_width + rect.x;

    ISurface *endISurf = renderdata->currentISurface;

    I32 bottom_y = rect.y + rect.height - 1;
    I32 scanliney = 0;
    for (scanliney = rect.y; scanliney < bottom_y; ++scanliney)
    {
        // background is pre-included
        renderdata->isurfaces[1].spanState = 1;
        // sort iedges in a list in order of ascending x
        if (renderdata->newIEdges[scanliney])
        {
            InsertNewIEdges(renderdata->newIEdges[scanliney], iedgeHead.next);
        }
        GenerateSpan(renderdata->isurfaces, screenStartX, screenEndX, scanliney, 
                     &currentSpan, &iedgeHead, &iedgeTail);

        // If we run out of spans, draw the image and flush span list.
        if (currentSpan >= maxSpan)
        {
            DrawSurfaces(renderdata->isurfaces, endISurf, pbuffer, bytes_per_row, zbuffer, 
                         zbuffer_width, renderbuffer->colormap, renderdata, sky, camera);
            
            // clear surface span list
            for (ISurface *surf = &(renderdata->isurfaces[1]); surf < endISurf; ++surf)
            {
                surf->spans = NULL;
            }
            currentSpan = spanlist;
        }

        if (renderdata->removeIEdges[scanliney])
        {
            RemoveEdges(renderdata->removeIEdges[scanliney]);
        }
        if (iedgeHead.next != &iedgeTail)
        {
            StepActiveIEdgeX(iedgeHead.next, &iedgeTail, &iedgeAfterTail);
        }
    }

    // last scan, x has already been stepped
    renderdata->isurfaces[1].spanState = 1;
    if (renderdata->newIEdges[scanliney])
    {
        InsertNewIEdges(renderdata->newIEdges[scanliney], iedgeHead.next);
    }
    GenerateSpan(renderdata->isurfaces, screenStartX, screenEndX, scanliney, &currentSpan, 
                 &iedgeHead, &iedgeTail);

    DrawSurfaces(renderdata->isurfaces, endISurf, pbuffer, bytes_per_row, zbuffer, 
                 zbuffer_width, renderbuffer->colormap, renderdata, sky, camera);
}

// take the entire screen as a texture and then do the same as water turbulent 
// drawing
void WarpScreen(U8 *pixelbuffer, I32 bytes_per_row, I32 bufferwidth, I32 bufferheight,
                I32 *sine_table, I32 framecount)
{
    const I32 MAX_RENDER_BUFFER_WIDTH = 640;
    const I32 MAX_RENDER_BUFFER_HEIGHT = 480;

    U8 tempbuffer[MAX_RENDER_BUFFER_HEIGHT * MAX_RENDER_BUFFER_WIDTH];

    sine_table = sine_table + ((I32)(framecount * 1.5f)& (SINE_SAMPLE_SIZE - 1));
    I32 sine_scale_y = 4;
    I32 sine_scale_x = 4;

    float stretch_rate_width = bufferwidth / (bufferwidth + sine_scale_x * 2.0f);
    float stretch_rate_height = bufferheight / (bufferheight + sine_scale_y * 2.0f);

    for (I32 y = 0; y < bufferheight; ++y)
    {
        for (I32 x = 0; x < bufferwidth; ++x)
        {
            I32 sine_y = (sine_table[x & (SINE_SAMPLE_SIZE - 1)] * sine_scale_y) >> 16;
            I32 sine_x = (sine_table[y & (SINE_SAMPLE_SIZE - 1)] * sine_scale_x) >> 16;
            float cy = (y + sine_y) * stretch_rate_height;
            float cx = (x + sine_x) * stretch_rate_width;

            tempbuffer[y * MAX_RENDER_BUFFER_WIDTH + x] = pixelbuffer[(I32)cy * bytes_per_row + (I32)cx];
        }
    }

    U8 *dest = pixelbuffer;
    U8 *src = tempbuffer;
    for (I32 y = 0; y < bufferheight; ++y)
    {
        MemCpy(dest, src, bufferwidth);
        dest += bytes_per_row;
        src += MAX_RENDER_BUFFER_WIDTH;
    }
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

void EdgeDrawing(RenderData *renderdata, Camera *camera, RenderBuffer *renderbuffer, SkyCanvas *sky)
{
    // TODO lw: why put these data on stack? easy to clear up every frame?
    IEdge iedgeStack[NUM_STACK_EDGE + (CACHE_SIZE - 1)/sizeof(IEdge) + 1];
    ISurface isurfaceStack[NUM_STACK_SURFACE + (CACHE_SIZE - 1)/sizeof(ISurface) + 1];

    SetupEdgeDrawingFrame(renderdata, iedgeStack, isurfaceStack);

    // TODO lw: how does it make sense in the case where the camera is facing 
    // away the root node 
    // construct data from BSP for span-drawing
    RenderWorld(renderdata->worldModel->nodes, camera, renderdata);

    SkyAnimate(sky);

    ScanEdge(renderdata, renderbuffer, sky, camera);
}

void SetupFrame(RenderData *renderdata, Camera *camera, float target_dt)
{
    Cvar *mipscale = CvarGet("mipscale");
    for (I32 i = 0; i < (MIP_NUM - 1); ++i)
    {
        renderdata->scaled_mip[i] = g_base_mip[i] * mipscale->val;
    }
    Cvar *mip_min = CvarGet("mipmin");
    renderdata->mip_min = (I32)mip_min->val;

    renderdata->framecount++;

    renderdata->oldViewLeaf = renderdata->currentViewLeaf;
    renderdata->currentViewLeaf = ModelFindViewLeaf(camera->position, renderdata->worldModel);

    renderdata->in_water = (renderdata->currentViewLeaf->contents <= CONTENTS_WATER);

    TransformFrustum(camera);
    SetupFrustumIndices(camera);

    UpdateVisibleLeaves(renderdata);

    AnimateLights(&g_lightsystem, renderdata->framecount);
}

/* 
 quake: GDI doesn't let us remap palette index 0, so we'll remap color mappings 
 from that black to another one
*/ 
void RemapColorMap(U8 *palette, U8 *colormap)
{
    I32 bestmatchmetric = 256*256*3;
    U8  bestmatch = 0;

	for (U8 i = 1 ; i < 256 ; i++)
	{
		I32 dr = palette[0] - palette[i*3];
		I32 dg = palette[1] - palette[i*3+1];
		I32 db = palette[2] - palette[i*3+2];

		I32 t = (dr * dr) + (dg * dg) + (db * db);

		if (t < bestmatchmetric)
		{
			bestmatchmetric = t;
			bestmatch = i;

			if (t == 0)
				break;
		}
	}

    I32 count = 1 << (8 + COLOR_SHADE_BITS); // 256 * 64
	for (I32 i = 0; i < count; i++)
	{
		if (*colormap == 0)
			*colormap = bestmatch;
        colormap++;
	}
}

void BuildGammaTable(U8 *gammatable, float gamma)
{
    for (I32 i = 0; i < 256; ++i)
    {
        float c = Power(i / 255.0f, gamma) * 255.0f;
        gammatable[i] = (U8)c;
    }
}

void GammaCorrect(U8 *gamma_table, U8 *new_palette, U8 *old_palette)
{
    for (I32 i = 0; i < 256; ++i)
    {
        new_palette[i * 3 + 0] = gamma_table[old_palette[i * 3 + 0]];
        new_palette[i * 3 + 1] = gamma_table[old_palette[i * 3 + 1]];
        new_palette[i * 3 + 2] = gamma_table[old_palette[i * 3 + 2]];
    }
}

RenderBuffer g_renderbuffer;
RenderData g_renderdata;

void RenderView(float dt)
{
    SetupFrame(&g_renderdata, &g_camera, dt);
    SkySetupFrame(&g_skycanvas);

    PushLights(&g_lightsystem, dt, g_renderdata.worldModel->nodes,
               g_renderdata.framecount, g_renderdata.worldModel->surfaces);

    EdgeDrawing(&g_renderdata, &g_camera, &g_renderbuffer, &g_skycanvas);

    U8 *pbuffer = g_renderbuffer.backbuffer 
                + g_camera.screen_rect.y * g_renderbuffer.bytes_per_row 
                + g_camera.screen_rect.x;

    if (g_renderdata.in_water)
    {
        WarpScreen(pbuffer, g_renderbuffer.bytes_per_row, g_renderbuffer.width, 
                   g_renderbuffer.height, g_renderdata.sine_table, g_renderdata.framecount);
    }
}

void RenderInit()
{
    CvarSet("drawflat", 0);
    CvarSet("mipscale", 1);
    CvarSet("mipmin", 0);

    BuildSineTable(g_renderdata.sine_table, SINE_TABLE_SIZE, SINE_SAMPLE_SIZE, 
                   1.0f, 0x10000);
}
