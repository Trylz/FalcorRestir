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

void DenoisingPass::createNRDIntegrationTextures()
{
    mNRDMotionVectors = FalcorTexture_to_NRDIntegrationTexture(mMotionVectorTexture);
    mNRDViewZ = FalcorTexture_to_NRDIntegrationTexture(mViewZTexture);
    mNRDNormalLinearRoughness = FalcorTexture_to_NRDIntegrationTexture(mNormalLinearRoughnessTexture);
    mInDiffuseRadianceHitTexture = FalcorTexture_to_NRDIntegrationTexture(m_InColorTexture);
    mOutRadianceHitTexture = FalcorTexture_to_NRDIntegrationTexture(mOuputTexture);
}

DenoisingPass::~DenoisingPass()
{
    delete mNRDMotionVectors;
    delete mNRDViewZ;
    delete mNRDNormalLinearRoughness;
    delete mInDiffuseRadianceHitTexture;
    delete mOutRadianceHitTexture;

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
    bool result = m_NRD->Initialize((uint16_t)mWidth, (uint16_t)mHeight, instanceCreationDesc, *m_nriDevice, m_NRI, m_NRI);
    NRD_ASSERT(result);
}

void DenoisingPass::packNRD(Falcor::RenderContext* pRenderContext)
{
    auto var = mpPackNRDPass->getRootVar();

    var["PerFrameCB"]["viewportDims"] = uint2(mWidth, mHeight);
    var["PerFrameCB"]["viewMat"] = transpose(mpScene->getCamera()->getViewMatrix());
    var["PerFrameCB"]["previousFrameViewProjMat"] = mPreviousFrameViewProjMat;

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
    auto var = mpUnpackNRDPass->getRootVar();

    var["PerFrameCB"]["viewportDims"] = uint2(mWidth, mHeight);
    var["gInOutOutput"] = mOuputTexture;

    mpUnpackNRDPass->execute(pRenderContext, mWidth, mHeight);
}

void DenoisingPass::render(Falcor::RenderContext* pRenderContext)
{
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

    m_NRI.CreateTextureD3D12(*m_nriDevice, textureDesc, (nri::Texture*&)Out_IntegrationTexture->state->texture);

    D3D12_RESOURCE_DESC resDesc = textureDesc.d3d12Resource->GetDesc();
    switch (resDesc.Format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        Out_IntegrationTexture->format = nri::Format::RGBA32_SFLOAT;
        break;

    case DXGI_FORMAT_R32G32B32_FLOAT:
        Out_IntegrationTexture->format = nri::Format::RGB32_SFLOAT;
        break;

    case DXGI_FORMAT_R8G8B8A8_UNORM:
        Out_IntegrationTexture->format = nri::Format::RGBA8_UNORM;
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

    // Do we want to transpose theses???
    memcpy(settings.viewToClipMatrix, &camera->getProjMatrix(), sizeof(settings.viewToClipMatrix));
    memcpy(settings.viewToClipMatrixPrev, &mPreviousFrameProjMat, sizeof(settings.viewToClipMatrixPrev));

    memcpy(settings.worldToViewMatrix, &camera->getViewMatrix(), sizeof(settings.worldToViewMatrix));
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

#if defined(_DEBUG)
    settings.debug = true;
#endif
    settings.frameIndex = mFrameIndex;

    settings.accumulationMode = settings.frameIndex ? nrd::AccumulationMode::CONTINUE : nrd::AccumulationMode::RESTART;
    settings.isMotionVectorInWorldSpace = false;
    settings.isBaseColorMetalnessAvailable = false;
    settings.enableValidation = false;
}

void DenoisingPass::dipatchNRD(Falcor::RenderContext* pRenderContext)
{
    //=======================================================================================================
    // SETTINGS
    //=======================================================================================================

    m_NRD->NewFrame();

    nrd::CommonSettings commonSettings = {};
    populateCommonSettings(commonSettings);
    NRD_ASSERT(m_NRD->SetCommonSettings(commonSettings));

    nrd::RelaxSettings denoiserSettings;
    populateDenoiserSettings(denoiserSettings, denoisingArgs);
    m_NRD->SetDenoiserSettings(NRD_ID(REBLUR_DIFFUSE_SPECULAR), &denoiserSettings);

    //=======================================================================================================
    // PERFORM DENOISING
    //=======================================================================================================

    NrdUserPool userPool = {};
    {
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_MV, *In_NRD_MotionVectorsTexture);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_VIEWZ, *In_NRD_ViewZTexture);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_NORMAL_ROUGHNESS, *In_NRD_NormalRoughnessTexture);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_BASECOLOR_METALNESS, *In_NRD_BaseColorMetalnessTexture);

        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, *In_NRD_DiffuseRadianceHitTexture);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, *In_NRD_SpecularRadianceHitTexture);

        NrdIntegration_SetResource(userPool, nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, *m_Out_NRD_DiffuseRadianceHitTexture);
        NrdIntegration_SetResource(userPool, nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, *m_Out_NRD_SpecularRadianceHitTexture);

        NrdIntegration_SetResource(userPool, nrd::ResourceType::OUT_VALIDATION, *m_Out_NRD_ValidationTexture);
    };

    const nrd::Identifier denoiserId = NRD_ID(REBLUR_DIFFUSE_SPECULAR);
    m_NRD->Denoise(&denoiserId, 1, *ConstantNRDContextSingleton::instance()->getNRICommandBuffer(), userPool);

    //=======================================================================================================
    // COPY NRD RESULT TO READBACK HEAPS
    //=======================================================================================================

    auto sharedCmdContext = SharedDx12CommandContextSingleton::instance()->getContext();

    sharedCmdContext.commandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            m_Out_Native_DiffuseRadianceHitTexture.buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE
        )
    );
    sharedCmdContext.commandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            m_Out_Native_SpecularRadianceHitTexture.buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE
        )
    );

    sharedCmdContext.commandList->CopyResource(m_diffuseRadianceReadbackHeap, m_Out_Native_DiffuseRadianceHitTexture.buffer);
    sharedCmdContext.commandList->CopyResource(m_specularRadianceReadbackHeap, m_Out_Native_SpecularRadianceHitTexture.buffer);

    sharedCmdContext.commandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            m_Out_Native_DiffuseRadianceHitTexture.buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        )
    );
    sharedCmdContext.commandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            m_Out_Native_SpecularRadianceHitTexture.buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        )
    );

    //=======================================================================================================
    // WAIT COMMANDS FINISH
    //=======================================================================================================

    SharedDx12CommandContextSingleton::instance()->endCommandRecording();
    SharedDx12CommandContextSingleton::instance()->finishTasks();

    //=======================================================================================================
    // BUILD FINAL RESULT
    //=======================================================================================================
    NEBULA_ASSERT(imageOut.getFormat() == ImageFormat::RGB32F);
    RGB32FImage* imageRGBFOut = dynamic_cast<RGB32FImage*>(&imageOut);
    NEBULA_ASSERT(imageRGBFOut);

    const CD3DX12_BOX iBox(0, 0, width, height);
    m_specularRadianceReadbackHeap->ReadFromSubresource(
        reinterpret_cast<void*>(m_OutSpecularRadianceHit->getMutableRawData()),
        (UINT)m_OutSpecularRadianceHit->getBytesPerRow(),
        0,
        0,
        &iBox
    );
    m_diffuseRadianceReadbackHeap->ReadFromSubresource(
        reinterpret_cast<void*>(m_OutDiffuseRadianceHit->getMutableRawData()), (UINT)m_OutDiffuseRadianceHit->getBytesPerRow(), 0, 0, &iBox
    );

    tbb::parallel_for(
        size_t(0),
        size_t(nbPixels),
        [&](size_t tbbIdx)
        {
            const nbUint32 pixelIdx = (nbUint32)tbbIdx;
            const nbUint32 pixelPosX = (nbUint32)(pixelIdx % width);
            const nbUint32 pixelPosY = (nbUint32)(pixelIdx / width);
            const Math::Uvec2 pixelPos = Math::Uvec2(pixelPosX, pixelPosY);

            const nbBool isSpecular = gBuffer->m_isSpecularFlag->getPixelFromPosition(pixelPos).r > 0.0f;

            RGBAFColor rawPixel;
            if (isSpecular)
            {
                m_OutSpecularRadianceHit->getPixelFromPosition(pixelPos);
            }
            else
            {
                m_OutDiffuseRadianceHit->getPixelFromPosition(pixelPos);
            }

            const RGBAFColor unpackedPixel = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(rawPixel);
            imageRGBFOut->setPixelFromPosition(RGBFColor(unpackedPixel.x, unpackedPixel.y, unpackedPixel.z), pixelPos);
        }
    );

    //=======================================================================================================
    // FREE NRD TEXTURES
    //=======================================================================================================
    free_NRDIntegrationTexture(In_NRD_DiffuseRadianceHitTexture);
    free_NRDIntegrationTexture(In_NRD_SpecularRadianceHitTexture);
    free_NRDIntegrationTexture(In_NRD_MotionVectorsTexture);
    free_NRDIntegrationTexture(In_NRD_NormalRoughnessTexture);
    free_NRDIntegrationTexture(In_NRD_ViewZTexture);
    free_NRDIntegrationTexture(In_NRD_BaseColorMetalnessTexture);

    //=======================================================================================================
    // FREE NATIVE TEXTURES
    //=======================================================================================================
    In_Native_DiffuseRadianceHitTexture.buffer->Release();
    In_Native_SpecularRadianceHitTexture.buffer->Release();
    In_Native_MotionVectorsTexture.buffer->Release();
    In_Native_NormalRoughnessTexture.buffer->Release();
    In_Native_ViewZTexture.buffer->Release();
    In_Native_BaseColorMetalnessTexture.buffer->Release();

    //=======================================================================================================
    // FREE PENDING RESOURCES
    //=======================================================================================================
    for (auto resource : pendingDx12UploadHeapsToFree)
        resource->Release();
}

} // namespace Restir
