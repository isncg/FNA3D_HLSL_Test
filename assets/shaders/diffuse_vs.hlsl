// SimpleDiffuse vertex shader — single directional light diffuse.

float4x4 WorldViewProj : register(c0);

struct VS_INPUT
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 Normal   : TEXCOORD0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    output.Position = mul(float4(input.Position, 1.0), WorldViewProj);
    output.Normal   = input.Normal;

    return output;
}
