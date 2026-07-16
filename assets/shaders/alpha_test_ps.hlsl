// AlphaTestEffect pixel shader — texture sample + alpha clip + fog.
// Based on FNA AlphaTestEffect.fx (PSAlphaTestLtGt / PSAlphaTestEqNe).
//
// AlphaTest uniform layout:
//   x = reference alpha value
//   y = tolerance (for eq/ne modes)
//   z = clip value when condition passes (0 = discard, typically)
//   w = clip value when condition fails (typically negative to force discard)
//
// AlphaFunc: 0 = less-than, 1 = greater-than, 2 = equal, 3 = not-equal

Texture2D<float4> Texture : register(t0);
SamplerState TextureSampler : register(s0);

float4 AlphaTest : register(c5);

float4 PSMain(float2 texCoord : TEXCOORD0, float4 diffuse : COLOR0) : SV_TARGET0
{
    float4 color = Texture.Sample(TextureSampler, texCoord) * diffuse;

    // Alpha test with selectable compare function
    float alpha = color.a;
    float ref = AlphaTest.x;
    float tol = AlphaTest.y;
    int func = (int) AlphaTest.z;

    float clipValue;
    if (func == 0)
    {
        // Less-than: clip if alpha >= ref (keep if alpha < ref)
        clipValue = (alpha < ref) ? 1.0 : AlphaTest.w;
    }
    else if (func == 1)
    {
        // Greater-than: clip if alpha <= ref (keep if alpha > ref)
        clipValue = (alpha > ref) ? 1.0 : AlphaTest.w;
    }
    else if (func == 2)
    {
        // Equal: clip if |alpha - ref| >= tol (keep if within tolerance)
        clipValue = (abs(alpha - ref) < tol) ? 1.0 : AlphaTest.w;
    }
    else
    {
        // Not-equal: clip if |alpha - ref| < tol (keep if outside tolerance)
        clipValue = (abs(alpha - ref) >= tol) ? 1.0 : AlphaTest.w;
    }

    clip(clipValue);
    return color;
}
