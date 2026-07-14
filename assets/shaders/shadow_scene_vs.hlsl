// Shadow scene vertex shader -- camera transform + light-space position.

float4x4 ViewProj      : register(c0);
float4x4 LightViewProj : register(c4);

struct VS_INPUT
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 Normal   : TEXCOORD0;
    float3 LightPos : TEXCOORD1;   /* light space: xy = NDC, z = [0,1] depth */
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 worldPos = float4(input.Position, 1.0);
    output.Position = mul(worldPos, ViewProj);
    output.Normal   = input.Normal;  /* world == model (world is identity) */
    /* Orthographic light projection: w == 1, no divide needed */
    output.LightPos = mul(worldPos, LightViewProj).xyz;

    return output;
}
