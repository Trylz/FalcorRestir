#include "DenoisingPass.h"
//#include "Dependencies/NvidiaNRD/Integration/NRDIntegration.hpp"
#include "Core/API/NativeHandleTraits.h"

#include <slang-gfx.h>

/*

#if defined(_DEBUG)
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Debug\\NRD.lib")
#else
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Release\\NRD.lib")
#endif


#if defined(_DEBUG)
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRI\\lib\\Debug\\NRI.lib")
#else
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRI\\lib\\Release\\NRI.lib")
#endif
*/

namespace Restir
{
DenoisingPass::DenoisingPass(Falcor::ref<Falcor::Device> pDevice, Falcor::RenderContext* pRenderContext, Falcor::ref<Falcor::Scene> pScene)
    : mpDevice(pDevice), mpScene(pScene), mpRenderContext(pRenderContext)
{
    initNRI(pRenderContext);
}

DenoisingPass::~DenoisingPass()
{
    //TODO
}

void DenoisingPass::initNRI(Falcor::RenderContext* pRenderContext)
{
    /*
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
    commandBufferDesc.d3d12CommandAllocator = nullptr;// YANN REALLY?

    m_NRI.CreateCommandBufferD3D12(*m_nriDevice, commandBufferDesc, m_nriCommandBuffer);
    */
}

void DenoisingPass::render(Falcor::RenderContext* pRenderContext)
{

}
}
