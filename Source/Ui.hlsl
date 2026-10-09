Texture2D<float> coverage : register(t0);
SamplerState linearSampler : register(s0);
struct Vertex { float2 position:POSITION; float2 uv:TEXCOORD; float4 color:COLOR; };
struct Pixel { float4 position:SV_Position; float2 uv:TEXCOORD; float4 color:COLOR; };
Pixel VSMain(Vertex input)
{
    Pixel output;
    output.position=float4(input.position.x/1280-1,1-input.position.y/720,0,1);
    output.uv=input.uv; output.color=input.color; return output;
}
float4 PSMain(Pixel input):SV_Target
{ return float4(input.color.rgb,input.color.a*coverage.Sample(linearSampler,input.uv)); }
