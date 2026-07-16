// YUV→RGBA pixel shader — YUV color space conversion to RGB.
// Based on FNA YUVToRGBAEffect.fx and YUVToRGBAEffectR.fx.
//
// ChannelMode: 0 = read alpha channel (original), 1 = read red channel (R variant)
// ColorSpace:  0 = BT.709, 1 = BT.601
// RescaleFactor: only used in red-channel mode (YUVToRGBAEffectR)

Texture2D<float4> TexY : register(t0);
Texture2D<float4> TexU : register(t1);
Texture2D<float4> TexV : register(t2);
SamplerState SamplerY : register(s0);
SamplerState SamplerU : register(s1);
SamplerState SamplerV : register(s2);

float4 RescaleFactor : register(c0);
int ChannelMode     : register(c1);
int ColorSpace      : register(c2);

float4 PSMain(float2 texCoord : TEXCOORD0) : SV_TARGET0
{
    const float3 offset = float3(-0.0625, -0.5, -0.5);

    float3 yuv;
    if (ChannelMode == 0)
    {
        // Alpha channel (original YUVToRGBAEffect)
        yuv.x = TexY.Sample(SamplerY, texCoord).a;
        yuv.y = TexU.Sample(SamplerU, texCoord).a;
        yuv.z = TexV.Sample(SamplerV, texCoord).a;
    }
    else
    {
        // Red channel + rescale (YUVToRGBAEffectR)
        yuv.x = TexY.Sample(SamplerY, texCoord).r * RescaleFactor.x;
        yuv.y = TexU.Sample(SamplerU, texCoord).r * RescaleFactor.y;
        yuv.z = TexV.Sample(SamplerV, texCoord).r * RescaleFactor.z;
    }

    yuv += offset;

    float3 rcoeff, gcoeff, bcoeff;
    if (ColorSpace == 0)
    {
        // ITU-R BT.709
        rcoeff = float3(1.164,  0.000,  1.793);
        gcoeff = float3(1.164, -0.213, -0.533);
        bcoeff = float3(1.164,  2.112,  0.000);
    }
    else
    {
        // ITU-R BT.601
        rcoeff = float3(1.164,  0.000,  1.596);
        gcoeff = float3(1.164, -0.391, -0.813);
        bcoeff = float3(1.164,  2.018,  0.000);
    }

    float4 rgba;
    rgba.x = dot(yuv, rcoeff);
    rgba.y = dot(yuv, gcoeff);
    rgba.z = dot(yuv, bcoeff);
    rgba.w = 1.0;
    return rgba;
}
