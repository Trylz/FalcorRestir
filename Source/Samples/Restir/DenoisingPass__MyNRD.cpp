#include "DenoisingPass.h"
#include "Dependencies/NvidiaNRD/Integration/NRDIntegration.hpp"
#include "GBuffer.h"
#include "Core/API/NativeHandleTraits.h"

#include <slang-gfx.h>
#include <d3d12.h>

#if defined(_DEBUG)
#pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRI\\lib\\Debug\\NRI.lib")
#pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Debug\\NRD.lib")
#else
#pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRI\\lib\\Release\\NRI.lib")
#pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Release\\NRD.lib")
#endif

#if defined _DEBUG
#include <cassert>
#define NRD_ASSERT(x) assert(x)
#else
#define NRD_ASSERT(x) (x)
#endif

namespace Restir
{
using namespace Falcor;

DenoisingPass::DenoisingPass(
    Falcor::ref<Falcor::Device> pDevice,
    Falcor::RenderContext* pRenderContext,
    Falcor::ref<Falcor::Scene> pScene,
    Falcor::ref<Falcor::Texture>& inColor,
    uint32_t width,
    uint32_t height
)
    : mpDevice(pDevice), mpScene(pScene), mpRenderContext(pRenderContext), mWidth(width), mHeight(height), m_InColorTexture(inColor)
{
    initNRI(pRenderContext);
    initNRD();

    createFalcorTextures(pDevice);
    createNRDIntegrationTextures();

    mpPackNRDPass = ComputePass::create(pDevice, "Samples/Restir/DenoisingPass_PackNRD.slang", "PackNRD");
    mpUnpackNRDPass = ComputePass::create(pDevice, "Samples/Restir/DenoisingPass_UnpackNRD.slang", "UnpackNRD");
}

void DenoisingPass::createFalcorTextures(Falcor::ref<Falcor::Device> pDevice)
{
    mViewZTexture = mpDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::R32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );
    mViewZTexture->setName("NRD_ViewZ");

    mMotionVectorTexture = mpDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::RG32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );
    mMotionVectorTexture->setName("NRD_MotionVectorTexture");

    mNormalLinearRoughnessTexture = mpDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );
    mNormalLinearRoughnessTexture->setName("NRD_NormalLinearRoughness");

    mOuputTexture = pDevice->createTexture2D(
        mWidth, mHeight, ResourceFormat::RGBA32Float, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mOuputTexture->setName("NRD_OutputTexture");
}

void DenoisingPass::createNRDIntegrationTextures()
{
    mNRDMotionVectors = FalcorTexture_to_NRDIntegrationTexture(mMotionVectorTexture);
    mNRDViewZ = FalcorTexture_to_NRDIntegrationTexture(mViewZTexture);
    mNRDNormalLinearRoughness = FalcorTexture_to_NRDIntegrationTexture(mNormalLinearRoughnessTexture);
    mInDiffuseRadianceHitTexture = FalcorTexture_to_NRDIntegrationTexture(m_InColorTexture);
    mOutDiffuseRadianceHitTexture = FalcorTexture_to_NRDIntegrationTexture(mOuputTexture);
}

DenoisingPass::~DenoisingPass()
{
    delete mNRDMotionVectors;
    delete mNRDViewZ;
    delete mNRDNormalLinearRoughness;
    delete mInDiffuseRadianceHitTexture;
    delete mOutDiffuseRadianceHitTexture;

    m_NRI.DestroyCommandBuffer(*m_nriCommandBuffer);
    nri::nriDestroyDevice(*m_nriDevice);

    m_NRD->Destroy();
    delete m_NRD;
}

void DenoisingPass::initNRI(Falcor::RenderContext* pRenderContext)
{
    nri::DeviceCreationD3D12Desc deviceDesc = {};
    deviceDesc.d3d12Device = mpDevice->getNativeHandle().as<ID3D12Device*>();

    gfx::InteropHandle queueHandle;
    mpDevice->getGfxCommandQueue()->getNativeHandle(&queueHandle);
    mNRINativeCommandQueue = (ID3D12CommandQueue*)queueHandle.handleValue;
    deviceDesc.d3d12GraphicsQueue = mNRINativeCommandQueue;

    deviceDesc.enableNRIValidation = false;

    nri::Result nriResult = nri::nriCreateDeviceFromD3D12Device(deviceDesc, m_nriDevice);

    // Get core functionality
    nriResult = nri::nriGetInterface(*m_nriDevice, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&m_NRI);

    nriResult = nri::nriGetInterface(*m_nriDevice, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&m_NRI);

    // Get appropriate "wrapper" extension (XXX - can be D3D11, D3D12 or VULKAN)
    nriResult = nri::nriGetInterface(*m_nriDevice, NRI_INTERFACE(nri::WrapperD3D12Interface), (nri::WrapperD3D12Interface*)&m_NRI);

    // Wrap the command buffer
    nri::CommandBufferD3D12Desc commandBufferDesc = {};
    mNRINativeCommandList = pRenderContext->getLowLevelData()->getCommandBufferNativeHandle().as<ID3D12GraphicsCommandList*>();
    commandBufferDesc.d3d12CommandList = mNRINativeCommandList;

    // Not needed for NRD integration layer, but needed for NRI validation layer
    const HRESULT allocatorCreateRes =
        deviceDesc.d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mNRINativeCommandAllocator));
    NRD_ASSERT(SUCCEEDED(allocatorCreateRes));
    mNRINativeCommandAllocator->SetName(L"NRD command allocator");

    commandBufferDesc.d3d12CommandAllocator = mNRINativeCommandAllocator;

    const nri::Result result = m_NRI.CreateCommandBufferD3D12(*m_nriDevice, commandBufferDesc, m_nriCommandBuffer);
    NRD_ASSERT(result == nri::Result::SUCCESS);
}

#define NRD_ID(x) nrd::Identifier(nrd::Denoiser::x)

void DenoisingPass::initNRD()
{
    m_NRD = new NrdIntegration(1, false, "DenoisingPass NrdIntegration");

    const nrd::DenoiserDesc denoiserDescs[] = {
        {NRD_ID(RELAX_DIFFUSE), nrd::Denoiser::RELAX_DIFFUSE},
    };

    nrd::InstanceCreationDesc instanceCreationDesc = {};
    instanceCreationDesc.denoisers = denoiserDescs;
    instanceCreationDesc.denoisersNum = _countof(denoiserDescs);

    // NRD itself is flexible and supports any kind of dynamic resolution scaling, but NRD INTEGRATION pre-
    // allocates resources with statically defined dimensions. DRS is only supported by adjusting the viewport
    // via "CommonSettings::rectSize"
    const bool result = m_NRD->Initialize((uint16_t)mWidth, (uint16_t)mHeight, instanceCreationDesc, *m_nriDevice, m_NRI, m_NRI);
    NRD_ASSERT(result);
}

void DenoisingPass::packNRD(Falcor::RenderContext* pRenderContext)
{
    FALCOR_PROFILE(pRenderContext, "DenoisingPass::packNRD");

    auto var = mpPackNRDPass->getRootVar();

    var["PerFrameCB"]["viewportDims"] = uint2(mWidth, mHeight);
    var["PerFrameCB"]["viewMat"] = transpose(mpScene->getCamera()->getViewMatrix());
    var["PerFrameCB"]["previousFrameViewProjMat"] = transpose(mPreviousFrameViewProjMat);

    var["gRadianceHit"] = m_InColorTexture;
    var["gNormalLinearRoughness"] = mNormalLinearRoughnessTexture;
    var["gViewZ"] = mViewZTexture;
    var["gMotionVector"] = mMotionVectorTexture;

    var["gPositionWs"] = GBufferSingleton::instance()->getCurrentPositionWsTexture();
    var["gAlbedo"] = GBufferSingleton::instance()->getAlbedoTexture();
    var["gNormalWs"] = GBufferSingleton::instance()->getCurrentNormalWsTexture();

    mpPackNRDPass->execute(pRenderContext, mWidth, mHeight);
}

void DenoisingPass::unpackNRD(Falcor::RenderContext* pRenderContext)
{
    FALCOR_PROFILE(pRenderContext, "DenoisingPass::unpackNRD");

    auto var = mpUnpackNRDPass->getRootVar();

    var["PerFrameCB"]["viewportDims"] = uint2(mWidth, mHeight);
    var["gInOutOutput"] = mOuputTexture;

    mpUnpackNRDPass->execute(pRenderContext, mWidth, mHeight);
}

void DenoisingPass::render(Falcor::RenderContext* pRenderContext)
{
    NRD_ASSERT(pRenderContext == mpRenderContext);

    FALCOR_PROFILE(pRenderContext, "DenoisingPass::render");

    packNRD(pRenderContext);
    dipatchNRD(pRenderContext);
    unpackNRD(pRenderContext);

    mPreviousFrameViewMat = mpScene->getCamera()->getViewMatrix();
    mPreviousFrameProjMat = mpScene->getCamera()->getProjMatrix();
    mPreviousFrameViewProjMat = mpScene->getCamera()->getViewProjMatrix();

    ++mFrameIndex;
}

NrdIntegrationTexture* DenoisingPass::FalcorTexture_to_NRDIntegrationTexture(Falcor::ref<Falcor::Texture>& falcorTexture)
{
    NrdIntegrationTexture* Out_IntegrationTexture = new NrdIntegrationTexture();

    // Create integration texture.
    Out_IntegrationTexture->state = new nri::TextureBarrierDesc();
    nri::TextureD3D12Desc textureDesc = {};
    textureDesc.d3d12Resource = falcorTexture->getNativeHandle().as<ID3D12Resource*>();

    const nri::Result result = m_NRI.CreateTextureD3D12(*m_nriDevice, textureDesc, (nri::Texture*&)Out_IntegrationTexture->state->texture);
    NRD_ASSERT(result == nri::Result::SUCCESS);

    D3D12_RESOURCE_DESC resDesc = textureDesc.d3d12Resource->GetDesc();
    switch (resDesc.Format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        Out_IntegrationTexture->format = nri::Format::RGBA32_SFLOAT;
        break;

    case DXGI_FORMAT_R32G32B32_FLOAT:
        Out_IntegrationTexture->format = nri::Format::RGB32_SFLOAT;
        break;

    case DXGI_FORMAT_R32G32_FLOAT:
        Out_IntegrationTexture->format = nri::Format::RG32_SFLOAT;
        break;

    case DXGI_FORMAT_R32_FLOAT:
        Out_IntegrationTexture->format = nri::Format::R32_SFLOAT;
        break;

    default:
        NRD_ASSERT(false);
    }
    // Init integration texture.
    // You need to specify the current state of the resource here, after denoising NRD can modify
    // this state. Application must continue state tracking from this point.
    // Useful information:
    //    SRV = nri::AccessBits::SHADER_RESOURCE, nri::TextureLayout::SHADER_RESOURCE
    //    UAV = nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::TextureLayout::GENERAL
    // entryDesc.nextState.accessBits = ConvertResourceStateToAccessBits(myResource->GetCurrentState());
    // entryDesc.nextState.layout = ConvertResourceStateToLayout(myResource->GetCurrentState());

    return Out_IntegrationTexture;
}

void DenoisingPass::populateCommonSettings(nrd::CommonSettings& settings)
{
    const auto& camera = mpScene->getCamera();

    // YANN:Do we want to transpose theses???
    const Falcor::float4x4 currProjMatrix = camera->getProjMatrix();

    memcpy(settings.viewToClipMatrix, &currProjMatrix, sizeof(settings.viewToClipMatrix));
    memcpy(settings.viewToClipMatrixPrev, &mPreviousFrameProjMat, sizeof(settings.viewToClipMatrixPrev));

    const Falcor::float4x4 currViewMatrix = camera->getViewMatrix();
    memcpy(settings.worldToViewMatrix, &currViewMatrix, sizeof(settings.worldToViewMatrix));
    memcpy(settings.worldToViewMatrixPrev, &mPreviousFrameViewMat, sizeof(settings.worldToViewMatrixPrev));
    //--------------------------------------------------------------------------------------------------------

    settings.motionVectorScale[0] = 1.0f;
    settings.motionVectorScale[1] = 1.0f;
    settings.motionVectorScale[2] = 0.0f;

    settings.cameraJitter[0] = 0.0f;
    settings.cameraJitter[1] = 0.0f;
    settings.cameraJitterPrev[0] = 0.0f;
    settings.cameraJitterPrev[1] = 0.0f;

    settings.resourceSize[0] = (uint16_t)mWidth;
    settings.resourceSize[1] = (uint16_t)mHeight;

    settings.resourceSizePrev[0] = (uint16_t)mWidth;
    settings.resourceSizePrev[1] = (uint16_t)mHeight;

    settings.rectSize[0] = (uint16_t)((float)mWidth + 0.5f);
    settings.rectSize[1] = (uint16_t)((float)mHeight + 0.5f);

    settings.rectSizePrev[0] = (uint16_t)((float)mWidth + 0.5f);
    settings.rectSizePrev[1] = (uint16_t)((float)mHeight + 0.5f);

    settings.viewZScale = 1.0f;

    settings.denoisingRange = 4.0f * mpScene->getSceneBounds().radius();

    settings.disocclusionThreshold = 0.01f;
    settings.disocclusionThresholdAlternate = 0.05f;

    // settings.strandMaterialID = MATERIAL_ID_HAIR / 3.0f;

    // settings.splitScreen = (m_Settings.denoiser == DENOISER_REFERENCE || m_Settings.RR) ? 1.0f : m_Settings.separator;
    // settings.printfAt[0] = wantPrintf ? (uint16_t)ImGui::GetIO().MousePos.x : 9999;
    // settings.printfAt[1] = wantPrintf ? (uint16_t)ImGui::GetIO().MousePos.y : 9999;

    settings.debug = false;
    settings.frameIndex = mFrameIndex;

    settings.accumulationMode = mFrameIndex ? nrd::AccumulationMode::CONTINUE : nrd::AccumulationMode::RESTART;
    settings.isMotionVectorInWorldSpace = false;
    settings.isBaseColorMetalnessAvailable = false;
    settings.enableValidation = false;
}

struct CD3DX12_RESOURCE_BARRIER : public D3D12_RESOURCE_BARRIER
{
    CD3DX12_RESOURCE_BARRIER() {}
    explicit CD3DX12_RESOURCE_BARRIER(const D3D12_RESOURCE_BARRIER& o) : D3D12_RESOURCE_BARRIER(o) {}
    static inline CD3DX12_RESOURCE_BARRIER Transition(
        _In_ ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE
    )
    {
        CD3DX12_RESOURCE_BARRIER result;
        ZeroMemory(&result, sizeof(result));
        D3D12_RESOURCE_BARRIER& barrier = result;
        result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        result.Flags = flags;
        barrier.Transition.pResource = pResource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter = stateAfter;
        barrier.Transition.Subresource = subresource;
        return result;
    }
    static inline CD3DX12_RESOURCE_BARRIER Aliasing(_In_ ID3D12Resource* pResourceBefore, _In_ ID3D12Resource* pResourceAfter)
    {
        CD3DX12_RESOURCE_BARRIER result;
        ZeroMemory(&result, sizeof(result));
        D3D12_RESOURCE_BARRIER& barrier = result;
        result.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Aliasing.pResourceBefore = pResourceBefore;
        barrier.Aliasing.pResourceAfter = pResourceAfter;
        return result;
    }
    static inline CD3DX12_RESOURCE_BARRIER UAV(_In_ ID3D12Resource* pResource)
    {
        CD3DX12_RESOURCE_BARRIER result;
        ZeroMemory(&result, sizeof(result));
        D3D12_RESOURCE_BARRIER& barrier = result;
        result.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = pResource;
        return result;
    }
    operator const D3D12_RESOURCE_BARRIER&() const { return *this; }
};

void DenoisingPass::TransitionTextureToCommon(Falcor::ref<Falcor::Texture>& falcorTexture)
{
    ID3D12Resource* nativeTexture = falcorTexture->getNativeHandle().as<ID3D12Resource*>();

    const CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        nativeTexture,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON
    );

    mNRINativeCommandList->ResourceBarrier(1, &barrier);
}

void DenoisingPass::dipatchNRD(Falcor::RenderContext* pRenderContext)
{
    FALCOR_PROFILE(pRenderContext, "DenoisingPass::dipatchNRD");

    //=======================================================================================================
    // SETTINGS
    //=======================================================================================================

    m_NRD->NewFrame();

    nrd::CommonSettings commonSettings = {};
    populateCommonSettings(commonSettings);
    NRD_ASSERT(m_NRD->SetCommonSettings(commonSettings));

    nrd::RelaxSettings denoiserSettings{};
    const bool SetDenoiserSettingsRes = m_NRD->SetDenoiserSettings(NRD_ID(RELAX_DIFFUSE), &denoiserSettings);
    NRD_ASSERT(SetDenoiserSettingsRes);

    //=======================================================================================================
    // PERFORM DENOISING
    //=======================================================================================================

    NrdUserPool userPool = {};
    {
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_MV, *mNRDMotionVectors);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_VIEWZ, *mNRDViewZ);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_NORMAL_ROUGHNESS, *mNRDNormalLinearRoughness);

        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, *mInDiffuseRadianceHitTexture);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, *mOutDiffuseRadianceHitTexture);
        // NrdIntegration_SetResource(userPool, nrd::ResourceType::OUT_VALIDATION, *m_Out_NRD_ValidationTexture);
    };

    mNRINativeCommandAllocator->Reset();
    const HRESULT hr = mNRINativeCommandList->Reset(mNRINativeCommandAllocator, nullptr);

    TransitionTextureToCommon(mViewZTexture);
    TransitionTextureToCommon(mMotionVectorTexture);
    TransitionTextureToCommon(mNormalLinearRoughnessTexture);
    TransitionTextureToCommon(mOuputTexture);
    TransitionTextureToCommon(m_InColorTexture);

    const nrd::Identifier denoiserId = NRD_ID(RELAX_DIFFUSE);
    m_NRD->Denoise(&denoiserId, 1, *m_nriCommandBuffer, userPool);

    TransitionTextureToCommon(mViewZTexture);
    TransitionTextureToCommon(mMotionVectorTexture);
    TransitionTextureToCommon(mNormalLinearRoughnessTexture);
    TransitionTextureToCommon(mOuputTexture);
    TransitionTextureToCommon(m_InColorTexture);

    //---------------------------------------------------------------------------------------------------------------------------------

    mNRINativeCommandList->Close();
    ID3D12CommandList* ppCommandLists[] = {mNRINativeCommandList};
    mNRINativeCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

} // namespace Restir
