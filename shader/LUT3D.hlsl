struct ShaderData
{
    float3 DMax;
    float3 DMin;
    float Size;
};

Texture2D<float4> BufferIn : register(t0);
Texture3D<float3> Cube : register(t1);
StructuredBuffer<ShaderData> Data : register(t2);

SamplerState TexSampler : register(s0);

RWTexture2D<float4> BufferOut : register(u0);

[numthreads(32, 32, 1)] void LUT3D(uint3 DTID : SV_DispatchThreadID)
{
    ShaderData d = Data[0];
    float4 p = BufferIn[DTID.xy].rgba;
    float3 c0 = p.rgb;
    float3 c1 = (c0 - d.DMin) / (d.DMax - d.DMin);
    float3 c2 = c1.bgr;
    BufferOut[DTID.xy].rgba = float4(Cube.SampleLevel(TexSampler, c2, 0), p.a);
}
