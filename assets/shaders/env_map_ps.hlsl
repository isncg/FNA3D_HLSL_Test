// EnvironmentMapEffect pixel shader — texture + cubemap with Fresnel blend.
// Based on FNA EnvironmentMapEffect.fx.

Texture2D<float4> Texture : register(t0);
SamplerState TextureSampler : register(s0);

TextureCube<float4> EnvironmentMap : register(t1);
SamplerState EnvMapSampler : register(s1);

float4 EnvironmentMapSpecular : register(c12);

float4 PSMain(
    float2 texCoord : TEXCOORD0,
    float3 envCoord : TEXCOORD1,
    float4 diffuse  : COLOR0,
    float4 specular : COLOR1) : SV_TARGET0
{
    // Sample base texture
    float4 texColor = Texture.Sample(TextureSampler, texCoord);
    float4 color = texColor * diffuse;

    // Sample cubemap
    float4 envColor = EnvironmentMap.Sample(EnvMapSampler, envCoord);

    // Fresnel blend: lerp between texture color and environment color
    // specular.rgb contains the Fresnel * EnvironmentMapAmount value
    color.rgb = lerp(color.rgb, envColor.rgb, specular.r);

    // Add environment map specular (from cubemap alpha)
    color.rgb += EnvironmentMapSpecular.rgb * envColor.a;

    color.a = diffuse.a;
    return color;
}
