#include "BRDF.slangh"
#include "Reservoir.slangh"

import Utils.Sampling.TinyUniformSampleGenerator;

cbuffer PerFrameCB
{
    uint2 viewportDims;
    float4x4 viewMat;
    float4x4 viewProjMat;
};

RWTexture2D<float4> gRadianceHit;
RWTexture2D<float4> gNormalRoughness;
RWTexture2D<float> gViewZ;
RWTexture2D<float2> gMotionVector;

[numthreads(16, 16, 1)]
void PackNRD(uint3 threadId: SV_DispatchThreadID)
{
    const uint2 pixel = threadId.xy;
    if (any(pixel >= viewportDims))
        return;










    const uint pixelLinearIndex = pixel.y * viewportDims.x + pixel.x;
    const size_t reservoirsStart = pixelLinearIndex * nbReservoirPerPixel;

    const float3 P = gPositionWs[pixel].xyz;
    const float3 N = gNormalWs[pixel].xyz;
    const float3 V = normalize(P - cameraPositionWs);
    const float3 diffuse = gAlbedo[pixel].xyz;
    const float3 specular = gSpecular[pixel].xyz;
    const float roughness = gSpecular[pixel].w;

    float3 outColor = float3(0.0f, 0.0f, 0.0f);

    float hitDistance = 1e8f;
    for (uint i = 0; i <nbReservoirPerPixel; ++i)
    {
        const RestirReservoir r = gReservoirs[reservoirsStart + i];
        outColor += computeColor(r, pixel, P, N, V, diffuse, specular, roughness);

        hitDistance = min(hitDistance, r.m_hitDistance);
    }

    outColor/= nbReservoirPerPixel;

    gOutput[pixel] = float3(outColor, hitDistance);
}

