// BasicEffect vertex shader — world transform, lighting, fog.
// Based on FNA BasicEffect.fx. Uses uniform-driven branching instead of
// compile-time permutations.
//
// LightingMode: 0 = none, 1 = vertex (3 dir lights), 2 = pixel (pass through)

float4x4 World      : register(c0);
float4x4 ViewProj   : register(c4);
float4 DiffuseColor : register(c8);
float4 EmissiveColor : register(c9);
float4 SpecularColor : register(c10); // .w = SpecularPower
float4 LightDir0    : register(c11);
float4 LightDiffuse0 : register(c12);
float4 LightDir1    : register(c13);
float4 LightDiffuse1 : register(c14);
float4 LightDir2    : register(c15);
float4 LightDiffuse2 : register(c16);
float4 EyePosition  : register(c17);
float4 FogVector    : register(c19);
float4 Modes        : register(c20); // x=LightingMode, y=FogEnable, z=TextureEnable

struct VS_INPUT
{
    float4 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position     : SV_POSITION;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    float2 TexCoord     : TEXCOORD2;
    float4 Diffuse      : COLOR0;
    float  FogFactor    : TEXCOORD3;
};

float3 ComputeVertexLighting(float3 worldNormal)
{
    // EmissiveColor serves as ambient — always present regardless of light direction
    float3 totalDiffuse = EmissiveColor.rgb;

    float3 lightDirs[3] = { LightDir0.xyz, LightDir1.xyz, LightDir2.xyz };
    float3 lightDiffs[3] = { LightDiffuse0.xyz, LightDiffuse1.xyz, LightDiffuse2.xyz };

    for (int i = 0; i < 3; i++)
    {
        float nDotL = max(dot(worldNormal, normalize(lightDirs[i])), 0.0);
        totalDiffuse += nDotL * lightDiffs[i];
    }

    return totalDiffuse;
}

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 worldPos = mul(input.Position, World);
    float3x3 worldInvTranspose = (float3x3) World; // simplified: assumes uniform scale
    float3 worldNormal = normalize(mul(input.Normal, worldInvTranspose));

    output.Position = mul(worldPos, ViewProj);
    output.WorldPos = worldPos.xyz;
    output.WorldNormal = worldNormal;
    output.TexCoord = input.TexCoord;

    int lightingMode = (int) Modes.x;
    int fogEnable = (int) Modes.y;

    if (lightingMode == 1)
    {
        // Vertex lighting
        float3 lighting = ComputeVertexLighting(worldNormal);
        output.Diffuse = float4(lighting * DiffuseColor.rgb, DiffuseColor.a);
    }
    else if (lightingMode == 2)
    {
        // Pixel lighting — pass material colors, PS does the work
        output.Diffuse = DiffuseColor;
    }
    else
    {
        // No lighting — just diffuse color
        output.Diffuse = DiffuseColor;
    }

    // Fog factor
    if (fogEnable)
    {
        output.FogFactor = dot(output.Position, FogVector);
    }
    else
    {
        output.FogFactor = 0.0;
    }

    return output;
}
