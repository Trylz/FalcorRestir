
#include "RestirApp.h"
#include "LightManager.h"
#include "ReservoirManager.h"
#include "SceneSettings.h"
#include "Utils/Math/FalcorMath.h"
#include "Utils/UI/TextRenderer.h"
#include <sstream>
#include <windows.h>
#include <iostream>

FALCOR_EXPORT_D3D12_AGILITY_SDK

// THE SCENE WE USE.
#define SCENE_NAME 0

#if SCENE_NAME == 0
static const std::string kScenePath = "Arcade/Arcade.pyscene";
static const Restir::SceneName kSceneName = Restir::SceneName::Arcade;
#elif SCENE_NAME == 1
// To work model is required. READ TestScenes\DragonBuddha\README.txt
static const std::string kScenePath = "../../../../TestScenes/DragonBuddha/dragonbuddha.pyscene";
static const Restir::SceneName kSceneName = Restir::SceneName::DragonBuddha;

#else // To work model is required. READ TestScenes\SanMiguel\README.txt
static const std::string kScenePath = "../../../../TestScenes/SanMiguel/sanmiguel.pyscene";
static const Restir::SceneName kSceneName = Restir::SceneName::SanMiguel;

#endif

// https://stackoverflow.com/questions/4804298/how-to-convert-wstring-into-string
std::wstring ExePath()
{
    TCHAR buffer[MAX_PATH] = {0};
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
    return std::wstring(buffer).substr(0, pos);
}

RestirApp::RestirApp(const SampleAppConfig& config) : SampleApp(config) {}

RestirApp::~RestirApp() {}

void RestirApp::onLoad(RenderContext* pRenderContext)
{
    if (getDevice()->isFeatureSupported(Device::SupportedFeatures::Raytracing) == false)
    {
        FALCOR_THROW("Device does not support raytracing!");
    }

    if (kSceneName == Restir::SceneName::SanMiguel || kSceneName == Restir::SceneName::DragonBuddha)
    {
        std::wstring exePath = ExePath();
        std::string str(exePath.begin(), exePath.end());
        str += "/" + kScenePath;
        loadScene(str, getTargetFbo().get(), pRenderContext);
    }
    else
    {
        loadScene(kScenePath, getTargetFbo().get(), pRenderContext);
    }
}

void RestirApp::onResize(uint32_t width, uint32_t height)
{
    float h = (float)height;
    float w = (float)width;

    if (mpCamera)
    {
        float aspectRatio = (w / h);
        mpCamera->setAspectRatio(aspectRatio);
    }
}

void RestirApp::onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    if (mpScene)
    {
        IScene::UpdateFlags updates = mpScene->update(pRenderContext, getGlobalClock().getTime());
        if (is_set(updates, IScene::UpdateFlags::GeometryChanged))
            FALCOR_THROW("This sample does not support scene geometry changes.");
        if (is_set(updates, IScene::UpdateFlags::RecompileNeeded))
            FALCOR_THROW("This sample does not support scene changes that require shader recompilation.");

        render(pRenderContext, pTargetFbo);
    }

    getTextRenderer().render(pRenderContext, getFrameRate().getMsg(), pTargetFbo, {20, 20});
}

void RestirApp::onGuiRender(Gui* pGui) {}

bool RestirApp::onKeyEvent(const KeyboardEvent& keyEvent)
{
    if (keyEvent.key == Input::Key::Space && keyEvent.type == KeyboardEvent::Type::KeyPressed)
    {
        // mRayTrace = !mRayTrace;
        return true;
    }

    if (mpScene && mpScene->onKeyEvent(keyEvent))
        return true;

    return false;
}

bool RestirApp::onMouseEvent(const MouseEvent& mouseEvent)
{
    return mpScene && mpScene->onMouseEvent(mouseEvent);
}

void RestirApp::loadScene(const std::string& path, const Fbo* pTargetFbo, RenderContext* pRenderContext)
{
    mpScene = Scene::create(getDevice(), path);
    mpCamera = mpScene->getCamera();

    // Update the controllers
    float radius = mpScene->getSceneBounds().radius();
    mpScene->setCameraSpeed(radius * 0.25f);
    float nearZ = std::max(0.1f, radius / 750.0f);
    float farZ = radius * 10;
    mpCamera->setDepthRange(nearZ, farZ);
    mpCamera->setAspectRatio((float)pTargetFbo->getWidth() / (float)pTargetFbo->getHeight());

    // Create scene settings singleton.
    Restir::SceneSettingsSingleton::create();
    switch (kSceneName)
    {
    case Restir::SceneName::Arcade:
        Restir::SceneSettingsSingleton::instance()->RISSamplesCount = 4;
        Restir::SceneSettingsSingleton::instance()->nbReservoirPerPixel = 1;
        Restir::SceneSettingsSingleton::instance()->shadingLightExponent = 3.0f;
        break;

    case Restir::SceneName::DragonBuddha:
        Restir::SceneSettingsSingleton::instance()->RISSamplesCount = 16;
        Restir::SceneSettingsSingleton::instance()->nbReservoirPerPixel = 3;
        break;

    case Restir::SceneName::SanMiguel:
        Restir::SceneSettingsSingleton::instance()->temporalWsRadiusThreshold = mpScene->getSceneBounds().radius() / 1000.0f;
        Restir::SceneSettingsSingleton::instance()->shadingLightExponent = 3.0f;
        ;
        break;
    }

    // Create the remaining singletons.
    Restir::GBufferSingleton::create();
    Restir::GBufferSingleton::instance()->init(getDevice(), mpScene, pTargetFbo->getWidth(), pTargetFbo->getHeight());

    Restir::LightManagerSingleton::create();
    Restir::LightManagerSingleton::instance()->init(getDevice(), mpScene, kSceneName);

    Restir::ReservoirManagerSingleton::create();
    Restir::ReservoirManagerSingleton::instance()->init(getDevice(), pTargetFbo->getWidth(), pTargetFbo->getHeight());

    // Create the render passes.
    mpRISPass = new Restir::RISPass(getDevice(), pTargetFbo->getWidth(), pTargetFbo->getHeight());
    mpVisibilityPass = new Restir::VisibilityPass(getDevice(), mpScene, pTargetFbo->getWidth(), pTargetFbo->getHeight());
    mpTemporalFilteringPass =
        new Restir::TemporalFilteringPass(getDevice(), mpScene, kSceneName, pTargetFbo->getWidth(), pTargetFbo->getHeight());

    mpDenoisingPass = new Restir::DenoisingPass(getDevice(), pRenderContext, mpScene, pTargetFbo->getWidth(), pTargetFbo->getHeight());
    mpShadingPass = new Restir::ShadingPass(getDevice(), pTargetFbo->getWidth(), pTargetFbo->getHeight());
}

void RestirApp::render(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    FALCOR_ASSERT(mpScene);
    FALCOR_PROFILE(pRenderContext, "RestirApp::render");

    /*
    std::cout << mpCamera->getPosition().x << " " << mpCamera->getPosition().y << " " << mpCamera->getPosition().z << std::endl;
    std::cout << mpCamera->getTarget().x << " " << mpCamera->getTarget().y << " " << mpCamera->getTarget().z << std::endl;
    std::cout << mpCamera->getUpVector().x << " " << mpCamera->getUpVector().y << " " << mpCamera->getUpVector().z << std::endl;
    std::cout << mpCamera->getFocalLength() << std::endl;
    std::cout << "-------------------------------------------------------------------------------------------------" << std::endl;
    */
    Restir::GBufferSingleton::instance()->render(pRenderContext);
    mpRISPass->render(pRenderContext, mpCamera);
    mpVisibilityPass->render(pRenderContext);
    mpTemporalFilteringPass->render(pRenderContext, mpCamera);
    mpDenoisingPass->render(pRenderContext);
    mpShadingPass->render(pRenderContext, mpCamera);

    pRenderContext->blit(mpShadingPass->getOuputTexture()->getSRV(), pTargetFbo->getRenderTargetView(0));

    Restir::GBufferSingleton::instance()->setNextFrame();
    Restir::ReservoirManagerSingleton::instance()->setNextFrame();
}

int runMain(int argc, char** argv)
{
    SampleAppConfig config;
    config.windowDesc.title = "HelloRestir";
    config.windowDesc.resizableWindow = true;

    RestirApp helloRestir(config);
    return helloRestir.run();
}

int main(int argc, char** argv)
{
    return catchAndReportAllExceptions([&]() { return runMain(argc, argv); });
}
