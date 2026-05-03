# D3D12 GPU Raytracer — Project Notes

## Project Summary

A study-purpose GPU raytracer built from scratch using **D3D12 + HLSL compute shaders**.  
Core ray tracing computation runs entirely on the GPU, with **no RTX/hardware ray tracing APIs** (no DXR, no VK_KHR_ray_tracing, no OptiX).  
Target platform: **Windows only**.

---

## Key Decisions

| Topic | Decision |
|---|---|
| Graphics API | D3D12 only (no Vulkan, no OpenGL) |
| Shader language | HLSL |
| GPU execution | Compute shader dispatched per pixel/tile |
| Ray tracing APIs | None — pure compute, works on non-RTX hardware |
| Sample content | Cornell Box, hardcoded in C++ (no file import needed) |
| Project purpose | Study / learning |

---

## Cornell Box — Hardcoded Geometry & Materials

No file I/O for assets. All geometry and material data defined as C++ structs and uploaded to GPU buffers at startup.

**Scene contents:**
- 5 quads: back wall, floor, ceiling, left wall (red), right wall (green)
- 1 area light: ceiling panel (emissive)
- 2 boxes inside the room (tall & short) — can defer to later milestone

**Material properties per surface:**
- Albedo (diffuse color RGB)
- Emissive color (for light source)
- Extendable later: roughness, reflectance, etc.

---

## Development Milestones

| # | Milestone | Status |
|---|---|---|
| 1 | D3D12 boilerplate — window, swap chain, command queue | ✅ Done |
| 2 | Compute shader dispatch → writes to UAV texture → blit to screen | ✅ Done |
| 3 | Ray–sphere intersection (GPU sanity check) | ✅ Done |
| 4 | Cornell Box geometry + ray–triangle intersection | ✅ Done |
| 5 | BVH acceleration structure | ✅ Done |
| 5b | Material refactor — separate RtMaterial buffer, MaterialIndex per triangle | ✅ Done |
| 6 | Shading — diffuse, shadow rays, path tracing | 🔧 In progress |

---

## Context Snapshot (paste this at the start of a new Claude session)

> We are building a **D3D12 compute shader GPU raytracer** (study project).  
> No RTX APIs — must run on non-RTX hardware. Windows only, HLSL.  
> Sample scene is a **Cornell Box hardcoded in C++** (no asset import pipeline).  
> Local workspace: `D:\PRGStudy\Rita`  
> Current milestone: **Milestone 6 — Shading (diffuse, shadow rays, path tracing)**  
> Last thing completed: Material refactor — Albedo/Emissive removed from RtTriangle; separate RtMaterial GPU buffer (t2) with MaterialIndex per triangle. 4 materials: white, red, green, light. Root signature expanded to 4 params. Image confirmed identical. Code style: Rt prefix on classes, Unreal-style PascalCase members, In prefix on parameters.

---

## Local Workspace

| Item | Path |
|---|---|
| Project root | `D:\PRGStudy\Rita` |
| Project notes | `D:\PRGStudy\Rita\PROJECT_NOTES.md` |
| Key source files | `src\RtD3DApp.h/cpp`, `src\RtD3DCommands.h/cpp`, `src\RtScene.h/cpp`, `src\RtSceneTypes.h`, `src\Main.cpp` |
| Shader | `src\shaders\RayGen.hlsl` → compiled to `bin\Debug\shaders\RayGen.cso` |

---

## Notes & References

- Cornell Box reference: https://www.graphics.cornell.edu/online/box/data.html
- D3D12 docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-graphics
- HLSL compute shader docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl
