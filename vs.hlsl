cbuffer MatrixBuffer : register(b0)
{
    matrix modelMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
};

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 worldPos : POSITION;
};

PixelInput main(VertexInput input)
{
    PixelInput output;

    float4 worldPosition = mul(float4(input.position, 1.0f), modelMatrix);
    output.worldPos = worldPosition.xyz;

    float4 viewPosition = mul(worldPosition, viewMatrix);
    output.position = mul(viewPosition, projectionMatrix);

    output.normal = normalize(mul(float4(input.normal, 0.0f), modelMatrix).xyz);

    output.texCoord = input.texCoord;

    return output;
}
