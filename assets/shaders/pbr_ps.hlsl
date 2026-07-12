// PBR pixel shader — Metallic-Roughness PBR (UE4 "Real Shading" model).
// GGX NDF + Smith GGX + Schlick Fresnel + Disney diffuse.

float3x3 WorldInverseTranspose : register(c8);
float3   EyePosition           : register(c11);
float3   LightDirection        : register(c12);
float3   LightColor            : register(c13);
float3   Albedo                : register(c14);
float    Metallic              : register(c15);
float    Roughness             : register(c16);

#define PBR_PI 3.14159265358979323846

// GGX / Trowbridge-Reitz NDF
float GGX_D(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PBR_PI * d * d);
}

// Smith GGX geometry (UE4 variant)
float Smith_G1(float NdotX, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}
float Smith_G(float NdotL, float NdotV, float roughness)
{
    return Smith_G1(NdotL, roughness) * Smith_G1(NdotV, roughness);
}

// Schlick Fresnel
float3 Schlick_F(float HdotV, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - HdotV, 0.0), 5.0);
}

// Disney (Burley 2012) diffuse
float3 DisneyDiffuse(float3 albedo, float NdotL, float NdotV, float LdotH,
                     float roughness, float metallic)
{
    float Fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float3 diff = albedo / PBR_PI
        * lerp(1.0, Fd90, pow(max(1.0 - NdotL, 0.0), 5.0))
        * lerp(1.0, Fd90, pow(max(1.0 - NdotV, 0.0), 5.0))
        * (1.0 - metallic);
    return diff;
}

float4 PSMain(float3 worldPos : TEXCOORD0, float3 worldNormal : TEXCOORD1) : SV_TARGET0
{
    float3 N = normalize(worldNormal);
    float3 V = normalize(EyePosition - worldPos);
    float3 L = normalize(LightDirection);
    float3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    float LdotH = max(dot(L, H), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), Albedo, Metallic);

    float D = GGX_D(NdotH, Roughness);
    float G = Smith_G(NdotL, NdotV, Roughness);
    float3 F = Schlick_F(HdotV, F0);

    float3 specular = (D * F * G) / max(4.0 * NdotV, 0.001);

    float3 diffuse = DisneyDiffuse(Albedo, NdotL, NdotV, LdotH, Roughness, Metallic);

    float3 color = (diffuse * NdotL + specular) * LightColor;
    return float4(color, 1.0);
}
