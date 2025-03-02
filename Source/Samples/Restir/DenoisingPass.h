#pragma once

#include "Falcor.h"

#include "Dependencies/NvidiaNRI/Include/NRIDescs.h"
#include "Dependencies/NvidiaNRI/Include/NRI.h"

#include "Dependencies/NvidiaNRI/Include/Extensions/NRIWrapperD3D12.h"
#include "Dependencies/NvidiaNRI/Include/Extensions/NRIHelper.h"

#include "Dependencies/NvidiaNRD/Include/NRD.h"

class NrdIntegration;

namespace Restir
{
struct NriInterface : public nri::CoreInterface, public nri::HelperInterface, public nri::WrapperD3D12Interface
{};

class DenoisingPass
{
public:
    DenoisingPass(
        Falcor::ref<Falcor::Device> pDevice,
        Falcor::RenderContext* pRenderContext,
        Falcor::ref<Falcor::Scene> pScene,
        uint32_t width,
        uint32_t height
    );
    ~DenoisingPass();

    void render(Falcor::RenderContext* pRenderContext, Falcor::ref<Falcor::Texture>& inColor);

private:
    void initNRI(Falcor::RenderContext* pRenderContext);
    void createTextures(Falcor::ref<Falcor::Device> pDevice);

    void packNRD(Falcor::RenderContext* pRenderContext, Falcor::ref<Falcor::Texture>& inColor);
    void dipatchNRD(Falcor::RenderContext* pRenderContext);
    void unpackNRD(Falcor::RenderContext* pRenderContext);

    Falcor::ref<Falcor::Device> mpDevice;
    Falcor::ref<Falcor::Scene> mpScene;
    Falcor::RenderContext* mpRenderContext; // TESTTING

    Falcor::ref<Falcor::ComputePass> mpPackNRDPass;
    Falcor::ref<Falcor::ComputePass> mpUnpackNRDPass;

    uint32_t mWidth;
    uint32_t mHeight;

    NriInterface m_NRI;

    nri::Device* m_nriDevice = nullptr;
    nri::CommandBuffer* m_nriCommandBuffer = nullptr;

    NrdIntegration* m_NRD = nullptr;

    Falcor::ref<Falcor::Texture> mViewZTexture;
    Falcor::ref<Falcor::Texture> mMotionVectorTexture;
    Falcor::ref<Falcor::Texture> mNormalLinearRoughnessTexture;
    Falcor::ref<Falcor::Texture> mOuputTexture;

    Falcor::float4x4 mPreviousFrameViewProjMat;
};
} // namespace Restir
