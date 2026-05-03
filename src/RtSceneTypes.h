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
