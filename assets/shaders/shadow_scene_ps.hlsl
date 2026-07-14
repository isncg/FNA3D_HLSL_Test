// Shadow scene pixel shader -- NdotL diffuse + 3x3 PCF shadow lookup.

Texture2D<float> ShadowMap     : register(t0);
SamplerState     ShadowSampler : register(s0);

float3 DirToLight   : register(c8);   /* world-space unit vector toward the light */
float4 ShadowParams : register(c9);   /* x=depthBias, y=texelSize,
                                         z=pcfRadius(texels), w=shadowStrength */
float4 DiffuseColor : register(c10);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 Normal   : TEXCOORD0;
    float3 LightPos : TEXCOORD1;
};

float4 PSMain(PS_INPUT input) : SV_TARGET0
{
    float3 N = normalize(input.Normal);
    float  NdotL = max(dot(N, normalize(DirToLight)), 0.0);

    /* Light-space NDC -> shadow map UV (same convention as ssao_ps.hlsl) */
    float2 uv = float2(input.LightPos.x * 0.5 + 0.5,
                       0.5 - input.LightPos.y * 0.5);
    float receiverZ = input.LightPos.z;

    float visibility = 1.0;
    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 &&
        receiverZ >= 0.0 && receiverZ <= 1.0 && NdotL > 0.0)
    {
        /* 3x3 PCF: point-sample and compare, then average the results */
        float sum = 0.0;
        [unroll] for (int dy = -1; dy <= 1; dy++)
        {
            [unroll] for (int dx = -1; dx <= 1; dx++)
            {
                float2 offs = float2(dx, dy) * ShadowParams.y * ShadowParams.z;
                float mapZ = ShadowMap.Sample(ShadowSampler, uv + offs);
                sum += (receiverZ - ShadowParams.x <= mapZ) ? 1.0 : 0.0;
            }
        }
        visibility = sum / 9.0;
    }

    float shadowFactor = 1.0 - ShadowParams.w * (1.0 - visibility);
    float ambient = 0.15;
    float3 color = DiffuseColor.rgb * (ambient + NdotL * shadowFactor);
    return float4(color, DiffuseColor.a);
}
