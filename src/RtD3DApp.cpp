#include "RtD3DApp.h"

#include <stdexcept>
#include <fstream>
#include <vector>
#include <cassert>
#include <chrono>
#include <thread>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#define THROW_IF_FAILED(hr) \
    if (FAILED(hr)) { throw std::runtime_error("D3D12 call failed at " __FILE__ ":" + std::to_string(__LINE__)); }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<uint8_t> LoadBinaryFile(const wchar_t* InRelativePath)
{
    wchar_t ExeDir[MAX_PATH];
    GetModuleFileNameW(nullptr, ExeDir, MAX_PATH);
    wchar_t* LastSlash = wcsrchr(ExeDir, TEXT('\\'));
    if (LastSlash)
    {
        *(LastSlash + 1) = TEXT('\0');
    }

    std::wstring FullPath = std::wstring(ExeDir) + InRelativePath;

    std::ifstream File(FullPath, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        throw std::runtime_error("Failed to open shader file");
    }

    const size_t FileSize = static_cast<size_t>(File.tellg());
    File.seekg(0);
    std::vector<uint8_t> Data(FileSize);
    File.read(reinterpret_cast<char*>(Data.data()), FileSize);
    return Data;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

RtD3DApp::RtD3DApp(HINSTANCE InAppHInstance, uint32_t InDisplayWidth, uint32_t InDisplayHeight, const std::wstring& InWindowTitle)
    : AppHInstance(InAppHInstance)
    , DisplayWidth(InDisplayWidth)
    , DisplayHeight(InDisplayHeight)
    , WindowTitle(InWindowTitle)
{
}

RtD3DApp::~RtD3DApp()
{
    Shutdown();
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

bool RtD3DApp::Initialize()
{
    if (!CreateAppWindow())
    {
        return false;
    }

    if (!InitD3D())
    {
        return false;
    }

    // One-time upload pass: record scene geometry copy commands, submit, flush
    GfxCommands.BeginFrame(0);
    if (!SampleScene.Initialize(D3DDevice.Get(), GfxCommands.GetCmdList()))
    {
        return false;
    }
    GfxCommands.EndFrame(0);
    GfxCommands.FlushGPU();

    if (!InitCompute())
    {
        return false;
    }

    ShowWindow(MainHwnd, SW_SHOWDEFAULT);
    UpdateWindow(MainHwnd);

    return true;
}

// ---------------------------------------------------------------------------
// Win32 Window
// ---------------------------------------------------------------------------

bool RtD3DApp::CreateAppWindow()
{
    const wchar_t* CLASS_NAME = L"RitaWindowClass";

    WNDCLASSEXW Wc = {};
    Wc.cbSize        = sizeof(WNDCLASSEXW);
    Wc.style         = CS_HREDRAW | CS_VREDRAW;
    Wc.lpfnWndProc   = RtD3DApp::WndProc;
    Wc.hInstance     = AppHInstance;
    Wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    Wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassExW(&Wc))
    {
        return false;
    }

    RECT Rect = { 0, 0, static_cast<LONG>(DisplayWidth), static_cast<LONG>(DisplayHeight) };
    AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);

    MainHwnd = CreateWindowExW(
        0, CLASS_NAME, WindowTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        Rect.right - Rect.left, Rect.bottom - Rect.top,
        nullptr, nullptr, AppHInstance, this);

    return MainHwnd != nullptr;
}

LRESULT CALLBACK RtD3DApp::WndProc(HWND InHwnd, UINT InMsg, WPARAM InWParam, LPARAM InLParam)
{
    if (InMsg == WM_CREATE)
    {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(InLParam);
        SetWindowLongPtrW(InHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
        return 0;
    }

    auto* App = reinterpret_cast<RtD3DApp*>(GetWindowLongPtrW(InHwnd, GWLP_USERDATA));

    switch (InMsg)
    {
    case WM_KEYDOWN:
        if (InWParam == VK_ESCAPE)
        {
            PostQuitMessage(0);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(InHwnd, InMsg, InWParam, InLParam);
}

// ---------------------------------------------------------------------------
// D3D12 Initialization
// ---------------------------------------------------------------------------

bool RtD3DApp::InitD3D()
{
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> DebugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController))))
        {
            DebugController->EnableDebugLayer();
        }
    }
#endif

    UINT FactoryFlags = 0;
#if defined(_DEBUG)
    FactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    THROW_IF_FAILED(CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&DXGIFactory)));

    ComPtr<IDXGIAdapter1> Adapter;
    for (UINT i = 0; DXGIFactory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&Adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 Desc;
        Adapter->GetDesc1(&Desc);
        if (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&D3DDevice))))
        {
            break;
        }
    }

    if (!D3DDevice)
    {
        return false;
    }
    D3DDevice->SetName(L"Rita::Device");

    if (!GfxCommands.Initialize(D3DDevice.Get()))
    {
        return false;
    }

    {
        DXGI_SWAP_CHAIN_DESC1 ScDesc = {};
        ScDesc.Width              = DisplayWidth;
        ScDesc.Height             = DisplayHeight;
        ScDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        ScDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        ScDesc.BufferCount        = FRAME_COUNT;
        ScDesc.SampleDesc.Count   = 1;
        ScDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> SwapChain1;
        THROW_IF_FAILED(DXGIFactory->CreateSwapChainForHwnd(
            GfxCommands.GetQueue(), MainHwnd, &ScDesc, nullptr, nullptr, &SwapChain1));
        DXGIFactory->MakeWindowAssociation(MainHwnd, DXGI_MWA_NO_ALT_ENTER);
        THROW_IF_FAILED(SwapChain1.As(&DXGISwapChain));
    }

    CurrentFrameIndex = DXGISwapChain->GetCurrentBackBufferIndex();

    {
        D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
        HeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        HeapDesc.NumDescriptors = FRAME_COUNT;
        THROW_IF_FAILED(D3DDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&RtvDescriptorHeap)));
        RtvDescriptorHeap->SetName(L"Rita::RTVHeap");
        RtvDescriptorIncrementSize = D3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        {
            THROW_IF_FAILED(DXGISwapChain->GetBuffer(i, IID_PPV_ARGS(&BackBufferTargets[i])));
            D3DDevice->CreateRenderTargetView(BackBufferTargets[i].Get(), nullptr, RtvHandle);

            wchar_t Name[64];
            swprintf_s(Name, L"Rita::BackBuffer[%u]", i);
            BackBufferTargets[i]->SetName(Name);

            RtvHandle.ptr += RtvDescriptorIncrementSize;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Compute Initialization
// ---------------------------------------------------------------------------

bool RtD3DApp::InitCompute()
{
    // --- Shader bytecode ---
    const std::vector<uint8_t> ShaderBytecode = LoadBinaryFile(L"shaders\\RayGen.cso");

    // --- Root signature ---
    // [0] Descriptor table: 2 UAVs — u0 OutputTexture, u1 AccumTexture
    // [1] Root SRV at t0 (triangle structured buffer)
    // [2] Root SRV at t1 (BVH node structured buffer)
    // [3] Root SRV at t2 (material structured buffer)
    // [4] Root constants at b0 — FrameIndex (1 × 32-bit)
    D3D12_DESCRIPTOR_RANGE UavRange = {};
    UavRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    UavRange.NumDescriptors     = 2; // u0 + u1
    UavRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER RootParams[6] = {};

    RootParams[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    RootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    RootParams[0].DescriptorTable.pDescriptorRanges   = &UavRange;
    RootParams[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    RootParams[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    RootParams[1].Descriptor.ShaderRegister = 0; // t0 — triangles
    RootParams[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    RootParams[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    RootParams[2].Descriptor.ShaderRegister = 1; // t1 — BVH nodes
    RootParams[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    RootParams[3].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    RootParams[3].Descriptor.ShaderRegister = 2; // t2 — materials
    RootParams[3].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    RootParams[4].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    RootParams[4].Constants.ShaderRegister = 0; // b0 — FrameIndex, LightCount
    RootParams[4].Constants.RegisterSpace  = 0;
    RootParams[4].Constants.Num32BitValues = 2;
    RootParams[4].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

    RootParams[5].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    RootParams[5].Descriptor.ShaderRegister = 3; // t3 — lights
    RootParams[5].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
    RootSigDesc.NumParameters = 6;
    RootSigDesc.pParameters   = RootParams;

    ComPtr<ID3DBlob> SerializedRootSig, ErrorBlob;
    THROW_IF_FAILED(D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &SerializedRootSig, &ErrorBlob));
    THROW_IF_FAILED(D3DDevice->CreateRootSignature(0, SerializedRootSig->GetBufferPointer(), SerializedRootSig->GetBufferSize(), IID_PPV_ARGS(&ComputeRootSignature)));
    ComputeRootSignature->SetName(L"Rita::ComputeRootSignature");

    // --- Compute PSO ---
    D3D12_COMPUTE_PIPELINE_STATE_DESC PsoDesc = {};
    PsoDesc.pRootSignature = ComputeRootSignature.Get();
    PsoDesc.CS             = { ShaderBytecode.data(), ShaderBytecode.size() };
    THROW_IF_FAILED(D3DDevice->CreateComputePipelineState(&PsoDesc, IID_PPV_ARGS(&ComputePipelineState)));
    ComputePipelineState->SetName(L"Rita::ComputePSO");

    // --- UAV descriptor heap — 2 slots: u0 OutputTexture, u1 AccumTexture ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
        HeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        HeapDesc.NumDescriptors = 2;
        HeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        THROW_IF_FAILED(D3DDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&UavDescriptorHeap)));
        UavDescriptorHeap->SetName(L"Rita::UAVHeap");
    }

    const uint32_t UavDescriptorSize =
        D3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // --- Compute output texture (u0) — display-format, blitted to back buffer ---
    {
        D3D12_HEAP_PROPERTIES HeapProps = {};
        HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC TexDesc = {};
        TexDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        TexDesc.Width            = DisplayWidth;
        TexDesc.Height           = DisplayHeight;
        TexDesc.DepthOrArraySize = 1;
        TexDesc.MipLevels        = 1;
        TexDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        TexDesc.SampleDesc.Count = 1;
        TexDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        THROW_IF_FAILED(D3DDevice->CreateCommittedResource(
            &HeapProps, D3D12_HEAP_FLAG_NONE, &TexDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&ComputeOutputTexture)));
        ComputeOutputTexture->SetName(L"Rita::ComputeOutputTexture");

        D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc = {};
        UavDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        UavDesc.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
        UavDesc.Texture2D.MipSlice = 0;
        D3DDevice->CreateUnorderedAccessView(
            ComputeOutputTexture.Get(), nullptr, &UavDesc,
            UavDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // --- Accumulation texture (u1) — full float precision for the running average ---
    {
        D3D12_HEAP_PROPERTIES HeapProps = {};
        HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC TexDesc = {};
        TexDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        TexDesc.Width            = DisplayWidth;
        TexDesc.Height           = DisplayHeight;
        TexDesc.DepthOrArraySize = 1;
        TexDesc.MipLevels        = 1;
        TexDesc.Format           = DXGI_FORMAT_R32G32B32A32_FLOAT;
        TexDesc.SampleDesc.Count = 1;
        TexDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        THROW_IF_FAILED(D3DDevice->CreateCommittedResource(
            &HeapProps, D3D12_HEAP_FLAG_NONE, &TexDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&AccumTexture)));
        AccumTexture->SetName(L"Rita::AccumTexture");

        D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc = {};
        UavDesc.Format             = DXGI_FORMAT_R32G32B32A32_FLOAT;
        UavDesc.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
        UavDesc.Texture2D.MipSlice = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE AccumHandle =
            UavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        AccumHandle.ptr += UavDescriptorSize; // slot 1
        D3DDevice->CreateUnorderedAccessView(
            AccumTexture.Get(), nullptr, &UavDesc, AccumHandle);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void RtD3DApp::Shutdown()
{
    GfxCommands.FlushGPU();
    SampleScene.Shutdown();
    GfxCommands.Shutdown();
}

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------

int RtD3DApp::Run()
{
    using Clock    = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double>;

    MSG Msg = {};
    while (true)
    {
        const auto FrameStart = Clock::now();

        while (PeekMessageW(&Msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
            if (Msg.message == WM_QUIT)
            {
                return static_cast<int>(Msg.wParam);
            }
        }

        Render();

        // Sleep off any remaining frame budget (skip if TargetMaxFps == 0)
        if (TargetMaxFps > 0)
        {
            const Duration TargetFrameTime(1.0 / TargetMaxFps);
            const Duration Elapsed = Clock::now() - FrameStart;
            const Duration SleepTime = TargetFrameTime - Elapsed;
            if (SleepTime > Duration(0))
            {
                std::this_thread::sleep_for(SleepTime);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Compute dispatch
// ---------------------------------------------------------------------------

void RtD3DApp::DispatchCompute(ID3D12GraphicsCommandList* InCmd)
{
    InCmd->SetComputeRootSignature(ComputeRootSignature.Get());
    InCmd->SetPipelineState(ComputePipelineState.Get());

    // [0] UAV output texture
    ID3D12DescriptorHeap* Heaps[] = { UavDescriptorHeap.Get() };
    InCmd->SetDescriptorHeaps(1, Heaps);
    InCmd->SetComputeRootDescriptorTable(0, UavDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    // [1] Triangle buffer
    InCmd->SetComputeRootShaderResourceView(1, SampleScene.GetTriangleBufferGPUAddress());

    // [2] BVH node buffer
    InCmd->SetComputeRootShaderResourceView(2, SampleScene.GetBVHNodeBufferGPUAddress());

    // [3] Material buffer
    InCmd->SetComputeRootShaderResourceView(3, SampleScene.GetMaterialBufferGPUAddress());

    // [4] Root constants: FrameIndex + LightCount
    const uint32_t RootConsts[2] = { FrameAccumCount, SampleScene.GetLightCount() };
    InCmd->SetComputeRoot32BitConstants(4, 2, RootConsts, 0);

    // [5] Light buffer
    InCmd->SetComputeRootShaderResourceView(5, SampleScene.GetLightBufferGPUAddress());

    const uint32_t GroupsX = (DisplayWidth  + 7) / 8;
    const uint32_t GroupsY = (DisplayHeight + 7) / 8;
    InCmd->Dispatch(GroupsX, GroupsY, 1);

    ++FrameAccumCount;
}

// ---------------------------------------------------------------------------
// Blit compute output �� back buffer
// ---------------------------------------------------------------------------

void RtD3DApp::BlitComputeOutput(ID3D12GraphicsCommandList* InCmd)
{
    D3D12_RESOURCE_BARRIER Barriers[2] = {};

    Barriers[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barriers[0].Transition.pResource   = ComputeOutputTexture.Get();
    Barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    Barriers[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barriers[1].Transition.pResource   = BackBufferTargets[CurrentFrameIndex].Get();
    Barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    Barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    Barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    InCmd->ResourceBarrier(2, Barriers);
    InCmd->CopyResource(BackBufferTargets[CurrentFrameIndex].Get(), ComputeOutputTexture.Get());

    Barriers[0].Transition.pResource   = BackBufferTargets[CurrentFrameIndex].Get();
    Barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    Barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;

    Barriers[1].Transition.pResource   = ComputeOutputTexture.Get();
    Barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    InCmd->ResourceBarrier(2, Barriers);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

D3D12_CPU_DESCRIPTOR_HANDLE RtD3DApp::GetCurrentRTV() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE Handle = RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    Handle.ptr += static_cast<SIZE_T>(CurrentFrameIndex) * RtvDescriptorIncrementSize;
    return Handle;
}

void RtD3DApp::Render()
{
    GfxCommands.BeginFrame(CurrentFrameIndex);
    auto* Cmd = GfxCommands.GetCmdList();

    DispatchCompute(Cmd);
    BlitComputeOutput(Cmd);

    GfxCommands.EndFrame(CurrentFrameIndex);

    DXGISwapChain->Present(1, 0);
    CurrentFrameIndex = DXGISwapChain->GetCurrentBackBufferIndex();
}
