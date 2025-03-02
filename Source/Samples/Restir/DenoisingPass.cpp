#include "DenoisingPass.h"
#include "Dependencies/NvidiaNRD/Integration/NRDIntegration.hpp"
#include "GBuffer.h"
#include "Core/API/NativeHandleTraits.h"

#include <slang-gfx.h>

#if defined(_DEBUG)
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRI\\lib\\Debug\\NRI.lib")
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Debug\\NRD.lib")
#else
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRI\\lib\\Release\\NRI.lib")
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Release\\NRD.lib")
#endif

namespace Restir
{
using namespace Falcor;

DenoisingPass::DenoisingPass(
    Falcor::ref<Falcor::Device> pDevice,
    Falcor::RenderContext* pRenderContext,
    Falcor::ref<Falcor::Scene> pScene,
    uint32_t width,
    uint32_t height
)
  : mpDevice(pDevice), mpScene(pScene), mpRenderContext(pRenderContext), mWidth(width), mHeight(height)
{
    initNRI(pRenderContext);
    createTextures(pDevice);

    mpPackNRDPass = ComputePass::create(pDevice, "Samples/Restir/DenoisingPass_PackNRD.slang.slang", "PackNRD");
    mpUnpackNRDPass = ComputePass::create(pDevice, "Samples/Restir/DenoisingPass_UnpackNRD.slang", "UnpackNRD");
}

void DenoisingPass::createTextures(Falcor::ref<Falcor::Device> pDevice)
{
    mViewZTexture = mpDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::R32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );

    mMotionVectorTexture = mpDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::RG32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );

    mNormalLinearRoughnessTexture = mpDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );

    mOuputTexture = pDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    ); 
}

DenoisingPass::~DenoisingPass()
{
    // TODO
}

void DenoisingPass::initNRI(Falcor::RenderContext* pRenderContext)
{
    nri::DeviceCreationD3D12Desc deviceDesc = {};
    deviceDesc.d3d12Device = mpDevice->getNativeHandle().as<ID3D12Device*>();

    gfx::InteropHandle queueHandle;
    mpDevice->getGfxCommandQueue()->getNativeHandle(&queueHandle);
    deviceDesc.d3d12GraphicsQueue = (ID3D12CommandQueue*)queueHandle.handleValue;

#if defined(_DEBUG)
    deviceDesc.enableNRIValidation = false;
#else
    deviceDesc.enableNRIValidation = false;
#endif
    nri::Result nriResult = nri::nriCreateDeviceFromD3D12Device(deviceDesc, m_nriDevice);

    // Get core functionality
    nriResult = nri::nriGetInterface(*m_nriDevice, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&m_NRI);

    nriResult = nri::nriGetInterface(*m_nriDevice, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&m_NRI);

    // Get appropriate "wrapper" extension (XXX - can be D3D11, D3D12 or VULKAN)
    nriResult = nri::nriGetInterface(*m_nriDevice, NRI_INTERFACE(nri::WrapperD3D12Interface), (nri::WrapperD3D12Interface*)&m_NRI);

    // Wrap the command buffer
    nri::CommandBufferD3D12Desc commandBufferDesc = {};
    commandBufferDesc.d3d12CommandList = pRenderContext->getLowLevelData()->getCommandBufferNativeHandle().as<ID3D12GraphicsCommandList*>();
    ;

    // Not needed for NRD integration layer, but needed for NRI validation layer
    commandBufferDesc.d3d12CommandAllocator = nullptr; // YANN REALLY?

    m_NRI.CreateCommandBufferD3D12(*m_nriDevice, commandBufferDesc, m_nriCommandBuffer);
}


void DenoisingPass::packNRD(Falcor::RenderContext* pRenderContext, Falcor::ref<Falcor::Texture>& inColor)
{
    auto var = mpPackNRDPass->getRootVar();

    var["PerFrameCB"]["viewportDims"] = uint2(mWidth, mHeight);
    var["PerFrameCB"]["viewMat"] = transpose(mpScene->getCamera()->getViewMatrix());
    var["PerFrameCB"]["previousFrameViewProjMat"] = mPreviousFrameViewProjMat;

    var["gRadianceHit"] = inColor;
    var["gNormalLinearRoughness"] = mNormalLinearRoughnessTexture;
    var["gViewZ"] = mViewZTexture;
    var["gMotionVector"] = mMotionVectorTexture;

    var["gPositionWs"] = GBufferSingleton::instance()->getCurrentPositionWsTexture();
    var["gAlbedo"] = GBufferSingleton::instance()->getAlbedoTexture();
    var["gNormalWs"] = GBufferSingleton::instance()->getCurrentNormalWsTexture();

    mpPackNRDPass->execute(pRenderContext, mWidth, mHeight);

    mPreviousFrameViewProjMat = mpScene->getCamera()->getViewProjMatrix();
}

void DenoisingPass::dipatchNRD(Falcor::RenderContext* pRenderContext)
{
}

void DenoisingPass::unpackNRD(Falcor::RenderContext* pRenderContext)
{

}

void DenoisingPass::render(Falcor::RenderContext* pRenderContext, Falcor::ref<Falcor::Texture>& inColor)
{
    packNRD(pRenderContext, inColor);
    dipatchNRD(pRenderContext);
    unpackNRD(pRenderContext);
}
} // namespace Restir
