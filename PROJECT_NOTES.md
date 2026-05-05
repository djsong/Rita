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
| 7 | Temporal accumulation — AccumTexture (R32G32B32A32), FrameIndex root constant, running average blend | ✅ Done |
| 7b | Frame rate cap — TargetMaxFps member, sleep_for remaining budget in Run() | ✅ Done |
| 7c | Brace convention — all if/for/while bodies braced throughout codebase | ✅ Done |
| 8 | Next event estimation — TraceShadow() + SampleLightPoint(), direct contribution at every bounce | ✅ Done |
| 8b | RtLight GPU buffer — light data moved from shader constants to StructuredBuffer<RtLight>, injected from RtSampleScene | ✅ Done |
| 8c | AddLight refactor — single AddLight(V0,V1,V2,V3,Normal,Emissive) call registers both geometry and light buffer entry; MatLight and duplicate AddQuad removed from BuildCornellBox | ✅ Done |
| 9 | Two-level BVH (BLAS + TLAS) — per-mesh local BVH + instance transforms | 🔲 Planned |

---

## Context Snapshot (paste this at the start of a new Claude session)

> We are building a **D3D12 compute shader GPU raytracer** (study project).  
> No RTX APIs — must run on non-RTX hardware. Windows only, HLSL.  
> Sample scene is a **Cornell Box hardcoded in C++** (no asset import pipeline).  
> Local workspace: `D:\PRGStudy\Rita`  
> Current milestone: **Milestone 8c complete — AddLight refactor**  
> Last thing completed: Light data fully refactored. RtLight GPU buffer (StructuredBuffer<RtLight> at t3) carries Corner/EdgeU/EdgeV/Normal/Emissive/Area per light, injected from RtSampleScene. AddLight(V0,V1,V2,V3,Normal,Emissive) now registers both visible geometry (via internal AddQuad) and the area light entry in one call — the separate MatLight material and duplicate AddQuad in BuildCornellBox have been removed. NEE toggle: #define RITA_RAYGEN_NEE 0/1. Code style: Rt prefix on classes, Unreal-style PascalCase members, In prefix on parameters, braces on all control flow bodies.  
> Next up: Milestone 9 — Two-level BVH (BLAS + TLAS). Per-mesh BVH built in local space (BLAS), top-level BVH over instances with 4×4 transforms (TLAS). Shader traversal becomes two-phase: test TLAS AABBs, transform ray into instance local space, traverse BLAS.

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
