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

**Scene contents (current):**
- Mesh 0 — Room: 5 quads (back wall, floor, ceiling, left wall red, right wall green) + ceiling area light; world space, identity transform
- Mesh 1 — Tall box: local-space AABB (0.30×0.30×1.20), 15° CW Y-rotation, placed right-back
- Mesh 2 — Short box: local-space AABB (0.30×0.30×0.60), 15° CCW Y-rotation, placed left-front
- Each mesh has its own BLAS; one RtInstance per mesh (1-to-1, to be decoupled in Milestone 10)

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
| 8d | Shader refactor — RayGen.hlsl reorganized into DXR-role sections: [Utilities], [Intersection], [Miss], [Closest Hit] (HitPayload struct + ClosestHit()), [Path Tracer], [Ray Generation]; SkyColor renamed to Miss | ✅ Done |
| 9a | BLAS/TLAS data structures — RtInstance (row_major float3x4 transforms, BLASRootNode, TriangleOffset) and RtTLASNode structs in RtSceneTypes.h + RayGen.hlsl with static_asserts | ✅ Done |
| 9b | Scene restructure — RtMesh (TriangleOffset, TriangleCount, BLASRootNode, Transform, InvTransform); BeginMesh/EndMesh helpers; BuildCornellBox splits into 3 meshes: room, tall box, short box | ✅ Done |
| 9c | Per-mesh BLAS build — BuildBLAS() loops over Meshes, builds per-mesh BVH into consecutive BVHNodes slots, populates Instances list; shader TraceRay/TraceShadow loop over instances using Inst.BLASRootNode; InstanceBuffer GPU resource added (t4, root slot [6], InstanceCount root constant) | ✅ Done |
| 9d | TLAS build — BuildTLAS() + SubdivideTLASNode() builds BVH over instance world-space AABBs (from BLAS root bounds); may reorder Instances array so InstanceBuffer uploaded after; TLASNodeBuffer GPU resource added (t5, root slot [7]); shader TraceRay/TraceShadow replaced with two-level TLAS→BLAS traversal using separate TLASStack[32] + BLASStack[32] | ✅ Done |
| 9e | Two-phase shader traversal — AddBox generates local-space AABB vertices + axis-aligned normals; FillBoxTransforms computes real Y-rotation + translation matrices; SubdivideTLASNode transforms BLAS root corners to world space for correct AABB/centroid; TraceRay/TraceShadow transform ray to local space per instance (mul InvTransform) before BLAS walk; PathTrace transforms hit normal to world space (mul upper 3×3 of Transform) before ClosestHit | ✅ Done |
| 10a | Instance/mesh decoupling — remove auto-instance generation from BuildBLAS(); add AddInstance(MeshIdx, T, InvT) so multiple instances can reference the same BLAS; BuildCornellBox registers instances explicitly | 🔲 Planned |
| 10b | AddSphere helper — triangulated UV sphere (stacks × slices); local space, centered at origin; new mesh type alongside AddBox | 🔲 Planned |
| 10c | Scene population — enrich Cornell Box interior using instancing (shared BLASes for identical-sized boxes) and new sphere meshes; vary materials | 🔲 Planned |

---

## Context Snapshot (paste this at the start of a new Claude session)

> We are building a **D3D12 compute shader GPU raytracer** (study project).  
> No RTX APIs — must run on non-RTX hardware. Windows only, HLSL.  
> Sample scene is a **Cornell Box hardcoded in C++** (no asset import pipeline).  
> Local workspace: `D:\PRGStudy\Rita`  
> Current milestone: **Milestone 9 fully complete — BLAS/TLAS two-level BVH with real transforms**  
> Last thing completed (9e): AddBox generates local-space AABB vertices (bottom at y=0, top at y=Height, centered at origin in XZ, axis-aligned normals). FillBoxTransforms() computes the real 3×4 row-major local→world [R|t] and world→local [R^T|-R^T·t] matrices for Y-rotation + translation. BuildCornellBox calls FillBoxTransforms then EndMesh(T,InvT) per box mesh. SubdivideTLASNode uses ExpandWorldAABBByTransformedCorners() (all 8 BLAS-root corners transformed to world space) for node AABB and partition centroid. TraceRay/TraceShadow transform each ray to local space (mul(InvTransform, float4(origin,1)) + mul((float3x3)InvTransform, dir)) before BLAS traversal — T invariant because rotation preserves direction length. TraceRay returns OutInstIndex. PathTrace transforms local normal to world space (normalize(mul((float3x3)Transform, n))) before ClosestHit.  
>  
> **Current architecture note:** BuildBLAS() auto-generates exactly one RtInstance per RtMesh — instances and meshes are currently 1-to-1 coupled. To reuse the same BLAS for multiple instances (true GPU instancing), this coupling needs to be broken (Milestone 10a).  
>  
> Next up: **Milestone 10 — Scene extension**  
> 10a: Decouple instances from meshes — remove auto-generation from BuildBLAS(), add AddInstance(MeshIdx, T, InvT), BuildCornellBox registers instances explicitly.  
> 10b: AddSphere helper — triangulated UV sphere in local space (stacks × slices).  
> 10c: Enrich scene — more boxes via instancing (shared BLAS), sphere(s), varied materials.

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
