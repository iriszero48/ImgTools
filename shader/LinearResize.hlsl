struct ShaderData
{
    float Width;
    float Height;
};

Texture2D<float4> BufferIn : register(t0);
StructuredBuffer<ShaderData> Data : register(t1);

SamplerState TexSampler : register(s0);

RWTexture2D<float4> BufferOut : register(u0);

[numthreads(32, 32, 1)] void LinearResize(uint3 GID
                                          : SV_GroupID, uint3 DTID
                                          : SV_DispatchThreadID, uint3 GTID
                                          : SV_GroupThreadID, int GI
                                          : SV_GroupIndex)
{
    ShaderData d = Data[0];

    float stepW = 1. / d.Width;
    float stepH = 1. / d.Height;
    float2 pos = DTID.xy / float2(d.Width, d.Height);

    BufferOut[DTID.xy].rgba = BufferIn.SampleLevel(TexSampler, pos, 0).rgba;
}
