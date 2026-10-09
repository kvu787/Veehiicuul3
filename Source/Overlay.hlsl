cbuffer ObjectConstants : register(b0)
{
    float4 worldToClip[3];
    float4 normalToView[3];
};
struct VertexInput { float3 position : POSITION; float4 color : COLOR; };
struct PixelInput { float4 position : SV_POSITION; float4 color : COLOR; };
PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    float4 p = float4(input.position, 1.0f);
    output.position = float4(dot(p, worldToClip[0]), dot(p, worldToClip[1]), dot(p, worldToClip[2]), 1.0f);
    output.color = input.color;
    return output;
}
float4 PSMain(PixelInput input) : SV_TARGET { return input.color; }
