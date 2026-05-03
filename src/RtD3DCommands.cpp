#include "RtD3DCommands.h"
#include <stdexcept>
#include <string>
#include <cassert>

// Helper macro ? throws on HRESULT failure
#define THROW_IF_FAILED(hr) \
    if (FAILED(hr)) { throw std::runtime_error("D3D12 call failed at " __FILE__ ":" + std::to_string(__LINE__)); }

bool RtD3DCommands::Initialize(ID3D12Device* InDevice)
{
    assert(InDevice);

    // --- Command Queue (Direct) ---
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    QueueDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    QueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    QueueDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    QueueDesc.NodeMask = 0;

    THROW_IF_FAILED(InDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&D3DCommandQueue)));
    D3DCommandQueue->SetName(L"Rita::MainCommandQueue");

    // --- Command Allocators (one per frame) ---
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
    {
        THROW_IF_FAILED(InDevice->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&FrameAllocators[i])));

        wchar_t Name[64];
        swprintf_s(Name, L"Rita::CommandAllocator[%u]", i);
        FrameAllocators[i]->SetName(Name);
    }

    // --- Command List (reused every frame) ---
    // Created in closed state; BeginFrame will reset it before use.
    THROW_IF_FAILED(InDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        FrameAllocators[0].Get(),
        nullptr, // no initial pipeline state
        IID_PPV_ARGS(&D3DCommandList)));
    D3DCommandList->Close(); // close immediately so BeginFrame can reset safely
    D3DCommandList->SetName(L"Rita::MainCommandList");

    // --- Fence for CPU/GPU synchronization ---
    THROW_IF_FAILED(InDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&D3DFence)));
    D3DFence->SetName(L"Rita::MainFence");

    // FrameFenceValues and NextFenceValue are zero-initialized by default

    FenceCompletionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!FenceCompletionEvent)
    {
        return false;
    }

    return true;
}

void RtD3DCommands::Shutdown()
{
    FlushGPU();

    if (FenceCompletionEvent)
    {
        CloseHandle(FenceCompletionEvent);
        FenceCompletionEvent = nullptr;
    }
}

void RtD3DCommands::BeginFrame(uint32_t InFrameIndex)
{
    // Wait until the GPU has finished using this frame's allocator
    if (D3DFence->GetCompletedValue() < FrameFenceValues[InFrameIndex])
    {
        D3DFence->SetEventOnCompletion(FrameFenceValues[InFrameIndex], FenceCompletionEvent);
        WaitForSingleObjectEx(FenceCompletionEvent, INFINITE, FALSE);
    }

    // Safe to reset now
    FrameAllocators[InFrameIndex]->Reset();
    D3DCommandList->Reset(FrameAllocators[InFrameIndex].Get(), nullptr);
}

void RtD3DCommands::EndFrame(uint32_t InFrameIndex)
{
    // Close and submit
    D3DCommandList->Close();

    ID3D12CommandList* Lists[] = { D3DCommandList.Get() };
    D3DCommandQueue->ExecuteCommandLists(1, Lists);

    // Signal with the global next value ? always monotonically increasing
    D3DCommandQueue->Signal(D3DFence.Get(), NextFenceValue);
    FrameFenceValues[InFrameIndex] = NextFenceValue;
    ++NextFenceValue;
}

void RtD3DCommands::FlushGPU()
{
    // Signal a single value past all queued work and wait for the GPU to reach it
    D3DCommandQueue->Signal(D3DFence.Get(), NextFenceValue);
    D3DFence->SetEventOnCompletion(NextFenceValue, FenceCompletionEvent);
    WaitForSingleObjectEx(FenceCompletionEvent, INFINITE, FALSE);
    ++NextFenceValue;
    // GetCompletedValue() is now >= all previous FrameFenceValues, so BeginFrame won't stall
}
