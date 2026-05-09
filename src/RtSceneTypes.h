#pragma once

#include <cstdint>

// CPU-side triangle layout — must exactly mirror RtTriangle in RayGen.hlsl.
// float[3] arrays are used (not a custom vec3 type) to guarantee no compiler padding.
struct RtTriangle
{
    float    V0[3];
    float    V1[3];
    float    V2[3];
    float    Normal[3];
    uint32_t MaterialIndex; // index into the material buffer
};
static_assert(sizeof(RtTriangle) == 52, "RtTriangle size mismatch between CPU and GPU");

// CPU-side material layout — must exactly mirror RtMaterial in RayGen.hlsl.
struct RtMaterial
{
    float Albedo[3];
    float Emissive[3];
};
static_assert(sizeof(RtMaterial) == 24, "RtMaterial size mismatch between CPU and GPU");

// Quad area light — must exactly mirror RtLight in RayGen.hlsl.
// A random point on the light is: Corner + U*EdgeU + V*EdgeV, U/V ∈ [0,1].
// Area is precomputed on the CPU as |EdgeU × EdgeV| so the shader doesn't have to.
struct RtLight
{
    float Corner[3];   // one corner of the quad
    float EdgeU[3];    // edge vector along one side
    float EdgeV[3];    // edge vector along the other side
    float Normal[3];   // outward normal (pointing toward the scene)
    float Emissive[3]; // emitted radiance
    float Area;        // |EdgeU × EdgeV|
};
static_assert(sizeof(RtLight) == 64, "RtLight size mismatch between CPU and GPU");

// BVH node — must exactly mirror RtBVHNode in RayGen.hlsl.
// TriangleCount == 0 → internal node: LeftOrFirst is the left child index (right = left+1).
// TriangleCount  > 0 → leaf node:     LeftOrFirst is the first triangle index.
struct RtBVHNode
{
    float    Min[3];         // AABB min
    uint32_t LeftOrFirst;    // see above
    float    Max[3];         // AABB max
    uint32_t TriangleCount;  // 0 = internal, >0 = leaf
};
static_assert(sizeof(RtBVHNode) == 32, "RtBVHNode size mismatch between CPU and GPU");

// TLAS node — must exactly mirror RtTLASNode in RayGen.hlsl.
// Identical memory layout to RtBVHNode; leaves index into the instance list instead of triangles.
// InstanceCount == 0 → internal node: LeftOrFirst is the left child index (right = left+1).
// InstanceCount  > 0 → leaf node:     LeftOrFirst is the first instance index.
struct RtTLASNode
{
    float    Min[3];          // AABB min (world space)
    uint32_t LeftOrFirst;     // see above
    float    Max[3];          // AABB max (world space)
    uint32_t InstanceCount;   // 0 = internal, >0 = leaf
};
static_assert(sizeof(RtTLASNode) == 32, "RtTLASNode size mismatch between CPU and GPU");

// Instance entry — must exactly mirror RtInstance in RayGen.hlsl.
// Transforms are 3×4 row-major matrices using the column-vector convention (matching DXR):
//   world_pos = Transform    * float4(local_pos, 1)
//   local_pos = InvTransform * float4(world_pos, 1)
// Assumption: instances use only rotation + translation (no non-uniform scale).
// This guarantees T is preserved when crossing spaces, and normals need only
// the upper 3×3 of Transform (transpose of inverse = forward rotation for rigid bodies).
struct RtInstance
{
    float    Transform[12];    // 3×4 row-major, local→world: used to bring normals to world space
    float    InvTransform[12]; // 3×4 row-major, world→local: used to transform rays into local space
    uint32_t BLASRootNode;     // root node index in the shared BVH node buffer
    uint32_t TriangleOffset;   // first triangle index in the shared triangle buffer
};
static_assert(sizeof(RtInstance) == 104, "RtInstance size mismatch between CPU and GPU");
