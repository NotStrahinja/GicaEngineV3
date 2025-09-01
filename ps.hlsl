cbuffer MatrixBuffer : register(b0)
{
    matrix modelMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
};

cbuffer LightBuffer : register(b1)
{
    float3 dirLightDirection;
    float  pad1;
    float3 dirLightColor;
    float  pad2;

    float3 cameraPosition;
    float  pad3;

    int    numPointLights;
    float3 pad4;
};

struct PointLight
{
    float3 position;
    float  range;
    float3 color;
    float  intensity;
};

Texture2D tex : register(t0);
StructuredBuffer<PointLight> pointLights : register(t1);
SamplerState sam : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 worldPos : POSITION;
};

float4 main(PixelInput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float3 color = float3(0.1f, 0.1f, 0.1f); // ambient

    // Directional light
    float3 dirLight = max(dot(normal, -dirLightDirection), 0.0f) * dirLightColor;
    color += dirLight;

    // Point lights
    for (int i = 0; i < numPointLights; ++i)
    {
        float3 toLight = pointLights[i].position - input.worldPos;
        float dist = length(toLight);
        if (dist < pointLights[i].range)
        {
            float3 lightDir = normalize(toLight);
            float diff = max(dot(normal, lightDir), 0.0f);
            float attenuation = saturate(1.0f - dist / pointLights[i].range);
            color += diff * pointLights[i].color * pointLights[i].intensity * attenuation;
        }
    }

    /*for (int i = 0; i < numPointLights; ++i)
    {
        float3 toLight = pointLights[i].position - input.worldPos;
        float dist = length(toLight);

        float3 lightDir = normalize(toLight);
        float diff = max(dot(normal, lightDir), 0.0f);

        color += diff * pointLights[i].color;
    }*/

    return float4(tex.Sample(sam, input.texCoord).rgb * color, 1.0f);
}
