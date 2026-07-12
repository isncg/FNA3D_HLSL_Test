// SpriteEffect pixel shader — sample texture and multiply by vertex color.

Texture2D<float4> Texture : register(t0);
SamplerState TextureSampler : register(s0);

float4 PSMain(float2 texCoord : TEXCOORD0, float4 color : COLOR0) : SV_TARGET0
{
    return Texture.Sample(TextureSampler, texCoord) * color;
}
