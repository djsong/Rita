#include "RtScene.h"

#include <stdexcept>
#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>

#define THROW_IF_FAILED(hr) \
    if (FAILED(hr)) { throw std::runtime_error("D3D12 call failed at " __FILE__ ":" + std::to_string(__LINE__)); }

// ---------------------------------------------------------------------------
// Fills a 3×4 row-major matrix with the identity transform.
// Row-major layout: [1,0,0,0 | 0,1,0,0 | 0,0,1,0]
// ---------------------------------------------------------------------------
static void FillIdentityTransform(float OutT[12])
{
    memset(OutT, 0, 48);
    OutT[0]  = 1.0f; // row 0, col 0
    OutT[5]  = 1.0f; // row 1, col 1
    OutT[10] = 1.0f; // row 2, col 2
}

// ---------------------------------------------------------------------------
// Fills the local→world and world→local 3×4 row-major matrix pair for a box mesh.
// The box is placed with its local origin (bottom) at world y = -1,
// translated to (InCenterX, InCenterZ) in XZ, and rotated around Y by InRotationDeg.
//
// Transform  (local→world):  [R | t]        where R = Y-rotation, t = (CenterX, -1, CenterZ)
// InvTransform (world→local): [R^T | -R^T*t]  (exact inverse for rotation+translation)
// ---------------------------------------------------------------------------
static void FillBoxTransforms(
    float InCenterX, float InCenterZ, float InRotationDeg,
    float OutTransform[12], float OutInvTransform[12])
{
    const float Rad = InRotationDeg * 3.14159265f / 180.0f;
    const float C   = cosf(Rad);
    const float S   = sinf(Rad);

    // Local→world:
    //   Row 0: [ C,  0, -S, CenterX ]
    //   Row 1: [ 0,  1,  0, -1      ]
    //   Row 2: [ S,  0,  C, CenterZ ]
    OutTransform[0]  =  C;    OutTransform[1]  = 0.0f; OutTransform[2]  = -S;   OutTransform[3]  = InCenterX;
    OutTransform[4]  = 0.0f;  OutTransform[5]  = 1.0f; OutTransform[6]  = 0.0f; OutTransform[7]  = -1.0f;
    OutTransform[8]  =  S;    OutTransform[9]  = 0.0f; OutTransform[10] =  C;   OutTransform[11] = InCenterZ;

    // World→local: [R^T | -R^T*t], t = (CenterX, -1, CenterZ)
    //   Row 0: [ C,  0,  S, -(C*CenterX + S*CenterZ) ]
    //   Row 1: [ 0,  1,  0,  1                        ]
    //   Row 2: [-S,  0,  C,   S*CenterX - C*CenterZ   ]
    OutInvTransform[0]  =  C;    OutInvTransform[1]  = 0.0f; OutInvTransform[2]  =  S;   OutInvTransform[3]  = -(C*InCenterX + S*InCenterZ);
    OutInvTransform[4]  = 0.0f;  OutInvTransform[5]  = 1.0f; OutInvTransform[6]  = 0.0f; OutInvTransform[7]  =  1.0f;
    OutInvTransform[8]  = -S;    OutInvTransform[9]  = 0.0f; OutInvTransform[10] =  C;   OutInvTransform[11] =  S*InCenterX - C*InCenterZ;
}

// ---------------------------------------------------------------------------
// Cornell Box material colors
// ---------------------------------------------------------------------------

static constexpr float WHITE[]   = { 0.73f, 0.73f, 0.73f };
static constexpr float RED[]     = { 0.65f, 0.05f, 0.05f };
static constexpr float GREEN[]   = { 0.12f, 0.45f, 0.15f };
static constexpr float LIGHT[]   = { 15.0f, 15.0f, 15.0f };
static constexpr float NO_EMIT[] = {  0.0f,  0.0f,  0.0f };

// ---------------------------------------------------------------------------
// Cornell Box definition
// Box extents: x∈[-1,1], y∈[-1,1], z∈[0,2]. Camera is at (0,0,-3).
// All normals point toward the interior of the box.
// ---------------------------------------------------------------------------

void RtSampleScene::BuildCornellBox()
{
    // Register materials up front — shared across all meshes; indices stay valid
    // regardless of mesh ordering since materials are a separate buffer.
    const uint32_t MatWhite = AddMaterial(WHITE, NO_EMIT);
    const uint32_t MatRed   = AddMaterial(RED,   NO_EMIT);
    const uint32_t MatGreen = AddMaterial(GREEN, NO_EMIT);

    // --- Mesh 0: Room ---
    // Back wall, floor, ceiling, left/right walls, ceiling light panel.
    // All geometry is in world space — identity transform.
    BeginMesh();
    {
        // Back wall — white, z=2, normal (0,0,-1)
        {
            const float N[]  = { 0, 0, -1 };
            const float V0[] = { -1, -1, 2 }, V1[] = { 1, -1, 2 }, V2[] = { 1, 1, 2 }, V3[] = { -1, 1, 2 };
            AddQuad(V0, V1, V2, V3, N, MatWhite);
        }

        // Floor — white, y=-1, normal (0,1,0)
        {
            const float N[]  = { 0, 1, 0 };
            const float V0[] = { -1, -1, 0 }, V1[] = { 1, -1, 0 }, V2[] = { 1, -1, 2 }, V3[] = { -1, -1, 2 };
            AddQuad(V0, V1, V2, V3, N, MatWhite);
        }

        // Ceiling — white, y=1, normal (0,-1,0)
        {
            const float N[]  = { 0, -1, 0 };
            const float V0[] = { -1, 1, 2 }, V1[] = { 1, 1, 2 }, V2[] = { 1, 1, 0 }, V3[] = { -1, 1, 0 };
            AddQuad(V0, V1, V2, V3, N, MatWhite);
        }

        // Left wall — red, x=-1, normal (1,0,0)
        {
            const float N[]  = { 1, 0, 0 };
            const float V0[] = { -1, -1, 2 }, V1[] = { -1, -1, 0 }, V2[] = { -1, 1, 0 }, V3[] = { -1, 1, 2 };
            AddQuad(V0, V1, V2, V3, N, MatRed);
        }

        // Right wall — green, x=1, normal (-1,0,0)
        {
            const float N[]  = { -1, 0, 0 };
            const float V0[] = { 1, -1, 0 }, V1[] = { 1, -1, 2 }, V2[] = { 1, 1, 2 }, V3[] = { 1, 1, 0 };
            AddQuad(V0, V1, V2, V3, N, MatGreen);
        }

        // Ceiling light — emissive quad, slightly below y=1 so it doesn't z-fight the ceiling.
        // AddLight registers both the visible geometry and the samplable area light entry.
        {
            const float N[]  = { 0, -1, 0 };
            const float V0[] = { -0.3f, 0.99f, 0.8f }, V1[] = {  0.3f, 0.99f, 0.8f },
                        V2[] = {  0.3f, 0.99f, 1.2f }, V3[] = { -0.3f, 0.99f, 1.2f };
            AddLight(V0, V1, V2, V3, N, LIGHT);
        }
    }
    EndMesh();

    // --- Mesh 1: Tall box ---
    // Right-back area, 15° CW rotation around Y, reaches ~60% of room height.
    // Geometry is in local space (AABB centered at origin); transform places it in the scene.
    {
        float T[12], InvT[12];
        FillBoxTransforms(0.33f, 1.4f, 15.0f, T, InvT);
        BeginMesh();
        AddBox(0.30f, 0.30f, 1.2f, MatWhite);
        EndMesh(T, InvT);
    }

    // --- Mesh 2: Short box ---
    // Left-front area, 15° CCW rotation around Y, reaches ~30% of room height.
    {
        float T[12], InvT[12];
        FillBoxTransforms(-0.32f, 1.0f, -15.0f, T, InvT);
        BeginMesh();
        AddBox(0.30f, 0.30f, 0.6f, MatWhite);
        EndMesh(T, InvT);
    }
}

void RtSampleScene::BeginMesh()
{
    CurrentMeshStart = static_cast<uint32_t>(Triangles.size());
}

void RtSampleScene::EndMesh()
{
    float Identity[12];
    FillIdentityTransform(Identity);
    EndMesh(Identity, Identity);
}

void RtSampleScene::EndMesh(const float* InTransform, const float* InInvTransform)
{
    RtMesh Mesh         = {};
    Mesh.TriangleOffset = CurrentMeshStart;
    Mesh.TriangleCount  = static_cast<uint32_t>(Triangles.size()) - CurrentMeshStart;
    Mesh.BLASRootNode   = 0; // set by BuildBLAS()
    memcpy(Mesh.Transform,    InTransform,    48);
    memcpy(Mesh.InvTransform, InInvTransform, 48);
    Meshes.push_back(Mesh);
}

void RtSampleScene::AddLight(
    const float* InV0, const float* InV1,
    const float* InV2, const float* InV3,
    const float* InNormal, const float* InEmissive)
{
    // Register as visible geometry so camera rays can see the light panel
    const uint32_t MatIdx = AddMaterial(WHITE, InEmissive);
    AddQuad(InV0, InV1, InV2, InV3, InNormal, MatIdx);

    // Derive edge vectors from the quad vertices: EdgeU = V1-V0, EdgeV = V3-V0
    const float EdgeU[3] = { InV1[0]-InV0[0], InV1[1]-InV0[1], InV1[2]-InV0[2] };
    const float EdgeV[3] = { InV3[0]-InV0[0], InV3[1]-InV0[1], InV3[2]-InV0[2] };

    // Area = |EdgeU × EdgeV|
    const float Cross[3] =
    {
        EdgeU[1]*EdgeV[2] - EdgeU[2]*EdgeV[1],
        EdgeU[2]*EdgeV[0] - EdgeU[0]*EdgeV[2],
        EdgeU[0]*EdgeV[1] - EdgeU[1]*EdgeV[0]
    };

    RtLight Light = {};
    memcpy(Light.Corner,   InV0,       12);
    memcpy(Light.EdgeU,    EdgeU,      12);
    memcpy(Light.EdgeV,    EdgeV,      12);
    memcpy(Light.Normal,   InNormal,   12);
    memcpy(Light.Emissive, InEmissive, 12);
    Light.Area = sqrtf(Cross[0]*Cross[0] + Cross[1]*Cross[1] + Cross[2]*Cross[2]);

    Lights.push_back(Light);
}

uint32_t RtSampleScene::AddMaterial(const float* InAlbedo, const float* InEmissive)
{
    RtMaterial Mat = {};
    memcpy(Mat.Albedo,   InAlbedo,   12);
    memcpy(Mat.Emissive, InEmissive, 12);
    Materials.push_back(Mat);
    return static_cast<uint32_t>(Materials.size() - 1);
}

void RtSampleScene::AddQuad(
    const float* InV0, const float* InV1, const float* InV2, const float* InV3,
    const float* InNormal, uint32_t InMaterialIndex)
{
    auto MakeTri = [&](const float* A, const float* B, const float* C) -> RtTriangle
    {
        RtTriangle Tri = {};
        memcpy(Tri.V0,     A,        12);
        memcpy(Tri.V1,     B,        12);
        memcpy(Tri.V2,     C,        12);
        memcpy(Tri.Normal, InNormal, 12);
        Tri.MaterialIndex = InMaterialIndex;
        return Tri;
    };

    Triangles.push_back(MakeTri(InV0, InV1, InV2));
    Triangles.push_back(MakeTri(InV0, InV2, InV3));
}

void RtSampleScene::AddBox(
    float    InHalfX, float    InHalfZ,
    float    InHeight,
    uint32_t InMaterialIndex)
{
    // Local-space AABB: centered at origin in XZ, bottom at y=0, top at y=InHeight.
    // The world transform (Y-rotation + translation to floor) is stored in the mesh's
    // RtInstance and applied by the shader at intersection time — see FillBoxTransforms.
    const float Yb = 0.0f;
    const float Yt = InHeight;

    float Vb[4][3] =
    {
        { -InHalfX, Yb, -InHalfZ },  // 0: left-near  bottom
        {  InHalfX, Yb, -InHalfZ },  // 1: right-near bottom
        {  InHalfX, Yb,  InHalfZ },  // 2: right-far  bottom
        { -InHalfX, Yb,  InHalfZ }   // 3: left-far   bottom
    };
    float Vt[4][3] =
    {
        { -InHalfX, Yt, -InHalfZ },  // 0: left-near  top
        {  InHalfX, Yt, -InHalfZ },  // 1: right-near top
        {  InHalfX, Yt,  InHalfZ },  // 2: right-far  top
        { -InHalfX, Yt,  InHalfZ }   // 3: left-far   top
    };

    // Axis-aligned outward normals in local space
    const float NNear[3]  = {  0.0f, 0.0f, -1.0f };  // -Z face
    const float NRight[3] = {  1.0f, 0.0f,  0.0f };  // +X face
    const float NFar[3]   = {  0.0f, 0.0f,  1.0f };  // +Z face
    const float NLeft[3]  = { -1.0f, 0.0f,  0.0f };  // -X face
    const float NTop[3]   = {  0.0f, 1.0f,  0.0f };  // +Y face

    AddQuad(Vb[0], Vb[1], Vt[1], Vt[0], NNear,  InMaterialIndex); // near face
    AddQuad(Vb[1], Vb[2], Vt[2], Vt[1], NRight, InMaterialIndex); // right face
    AddQuad(Vb[2], Vb[3], Vt[3], Vt[2], NFar,   InMaterialIndex); // far face
    AddQuad(Vb[3], Vb[0], Vt[0], Vt[3], NLeft,  InMaterialIndex); // left face
    AddQuad(Vt[0], Vt[1], Vt[2], Vt[3], NTop,   InMaterialIndex); // top face
}

// ---------------------------------------------------------------------------
// BLAS build — one BVH per mesh, nodes stored consecutively in BVHNodes.
// SubdivideBVHNode uses a midpoint split along the longest AABB axis.
// ---------------------------------------------------------------------------

void RtSampleScene::BuildBLAS()
{
    const uint32_t TotalTriCount = static_cast<uint32_t>(Triangles.size());
    BVHNodes.resize(2 * TotalTriCount); // upper bound: sum(2*Ni-1) <= 2*TotalN
    BVHNextFreeNode = 0;

    // Build one BVH per mesh; each mesh's nodes follow the previous mesh's in BVHNodes.
    for (RtMesh& Mesh : Meshes)
    {
        Mesh.BLASRootNode = BVHNextFreeNode++;
        SubdivideBVHNode(Mesh.BLASRootNode, Mesh.TriangleOffset, Mesh.TriangleCount);
    }

    BVHNodes.resize(BVHNextFreeNode); // trim to actual size

    // Build one RtInstance per mesh so the GPU can locate each BLAS and its triangle range.
    Instances.clear();
    Instances.reserve(Meshes.size());
    for (const RtMesh& Mesh : Meshes)
    {
        RtInstance Inst   = {};
        memcpy(Inst.Transform,    Mesh.Transform,    48);
        memcpy(Inst.InvTransform, Mesh.InvTransform, 48);
        Inst.BLASRootNode   = Mesh.BLASRootNode;
        Inst.TriangleOffset = Mesh.TriangleOffset;
        Instances.push_back(Inst);
    }
}

void RtSampleScene::SubdivideBVHNode(uint32_t InNodeIdx, uint32_t InTriFirst, uint32_t InTriCount)
{
    RtBVHNode& Node = BVHNodes[InNodeIdx];

    // Compute AABB over all triangles in [InTriFirst, InTriFirst+InTriCount)
    for (int i = 0; i < 3; ++i) 
    { 
        Node.Min[i] = 1e30f; 
        Node.Max[i] = -1e30f; 
    }
    for (uint32_t i = InTriFirst; i < InTriFirst + InTriCount; ++i)
    {
        const float* Verts[3] = { Triangles[i].V0, Triangles[i].V1, Triangles[i].V2 };
        for (const float* V : Verts)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Node.Min[Axis] = min(Node.Min[Axis], V[Axis]);
                Node.Max[Axis] = max(Node.Max[Axis], V[Axis]);
            }
        }
    }

    // Leaf: two or fewer triangles, no point splitting further
    if (InTriCount <= 2)
    {
        Node.LeftOrFirst   = InTriFirst;
        Node.TriangleCount = InTriCount;
        return;
    }

    // Find the longest axis of the AABB
    int   SplitAxis = 0;
    float MaxExtent = Node.Max[0] - Node.Min[0];
    for (int i = 1; i < 3; ++i)
    {
        const float Extent = Node.Max[i] - Node.Min[i];
        if (Extent > MaxExtent) { MaxExtent = Extent; SplitAxis = i; }
    }

    // Partition triangles around the centroid midpoint on SplitAxis
    const float MidPoint = 0.5f * (Node.Min[SplitAxis] + Node.Max[SplitAxis]);

    int Left  = static_cast<int>(InTriFirst);
    int Right = static_cast<int>(InTriFirst + InTriCount) - 1;
    while (Left <= Right)
    {
        const float Centroid = (Triangles[Left].V0[SplitAxis]
                              + Triangles[Left].V1[SplitAxis]
                              + Triangles[Left].V2[SplitAxis]) / 3.0f;
        if (Centroid <= MidPoint)
        {
            ++Left;
        }
        else
        {
            std::swap(Triangles[Left], Triangles[Right--]);
        }
    }

    const uint32_t LeftCount = static_cast<uint32_t>(Left) - InTriFirst;

    // Degenerate split fallback — make a leaf to avoid infinite recursion
    if (LeftCount == 0 || LeftCount == InTriCount)
    {
        Node.LeftOrFirst   = InTriFirst;
        Node.TriangleCount = InTriCount;
        return;
    }

    // Allocate left and right child slots (always consecutive)
    const uint32_t LeftChildIdx  = BVHNextFreeNode++;
    const uint32_t RightChildIdx = BVHNextFreeNode++;

    Node.LeftOrFirst   = LeftChildIdx; // RightChild = LeftChildIdx + 1
    Node.TriangleCount = 0;            // internal node

    SubdivideBVHNode(LeftChildIdx,  InTriFirst,              LeftCount);
    SubdivideBVHNode(RightChildIdx, InTriFirst + LeftCount,  InTriCount - LeftCount);
}

// ---------------------------------------------------------------------------
// Transforms all 8 corners of a local-space AABB through a 3×4 row-major matrix
// and expands the caller's world-space AABB (WorldMin/WorldMax) to enclose them.
// Used by SubdivideTLASNode to compute correct world-space bounds for rotated instances.
// ---------------------------------------------------------------------------
static void ExpandWorldAABBByTransformedCorners(
    const float* T,         // [12] 3×4 row-major local→world matrix
    const float* LocalMin,  // [3]  local AABB min
    const float* LocalMax,  // [3]  local AABB max
    float*       WorldMin,  // [3]  in/out world AABB min
    float*       WorldMax)  // [3]  in/out world AABB max
{
    for (int CornerIdx = 0; CornerIdx < 8; ++CornerIdx)
    {
        const float Lx = (CornerIdx & 1) ? LocalMax[0] : LocalMin[0];
        const float Ly = (CornerIdx & 2) ? LocalMax[1] : LocalMin[1];
        const float Lz = (CornerIdx & 4) ? LocalMax[2] : LocalMin[2];

        const float Wx = T[0]*Lx + T[1]*Ly + T[2]*Lz + T[3];
        const float Wy = T[4]*Lx + T[5]*Ly + T[6]*Lz + T[7];
        const float Wz = T[8]*Lx + T[9]*Ly + T[10]*Lz + T[11];

        WorldMin[0] = min(WorldMin[0], Wx);  WorldMax[0] = max(WorldMax[0], Wx);
        WorldMin[1] = min(WorldMin[1], Wy);  WorldMax[1] = max(WorldMax[1], Wy);
        WorldMin[2] = min(WorldMin[2], Wz);  WorldMax[2] = max(WorldMax[2], Wz);
    }
}

// ---------------------------------------------------------------------------
// TLAS build — BVH over world-space instance AABBs (taken from BLAS root bounds).
// Instances may be reordered in-place during partitioning, so InstanceBuffer must
// be uploaded after BuildTLAS, not after BuildBLAS.
// ---------------------------------------------------------------------------

void RtSampleScene::BuildTLAS()
{
    const uint32_t InstCount = static_cast<uint32_t>(Instances.size());
    TLASNodes.resize(2 * InstCount); // upper bound: 2N-1 nodes
    TLASNextFreeNode = 0;

    SubdivideTLASNode(TLASNextFreeNode++, 0, InstCount);

    TLASNodes.resize(TLASNextFreeNode); // trim to actual size
}

void RtSampleScene::SubdivideTLASNode(uint32_t InNodeIdx, uint32_t InInstFirst, uint32_t InInstCount)
{
    RtTLASNode& Node = TLASNodes[InNodeIdx];

    // Compute world-space AABB over all instances in [InInstFirst, InInstFirst+InInstCount).
    // Each instance's BLAS root bounds are in local space; we transform all 8 corners to
    // world space via the instance's Transform and take the enclosing AABB.
    for (int i = 0; i < 3; ++i)
    {
        Node.Min[i] =  1e30f;
        Node.Max[i] = -1e30f;
    }
    for (uint32_t i = InInstFirst; i < InInstFirst + InInstCount; ++i)
    {
        const RtBVHNode& BLASRoot = BVHNodes[Instances[i].BLASRootNode];
        ExpandWorldAABBByTransformedCorners(
            Instances[i].Transform,
            BLASRoot.Min, BLASRoot.Max,
            Node.Min, Node.Max);
    }

    // Leaf: one instance, no point splitting further
    if (InInstCount <= 1)
    {
        Node.LeftOrFirst   = InInstFirst;
        Node.InstanceCount = InInstCount;
        return;
    }

    // Find the longest axis of the combined AABB
    int   SplitAxis = 0;
    float MaxExtent = Node.Max[0] - Node.Min[0];
    for (int i = 1; i < 3; ++i)
    {
        const float Extent = Node.Max[i] - Node.Min[i];
        if (Extent > MaxExtent) { MaxExtent = Extent; SplitAxis = i; }
    }

    // Partition instances around the centroid midpoint on SplitAxis.
    // Centroid = midpoint of each instance's BLAS root AABB on SplitAxis.
    const float MidPoint = 0.5f * (Node.Min[SplitAxis] + Node.Max[SplitAxis]);

    int Left  = static_cast<int>(InInstFirst);
    int Right = static_cast<int>(InInstFirst + InInstCount) - 1;
    while (Left <= Right)
    {
        // Compute the world-space centroid of this instance along SplitAxis
        const RtBVHNode& BLASRoot = BVHNodes[Instances[Left].BLASRootNode];
        float WMin[3] = { 1e30f, 1e30f, 1e30f };
        float WMax[3] = { -1e30f, -1e30f, -1e30f };
        ExpandWorldAABBByTransformedCorners(
            Instances[Left].Transform,
            BLASRoot.Min, BLASRoot.Max,
            WMin, WMax);
        const float Centroid = 0.5f * (WMin[SplitAxis] + WMax[SplitAxis]);
        if (Centroid <= MidPoint)
        {
            ++Left;
        }
        else
        {
            std::swap(Instances[Left], Instances[Right--]);
        }
    }

    const uint32_t LeftCount = static_cast<uint32_t>(Left) - InInstFirst;

    // Degenerate split fallback — make a leaf to avoid infinite recursion
    if (LeftCount == 0 || LeftCount == InInstCount)
    {
        Node.LeftOrFirst   = InInstFirst;
        Node.InstanceCount = InInstCount;
        return;
    }

    // Allocate left and right child slots (always consecutive)
    const uint32_t LeftChildIdx  = TLASNextFreeNode++;
    const uint32_t RightChildIdx = TLASNextFreeNode++;

    Node.LeftOrFirst   = LeftChildIdx; // RightChild = LeftChildIdx + 1
    Node.InstanceCount = 0;            // internal node

    SubdivideTLASNode(LeftChildIdx,  InInstFirst,             LeftCount);
    SubdivideTLASNode(RightChildIdx, InInstFirst + LeftCount, InInstCount - LeftCount);
}

// ---------------------------------------------------------------------------
// GPU upload helper
// ---------------------------------------------------------------------------

void RtSampleScene::UploadBuffer(
    ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmd,
    const void* InData, uint32_t InByteSize,
    const wchar_t* InDebugName,
    ComPtr<ID3D12Resource>& OutGPUBuffer,
    ComPtr<ID3D12Resource>& OutUploadBuffer)
{
    D3D12_RESOURCE_DESC BufDesc = {};
    BufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufDesc.Width            = InByteSize;
    BufDesc.Height           = 1;
    BufDesc.DepthOrArraySize = 1;
    BufDesc.MipLevels        = 1;
    BufDesc.SampleDesc.Count = 1;
    BufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // Default heap — GPU reads from here
    D3D12_HEAP_PROPERTIES DefaultHeap = {};
    DefaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    THROW_IF_FAILED(InDevice->CreateCommittedResource(
        &DefaultHeap, D3D12_HEAP_FLAG_NONE, &BufDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&OutGPUBuffer)));
    OutGPUBuffer->SetName(InDebugName);

    // Upload heap — CPU writes here
    D3D12_HEAP_PROPERTIES UploadHeap = {};
    UploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    THROW_IF_FAILED(InDevice->CreateCommittedResource(
        &UploadHeap, D3D12_HEAP_FLAG_NONE, &BufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&OutUploadBuffer)));

    // Map, copy, unmap
    void* MappedPtr = nullptr;
    D3D12_RANGE ReadRange = { 0, 0 };
    THROW_IF_FAILED(OutUploadBuffer->Map(0, &ReadRange, &MappedPtr));
    memcpy(MappedPtr, InData, InByteSize);
    OutUploadBuffer->Unmap(0, nullptr);

    // Record copy and transition
    InCmd->CopyBufferRegion(OutGPUBuffer.Get(), 0, OutUploadBuffer.Get(), 0, InByteSize);

    D3D12_RESOURCE_BARRIER Barrier = {};
    Barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource   = OutGPUBuffer.Get();
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    Barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    InCmd->ResourceBarrier(1, &Barrier);
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

bool RtSampleScene::Initialize(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmd)
{
    BuildCornellBox();
    BuildBLAS();  // reorders Triangles within each mesh's range, fills BVHNodes + Instances
    BuildTLAS();  // may reorder Instances, fills TLASNodes — must run before InstanceBuffer upload

    UploadBuffer(InDevice, InCmd,
        Triangles.data(),
        static_cast<uint32_t>(Triangles.size() * sizeof(RtTriangle)),
        TEXT("Rita::TriangleBuffer"),
        TriangleBuffer, TriangleUploadBuffer);

    UploadBuffer(InDevice, InCmd,
        BVHNodes.data(),
        static_cast<uint32_t>(BVHNodes.size() * sizeof(RtBVHNode)),
        TEXT("Rita::BVHNodeBuffer"),
        BVHNodeBuffer, BVHNodeUploadBuffer);

    UploadBuffer(InDevice, InCmd,
        Materials.data(),
        static_cast<uint32_t>(Materials.size() * sizeof(RtMaterial)),
        TEXT("Rita::MaterialBuffer"),
        MaterialBuffer, MaterialUploadBuffer);

    UploadBuffer(InDevice, InCmd,
        Lights.data(),
        static_cast<uint32_t>(Lights.size() * sizeof(RtLight)),
        TEXT("Rita::LightBuffer"),
        LightBuffer, LightUploadBuffer);

    UploadBuffer(InDevice, InCmd,
        Instances.data(),
        static_cast<uint32_t>(Instances.size() * sizeof(RtInstance)),
        TEXT("Rita::InstanceBuffer"),
        InstanceBuffer, InstanceUploadBuffer);

    UploadBuffer(InDevice, InCmd,
        TLASNodes.data(),
        static_cast<uint32_t>(TLASNodes.size() * sizeof(RtTLASNode)),
        TEXT("Rita::TLASNodeBuffer"),
        TLASNodeBuffer, TLASNodeUploadBuffer);

    return true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void RtSampleScene::Shutdown()
{
    TriangleBuffer.Reset();
    TriangleUploadBuffer.Reset();
    BVHNodeBuffer.Reset();
    BVHNodeUploadBuffer.Reset();
    MaterialBuffer.Reset();
    MaterialUploadBuffer.Reset();
    LightBuffer.Reset();
    LightUploadBuffer.Reset();
    InstanceBuffer.Reset();
    InstanceUploadBuffer.Reset();
    TLASNodeBuffer.Reset();
    TLASNodeUploadBuffer.Reset();
    Triangles.clear();
    BVHNodes.clear();
    Materials.clear();
    Lights.clear();
    Meshes.clear();
    Instances.clear();
    TLASNodes.clear();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

D3D12_GPU_VIRTUAL_ADDRESS RtSampleScene::GetTriangleBufferGPUAddress() const
{
    return TriangleBuffer->GetGPUVirtualAddress();
}

D3D12_GPU_VIRTUAL_ADDRESS RtSampleScene::GetBVHNodeBufferGPUAddress() const
{
    return BVHNodeBuffer->GetGPUVirtualAddress();
}

D3D12_GPU_VIRTUAL_ADDRESS RtSampleScene::GetMaterialBufferGPUAddress() const
{
    return MaterialBuffer->GetGPUVirtualAddress();
}

D3D12_GPU_VIRTUAL_ADDRESS RtSampleScene::GetLightBufferGPUAddress() const
{
    return LightBuffer->GetGPUVirtualAddress();
}

uint32_t RtSampleScene::GetLightCount() const
{
    return static_cast<uint32_t>(Lights.size());
}

D3D12_GPU_VIRTUAL_ADDRESS RtSampleScene::GetInstanceBufferGPUAddress() const
{
    return InstanceBuffer->GetGPUVirtualAddress();
}

uint32_t RtSampleScene::GetInstanceCount() const
{
    return static_cast<uint32_t>(Instances.size());
}

D3D12_GPU_VIRTUAL_ADDRESS RtSampleScene::GetTLASNodeBufferGPUAddress() const
{
    return TLASNodeBuffer->GetGPUVirtualAddress();
}
