// EnvironmentMapEffect vertex shader — world transform, reflection vector.
// Based on FNA EnvironmentMapEffect.fx.

float4x4 World      : register(c0);
float4x4 ViewProj   : register(c4);
float4 EyePosition  : register(c8);
float4 DiffuseColor : register(c9);
float EnvironmentMapAmount : register(c10);
float FresnelFactor        : register(c11);
float4 EnvironmentMapSpecular : register(c12);

struct VS_INPUT
{
    float4 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position    : SV_POSITION;
    float2 TexCoord    : TEXCOORD0;
    float3 EnvCoord    : TEXCOORD1; // cubemap reflection vector
    float4 Diffuse     : COLOR0;
    float4 Specular    : COLOR1;    // .rgb = fresnel/amount, .a = fog factor
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 worldPos = mul(input.Position, World);
    float3x3 worldInvTranspose = (float3x3) World;
    float3 worldNormal = normalize(mul(input.Normal, worldInvTranspose));

    output.Position = mul(worldPos, ViewProj);
    output.TexCoord = input.TexCoord;

    // Reflection vector for cubemap lookup
    float3 eyeToPos = worldPos.xyz - EyePosition.xyz;
    float3 eyeDir = normalize(eyeToPos);
    output.EnvCoord = reflect(eyeDir, worldNormal);

    output.Diffuse = DiffuseColor;

    // Fresnel factor
    float fresnel = pow(max(1.0 - abs(dot(eyeDir, worldNormal)), 0.0), FresnelFactor);
    fresnel *= EnvironmentMapAmount;
    output.Specular = float4(fresnel.xxx, 0.0);

    return output;
}
