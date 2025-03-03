#include "DenoisingPass.h"
#include "Dependencies/NvidiaNRD/Integration/NRDIntegration.hpp"
#include "GBuffer.h"
#include "Core/API/NativeHandleTraits.h"

#include <slang-gfx.h>
#include <d3d12.h>

#if defined(_DEBUG)
#pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Debug\\NRD.lib")
#else
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
    mpDevice->requireD3D12();

    mpPackNRDPass = ComputePass::create(pDevice, "Samples/Restir/DenoisingPass_PackNRD.slang", "PackNRD");
    mpUnpackNRDPass = ComputePass::create(pDevice, "Samples/Restir/DenoisingPass_UnpackNRD.slang", "UnpackNRD");

    createFalcorTextures(pDevice);
    initNRD();
}

static void* nrdAllocate(void* userArg, size_t size, size_t alignment)
{
    return malloc(size);
}

static void* nrdReallocate(void* userArg, void* memory, size_t size, size_t alignment)
{
    return realloc(memory, size);
}

static void nrdFree(void* userArg, void* memory)
{
    free(memory);
}

static ResourceFormat getFalcorFormat(nrd::Format format)
{
    switch (format)
    {
    case nrd::Format::R8_UNORM:
        return ResourceFormat::R8Unorm;
    case nrd::Format::R8_SNORM:
        return ResourceFormat::R8Snorm;
    case nrd::Format::R8_UINT:
        return ResourceFormat::R8Uint;
    case nrd::Format::R8_SINT:
        return ResourceFormat::R8Int;
    case nrd::Format::RG8_UNORM:
        return ResourceFormat::RG8Unorm;
    case nrd::Format::RG8_SNORM:
        return ResourceFormat::RG8Snorm;
    case nrd::Format::RG8_UINT:
        return ResourceFormat::RG8Uint;
    case nrd::Format::RG8_SINT:
        return ResourceFormat::RG8Int;
    case nrd::Format::RGBA8_UNORM:
        return ResourceFormat::RGBA8Unorm;
    case nrd::Format::RGBA8_SNORM:
        return ResourceFormat::RGBA8Snorm;
    case nrd::Format::RGBA8_UINT:
        return ResourceFormat::RGBA8Uint;
    case nrd::Format::RGBA8_SINT:
        return ResourceFormat::RGBA8Int;
    case nrd::Format::RGBA8_SRGB:
        return ResourceFormat::RGBA8UnormSrgb;
    case nrd::Format::R16_UNORM:
        return ResourceFormat::R16Unorm;
    case nrd::Format::R16_SNORM:
        return ResourceFormat::R16Snorm;
    case nrd::Format::R16_UINT:
        return ResourceFormat::R16Uint;
    case nrd::Format::R16_SINT:
        return ResourceFormat::R16Int;
    case nrd::Format::R16_SFLOAT:
        return ResourceFormat::R16Float;
    case nrd::Format::RG16_UNORM:
        return ResourceFormat::RG16Unorm;
    case nrd::Format::RG16_SNORM:
        return ResourceFormat::RG16Snorm;
    case nrd::Format::RG16_UINT:
        return ResourceFormat::RG16Uint;
    case nrd::Format::RG16_SINT:
        return ResourceFormat::RG16Int;
    case nrd::Format::RG16_SFLOAT:
        return ResourceFormat::RG16Float;
    case nrd::Format::RGBA16_UNORM:
        return ResourceFormat::RGBA16Unorm;
    case nrd::Format::RGBA16_SNORM:
        return ResourceFormat::Unknown; // Not defined in Falcor
    case nrd::Format::RGBA16_UINT:
        return ResourceFormat::RGBA16Uint;
    case nrd::Format::RGBA16_SINT:
        return ResourceFormat::RGBA16Int;
    case nrd::Format::RGBA16_SFLOAT:
        return ResourceFormat::RGBA16Float;
    case nrd::Format::R32_UINT:
        return ResourceFormat::R32Uint;
    case nrd::Format::R32_SINT:
        return ResourceFormat::R32Int;
    case nrd::Format::R32_SFLOAT:
        return ResourceFormat::R32Float;
    case nrd::Format::RG32_UINT:
        return ResourceFormat::RG32Uint;
    case nrd::Format::RG32_SINT:
        return ResourceFormat::RG32Int;
    case nrd::Format::RG32_SFLOAT:
        return ResourceFormat::RG32Float;
    case nrd::Format::RGB32_UINT:
        return ResourceFormat::RGB32Uint;
    case nrd::Format::RGB32_SINT:
        return ResourceFormat::RGB32Int;
    case nrd::Format::RGB32_SFLOAT:
        return ResourceFormat::RGB32Float;
    case nrd::Format::RGBA32_UINT:
        return ResourceFormat::RGBA32Uint;
    case nrd::Format::RGBA32_SINT:
        return ResourceFormat::RGBA32Int;
    case nrd::Format::RGBA32_SFLOAT:
        return ResourceFormat::RGBA32Float;
    case nrd::Format::R10_G10_B10_A2_UNORM:
        return ResourceFormat::RGB10A2Unorm;
    case nrd::Format::R10_G10_B10_A2_UINT:
        return ResourceFormat::RGB10A2Uint;
    case nrd::Format::R11_G11_B10_UFLOAT:
        return ResourceFormat::R11G11B10Float;
    case nrd::Format::R9_G9_B9_E5_UFLOAT:
        return ResourceFormat::RGB9E5Float;
    default:
        FALCOR_THROW("Unsupported NRD format.");
    }
}

static void copyMatrix(float* dstMatrix, const float4x4& srcMatrix)
{
    float4x4 col_major = transpose(srcMatrix);
    memcpy(dstMatrix, static_cast<const float*>(col_major.data()), sizeof(float4x4));
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


void DenoisingPass::initNRD()
{
    mpDenoiser = nullptr;

    const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();

    const nrd::MethodDesc methods[] = {{nrd::Method::RELAX_DIFFUSE, uint16_t(mWidth), uint16_t(mHeight)}};

    nrd::DenoiserCreationDesc denoiserCreationDesc;
    denoiserCreationDesc.memoryAllocatorInterface.Allocate = nrdAllocate;
    denoiserCreationDesc.memoryAllocatorInterface.Reallocate = nrdReallocate;
    denoiserCreationDesc.memoryAllocatorInterface.Free = nrdFree;
    denoiserCreationDesc.requestedMethodNum = 1;
    denoiserCreationDesc.requestedMethods = methods;

    nrd::Result res = nrd::CreateDenoiser(denoiserCreationDesc, mpDenoiser);

    if (res != nrd::Result::SUCCESS)
        FALCOR_THROW("NRDPass: Failed to create NRD denoiser");

    createResources();
    createPipelines();
}

void DenoisingPass::createPipelines()
{
    mpPasses.clear();
    mpCachedProgramKernels.clear();
    mpCSOs.clear();
    mCBVSRVUAVdescriptorSetLayouts.clear();
    mpRootSignatures.clear();

    // Get denoiser desc for currently initialized denoiser implementation.
    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*mpDenoiser);

    // Create samplers descriptor layout and set.
    D3D12DescriptorSetLayout SamplersDescriptorSetLayout;

    for (uint32_t j = 0; j < denoiserDesc.staticSamplerNum; j++)
    {
        SamplersDescriptorSetLayout.addRange(ShaderResourceType::Sampler, denoiserDesc.staticSamplers[j].registerIndex, 1);
    }
    mpSamplersDescriptorSet =
        D3D12DescriptorSet::create(mpDevice, SamplersDescriptorSetLayout, D3D12DescriptorSetBindingUsage::ExplicitBind);

    // Set sampler descriptors right away.
    for (uint32_t j = 0; j < denoiserDesc.staticSamplerNum; j++)
    {
        mpSamplersDescriptorSet->setSampler(0, j, mpSamplers[j].get());
    }

    // Go over NRD passes and creating descriptor sets, root signatures and PSOs for each.
    for (uint32_t i = 0; i < denoiserDesc.pipelineNum; i++)
    {
        const nrd::PipelineDesc& nrdPipelineDesc = denoiserDesc.pipelines[i];
        const nrd::ComputeShader& nrdComputeShader = nrdPipelineDesc.computeShaderDXIL;

        // Initialize descriptor set.
        D3D12DescriptorSetLayout CBVSRVUAVdescriptorSetLayout;

        // Add constant buffer to descriptor set.
        CBVSRVUAVdescriptorSetLayout.addRange(ShaderResourceType::Cbv, denoiserDesc.constantBufferDesc.registerIndex, 1);

        for (uint32_t j = 0; j < nrdPipelineDesc.descriptorRangeNum; j++)
        {
            const nrd::DescriptorRangeDesc& nrdDescriptorRange = nrdPipelineDesc.descriptorRanges[j];

            ShaderResourceType descriptorType = nrdDescriptorRange.descriptorType == nrd::DescriptorType::TEXTURE
                                                    ? ShaderResourceType::TextureSrv
                                                    : ShaderResourceType::TextureUav;

            CBVSRVUAVdescriptorSetLayout.addRange(descriptorType, nrdDescriptorRange.baseRegisterIndex, nrdDescriptorRange.descriptorNum);
        }

        mCBVSRVUAVdescriptorSetLayouts.push_back(CBVSRVUAVdescriptorSetLayout);

        // Create root signature for the NRD pass.
        D3D12RootSignature::Desc rootSignatureDesc;
        rootSignatureDesc.addDescriptorSet(SamplersDescriptorSetLayout);
        rootSignatureDesc.addDescriptorSet(CBVSRVUAVdescriptorSetLayout);

        const D3D12RootSignature::Desc& desc = rootSignatureDesc;

        ref<D3D12RootSignature> pRootSig = D3D12RootSignature::create(mpDevice, desc);

        mpRootSignatures.push_back(pRootSig);

        // Create Compute PSO for the NRD pass.
        {
            std::string shaderFileName = "nrd/Shaders/Source/" + std::string(nrdPipelineDesc.shaderFileName) + ".hlsl";

            ProgramDesc programDesc;
            programDesc.addShaderLibrary(shaderFileName).csEntry(nrdPipelineDesc.shaderEntryPointName);
            programDesc.setCompilerFlags(SlangCompilerFlags::MatrixLayoutColumnMajor);
            // Disable warning 30056: non-short-circuiting `?:` operator is deprecated, use 'select' instead.
            programDesc.setCompilerArguments({"-Wno-30056"});
            DefineList defines;
            defines.add("NRD_COMPILER_DXC");
            defines.add("NRD_USE_OCT_NORMAL_ENCODING", "1");
            defines.add("NRD_USE_MATERIAL_ID", "0");
            ref<ComputePass> pPass = ComputePass::create(mpDevice, programDesc, defines);

            ref<Program> pProgram = pPass->getProgram();
            ref<const ProgramKernels> pProgramKernels = pProgram->getActiveVersion()->getKernels(mpDevice.get(), pPass->getVars().get());

            ComputeStateObjectDesc csoDesc;
            csoDesc.pProgramKernels = pProgramKernels;
            csoDesc.pD3D12RootSignatureOverride = pRootSig;

            ref<ComputeStateObject> pCSO = mpDevice->createComputeStateObject(csoDesc);

            mpPasses.push_back(pPass);
            mpCachedProgramKernels.push_back(pProgramKernels);
            mpCSOs.push_back(pCSO);
        }
    }
}

void DenoisingPass::createResources()
{
    // Destroy previously created resources.
    mpSamplers.clear();
    mpPermanentTextures.clear();
    mpTransientTextures.clear();

    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*mpDenoiser);
    const uint32_t poolSize = denoiserDesc.permanentPoolSize + denoiserDesc.transientPoolSize;

    // Create samplers.
    for (uint32_t i = 0; i < denoiserDesc.staticSamplerNum; i++)
    {
        const nrd::StaticSamplerDesc& nrdStaticsampler = denoiserDesc.staticSamplers[i];
        Sampler::Desc samplerDesc;
        samplerDesc.setFilterMode(TextureFilteringMode::Linear, TextureFilteringMode::Linear, TextureFilteringMode::Point);

        if (nrdStaticsampler.sampler == nrd::Sampler::NEAREST_CLAMP || nrdStaticsampler.sampler == nrd::Sampler::LINEAR_CLAMP)
        {
            samplerDesc.setAddressingMode(TextureAddressingMode::Clamp, TextureAddressingMode::Clamp, TextureAddressingMode::Clamp);
        }
        else
        {
            samplerDesc.setAddressingMode(TextureAddressingMode::Mirror, TextureAddressingMode::Mirror, TextureAddressingMode::Mirror);
        }

        if (nrdStaticsampler.sampler == nrd::Sampler::NEAREST_CLAMP || nrdStaticsampler.sampler == nrd::Sampler::NEAREST_MIRRORED_REPEAT)
        {
            samplerDesc.setFilterMode(TextureFilteringMode::Point, TextureFilteringMode::Point, TextureFilteringMode::Point);
        }
        else
        {
            samplerDesc.setFilterMode(TextureFilteringMode::Linear, TextureFilteringMode::Linear, TextureFilteringMode::Point);
        }

        mpSamplers.push_back(mpDevice->createSampler(samplerDesc));
    }

    // Texture pool.
    for (uint32_t i = 0; i < poolSize; i++)
    {
        const bool isPermanent = (i < denoiserDesc.permanentPoolSize);

        // Get texture desc.
        const nrd::TextureDesc& nrdTextureDesc =
            isPermanent ? denoiserDesc.permanentPool[i] : denoiserDesc.transientPool[i - denoiserDesc.permanentPoolSize];

        // Create texture.
        ResourceFormat textureFormat = getFalcorFormat(nrdTextureDesc.format);
        ref<Texture> pTexture = mpDevice->createTexture2D(
            nrdTextureDesc.width,
            nrdTextureDesc.height,
            textureFormat,
            1u,
            nrdTextureDesc.mipNum,
            nullptr,
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
        );

        if (isPermanent)
            mpPermanentTextures.push_back(pTexture);
        else
            mpTransientTextures.push_back(pTexture);
    }
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
