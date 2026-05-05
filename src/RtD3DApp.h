#pragma once

#include "RtD3DCommands.h"
#include "RtScene.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <cstdint>

using Microsoft::WRL::ComPtr;

class RtD3DApp
{
public:
    RtD3DApp(HINSTANCE InAppHInstance, uint32_t InDisplayWidth, uint32_t InDisplayHeight, const std::wstring& InWindowTitle);
    ~RtD3DApp();

    bool Initialize();
    int  Run();

    static LRESULT CALLBACK WndProc(HWND InHwnd, UINT InMsg, WPARAM InWParam, LPARAM InLParam);

private:
    // Window
    bool CreateAppWindow();
    HINSTANCE    AppHInstance  = nullptr;
    HWND         MainHwnd      = nullptr;
    uint32_t     DisplayWidth  = 0;
    uint32_t     DisplayHeight = 0;
    std::wstring WindowTitle;

    // D3D12 core
    bool InitD3D();
    void Shutdown();

    ComPtr<ID3D12Device>    D3DDevice;
    ComPtr<IDXGIFactory6>   DXGIFactory;
    ComPtr<IDXGISwapChain3> DXGISwapChain;

    static constexpr uint32_t FRAME_COUNT = RtD3DCommands::FRAME_COUNT;

    ComPtr<ID3D12DescriptorHeap>  RtvDescriptorHeap;
    ComPtr<ID3D12Resource>        BackBufferTargets[FRAME_COUNT];
    uint32_t                      RtvDescriptorIncrementSize = 0;
    uint32_t                      CurrentFrameIndex          = 0;

    // Commands
    RtD3DCommands GfxCommands;

    // Scene geometry
    RtSampleScene SampleScene;

    // Compute
    bool InitCompute();
    void DispatchCompute(ID3D12GraphicsCommandList* InCmd);
    void BlitComputeOutput(ID3D12GraphicsCommandList* InCmd);

    ComPtr<ID3D12RootSignature>  ComputeRootSignature;
    ComPtr<ID3D12PipelineState>  ComputePipelineState;
    ComPtr<ID3D12Resource>       ComputeOutputTexture;
    ComPtr<ID3D12Resource>       AccumTexture;        // full-precision running average (u1)
    ComPtr<ID3D12DescriptorHeap> UavDescriptorHeap;

    uint32_t                     FrameAccumCount = 0; // total frames accumulated so far

    // Per-frame rendering
    void Render();
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;

    uint32_t TargetMaxFps = 120; // frame rate cap; set to 0 to disable
};
