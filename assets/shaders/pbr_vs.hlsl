// PBR vertex shader — Metallic-Roughness PBR.
// Passes WorldPos and WorldNormal to pixel shader.

float4x4 WorldViewProj : register(c0);
float4x4 World         : register(c4);

struct VS_INPUT
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
};

struct VS_OUTPUT
{
    float4 Position    : SV_POSITION;
    float3 WorldPos    : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 pos_ws = mul(float4(input.Position, 1.0), World);
    output.Position    = mul(float4(input.Position, 1.0), WorldViewProj);
    output.WorldPos    = pos_ws.xyz;
    output.WorldNormal = input.Normal;

    return output;
}
