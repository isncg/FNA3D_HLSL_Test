// SimpleDiffuse pixel shader — single directional light, no specular.

float3 LightDirection : register(c4);
float4 DiffuseColor   : register(c5);

float4 PSMain(float3 normal : TEXCOORD0) : SV_TARGET0
{
    float3 N = normalize(normal);
    float3 L = normalize(LightDirection);
    float  NdotL = max(dot(N, L), 0.0);

    return float4(DiffuseColor.rgb * NdotL, DiffuseColor.a);
}
