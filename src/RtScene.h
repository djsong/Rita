#pragma once

#include "RtSceneTypes.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// CPU-only mesh record — tracks which triangle range belongs to one logical mesh
// and what world transform it carries. Not uploaded to GPU directly; Step 9d will
// convert each RtMesh into an RtInstance GPU entry.
struct RtMesh
{
    uint32_t TriangleOffset;   // first triangle index in the global Triangles array
    uint32_t TriangleCount;    // number of triangles belonging to this mesh
    uint32_t BLASRootNode;     // root node index in BVHNodes, set by BuildBLAS()
    float    Transform[12];    // 3×4 row-major, local→world (identity = mesh is already in world space)
    float    InvTransform[12]; // 3×4 row-major, world→local
};

// Owns the Cornell Box triangle + material data, its BVH, and their GPU buffers.
// Initialize() records all upload commands onto the provided command list;
// the caller must submit and flush the queue before rendering.
class RtSampleScene
{
public:
    bool     Initialize(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmd);
    void     Shutdown();

    D3D12_GPU_VIRTUAL_ADDRESS GetTriangleBufferGPUAddress()  const;
    D3D12_GPU_VIRTUAL_ADDRESS GetBVHNodeBufferGPUAddress()   const;
    D3D12_GPU_VIRTUAL_ADDRESS GetMaterialBufferGPUAddress()  const;
    D3D12_GPU_VIRTUAL_ADDRESS GetLightBufferGPUAddress()     const;
    D3D12_GPU_VIRTUAL_ADDRESS GetInstanceBufferGPUAddress()  const;
    D3D12_GPU_VIRTUAL_ADDRESS GetTLASNodeBufferGPUAddress()  const;
    uint32_t                  GetLightCount()                const;
    uint32_t                  GetInstanceCount()             const;

private:
    // --- Scene build ---
    void     BuildCornellBox();
    // Opens a new mesh — records the current triangle count as the start of the next mesh.
    // Must be paired with EndMesh().
    void     BeginMesh();
    // Closes the current mesh with an identity transform (mesh geometry is in world space).
    void     EndMesh();
    // Closes the current mesh with explicit local→world and world→local transforms.
    // Used for meshes whose geometry is in local space (e.g. the rotated Cornell boxes).
    void     EndMesh(const float* InTransform, const float* InInvTransform);
    uint32_t AddMaterial(const float* InAlbedo, const float* InEmissive);
    // Registers a quad as both visible geometry and a samplable area light.
    // Combines AddQuad + light buffer registration so callers only make one call.
    void     AddLight(const float* InV0, const float* InV1,
                      const float* InV2, const float* InV3,
                      const float* InNormal, const float* InEmissive);
    void     AddQuad(
                 const float* InV0, const float* InV1,
                 const float* InV2, const float* InV3,
                 const float* InNormal,
                 uint32_t     InMaterialIndex);
    // Adds a box in local space: centered at origin in XZ, bottom at y=0, top at y=InHeight.
    // No bottom face is generated — the floor quad covers it.
    // The world transform (Y-rotation + translation) is stored in the mesh's RtInstance
    // and applied by the shader at intersection time.
    void     AddBox(
                 float    InHalfX,      float    InHalfZ,
                 float    InHeight,
                 uint32_t InMaterialIndex);

    // --- BLAS build ---
    // Builds one BVH per mesh over its triangle range; node arrays are stored consecutively
    // in BVHNodes with each mesh recording its BLASRootNode offset.
    // Also populates the Instances list (one RtInstance per mesh) for GPU upload.
    void BuildBLAS();
    void SubdivideBVHNode(uint32_t InNodeIdx, uint32_t InTriFirst, uint32_t InTriCount);

    // --- TLAS build ---
    // Builds a BVH over the world-space AABBs of all instances (taken from their BLAS root bounds).
    // May reorder the Instances array during partitioning — InstanceBuffer must be uploaded after this.
    void BuildTLAS();
    void SubdivideTLASNode(uint32_t InNodeIdx, uint32_t InInstFirst, uint32_t InInstCount);

    // --- CPU data (BLAS build reorders Triangles within each mesh's range in-place) ---
    std::vector<RtTriangle> Triangles;
    std::vector<RtMaterial> Materials;
    std::vector<RtBVHNode>  BVHNodes;
    std::vector<RtLight>    Lights;
    std::vector<RtMesh>     Meshes;
    std::vector<RtInstance> Instances;     // one per mesh; populated by BuildBLAS(), may be reordered by BuildTLAS()
    std::vector<RtTLASNode> TLASNodes;    // populated by BuildTLAS()
    uint32_t                BVHNextFreeNode  = 0;
    uint32_t                TLASNextFreeNode = 0;
    uint32_t                CurrentMeshStart = 0; // triangle index where the current BeginMesh() started

    // --- GPU buffers ---
    ComPtr<ID3D12Resource>  TriangleBuffer;
    ComPtr<ID3D12Resource>  TriangleUploadBuffer;

    ComPtr<ID3D12Resource>  BVHNodeBuffer;
    ComPtr<ID3D12Resource>  BVHNodeUploadBuffer;

    ComPtr<ID3D12Resource>  MaterialBuffer;
    ComPtr<ID3D12Resource>  MaterialUploadBuffer;

    ComPtr<ID3D12Resource>  LightBuffer;
    ComPtr<ID3D12Resource>  LightUploadBuffer;

    ComPtr<ID3D12Resource>  InstanceBuffer;
    ComPtr<ID3D12Resource>  InstanceUploadBuffer;

    ComPtr<ID3D12Resource>  TLASNodeBuffer;
    ComPtr<ID3D12Resource>  TLASNodeUploadBuffer;

    // --- Helper: create default+upload buffer pair and record the copy ---
    void UploadBuffer(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmd,
                      const void* InData, uint32_t InByteSize,
                      const wchar_t* InDebugName,
                      ComPtr<ID3D12Resource>& OutGPUBuffer,
                      ComPtr<ID3D12Resource>& OutUploadBuffer);
};
