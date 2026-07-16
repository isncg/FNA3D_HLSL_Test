// BasicEffect pixel shader — texture, pixel lighting, fog.
// Based on FNA BasicEffect.fx.

Texture2D<float4> Texture : register(t0);
SamplerState TextureSampler : register(s0);

float4 DiffuseColor   : register(c8);
float4 EmissiveColor   : register(c9);
float4 SpecularColor   : register(c10); // .w = SpecularPower
float4 LightDir0       : register(c11);
float4 LightDiffuse0   : register(c12);
float4 LightDir1       : register(c13);
float4 LightDiffuse1   : register(c14);
float4 LightDir2       : register(c15);
float4 LightDiffuse2   : register(c16);
float4 EyePosition     : register(c17);
float4 FogColor        : register(c18);
float4 Modes           : register(c20); // x=LightingMode, y=FogEnable, z=TextureEnable

float3 ComputePixelLighting(float3 worldNormal, float3 worldPos)
{
    float3 totalDiffuse = EmissiveColor.rgb;
    float3 totalSpecular = float3(0, 0, 0);

    float3 lightDirs[3] = { LightDir0.xyz, LightDir1.xyz, LightDir2.xyz };
    float3 lightDiffs[3] = { LightDiffuse0.xyz, LightDiffuse1.xyz, LightDiffuse2.xyz };

    float3 eyeDir = normalize(EyePosition.xyz - worldPos);
    float specPower = SpecularColor.w;

    for (int i = 0; i < 3; i++)
    {
        float3 lightDir = normalize(lightDirs[i]);
        float nDotL = max(dot(worldNormal, lightDir), 0.0);
        totalDiffuse += nDotL * lightDiffs[i];

        // Specular (Blinn-Phong)
        float3 halfVec = normalize(lightDir + eyeDir);
        float spec = pow(max(dot(worldNormal, halfVec), 0.0), specPower);
        totalSpecular += spec * lightDiffs[i] * SpecularColor.rgb;
    }

    return totalDiffuse + totalSpecular;
}

float4 PSMain(
    float3 worldPos    : TEXCOORD0,
    float3 worldNormal : TEXCOORD1,
    float2 texCoord    : TEXCOORD2,
    float4 diffuse     : COLOR0,
    float  fogFactor   : TEXCOORD3) : SV_TARGET0
{
    int lightingMode = (int) Modes.x;
    int fogEnable = (int) Modes.y;
    int textureEnable = (int) Modes.z;

    float4 baseColor = diffuse;

    if (lightingMode == 2)
    {
        // Per-pixel lighting
        float3 n = normalize(worldNormal);
        float3 lighting = ComputePixelLighting(n, worldPos);
        baseColor = float4(lighting * DiffuseColor.rgb, DiffuseColor.a);
    }

    if (textureEnable)
    {
        baseColor *= Texture.Sample(TextureSampler, texCoord);
    }

    float4 color = baseColor;

    if (fogEnable)
    {
        float fog = saturate(fogFactor);
        color.rgb = lerp(color.rgb, FogColor.rgb, fog);
    }

    return color;
}
