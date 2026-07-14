// Shadow map visualization -- R32F depth to grayscale RGBA8 for ImGui.

Texture2D<float> ShadowMap     : register(t0);
SamplerState     ShadowSampler : register(s0);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

float4 PSMain(PS_INPUT input) : SV_TARGET0
{
    float d = ShadowMap.Sample(ShadowSampler, input.UV);
    return float4(d, d, d, 1.0);
}
