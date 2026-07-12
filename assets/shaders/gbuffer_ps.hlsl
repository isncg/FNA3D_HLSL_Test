struct PS_INPUT
{
    float4 Position   : SV_POSITION;
    float3 ViewNormal : TEXCOORD0;
    float  ViewDepth  : TEXCOORD1;
};

struct PS_OUTPUT
{
    float4 Normal : SV_TARGET0;
    float  Depth  : SV_TARGET1;
};

PS_OUTPUT PSMain(PS_INPUT input)
{
    PS_OUTPUT output;

    float3 N = normalize(input.ViewNormal);

    /* Encode normal from [-1,1] to [0,1] */
    output.Normal = float4(N * 0.5 + 0.5, 1.0);

    /* Write raw view-space Z to R32F target */
    output.Depth = input.ViewDepth;

    return output;
}
