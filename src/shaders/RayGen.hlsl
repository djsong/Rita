// ---------------------------------------------------------------------------
// Scene data — must mirror RtTriangle / RtBVHNode / RtMaterial in RtSceneTypes.h
// ---------------------------------------------------------------------------
struct RtTriangle
{
    float3 V0, V1, V2;
    float3 Normal;
    uint   MaterialIndex;
};

struct RtBVHNode
{
    float3 BoundsMin;
    uint   LeftOrFirst;   // internal: left child (right = left+1); leaf: first triangle index
    float3 BoundsMax;
    uint   TriangleCount; // 0 = internal node, >0 = leaf
};

struct RtMaterial
{
    float3 Albedo;
    float3 Emissive;
};

// Must mirror RtLight in RtSceneTypes.h
struct RtLight
{
    float3 Corner;   // one corner of the quad
    float3 EdgeU;    // edge vector along one side
    float3 EdgeV;    // edge vector along the other side
    float3 Normal;   // outward normal (pointing toward the scene)
    float3 Emissive; // emitted radiance
    float  Area;     // |EdgeU × EdgeV|, precomputed on CPU
};

// ---------------------------------------------------------------------------
// Resource bindings
// ---------------------------------------------------------------------------
RWTexture2D<float4>          OutputTexture  : register(u0); // display output (gamma corrected)
RWTexture2D<float4>          AccumTexture   : register(u1); // full-float running average
StructuredBuffer<RtTriangle> SceneTriangles : register(t0);
StructuredBuffer<RtBVHNode>  BVHNodes       : register(t1);
StructuredBuffer<RtMaterial> Materials      : register(t2);
StructuredBuffer<RtLight>    Lights         : register(t3);

cbuffer RootConstants : register(b0)
{
    uint FrameIndex;  // total frames accumulated — used to weight the running average
    uint LightCount;  // number of entries in the Lights buffer
}

// ---------------------------------------------------------------------------
// Feature switches — recompile to toggle
// ---------------------------------------------------------------------------
// 0 = pure unidirectional path tracing (baseline)
// 1 = next event estimation: explicit shadow ray to the light at every bounce
#define RITA_RAYGEN_NEE 1

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const float3 CameraPos   = float3(0.0f, 0.0f, -3.0f);
static const float  VerticalFov = 60.0f;

static const float PI         = 3.14159265f;
static const int   MaxBounces = 8;
static const int   NumSamples = 1;        // 1 sample/frame — temporal accumulation does the averaging
static const float RayEpsilon = 1e-4f;

// ===========================================================================
// [Utilities] PRNG and sampling helpers
// ===========================================================================

// PCG hash — fast, high-quality PRNG suitable for shaders.
// Advances InOutState and returns a uniform float in [0, 1).
uint PCGHash(uint InState)
{
    uint State = InState * 747796405u + 2891336453u;
    uint Word  = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float RandFloat(inout uint InOutState)
{
    InOutState = PCGHash(InOutState);
    return float(InOutState) * (1.0f / 4294967296.0f); // [0, 1)
}

// Cosine-weighted hemisphere sample aligned to InNormal.
// PDF = cos(theta) / PI, which cancels with the Lambertian BRDF (albedo/PI),
// so the caller only needs to multiply throughput by albedo — no extra cos factor.
float3 CosineHemisphere(float3 InNormal, inout uint InOutRng)
{
    float U1 = RandFloat(InOutRng);
    float U2 = RandFloat(InOutRng);

    // Polar coords in tangent space (Z = up, aligned with normal after rotation)
    float SinTheta = sqrt(1.0f - U2);
    float CosTheta = sqrt(U2);
    float Phi      = 2.0f * PI * U1;

    float3 LocalDir = float3(cos(Phi) * SinTheta, sin(Phi) * SinTheta, CosTheta);

    // Build orthonormal basis (tangent frame) aligned to InNormal
    float3 Up      = abs(InNormal.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 Tangent = normalize(cross(Up, InNormal));
    float3 Binorm  = cross(InNormal, Tangent);

    return normalize(LocalDir.x * Tangent + LocalDir.y * Binorm + LocalDir.z * InNormal);
}

// Returns a uniformly random point on a quad light.
// Parametric form: Corner + U*EdgeU + V*EdgeV, U/V in [0, 1].
float3 SampleLightPoint(RtLight InLight, inout uint InOutRng)
{
    return InLight.Corner
         + RandFloat(InOutRng) * InLight.EdgeU
         + RandFloat(InOutRng) * InLight.EdgeV;
}

// ===========================================================================
// [Intersection] Ray-geometry tests and BVH traversal
//
//   RayTriangleIntersect — Möller–Trumbore single-triangle test
//   RayAABBIntersect     — slab method AABB test
//   TraceRay             — full BVH traversal; finds the nearest triangle hit
//   TraceShadow          — shadow BVH traversal; returns true if path is occluded
//                          (equivalent to a shadow ray with any-hit that skips emissives)
// ===========================================================================

// Möller–Trumbore ray-triangle intersection
bool RayTriangleIntersect(float3 InRayOrigin, float3 InRayDir,
                          float3 V0, float3 V1, float3 V2,
                          out float OutT)
{
    const float EPSILON = 1e-6f;
    float3 E1  = V1 - V0;
    float3 E2  = V2 - V0;
    float3 H   = cross(InRayDir, E2);
    float  Det = dot(E1, H);

    if (abs(Det) < EPSILON) { OutT = -1.0f; return false; }

    float  InvDet = 1.0f / Det;
    float3 S      = InRayOrigin - V0;
    float  U      = InvDet * dot(S, H);
    if (U < 0.0f || U > 1.0f) { OutT = -1.0f; return false; }

    float3 Q = cross(S, E1);
    float  V = InvDet * dot(InRayDir, Q);
    if (V < 0.0f || U + V > 1.0f) { OutT = -1.0f; return false; }

    OutT = InvDet * dot(E2, Q);
    return OutT > EPSILON;
}

// Slab-based ray-AABB intersection — returns true if the ray hits the box
// closer than InTMax. InvRayDir = 1/RayDir, precomputed by the caller.
bool RayAABBIntersect(float3 InRayOrigin, float3 InvRayDir,
                      float3 BoxMin, float3 BoxMax, float InTMax)
{
    float3 T0    = (BoxMin - InRayOrigin) * InvRayDir;
    float3 T1    = (BoxMax - InRayOrigin) * InvRayDir;
    float3 TMinV = min(T0, T1);
    float3 TMaxV = max(T0, T1);
    float TEnter = max(max(TMinV.x, TMinV.y), TMinV.z);
    float TExit  = min(min(TMaxV.x, TMaxV.y), TMaxV.z);
    return TExit >= max(TEnter, 0.0f) && TEnter < InTMax;
}

// BVH traversal — finds the nearest triangle hit along the ray.
// Returns true if any triangle was hit; OutT and OutTriIndex are set on hit.
bool TraceRay(float3 InRayOrigin, float3 InRayDir,
              out float OutT, out int OutTriIndex)
{
    float3 InvRayDir = 1.0f / InRayDir;
    OutT        = 1e30f;
    OutTriIndex = -1;

    int Stack[32];
    int StackTop = 0;
    Stack[StackTop++] = 0; // root node

    while (StackTop > 0)
    {
        int       NodeIdx = Stack[--StackTop];
        RtBVHNode Node    = BVHNodes[NodeIdx];

        if (!RayAABBIntersect(InRayOrigin, InvRayDir, Node.BoundsMin, Node.BoundsMax, OutT))
        {
            continue;
        }

        if (Node.TriangleCount > 0) // leaf — test each triangle
        {
            for (uint i = Node.LeftOrFirst; i < Node.LeftOrFirst + Node.TriangleCount; ++i)
            {
                float T = 0.0f;
                if (RayTriangleIntersect(InRayOrigin, InRayDir,
                        SceneTriangles[i].V0, SceneTriangles[i].V1, SceneTriangles[i].V2, T)
                    && T < OutT)
                {
                    OutT        = T;
                    OutTriIndex = (int)i;
                }
            }
        }
        else // internal — push children
        {
            Stack[StackTop++] = (int)Node.LeftOrFirst;
            Stack[StackTop++] = (int)Node.LeftOrFirst + 1;
        }
    }

    return OutTriIndex >= 0;
}

// Shadow ray traversal — returns true if opaque geometry occludes the path before InMaxT.
// Emissive (light) triangles are skipped: they are the target, not potential occluders.
// Equivalent to a shadow TraceRay with an any-hit shader that rejects emissive surfaces.
bool TraceShadow(float3 InRayOrigin, float3 InRayDir, float InMaxT)
{
    float3 InvRayDir = 1.0f / InRayDir;

    int Stack[32];
    int StackTop = 0;
    Stack[StackTop++] = 0;

    while (StackTop > 0)
    {
        int       NodeIdx = Stack[--StackTop];
        RtBVHNode Node    = BVHNodes[NodeIdx];

        if (!RayAABBIntersect(InRayOrigin, InvRayDir, Node.BoundsMin, Node.BoundsMax, InMaxT))
        {
            continue;
        }

        if (Node.TriangleCount > 0)
        {
            for (uint i = Node.LeftOrFirst; i < Node.LeftOrFirst + Node.TriangleCount; ++i)
            {
                // Skip light triangles — they are the shadow ray target, not occluders
                if (any(Materials[SceneTriangles[i].MaterialIndex].Emissive > 0.0f))
                {
                    continue;
                }

                float T = 0.0f;
                if (RayTriangleIntersect(InRayOrigin, InRayDir,
                        SceneTriangles[i].V0, SceneTriangles[i].V1, SceneTriangles[i].V2, T)
                    && T < InMaxT)
                {
                    return true; // occluded
                }
            }
        }
        else
        {
            Stack[StackTop++] = (int)Node.LeftOrFirst;
            Stack[StackTop++] = (int)Node.LeftOrFirst + 1;
        }
    }

    return false; // unoccluded — light is visible
}

// ===========================================================================
// [Miss Shader] Called when a ray escapes the scene without hitting geometry
// ===========================================================================

// Sky gradient: white horizon → blue zenith
float3 Miss(float3 InRayDir)
{
    float T = 0.5f * (InRayDir.y + 1.0f);
    return lerp(float3(1.0f, 1.0f, 1.0f), float3(0.3f, 0.5f, 1.0f), T);
}

// ===========================================================================
// [Closest Hit Shader] Evaluates shading at a hit point
//
// Computes the direct light contribution for this bounce and sets up the
// next scatter ray. Returns a HitPayload describing all state changes so
// PathTrace's bounce loop stays free of shading logic.
// ===========================================================================

struct HitPayload
{
    float3 Radiance;      // direct light contribution for this bounce (pre-multiplied by throughput)
    float3 Throughput;    // updated throughput to carry forward to the next bounce
    float3 NextRayOrigin; // origin for the next bounce ray
    float3 NextRayDir;    // direction for the next bounce ray
    bool   bTerminate;    // true if the path should end here
};

HitPayload ClosestHit(RtTriangle InTri, RtMaterial InMat, float3 InHitPoint,
                      float3 InThroughput, int InBounce, inout uint InOutRng)
{
    HitPayload Out = (HitPayload)0;

#if RITA_RAYGEN_NEE
    // Direct camera rays that hit the light panel still show it as bright.
    // Indirect bounces skip the emissive term — the shadow ray below already
    // accounts for direct illumination, so adding it again would double-count.
    if (InBounce == 0)
    {
        Out.Radiance = InThroughput * InMat.Emissive;
    }

    // Emissive surfaces do not scatter further
    if (any(InMat.Emissive > 0.0f))
    {
        Out.bTerminate = true;
        return Out;
    }

    // Explicit shadow ray to a randomly chosen area light
    {
        // Pick one light uniformly at random; LightCount corrects for the 1/LightCount
        // probability of choosing it (multiple importance sampling, uniform light PDF).
        uint    LightIdx = min(uint(RandFloat(InOutRng) * float(LightCount)), LightCount - 1);
        RtLight Light    = Lights[LightIdx];

        float3 ShadowOrigin = InHitPoint + InTri.Normal * RayEpsilon;
        float3 LightPoint   = SampleLightPoint(Light, InOutRng);
        float3 ToLight      = LightPoint - ShadowOrigin;
        float  LightDist    = length(ToLight);
        float3 LightDir     = ToLight / LightDist;

        float CosAtSurface = dot(InTri.Normal,   LightDir);
        float CosAtLight   = dot(Light.Normal,  -LightDir);

        if (CosAtSurface > 0.0f && CosAtLight > 0.0f)
        {
            if (!TraceShadow(ShadowOrigin, LightDir, LightDist))
            {
                // Estimator: (albedo/PI) * L_e * cos_surface * cos_light * A / r²
                // Multiplied by LightCount: PDF of choosing this light was 1/LightCount.
                // 1/PI from the Lambertian BRDF stays — we sample in area measure here,
                // unlike the indirect bounce where it cancels with the cosine PDF.
                float GeometryFactor = CosAtSurface * CosAtLight / (LightDist * LightDist);
                Out.Radiance += InThroughput * InMat.Albedo * Light.Emissive
                              * GeometryFactor * Light.Area * float(LightCount) / PI;
            }
        }
    }
#else
    // Pure path tracing: accumulate emissive at this surface, then terminate if it was a light
    Out.Radiance = InThroughput * InMat.Emissive;
    if (any(InMat.Emissive > 0.0f))
    {
        Out.bTerminate = true;
        return Out;
    }
#endif

    // Diffuse scatter: multiply throughput by albedo.
    // cos(theta)/PI from the BRDF and PI/cos(theta) from the cosine PDF cancel exactly,
    // leaving just albedo here.
    Out.Throughput = InThroughput * InMat.Albedo;

    // Russian roulette: terminate low-contribution paths early
    if (max(max(Out.Throughput.r, Out.Throughput.g), Out.Throughput.b) < 0.01f)
    {
        Out.bTerminate = true;
        return Out;
    }

    // Spawn the next ray from the hit point, nudged along the normal to avoid self-intersection
    Out.NextRayOrigin = InHitPoint + InTri.Normal * RayEpsilon;
    Out.NextRayDir    = CosineHemisphere(InTri.Normal, InOutRng);
    return Out;
}

// ===========================================================================
// [Path Tracer] Orchestrates the bounce loop
//
// Drives the path from the primary ray through up to MaxBounces. Calls into
// TraceRay (intersection), Miss (escaped rays), and ClosestHit (hit shading)
// — mirroring the roles those shader stages play in a DXR pipeline.
// ===========================================================================

float3 PathTrace(float3 InRayOrigin, float3 InRayDir, inout uint InOutRng)
{
    float3 Throughput = float3(1, 1, 1); // energy carried by this path
    float3 Radiance   = float3(0, 0, 0); // accumulated light

    float3 RayOrigin = InRayOrigin;
    float3 RayDir    = InRayDir;

    for (int Bounce = 0; Bounce < MaxBounces; ++Bounce)
    {
        float T        = 0.0f;
        int   TriIndex = 0;
        
        if (TraceRay(RayOrigin, RayDir, T, TriIndex))
        {
            RtTriangle Tri = SceneTriangles[TriIndex];
            RtMaterial Mat = Materials[Tri.MaterialIndex];
            float3 HitPoint = RayOrigin + T * RayDir;

            HitPayload Payload = ClosestHit(Tri, Mat, HitPoint, Throughput, Bounce, InOutRng);
            Radiance += Payload.Radiance;

            if (Payload.bTerminate)
            {
                break;
            }

            Throughput = Payload.Throughput;
            RayOrigin = Payload.NextRayOrigin;
            RayDir = Payload.NextRayDir;
        }
        else
        {
            Radiance += Throughput * Miss(RayDir);
            break;
        }
    }

    return Radiance;
}

// ===========================================================================
// [Ray Generation] Entry point — one thread per pixel
//
// Generates the primary ray for each pixel, calls PathTrace, and blends the
// result into the temporal accumulation buffer.
// ===========================================================================

[numthreads(8, 8, 1)]
void CSMain(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width = 100, Height = 100;
    OutputTexture.GetDimensions(Width, Height);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    // Pixel center → NDC; flip Y so +Y is up in world space
    float2 NDC = float2(
         (float(DispatchThreadID.x) + 0.5f) / float(Width)  * 2.0f - 1.0f,
        -(float(DispatchThreadID.y) + 0.5f) / float(Height) * 2.0f + 1.0f);

    float AspectRatio = float(Width) / float(Height);
    float TanHalfFov  = tan(radians(VerticalFov * 0.5f));

    float3 PrimaryDir = normalize(float3(
        NDC.x * AspectRatio * TanHalfFov,
        NDC.y * TanHalfFov,
        1.0f));

    // Per-pixel RNG seed — include FrameIndex so every frame explores different directions
    uint RngState = PCGHash(DispatchThreadID.x * 1973u + DispatchThreadID.y * 9277u + FrameIndex * 26699u);

    // Trace one new sample this frame
    float3 NewSample = PathTrace(CameraPos, PrimaryDir, RngState);

    // Blend into the running average:
    //   AccumColor = (AccumColor * FrameIndex + NewSample) / (FrameIndex + 1)
    // On the very first frame (FrameIndex == 0) this reduces to just NewSample.
    float3 PrevAccum = AccumTexture[DispatchThreadID.xy].rgb;
    float3 NewAccum  = (PrevAccum * float(FrameIndex) + NewSample) / float(FrameIndex + 1);
    AccumTexture[DispatchThreadID.xy] = float4(NewAccum, 1.0f);

    // Gamma-correct the accumulated linear value for display
    float3 DisplayColor = sqrt(max(NewAccum, 0.0f));
    OutputTexture[DispatchThreadID.xy] = float4(DisplayColor, 1.0f);
}
