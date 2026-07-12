// NormalEffect pixel shader — map normal [-1,1] to RGB [0,1].

float4 PSMain(float3 normal : TEXCOORD0) : SV_TARGET0
{
    float3 color = normal * 0.5 + 0.5;
    return float4(color, 1.0);
}
