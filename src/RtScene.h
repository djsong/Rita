#pragma once

#include "RtSceneTypes.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// Owns the Cornell Box triangle + material data, its BVH, and their GPU buffers.
// Initialize() records all upload commands onto the provided command list;
// the caller must submit and flush the queue before rendering.
class RtSampleScene
{
public:
    bool     Initialize(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmd);
    void     Shutdown();

    D3D12_GPU_VIRTUAL_ADDRESS GetTriangleBufferGPUAddress() const;
    D3D12_GPU_VIRTUAL_ADDRESS GetBVHNodeBufferGPUAddress()  const;
    D3D12_GPU_VIRTUAL_ADDRESS GetMaterialBufferGPUAddress() const;
    D3D12_GPU_VIRTUAL_ADDRESS GetLightBufferGPUAddress()    const;
    uint32_t                  GetLightCount()               const;

private:
    // --- Scene build ---
    void     BuildCornellBox();
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
    // Adds a box sitting on the floor (y=-1), top at y = -1 + InHeight.
    // InRotationDeg rotates the box around the Y axis.
    // No bottom face is generated — the floor quad covers it.
    void     AddBox(
                 float    InCenterX,    float    InCenterZ,
                 float    InHalfX,      float    InHalfZ,
                 float    InHeight,
                 float    InRotationDeg,
                 uint32_t InMaterialIndex);

    // --- BVH build ---
    void BuildBVH();
    void SubdivideBVHNode(uint32_t InNodeIdx, uint32_t InTriFirst, uint32_t InTriCount);

    // --- CPU data (BVH build reorders Triangles in-place) ---
    std::vector<RtTriangle> Triangles;
    std::vector<RtMaterial> Materials;
    std::vector<RtBVHNode>  BVHNodes;
    std::vector<RtLight>    Lights;
    uint32_t                BVHNextFreeNode = 0;

    // --- GPU buffers ---
    ComPtr<ID3D12Resource>  TriangleBuffer;
    ComPtr<ID3D12Resource>  TriangleUploadBuffer;

    ComPtr<ID3D12Resource>  BVHNodeBuffer;
    ComPtr<ID3D12Resource>  BVHNodeUploadBuffer;

    ComPtr<ID3D12Resource>  MaterialBuffer;
    ComPtr<ID3D12Resource>  MaterialUploadBuffer;

    ComPtr<ID3D12Resource>  LightBuffer;
    ComPtr<ID3D12Resource>  LightUploadBuffer;

    // --- Helper: create default+upload buffer pair and record the copy ---
    void UploadBuffer(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmd,
                      const void* InData, uint32_t InByteSize,
                      const wchar_t* InDebugName,
                      ComPtr<ID3D12Resource>& OutGPUBuffer,
                      ComPtr<ID3D12Resource>& OutUploadBuffer);
};
