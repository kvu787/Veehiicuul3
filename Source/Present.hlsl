Texture2D<float4> image : register(t0);
SamplerState linearSampler : register(s0);
struct Pixel { float4 position:SV_Position; float2 uv:TEXCOORD; };
Pixel VSMain(uint index:SV_VertexID)
{
    Pixel output; output.uv=float2((index<<1)&2,index&2);
    output.position=float4(output.uv*float2(2,-2)+float2(-1,1),0,1); return output;
}
float4 PSMain(Pixel input):SV_Target { return image.Sample(linearSampler,input.uv); }
