struct ShaderData
{
    float Hue;
    float Saturation;
    float Lightness;
};

precise float Mod(precise const float lhs, precise const float rhs)
{
    return fmod(rhs + fmod(lhs, rhs), rhs);
}

precise float3 RgbToHsl(precise const float3 color)
{
    precise const float r = color.x;
    precise const float g = color.y;
    precise const float b = color.z;

    precise const float cMax = max(max(r, g), b);
    precise const float cMin = min(min(r, g), b);
    precise const float delta = cMax - cMin;

    precise const float l = (cMax + cMin) / 2.;

    precise float h = 0;
    if (delta != 0)
    {
        h = 60.;
        if (cMax == r)
            h *= Mod((g - b) / delta, 6.);
        else if (cMax == g)
            h *= (b - r) / delta + 2.;
        else if (cMax == b)
            h *= (r - g) / delta + 4.;
    }

    precise const float s = delta == 0 ? 0 : delta / (1 - abs(2 * l - 1));

    return float3(h, s, l);
}

precise float3 HslToRgb(precise const float3 color)
{
    precise const float h = color.x;
    precise const float s = color.y;
    precise const float l = color.z;

    precise const float c = (1. - abs(2. * l - 1.)) * s;
    precise const float x = c * (1. - abs(fmod(h / 60., 2.) - 1.));
    precise const float m = l - c / 2.;

    precise float3 res = float3(0, 0, 0);

    if (0. <= h && h < 60.)
    {
        res = float3(c, x, 0);
    }
    else if (60. <= h && h < 120.)
    {
        res = float3(x, c, 0);
    }
    else if (120. <= h && h < 180.)
    {
        res = float3(0, c, x);
    }
    else if (180. <= h && h < 240.)
    {
        res = float3(0, x, c);
    }
    else if (240. <= h && h < 300.)
    {
        res = float3(x, 0, c);
    }
    else if (300. <= h && h < 360.)
    {
        res = float3(c, 0, x);
    }

    res.x += m;
    res.y += m;
    res.z += m;

    return res;
}

Texture2D BufferIn : register(t0);
StructuredBuffer<ShaderData> Data : register(t1);

RWTexture2D<float4> BufferOut : register(u0);

[numthreads(32, 32, 1)] void HueSaturation(uint3 DTid
                                           : SV_DispatchThreadID)
{
    precise const ShaderData data = Data[0];

    precise const float4 pix = BufferIn[DTid.xy].rgba;

    float3 hsl = RgbToHsl(pix.rgb);

    hsl.x = Mod(data.Hue + hsl.x, 360.f);
    hsl.y = clamp(data.Saturation / 100.f + hsl.y, 0.f, 1.f);
    hsl.z = clamp(data.Lightness / 100.f + hsl.z, 0.f, 1.f);

    BufferOut[DTid.xy].rgba = float4(HslToRgb(hsl).xyz, pix.a);
}