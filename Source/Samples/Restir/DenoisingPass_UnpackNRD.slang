#include "NRDEncoding.hlsli"// todo USE PROPER YANN
#include "NRD.hlsli"// todo USE PROPER YANN

cbuffer PerFrameCB
{
    uint2 viewportDims;
};

RWTexture2D<float4> gInOutOutput;

[numthreads(16, 16, 1)] void UnpackNRD(uint3 threadId
                                       : SV_DispatchThreadID)
{
    const uint2 pixel = threadId.xy;
    if (any(pixel >= viewportDims))
        return;

    gInOutOutput[pixel] = RELAX_BackEnd_UnpackRadiance(gInOutOutput[pixel]);

}

