#pragma once
#include "Falcor.h"

namespace Restir
{
class TemporalFilteringPass
{
public:
    TemporalFilteringPass(Falcor::ref<Falcor::Device> pDevice, uint32_t width, uint32_t height);

    void render(Falcor::RenderContext* pRenderContext, Falcor::ref<Falcor::Camera> pCamera);

private:
    uint32_t mWidth;
    uint32_t mHeight;
    Falcor::float4x4 mPreviousFrameViewProjMat;
    uint32_t mSampleIndex = 0u;
    Falcor::ref<Falcor::ComputePass> mpTemporalFilteringPass;
};
} // namespace Restir
