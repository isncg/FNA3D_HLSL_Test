Texture2D<float4> NormalTex     : register(t0);
SamplerState      NormalSampler : register(s0);
Texture2D<float>  DepthTex      : register(t1);
SamplerState      DepthSampler  : register(s1);

float4x4 Projection : register(c0);
float4   SSAOParams : register(c4);

#define SSAO_RADIUS    SSAOParams.x
#define SSAO_BIAS      SSAOParams.y
#define SSAO_INTENSITY SSAOParams.z

/* 16 hemisphere samples (tangent space, z = hemisphere up) */
static const float3 gSamples[16] =
{
    float3( 0.5381,  0.1856,  0.4319),
    float3( 0.1379,  0.7723,  0.4539),
    float3(-0.4292, -0.1142,  0.7212),
    float3( 0.4519, -0.3659,  0.6540),
    float3(-0.7832,  0.3897,  0.2581),
    float3( 0.1810, -0.4989,  0.4318),
    float3( 0.6597,  0.6147,  0.2391),
    float3(-0.2563,  0.8102,  0.3471),
    float3(-0.4152, -0.7027,  0.4473),
    float3( 0.8838, -0.1142,  0.3151),
    float3(-0.5258,  0.0894,  0.7721),
    float3( 0.2947,  0.2147,  0.8339),
    float3(-0.0853, -0.3145,  0.8671),
    float3(-0.6723, -0.5138,  0.5374),
    float3( 0.0658,  0.9215,  0.2756),
    float3( 0.7381, -0.4273,  0.5585),
};

/* Simple hash for random rotation per pixel */
static float hash2D(float2 uv)
{
    return frac(sin(dot(uv, float2(127.1, 311.7))) * 43758.5453);
}

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    /* Sample G-buffer */
    float3 encodedN = NormalTex.Sample(NormalSampler, input.UV).rgb;
    float  viewZ    = DepthTex.Sample(DepthSampler, input.UV);

    /* Skip sky / far plane pixels */
    if (viewZ <= 0.0 || viewZ >= 100.0)
        return float4(1.0, 1.0, 1.0, 1.0);

    /* Decode normal and reconstruct view-space position */
    float3 N = normalize(encodedN * 2.0 - 1.0);

    /* NDC from UV (UV.y=0 at top → ndc.y=1 at top) */
    float2 ndc = float2(input.UV.x * 2.0 - 1.0, 1.0 - input.UV.y * 2.0);

    float3 viewPos;
    viewPos.x = ndc.x * viewZ / Projection._11;
    viewPos.y = ndc.y * viewZ / Projection._22;
    viewPos.z = viewZ;

    /* Random rotation from hash */
    float r = hash2D(input.UV) * 6.2831853; /* 2*PI */
    float2 rot = float2(cos(r), sin(r));

    /* Build tangent-to-view transform */
    float3 T, B;
    if (abs(N.z) < 0.999)
    {
        T = normalize(cross(N, float3(0.0, 1.0, 0.0)));
    }
    else
    {
        T = normalize(cross(N, float3(1.0, 0.0, 0.0)));
    }
    B = cross(N, T);

    float occlusion = 0.0;
    float radius = SSAO_RADIUS;

    for (int i = 0; i < 16; i++)
    {
        /* Rotate hemisphere sample in tangent space */
        float3 sampleDir = gSamples[i];
        float3 sampleTan;
        sampleTan.x = sampleDir.x * rot.x + sampleDir.y * rot.y;
        sampleTan.y = sampleDir.x * -rot.y + sampleDir.y * rot.x;
        sampleTan.z = sampleDir.z;

        /* Transform to view space */
        float3 sampleView = sampleTan.x * T + sampleTan.y * B + sampleTan.z * N;

        /* Offset view-space position by sample */
        float3 samplePos = viewPos + sampleView * radius;

        /* Project to screen */
        float4 sampleClip;
        sampleClip.x = samplePos.x * Projection._11;
        sampleClip.y = samplePos.y * Projection._22;
        sampleClip.z = samplePos.z * Projection._33 + Projection._43;
        sampleClip.w = samplePos.z;

        float2 sampleUV;
        sampleUV.x = (sampleClip.x / sampleClip.w) * 0.5 + 0.5;
        sampleUV.y = (1.0 - sampleClip.y / sampleClip.w) * 0.5;

        /* Sample depth at projected position */
        float sampleDepth = DepthTex.Sample(DepthSampler, sampleUV);

        /* Reconstruct full 3D view-space position at the sample UV */
        float2 sampleNDC = float2(sampleUV.x * 2.0 - 1.0, 1.0 - sampleUV.y * 2.0);
        float3 sampleViewPos;
        sampleViewPos.x = sampleNDC.x * sampleDepth / Projection._11;
        sampleViewPos.y = sampleNDC.y * sampleDepth / Projection._22;
        sampleViewPos.z = sampleDepth;

        /* Range check: 3D distance between surface point and sample geometry */
        float rangeCheck = length(sampleViewPos - viewPos) < radius ? 1.0 : 0.0;

        /* Heights above the surface tangent plane (signed by normal direction) */
        float geomHeight  = dot(sampleViewPos - viewPos, N);
        float probeHeight = dot(samplePos - viewPos, N);

        /* Occlusion: geometry is between the surface plane and the probe */
        occlusion += (geomHeight > SSAO_BIAS && geomHeight < probeHeight ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion /= 16.0;
    float ao = 1.0 - occlusion * SSAO_INTENSITY;
    ao = saturate(ao);

    return float4(ao, ao, ao, 1.0);
}
