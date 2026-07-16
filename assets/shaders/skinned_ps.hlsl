// SkinnedEffect pixel shader — simple texture × diffuse.
// Based on FNA SkinnedEffect.fx.

Texture2D<float4> Texture : register(t0);
SamplerState TextureSampler : register(s0);

float4 PSMain(
    float2 texCoord : TEXCOORD0,
    float4 diffuse  : COLOR0) : SV_TARGET0
{
    float4 color = Texture.Sample(TextureSampler, texCoord) * diffuse;
    return color;
}
