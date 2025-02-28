#include "BRDF.slangh"
#include "Light.slangh"
#include "Reservoir.slangh"

import Utils.Sampling.TinyUniformSampleGenerator;

cbuffer PerFrameCB
{
    uint2 viewportDims;
    float3 cameraPositionWs;
    float4x4 previousFrameViewProjMat;
    uint nbReservoirPerPixel;
    uint sampleIndex;
    uint motion;// Maybe we wont need this is denoising does a great job
};

RWStructuredBuffer<RestirReservoir> gCurrentFrameReservoirs;
StructuredBuffer<RestirReservoir> gPreviousFrameReservoirs;

Texture2D<float4> gPositionWs;

Texture2D<float4> gCurrentNormalWs;
Texture2D<float4> gPreviousNormalWs;

Texture2D<float4> gAlbedo;
Texture2D<float4> gSpecular;

float luma(float3 v)
{
    return 0.2126f * v.r + 0.7152f * v.g + 0.0722f * v.b;
}

int2 getPreviousFramePixelPos(float4 P, float width, float height)
{
    float4 ndc = mul(P, previousFrameViewProjMat);
    ndc = ndc / ndc.w;
    float2 s = (ndc.xy + float2(1, 1)) * 0.5f;
    s = float2(s.x, 1.f - s.y);
    return (int2)(s * float2(width, height));
}

RestirReservoir combineReservoirs(
    RestirReservoir r1,
    RestirReservoir r2,
    float3 P,
    float3 N,
    float3 V,
    float3 diffuse,
    float3 specular,
    float roughness,
    inout TinyUniformSampleGenerator rng
)
{
    RestirReservoir s;
    initReservoir(s);

    {
        float3 ppxSpectrum = r1.mY.mIncomingRadiance;
        {
            const float3 L = normalize(r1.mY.mLightSamplePosition - P);
            const float3 sampledBrdf = evaluateBRDF(N, L, V, diffuse, specular, roughness);
            ppxSpectrum *= sampledBrdf * max(0.0f, dot(L, N));
        }
        const float ppx = luma(ppxSpectrum);

        updateReservoir(s, rng, r1.mY, ppx * r1.m_W * (float)r1.mM);
    }

    {
        float3 ppxSpectrum = r2.mY.mIncomingRadiance;
        {
            const float3 L = normalize(r2.mY.mLightSamplePosition - P);
            const float3 sampledBrdf = evaluateBRDF(N, L, V, diffuse, specular, roughness);
            ppxSpectrum *= sampledBrdf * max(0.0f, dot(L, N));
        }
        const float ppx = luma(ppxSpectrum);

        updateReservoir(s, rng, r2.mY, ppx * r2.m_W * (float)r2.mM);
    }

    s.mM = r1.mM + r2.mM;

	// Compute and set global weight.
    {
        float3 ppxSpectrum = s.mY.mIncomingRadiance;
        {
            const float3 L = normalize(s.mY.mLightSamplePosition - P);
            const float3 sampledBrdf = evaluateBRDF(N, L, V, diffuse, specular, roughness);
            ppxSpectrum *= sampledBrdf * max(0.0f, dot(L, N));
        }

        const float ppx = luma(ppxSpectrum);
        if (ppx != 0.0f)
        {
            s.m_W = s.mWsum / ((float)s.mM * ppx);
        }
        else
        {
            s.m_W = 0.0f;
        }
    }

    return s;
}

[numthreads(16, 16, 1)] void TemporalFilteringPass(uint3 threadId
                                        : SV_DispatchThreadID)
{
    const uint2 pixel = threadId.xy;
    if (any(pixel >= viewportDims))
        return;

    if (gPositionWs[pixel].w == 0.0f)
        return;

    const float3 P = gPositionWs[pixel].xyz;

    int2 previousPixelPos = getPreviousFramePixelPos(float4(P, 1.0f), (float)viewportDims.x, (float)viewportDims.y);
    if (previousPixelPos.x < 0 || previousPixelPos.x >= (int)viewportDims.x)
        return;
    if (previousPixelPos.y < 0 || previousPixelPos.y >= (int)viewportDims.y)
        return;
    if (gPositionWs[previousPixelPos].w == 0.0f)
        return;

    const float3 N = gCurrentNormalWs[pixel].xyz;






    const float3 V = normalize(P - cameraPositionWs);
    const float3 diffuse = gAlbedo[pixel].xyz;
    const float3 specular = gSpecular[pixel].xyz;
    const float roughness = gSpecular[pixel].w;

    // Current pixel
    const uint currentPixelLinearIndex = pixel.y * viewportDims.x + pixel.x;
    const uint currentPixelReservoirsStart = currentPixelLinearIndex * nbReservoirPerPixel;

    // Previous pixel
    const uint previousPixelLinearIndex = previousPixelPos.y * viewportDims.x + previousPixelPos.x;
    const uint previousPixelReservoirsStart = previousPixelLinearIndex * nbReservoirPerPixel;

    // Init rng
    TinyUniformSampleGenerator rng = TinyUniformSampleGenerator(pixel.xy, sampleIndex);

    // Combine reservoirs
    for (uint i = 0; i < nbReservoirPerPixel; ++i)
    {
        RestirReservoir currentReservoir = gCurrentFrameReservoirs[currentPixelReservoirsStart + i];

        RestirReservoir previousReservoir = gPreviousFrameReservoirs[previousPixelReservoirsStart + i];
        if (motion > 0)
        {
            // Clamp M according to paper.
            previousReservoir.mM = min(10 * currentReservoir.mM, previousReservoir.mM);
        }

        gCurrentFrameReservoirs[currentPixelReservoirsStart + i] = combineReservoirs(currentReservoir, previousReservoir, P, N, V, diffuse, specular, roughness, rng);
    }
}
