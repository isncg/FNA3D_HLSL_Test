// Shadow depth pixel shader -- write light-space depth to R32F target.

struct PS_INPUT
{
    float4 Position   : SV_POSITION;
    float  LightDepth : TEXCOORD0;
};

float PSMain(PS_INPUT input) : SV_TARGET0
{
    return input.LightDepth;
}
