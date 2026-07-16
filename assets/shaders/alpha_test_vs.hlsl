// AlphaTestEffect vertex shader — transforms position, passes texcoord.
// Based on FNA AlphaTestEffect.fx (VSAlphaTest).

float4x4 WorldViewProj : register(c0);
float4 DiffuseColor     : register(c4);

struct VS_INPUT
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Diffuse  : COLOR0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = mul(input.Position, WorldViewProj);
    output.TexCoord = input.TexCoord;
    output.Diffuse = DiffuseColor;
    return output;
}
