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
    uint   LeftOrFirst;  // internal: left child (right = left+1); leaf: first triangle index
    float3 BoundsMax;
    uint   TriangleCount; // 0 = internal node, >0 = leaf
};

struct RtMaterial
{
    float3 Albedo;
    float3 Emissive;
};

RWTexture2D<float4>          OutputTexture  : register(u0);
StructuredBuffer<RtTriangle> SceneTriangles : register(t0);
StructuredBuffer<RtBVHNode>  BVHNodes       : register(t1);
StructuredBuffer<RtMaterial> Materials      : register(t2);

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
static const float3 CameraPos   = float3(0.0f, 0.0f, -3.0f);
static const float  VerticalFov = 60.0f;

// ---------------------------------------------------------------------------
// Path tracing constants
// ---------------------------------------------------------------------------
static const float PI         = 3.14159265f;
static const int   MaxBounces = 8;
static const int   NumSamples = 64; // Cleaner result with more sample, but you cannot put that much.
static const float RayEpsilon = 1e-4f;

// ---------------------------------------------------------------------------
// Sky gradient: white horizon → blue zenith
// ---------------------------------------------------------------------------
float3 SkyColor(float3 RayDir)
{
    float T = 0.5f * (RayDir.y + 1.0f);
    return lerp(float3(1.0f, 1.0f, 1.0f), float3(0.3f, 0.5f, 1.0f), T);
}

// ---------------------------------------------------------------------------
// PCG hash — fast, high-quality PRNG suitable for shaders.
// Advances InOutState and returns a uniform float in [0, 1).
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Cosine-weighted hemisphere sample aligned to InNormal.
// PDF = cos(theta) / PI, which cancels with the Lambertian BRDF (albedo/PI),
// so the caller only needs to multiply throughput by albedo — no extra cos factor.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Möller–Trumbore ray-triangle intersection
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Slab-based ray-AABB intersection — returns true if the ray hits the box
// closer than InTMax. InvRayDir = 1/RayDir, precomputed by the caller.
// ---------------------------------------------------------------------------
bool RayAABBIntersect(float3 InRayOrigin, float3 InvRayDir,
                      float3 BoxMin, float3 BoxMax, float InTMax)
{
    float3 T0 = (BoxMin - InRayOrigin) * InvRayDir;
    float3 T1 = (BoxMax - InRayOrigin) * InvRayDir;
    float3 TMinV = min(T0, T1);
    float3 TMaxV = max(T0, T1);
    float TEnter = max(max(TMinV.x, TMinV.y), TMinV.z);
    float TExit  = min(min(TMaxV.x, TMaxV.y), TMaxV.z);
    return TExit >= max(TEnter, 0.0f) && TEnter < InTMax;
}

// ---------------------------------------------------------------------------
// BVH traversal — finds the nearest triangle hit along the ray.
// Returns true if any triangle was hit; OutT and OutTriIndex are set on hit.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Path trace a single ray — returns the radiance arriving along InRayDir.
// Follows the path for up to MaxBounces diffuse bounces.
// ---------------------------------------------------------------------------
float3 PathTrace(float3 InRayOrigin, float3 InRayDir, inout uint InOutRng)
{
    float3 Throughput = float3(1, 1, 1); // energy carried by this path
    float3 Radiance   = float3(0, 0, 0); // accumulated light

    float3 RayOrigin = InRayOrigin;
    float3 RayDir    = InRayDir;

    for (int Bounce = 0; Bounce < MaxBounces; ++Bounce)
    {
        float T = 0.0f;
        int   TriIndex = 0;
        if (!TraceRay(RayOrigin, RayDir, T, TriIndex))
        {
            // Ray escaped — add sky contribution and end the path
            Radiance += Throughput * SkyColor(RayDir);
            break;
        }

        RtTriangle Tri = SceneTriangles[TriIndex];
        RtMaterial Mat = Materials[Tri.MaterialIndex];

        // Add emitted light at this surface
        Radiance += Throughput * Mat.Emissive;

        // Emissive surfaces do not scatter — terminate the path
        if (any(Mat.Emissive > 0.0f))
        {
            break;
        }

        // Diffuse scatter: multiply throughput by albedo.
        // The cos(theta)/PI from the BRDF and the PI/cos(theta) from the PDF cancel
        // exactly with cosine-weighted sampling, leaving just albedo here.
        Throughput *= Mat.Albedo;

        // Cheap Russian roulette: kill low-contribution paths early
        if (max(max(Throughput.r, Throughput.g), Throughput.b) < 0.01f)
        {
            break;
        }

        // Spawn the next ray from the hit point, nudged along the normal
        float3 HitPoint = RayOrigin + T * RayDir;
        RayOrigin = HitPoint + Tri.Normal * RayEpsilon;
        RayDir    = CosineHemisphere(Tri.Normal, InOutRng);
    }

    return Radiance;
}

// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void CSMain(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width = 100, Height = 100;
    OutputTexture.GetDimensions(Width, Height);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    // Pixel center → NDC, flip Y so +Y is up in world space
    float2 NDC = float2(
         (float(DispatchThreadID.x) + 0.5f) / float(Width)  * 2.0f - 1.0f,
        -(float(DispatchThreadID.y) + 0.5f) / float(Height) * 2.0f + 1.0f);

    float AspectRatio = float(Width) / float(Height);
    float TanHalfFov  = tan(radians(VerticalFov * 0.5f));

    float3 PrimaryDir = normalize(float3(
        NDC.x * AspectRatio * TanHalfFov,
        NDC.y * TanHalfFov,
        1.0f));

    // Per-pixel RNG seed, unique across the screen
    uint RngState = (DispatchThreadID.x * 1973u + DispatchThreadID.y * 9277u) | 1u;

    // Accumulate NumSamples independent paths and average
    float3 AccumulatedColor = float3(0, 0, 0);
    for (int Sample = 0; Sample < NumSamples; ++Sample)
    {
        // Advance seed so each sample gets a fresh, uncorrelated sequence
        RngState = PCGHash(RngState + uint(Sample) * 26699u);
        AccumulatedColor += PathTrace(CameraPos, PrimaryDir, RngState);
    }

    float3 Color = AccumulatedColor / float(NumSamples);

    // Gamma correction: convert linear light values to display-space
    // (gamma 2.0 approximation — sqrt is close enough for a study renderer)
    Color = sqrt(max(Color, 0.0f));

    OutputTexture[DispatchThreadID.xy] = float4(Color, 1.0f);
}
