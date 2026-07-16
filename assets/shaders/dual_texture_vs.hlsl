// DualTextureEffect vertex shader — transforms position, passes two texcoords.
// Based on FNA DualTextureEffect.fx (VSDualTexture).

float4x4 WorldViewProj : register(c0);
float4 DiffuseColor     : register(c4);

struct VS_INPUT
{
    float4 Position  : POSITION0;
    float2 TexCoord  : TEXCOORD0;
    float2 TexCoord2 : TEXCOORD1;
};

struct VS_OUTPUT
{
    float4 Position  : SV_POSITION;
    float2 TexCoord  : TEXCOORD0;
    float2 TexCoord2 : TEXCOORD1;
    float4 Diffuse   : COLOR0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = mul(input.Position, WorldViewProj);
    output.TexCoord = input.TexCoord;
    output.TexCoord2 = input.TexCoord2;
    output.Diffuse = DiffuseColor;
    return output;
}
