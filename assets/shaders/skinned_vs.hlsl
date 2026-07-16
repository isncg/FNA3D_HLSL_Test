// SkinnedEffect vertex shader — GPU bone skinning with lighting.
// Based on FNA SkinnedEffect.fx.
//
// Supports 2 bones per vertex. BlendWeights select which bones.
// BoneCount uniform: 1 or 2 (controls how many weights to use).

float4x4 World        : register(c0);
float4x4 ViewProj     : register(c4);
float4 DiffuseColor   : register(c8);
float4 Modes          : register(c20); // x = boneCount

float4x4 Bone0        : register(c22);
float4x4 Bone1        : register(c26);

struct VS_INPUT
{
    float4 Position      : POSITION0;
    float3 Normal        : NORMAL0;
    float2 TexCoord      : TEXCOORD0;
    float4 BlendIndices  : BLENDINDICES0;
    float4 BlendWeights  : BLENDWEIGHT0;
};

struct VS_OUTPUT
{
    float4 Position    : SV_POSITION;
    float2 TexCoord    : TEXCOORD0;
    float4 Diffuse     : COLOR0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;

    int boneCount = (int) Modes.x;

    // Build weighted skinning matrix from 2 bones
    float4x4 skinMatrix = Bone0 * input.BlendWeights.x;

    if (boneCount >= 2)
    {
        skinMatrix += Bone1 * input.BlendWeights.y;
    }

    // Skin position and normal
    float4 skinnedPos = mul(float4(input.Position.xyz, 1.0), skinMatrix);
    float3 skinnedNormal = mul(float4(input.Normal, 0.0), skinMatrix).xyz;

    // World transform
    float4 worldPos = mul(skinnedPos, World);
    float3x3 worldIT = (float3x3) World;
    float3 worldNormal = normalize(mul(skinnedNormal, worldIT));

    // Simple directional light from above
    float3 lightDir = normalize(float3(0.5, -1.0, 0.3));
    float nDotL = max(dot(worldNormal, lightDir), 0.0);
    float ambient = 0.2;
    float lighting = ambient + nDotL * 0.8;

    output.Position = mul(worldPos, ViewProj);
    output.TexCoord = input.TexCoord;
    output.Diffuse = float4(DiffuseColor.rgb * lighting, DiffuseColor.a);

    return output;
}
