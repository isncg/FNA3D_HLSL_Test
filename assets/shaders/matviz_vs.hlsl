// Matrix visualizer vertex shader — pure passthrough, no matrix transform.
// Outputs fullscreen quad NDC positions + normalized UV.

struct VS_INPUT
{
    float3 Position : POSITION0;
    float2 UV       : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = float4(input.Position, 1.0);
    output.UV = input.UV;
    return output;
}
