// SpriteEffect vertex shader — passthrough NDC quad with texcoord + color.
// MatrixTransform is identity in the original test, so it's declared but unused.

float4x4 MatrixTransform : register(c0);

struct VS_INPUT
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = mul(input.Position, MatrixTransform);
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}
