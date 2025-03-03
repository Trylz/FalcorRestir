#include "NRDEncoding.hlsli"// todo USE PROPER YANN
#include "NRD.hlsli"// todo USE PROPER YANN

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
    const size_t reservoirsStart = pixelLinearIndex * nbReservoirPerPixel;

    RestirReservoir r = gReservoirs[reservoirsStart + reservoirIndex];
    r.mY.mIncomingRadiance = RELAX_BackEnd_UnpackRadiance(gNRDOuputTexture[pixel]);

    gReservoirs[pixel] = r;

}

