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
    DenoisingPass(Falcor::ref<Falcor::Device> pDevice, Falcor::RenderContext* pRenderContext, Falcor::ref<Falcor::Scene> pScene);
    ~DenoisingPass();

    void render(Falcor::RenderContext* pRenderContext);

private:
    void initNRI(Falcor::RenderContext* pRenderContext);

    Falcor::ref<Falcor::Device> mpDevice;
    Falcor::ref<Falcor::Scene> mpScene;
    Falcor::RenderContext* mpRenderContext; // TESTTING

    // NRI ------------------------------------------------------------
    NriInterface m_NRI;

    nri::Device* m_nriDevice = nullptr;
    nri::CommandBuffer* m_nriCommandBuffer = nullptr;

    // NRD ------------------------------------------------------------
    NrdIntegration* m_NRD = nullptr;



};
}
