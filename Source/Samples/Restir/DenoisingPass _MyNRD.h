#pragma once

#include "Falcor.h"

#include "Dependencies/NvidiaNRI/Include/NRIDescs.h"
#include "Dependencies/NvidiaNRI/Include/NRI.h"

#include "Dependencies/NvidiaNRI/Include/Extensions/NRIWrapperD3D12.h"
#include "Dependencies/NvidiaNRI/Include/Extensions/NRIHelper.h"

#include "Dependencies/NvidiaNRD/Include/NRD.h"

class NrdIntegration;
struct NrdIntegrationTexture;

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
        Falcor::ref<Falcor::Texture>& inColor,
        uint32_t width,
        uint32_t height
    );
    ~DenoisingPass();

    void render(Falcor::RenderContext* pRenderContext);
    inline Falcor::ref<Falcor::Texture>& getOuputTexture() { return mOuputTexture; };

private:
    void initNRI(Falcor::RenderContext* pRenderContext);
    void initNRD();
    void createFalcorTextures(Falcor::ref<Falcor::Device> pDevice);
    void createNRDIntegrationTextures();

    void packNRD(Falcor::RenderContext* pRenderContext);
    void dipatchNRD(Falcor::RenderContext* pRenderContext);
    void unpackNRD(Falcor::RenderContext* pRenderContext);

    NrdIntegrationTexture* FalcorTexture_to_NRDIntegrationTexture(Falcor::ref<Falcor::Texture>& falcorTexture);
    void populateCommonSettings(nrd::CommonSettings& settings);

    void TransitionTextureToCommon(Falcor::ref<Falcor::Texture>& falcorTexture);

    Falcor::ref<Falcor::Device> mpDevice;
    Falcor::ref<Falcor::Scene> mpScene;
    Falcor::RenderContext* mpRenderContext; // TESTTING

    Falcor::ref<Falcor::ComputePass> mpPackNRDPass;
    Falcor::ref<Falcor::ComputePass> mpUnpackNRDPass;

    uint32_t mWidth;
    uint32_t mHeight;

    uint32_t mFrameIndex = 0u;

    NriInterface m_NRI;

    nri::Device* m_nriDevice = nullptr;
    nri::CommandBuffer* m_nriCommandBuffer = nullptr;

    NrdIntegration* m_NRD = nullptr;

    Falcor::ref<Falcor::Texture> mViewZTexture;
    Falcor::ref<Falcor::Texture> mMotionVectorTexture;
    Falcor::ref<Falcor::Texture> mNormalLinearRoughnessTexture;
    Falcor::ref<Falcor::Texture> mOuputTexture;

    Falcor::ref<Falcor::Texture> m_InColorTexture; // From Shading pass.

    Falcor::float4x4 mPreviousFrameViewMat;
    Falcor::float4x4 mPreviousFrameProjMat;
    Falcor::float4x4 mPreviousFrameViewProjMat;

    NrdIntegrationTexture* mNRDMotionVectors;
    NrdIntegrationTexture* mNRDViewZ;
    NrdIntegrationTexture* mNRDNormalLinearRoughness;
    NrdIntegrationTexture* mInDiffuseRadianceHitTexture;
    NrdIntegrationTexture* mOutDiffuseRadianceHitTexture;

    ID3D12CommandAllocator* mNRINativeCommandAllocator;
    ID3D12CommandQueue* mNRINativeCommandQueue;
    ID3D12GraphicsCommandList* mNRINativeCommandList;
};
} // namespace Restir
