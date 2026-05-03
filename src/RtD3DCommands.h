#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// Wraps a D3D12 command queue, allocator per frame, and a reusable command list.
// For Milestone 1 we use a single direct queue for rendering.
class RtD3DCommands
{
public:
    static constexpr uint32_t FRAME_COUNT = 2; // double buffering

    bool Initialize(ID3D12Device* InDevice);
    void Shutdown();

    // Begin recording: reset FrameAllocators[FrameIndex] and the command list.
    void BeginFrame(uint32_t InFrameIndex);

    // Stop recording and submit to the queue.
    void EndFrame(uint32_t InFrameIndex);

    // Flush: signal and wait until GPU is fully idle (use at shutdown or resize).
    void FlushGPU();

    ID3D12CommandQueue*        GetQueue()   const { return D3DCommandQueue.Get(); }
    ID3D12GraphicsCommandList* GetCmdList() const { return D3DCommandList.Get(); }

    uint64_t GetCurrentFenceValue() const { return FrameFenceValues[0]; } // for simple use

private:
    ComPtr<ID3D12CommandQueue>          D3DCommandQueue;
    ComPtr<ID3D12CommandAllocator>      FrameAllocators[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList>   D3DCommandList;

    ComPtr<ID3D12Fence>                 D3DFence;
    uint64_t                            NextFenceValue              = 1; // always increasing, never resets
    uint64_t                            FrameFenceValues[FRAME_COUNT] = {};
    HANDLE                              FenceCompletionEvent = nullptr;
};
