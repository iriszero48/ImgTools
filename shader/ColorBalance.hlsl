static const int Shadows = 0;
static const int Midtones = 1;
static const int Highlights = 2;

struct ShaderData
{
    int Range;
    precise float CyanRed;
    precise float MagentaGreen;
    precise float YellowBlue;
    int PreserveLuminosity;
};

precise float Apply(precise const float v, precise const float l,
                    precise float s, precise float m, precise float h)
{
    precise const float a = 0.25, b = 1. / 3., scale = 0.7;

    s *= clamp((l - b) / -a + .5, 0., 1.) * scale;
    m *= clamp((l - b) / a + .5, 0., 1.) * clamp((l + b - 1.) / -a + .5, 0., 1.) *
         scale;
    h *= clamp((l + b - 1.) / a + .5, 0., 1.) * scale;

    return clamp(v + s + m + h, 0., 1.);
}

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

precise float Boole(const bool v) { return v ? 1. : 0.; }

Texture2D BufferIn : register(t0);
StructuredBuffer<ShaderData> Data : register(t1);

RWTexture2D<float4> BufferOut : register(u0);

[numthreads(32, 32, 1)] void ColorBalance(uint3 DTid
                                          : SV_DispatchThreadID)
{
    precise const ShaderData data = Data[0];
    const int range = data.Range;
    const precise float cyanRed = data.CyanRed;
    const precise float magentaGreen = data.MagentaGreen;
    const precise float yellowBlue = data.YellowBlue;

    precise const float4 pix = BufferIn[DTid.xy].rgba;
    precise const float r = pix.x;
    precise const float g = pix.y;
    precise const float b = pix.z;
    precise const float a = pix.w;

    precise float3 gamma;
    precise float3 res = float3(0, 0, 0);

    if (range == Midtones)
    {
        static const float Delta = 0.0033944;

        gamma[0] =
            exp(-Delta * cyanRed + Delta * magentaGreen + Delta * yellowBlue);
        gamma[1] =
            exp(+Delta * cyanRed - Delta * magentaGreen + Delta * yellowBlue);
        gamma[2] =
            exp(+Delta * cyanRed + Delta * magentaGreen - Delta * yellowBlue);

        res.r = clamp(pow(clamp(r, 0., 1.), gamma[0]), 0., 1.);
        res.g = clamp(pow(clamp(g, 0., 1.), gamma[1]), 0., 1.);
        res.b = clamp(pow(clamp(b, 0., 1.), gamma[2]), 0., 1.);
    }
    else if (range == Shadows)
    {
        static const float Delta = 0.003923;

        gamma[0] = -Delta * cyanRed * Boole(cyanRed < 0.) +
                   Delta * magentaGreen * Boole(magentaGreen > 0.) +
                   Delta * yellowBlue * Boole(yellowBlue > 0.);
        gamma[1] = +Delta * cyanRed * Boole(cyanRed > 0.) -
                   Delta * magentaGreen * Boole(magentaGreen < 0.) +
                   Delta * yellowBlue * Boole(yellowBlue > 0.);
        gamma[2] = +Delta * cyanRed * Boole(cyanRed > 0.) +
                   Delta * magentaGreen * Boole(magentaGreen > 0.) -
                   Delta * yellowBlue * Boole(yellowBlue < 0.);

        res.r = clamp((r - gamma[0]) / (1. - gamma[0]), 0., 1.);
        res.g = clamp((g - gamma[1]) / (1. - gamma[1]), 0., 1.);
        res.b = clamp((b - gamma[2]) / (1. - gamma[2]), 0., 1.);
    }
    else if (range == Highlights)
    {
        static const float Delta = 0.003923;

        gamma[0] = +Delta * cyanRed * Boole(cyanRed > 0.) -
                   Delta * magentaGreen * Boole(magentaGreen < 0.) -
                   Delta * yellowBlue * Boole(yellowBlue < 0.);
        gamma[1] = -Delta * cyanRed * Boole(cyanRed < 0.) +
                   Delta * magentaGreen * Boole(magentaGreen > 0.) -
                   Delta * yellowBlue * Boole(yellowBlue < 0.);
        gamma[2] = -Delta * cyanRed * Boole(cyanRed < 0.) -
                   Delta * magentaGreen * Boole(magentaGreen < 0.) +
                   Delta * yellowBlue * Boole(yellowBlue > 0.);

        res.r = clamp(r / (1. - gamma[0]), 0., 1.);
        res.g = clamp(g / (1. - gamma[1]), 0., 1.);
        res.b = clamp(b / (1. - gamma[2]), 0., 1.);
    }

    if (data.PreserveLuminosity)
    {
        float3 hsl = RgbToHsl(res);
        hsl.z = RgbToHsl(pix.rgb).z;
        res.xyz = HslToRgb(hsl).xyz;
    }

    BufferOut[DTid.xy].rgba = float4(res.x, res.y, res.z, a);
}
