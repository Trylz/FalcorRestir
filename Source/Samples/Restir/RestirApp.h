#pragma once

#include "Falcor.h"
#include "DenoisingPass.h"
#include "GBuffer.h"
#include "RISPass.h"
#include "ShadingPass.h"
#include "TemporalFilteringPass.h"
#include "VisibilityPass.h"
#include "Core/SampleApp.h"

using namespace Falcor;

class RestirApp : public SampleApp
{
public:
    RestirApp(const SampleAppConfig& config);
    ~RestirApp();

    void onLoad(RenderContext* pRenderContext) override;
    void onResize(uint32_t width, uint32_t height) override;
    void onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo) override;
    void onGuiRender(Gui* pGui) override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;

private:
    void loadScene(const std::string& path, const Fbo* pTargetFbo, RenderContext* pRenderContext);
    void render(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo);

    ref<Scene> mpScene;
    ref<Camera> mpCamera;

    Restir::RISPass* mpRISPass = nullptr;
    Restir::VisibilityPass* mpVisibilityPass = nullptr;
    Restir::ShadingPass* mpShadingPass = nullptr;
    Restir::DenoisingPass* mpDenoisingPass = nullptr;
    Restir::TemporalFilteringPass* mpTemporalFilteringPass = nullptr;
};
