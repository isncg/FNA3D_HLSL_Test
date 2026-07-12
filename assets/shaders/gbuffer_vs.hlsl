float4x4 ViewProj : register(c0);
float4x4 View     : register(c4);

struct VS_INPUT
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
};

struct VS_OUTPUT
{
    float4 Position   : SV_POSITION;
    float3 ViewNormal : TEXCOORD0;
    float  ViewDepth  : TEXCOORD1;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 worldPos = float4(input.Position, 1.0);
    float4 viewPos  = mul(worldPos, View);
    output.Position  = mul(worldPos, ViewProj);
    output.ViewNormal = mul(float4(input.Normal, 0.0), View).xyz;
    output.ViewDepth  = viewPos.z;

    return output;
}
