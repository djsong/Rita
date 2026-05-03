#include "RtD3DApp.h"

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nShowCmd)
{
    RtD3DApp App(hInstance, 1280, 720, TEXT("Rita - GPU Raytracer"));

    try
    {
        if (!App.Initialize())
        {
            MessageBox(nullptr, TEXT("Failed to initialize RtD3DApp."), TEXT("Error"), MB_OK | MB_ICONERROR);
            return -1;
        }

        return App.Run();
    }
    catch (const std::exception& Ex)
    {
        MessageBoxA(nullptr, Ex.what(), "Fatal Error", MB_OK | MB_ICONERROR);
        return -1;
    }
}
