// Shadow depth vertex shader -- render scene from the light's view.

float4x4 LightViewProj : register(c0);

struct VS_INPUT
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;   /* unused, but keeps attribute locations
                                    aligned with the shared geometry decl
                                    (DXC assigns by declaration order) */
};

struct VS_OUTPUT
{
    float4 Position   : SV_POSITION;
    float  LightDepth : TEXCOORD0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 clipPos = mul(float4(input.Position, 1.0), LightViewProj);
    output.Position   = clipPos;
    /* Orthographic projection: w == 1, z already linear in [0,1] */
    output.LightDepth = clipPos.z;

    return output;
}
