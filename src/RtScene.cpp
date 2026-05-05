#include "RtScene.h"

#include <stdexcept>
#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>

#define THROW_IF_FAILED(hr) \
    if (FAILED(hr)) { throw std::runtime_error("D3D12 call failed at " __FILE__ ":" + std::to_string(__LINE__)); }

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
    // Register all materials up front; keep indices as named constants for clarity
    const uint32_t MatWhite = AddMaterial(WHITE, NO_EMIT);
    const uint32_t MatRed   = AddMaterial(RED,   NO_EMIT);
    const uint32_t MatGreen = AddMaterial(GREEN, NO_EMIT);

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

    // Tall box — right-back area, 15° CW rotation, reaches ~60% of room height
    AddBox( 0.33f, 1.4f,  0.30f, 0.30f,  1.2f,  15.0f, MatWhite);

    // Short box — left-front area, 15° CCW rotation, reaches ~30% of room height
    AddBox(-0.32f, 1.0f,  0.30f, 0.30f,  0.6f, -15.0f, MatWhite);
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
    float InCenterX, float InCenterZ,
    float InHalfX,   float InHalfZ,
    float InHeight,
    float InRotationDeg,
    uint32_t InMaterialIndex)
{
    const float Rad = InRotationDeg * 3.14159265f / 180.0f;
    const float C   = cosf(Rad);
    const float S   = sinf(Rad);

    // Rotate local (Lx, Lz) around Y and translate to world space at height Wy
    auto Corner = [&](float Lx, float Lz, float Wy, float OutV[3])
    {
        OutV[0] = InCenterX + Lx * C - Lz * S;
        OutV[1] = Wy;
        OutV[2] = InCenterZ + Lx * S + Lz * C;
    };

    const float Yb = -1.0f;             // bottom y — sits on the floor
    const float Yt = -1.0f + InHeight;  // top y

    // 4 corners in local XZ, going: left-near, right-near, right-far, left-far
    // ("near" = lower z before rotation, i.e. closer to camera)
    float Vb[4][3], Vt[4][3];
    Corner(-InHalfX, -InHalfZ, Yb, Vb[0]);  Corner(-InHalfX, -InHalfZ, Yt, Vt[0]);
    Corner( InHalfX, -InHalfZ, Yb, Vb[1]);  Corner( InHalfX, -InHalfZ, Yt, Vt[1]);
    Corner( InHalfX,  InHalfZ, Yb, Vb[2]);  Corner( InHalfX,  InHalfZ, Yt, Vt[2]);
    Corner(-InHalfX,  InHalfZ, Yb, Vb[3]);  Corner(-InHalfX,  InHalfZ, Yt, Vt[3]);

    // Outward face normals — each unrotated axis normal rotated by the same θ around Y.
    // Derivation: apply the Y-rotation matrix (cos θ, 0, -sin θ / 0,1,0 / sin θ, 0, cos θ).
    const float NNear[3]  = {  S, 0.0f, -C };  // face 0-1, rotated (0,0,-1)
    const float NRight[3] = {  C, 0.0f,  S };  // face 1-2, rotated (1,0,0)
    const float NFar[3]   = { -S, 0.0f,  C };  // face 2-3, rotated (0,0,1)
    const float NLeft[3]  = { -C, 0.0f, -S };  // face 3-0, rotated (-1,0,0)
    const float NTop[3]   = {  0, 1.0f,  0 };  // top face

    AddQuad(Vb[0], Vb[1], Vt[1], Vt[0], NNear,  InMaterialIndex); // near side
    AddQuad(Vb[1], Vb[2], Vt[2], Vt[1], NRight, InMaterialIndex); // right side
    AddQuad(Vb[2], Vb[3], Vt[3], Vt[2], NFar,   InMaterialIndex); // far side
    AddQuad(Vb[3], Vb[0], Vt[0], Vt[3], NLeft,  InMaterialIndex); // left side
    AddQuad(Vt[0], Vt[1], Vt[2], Vt[3], NTop,   InMaterialIndex); // top
}

// ---------------------------------------------------------------------------
// BVH build — midpoint split along the longest AABB axis
// ---------------------------------------------------------------------------

void RtSampleScene::BuildBVH()
{
    const uint32_t TriCount = static_cast<uint32_t>(Triangles.size());
    BVHNodes.resize(2 * TriCount); // upper bound: 2N-1 nodes
    BVHNextFreeNode = 0;

    SubdivideBVHNode(BVHNextFreeNode++, 0, TriCount);

    BVHNodes.resize(BVHNextFreeNode); // trim to actual size
}

void RtSampleScene::SubdivideBVHNode(uint32_t InNodeIdx, uint32_t InTriFirst, uint32_t InTriCount)
{
    RtBVHNode& Node = BVHNodes[InNodeIdx];

    // Compute AABB over all triangles in [InTriFirst, InTriFirst+InTriCount)
    for (int i = 0; i < 3; ++i) { Node.Min[i] = 1e30f; Node.Max[i] = -1e30f; }
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
    BuildBVH(); // reorders Triangles in-place, fills BVHNodes

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
    Triangles.clear();
    BVHNodes.clear();
    Materials.clear();
    Lights.clear();
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
