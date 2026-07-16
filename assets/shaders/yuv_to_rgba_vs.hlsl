// YUV→RGBA vertex shader — fullscreen quad passthrough.
// Based on FNA YUVToRGBAEffect.fx / YUVToRGBAEffectR.fx.
// No matrix transform — vertices are already in NDC.

struct VS_INPUT
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = input.Position;
    output.Position.w = 1.0;
    output.TexCoord = input.TexCoord;
    return output;
}
