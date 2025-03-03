#include "NRDEncoding.hlsli"
#include "NRD.hlsli"
#include "Reservoir.slangh"

import Utils.Sampling.TinyUniformSampleGenerator;

cbuffer PerFrameCB
{
    uint2 viewportDims;
    uint nbReservoirPerPixel;
    uint reservoirIndex;
};

RWStructuredBuffer<RestirReservoir> gReservoirs;
Texture2D<float4> gNRDOuputTexture;

[numthreads(16, 16, 1)] void UnpackNRD(uint3 threadId
                                       : SV_DispatchThreadID)
{
    const uint2 pixel = threadId.xy;
    if (any(pixel >= viewportDims))
        return;

    const uint pixelLinearIndex = pixel.y * viewportDims.x + pixel.x;
    const uint reservoirsStart = pixelLinearIndex * nbReservoirPerPixel;
    const uint reservoir = reservoirsStart + reservoirIndex;

    RestirReservoir r = gReservoirs[reservoir];
    r.mY.mIncomingRadiance = RELAX_BackEnd_UnpackRadiance(gNRDOuputTexture[pixel]).xyz;// What would we do with .w? The denoised hit distance.

    gReservoirs[reservoir] = r;
}

