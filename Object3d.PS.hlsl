#include "Object3d.hlsli"

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy).bgra;
    
    if (textureColor.a <= 0.5)
    {
        discard;
    }
    if (gMaterial.lightingMode == 1) // Lambert
    {
        float NdotL = dot(normalize(input.normal), -normalize(gDirectionalLight.direction));
        float lambert = saturate(NdotL);
        output.color = gMaterial.color * textureColor * gDirectionalLight.color * lambert * gDirectionalLight.intensity;
    }
    else if (gMaterial.lightingMode == 2) // Half Lambert
    {
        float NdotL = dot(normalize(input.normal), -normalize(gDirectionalLight.direction));
        float halfLambert = pow(NdotL * 0.5f + 0.5f, 2.0f);
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * halfLambert * gDirectionalLight.intensity;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else // 0: Lightingなし
    {
        output.color = gMaterial.color * textureColor;
    }
    
    if (output.color.a == 0.0)
    {
        discard;
    }
    return output;
}