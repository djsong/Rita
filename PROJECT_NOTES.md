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
| 6 | Path tracing — diffuse bounces, emissive light, PCG PRNG, multi-sample, gamma | ✅ Done |
| 6b | Cornell Box boxes — tall & short box via AddBox() helper (rotated, 5-face quads) | ✅ Done |
| 7 | Temporal accumulation — ping-pong float accumulation texture, reset on change | 🔲 Next |
| 8 | Next event estimation — explicit shadow ray to light each bounce | 🔲 Planned |
| 9 | Two-level BVH (BLAS + TLAS) — per-mesh local BVH + instance transforms | 🔲 Planned |

---

## Context Snapshot (paste this at the start of a new Claude session)

> We are building a **D3D12 compute shader GPU raytracer** (study project).  
> No RTX APIs — must run on non-RTX hardware. Windows only, HLSL.  
> Sample scene is a **Cornell Box hardcoded in C++** (no asset import pipeline).  
> Local workspace: `D:\PRGStudy\Rita`  
> Current milestone: **Milestone 6 — Shading (diffuse, shadow rays, path tracing)**  
> Last thing completed: Full Cornell Box with path tracing. Diffuse path tracing (PCG PRNG, cosine-weighted hemisphere sampling, up to 8 bounces, N samples/pixel averaged, gamma 2.0 correction). Tall and short boxes added via AddBox() helper (Y-axis rotation, 5-quad faces, no bottom). Code style: Rt prefix on classes, Unreal-style PascalCase members, In prefix on parameters.
> Next up: Milestone 7 — Temporal accumulation. Keep a float accumulation texture across frames; blend each new frame in with weight 1/N. Requires a second UAV texture (AccumTexture) and a frame counter root constant. Reset accumulator when scene or camera changes.

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
