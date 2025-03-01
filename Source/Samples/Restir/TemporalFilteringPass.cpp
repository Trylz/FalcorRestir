#include "TemporalFilteringPass.h"
#include "GBuffer.h"
#include "ReservoirManager.h"

namespace Restir
{
using namespace Falcor;

TemporalFilteringPass::TemporalFilteringPass(
    ref<Device> pDevice,
    Falcor::ref<Falcor::Scene> pScene,
    SceneName sceneName,
    uint32_t width,
    uint32_t height
)
    : mpScene(pScene), mWidth(width), mHeight(height), mSceneName(sceneName)
{
    mpTemporalFilteringPass = ComputePass::create(pDevice, "Samples/Restir/TemporalFilteringPass.slang", "TemporalFilteringPass");
}

void TemporalFilteringPass::render(Falcor::RenderContext* pRenderContext, ref<Camera> pCamera)
{
    FALCOR_PROFILE(pRenderContext, "TemporalFilteringPass::render");

    auto var = mpTemporalFilteringPass->getRootVar();

    var["PerFrameCB"]["viewportDims"] = uint2(mWidth, mHeight);
    var["PerFrameCB"]["cameraPositionWs"] = pCamera->getPosition();
    var["PerFrameCB"]["previousFrameViewProjMat"] = transpose(mPreviousFrameViewProjMat);
    var["PerFrameCB"]["nbReservoirPerPixel"] = ReservoirManager::nbReservoirPerPixel;
    var["PerFrameCB"]["sampleIndex"] = ++mSampleIndex;
    var["PerFrameCB"]["sceneRadius"] = mpScene->getSceneBounds().radius();
    var["PerFrameCB"]["motion"] = (uint)(mPreviousFrameViewProjMat != pCamera->getViewProjMatrix());
    var["PerFrameCB"]["performRadiusCheck"] = (uint)(mSceneName == SceneName::SanMiguel);

    var["gCurrentFrameReservoirs"] = ReservoirManagerSingleton::instance()->getCurrentFrameReservoirBuffer();
    var["gPreviousFrameReservoirs"] = ReservoirManagerSingleton::instance()->getPreviousFrameReservoirBuffer();

    var["gCurrentPositionWs"] = GBufferSingleton::instance()->getCurrentPositionWsTexture();
    var["gPreviousPositionWs"] = GBufferSingleton::instance()->getPreviousPositionWsTexture();

    var["gCurrentNormalWs"] = GBufferSingleton::instance()->getCurrentNormalWsTexture();
    var["gPreviousNormalWs"] = GBufferSingleton::instance()->getPreviousNormalWsTexture();
    var["gAlbedo"] = GBufferSingleton::instance()->getAlbedoTexture();
    var["gSpecular"] = GBufferSingleton::instance()->getSpecularTexture();

    mpTemporalFilteringPass->execute(pRenderContext, mWidth, mHeight);
    mPreviousFrameViewProjMat = pCamera->getViewProjMatrix();
}
} // namespace Restir
