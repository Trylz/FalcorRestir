/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "YannOptixDenoiser.h"
#include "GBuffer.h"

FALCOR_ENUM_INFO(
    OptixDenoiserModelKind,
    {
        {OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_LDR, "LDR"},
        {OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_HDR, "HDR"},
        {OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_AOV, "AOV"},
        {OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_TEMPORAL, "Temporal"},
    }
);
FALCOR_ENUM_REGISTER(OptixDenoiserModelKind);

namespace Restir
{
YannOptixDenoiser::YannOptixDenoiser(
    Falcor::ref<Falcor::Device> pDevice,
    Falcor::ref<Falcor::Scene> pScene,
    uint32_t width,
    uint32_t height
)
    : mpDevice(pDevice), mpScene(pScene), mWidth(width), mHeight(height)
{
    
    mpConvertTexToBuf = ComputePass::create(mpDevice, "Samples/Restir/ConvertTexToBuf.slang", "main");
    mpConvertNormalsToBuf = ComputePass::create(mpDevice, "Samples/Restir/ConvertNormalsToBuf.slang", "main");
    mpConvertMotionVectors = ComputePass::create(mpDevice, "Samples/Restir/ConvertMotionVectorInputs.slang", "main");
    mpConvertBufToTex = FullScreenPass::create(mpDevice, "Samples/Restir/ConvertBufToTex.slang");
    mpFbo = Fbo::create(mpDevice);
}

void YannOptixDenoiser::compile(RenderContext* pRenderContext)
{
    // Initialize OptiX context.
    mOptixContext = initOptix(mpDevice.get());
        
    // Set correct parameters for the provided inputs.
    mDenoiser.options.guideNormal = 1u;
    mDenoiser.options.guideAlbedo = 1u;

    // If the user specified a denoiser on initialization, respect that.  Otherwise, choose the "best"
    mSelectedModel = OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_TEMPORAL;
    mDenoiser.modelKind = OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_TEMPORAL;

    // (Re-)allocate temporary buffers when render resolution changes
    uint2 newSize = uint2(mWidth, mHeight);

    // If allowing tiled denoising, these may be smaller than the window size (TODO; not currently handled)
    mDenoiser.tileWidth = newSize.x;
    mDenoiser.tileHeight = newSize.y;

    // Reallocate / reszize our staging buffers for transferring data to and from OptiX / CUDA / DXR
    reallocateStagingBuffers(pRenderContext);

    // Size intensity and hdrAverage buffers correctly.  Only one at a time is used, but these are small, so create them both
    if (mDenoiser.intensityBuffer.getSize() != (1 * sizeof(float)))
        mDenoiser.intensityBuffer.resize(1 * sizeof(float));
    if (mDenoiser.hdrAverageBuffer.getSize() != (3 * sizeof(float)))
        mDenoiser.hdrAverageBuffer.resize(3 * sizeof(float));

    // Create an intensity GPU buffer to pass to OptiX when appropriate
    if (!mDenoiser.kernelPredictionMode || !mDenoiser.useAOVs)// YANN nt sure about this
    {
        mDenoiser.params.hdrIntensity = mDenoiser.intensityBuffer.getDevicePtr();
        mDenoiser.params.hdrAverageColor = static_cast<CUdeviceptr>(0);
    }
    else // Create an HDR average color GPU buffer to pass to OptiX when appropriate
    {
        mDenoiser.params.hdrIntensity = static_cast<CUdeviceptr>(0);
        mDenoiser.params.hdrAverageColor = mDenoiser.hdrAverageBuffer.getDevicePtr();
    }
}

void YannOptixDenoiser::reallocateStagingBuffers(RenderContext* pRenderContext)
{
    // Allocate buffer for our noisy inputs to the denoiser
    allocateStagingBuffer(pRenderContext, mDenoiser.interop.denoiserInput, mDenoiser.layer.input);

    // Allocate buffer for our denoised outputs from the denoiser
    allocateStagingBuffer(pRenderContext, mDenoiser.interop.denoiserOutput, mDenoiser.layer.output);

    // Allocate a guide buffer for our normals (if necessary)
    if (mDenoiser.options.guideNormal > 0)
        allocateStagingBuffer(pRenderContext, mDenoiser.interop.normal, mDenoiser.guideLayer.normal, OPTIX_PIXEL_FORMAT_FLOAT3);//YANN this is strange
    else
        freeStagingBuffer(mDenoiser.interop.normal, mDenoiser.guideLayer.normal);

    // Allocate a guide buffer for our albedo (if necessary)
    if (mDenoiser.options.guideAlbedo > 0)
        allocateStagingBuffer(pRenderContext, mDenoiser.interop.albedo, mDenoiser.guideLayer.albedo);
    else
        freeStagingBuffer(mDenoiser.interop.albedo, mDenoiser.guideLayer.albedo);

    // Allocate a guide buffer for our motion vectors (if necessary)
    allocateStagingBuffer(pRenderContext, mDenoiser.interop.motionVec, mDenoiser.guideLayer.flow, OPTIX_PIXEL_FORMAT_FLOAT2);
}

void YannOptixDenoiser::allocateStagingBuffer(RenderContext* pRenderContext, Interop& interop, OptixImage2D& image, OptixPixelFormat format)
{
    // Determine what sort of format this buffer should be
    uint32_t elemSize = 4 * sizeof(float);
    ResourceFormat falcorFormat = ResourceFormat::RGBA32Float;
    switch (format)
    {
    case OPTIX_PIXEL_FORMAT_FLOAT4:
        elemSize = 4 * sizeof(float);
        falcorFormat = ResourceFormat::RGBA32Float;
        break;
    case OPTIX_PIXEL_FORMAT_FLOAT3:
        elemSize = 3 * sizeof(float);
        falcorFormat = ResourceFormat::RGBA32Float;
        break;
    case OPTIX_PIXEL_FORMAT_FLOAT2:
        elemSize = 2 * sizeof(float);
        falcorFormat = ResourceFormat::RG32Float;
        break;
    default:
        FALCOR_THROW("OptixDenoiser called allocateStagingBuffer() with unsupported format");
    }

    // If we had an existing buffer in this location, free it.
    if (interop.devicePtr)
        cuda_utils::freeSharedDevicePtr((void*)interop.devicePtr);

    // Create a new DX <-> CUDA shared buffer using the Falcor API to create, then find its CUDA pointer.
    interop.buffer = mpDevice->createTypedBuffer(
        falcorFormat,
        mWidth * mHeight,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess | ResourceBindFlags::RenderTarget | ResourceBindFlags::Shared
    );
    interop.devicePtr = (CUdeviceptr)exportBufferToCudaDevice(interop.buffer);

    // Setup an OptiXImage2D structure so OptiX will used this new buffer for image data
    image.width = mWidth;
    image.height = mHeight;
    image.rowStrideInBytes = mWidth * elemSize;
    image.pixelStrideInBytes = elemSize;
    image.format = format;
    image.data = interop.devicePtr;
}

void YannOptixDenoiser::freeStagingBuffer(Interop& interop, OptixImage2D& image)
{
    // Free the CUDA memory for this buffer, then set our other references to it to NULL to avoid
    // accidentally trying to access the freed memory.
    if (interop.devicePtr)
        cuda_utils::freeSharedDevicePtr((void*)interop.devicePtr);
    interop.buffer = nullptr;
    image.data = static_cast<CUdeviceptr>(0);
}

void YannOptixDenoiser::execute(RenderContext* pRenderContext)
{
    if (mFirstFrame)
    {
        compile(pRenderContext);
        setupDenoiser();
    }

    // Copy input textures to correct format OptiX images / buffers for denoiser inputs
    // Note: if () conditions are somewhat excessive, due to attempts to track down mysterious, hard-to-repo crashes
    convertTexToBuf(pRenderContext, renderData.getTexture(kColorInput), mDenoiser.interop.denoiserInput.buffer, mBufferSize);
    if (mHasAlbedoInput && mDenoiser.options.guideAlbedo)
    {
        convertTexToBuf(pRenderContext, renderData.getTexture(kAlbedoInput), mDenoiser.interop.albedo.buffer, mBufferSize);
    }
    if (mHasNormalInput && mDenoiser.options.guideNormal)
    {
        convertNormalsToBuf(
            pRenderContext,
            renderData.getTexture(kNormalInput),
            mDenoiser.interop.normal.buffer,
            mBufferSize,
            transpose(inverse(mpScene->getCamera()->getViewMatrix()))
        );
    }
    if (mHasMotionInput && mDenoiser.modelKind == OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_TEMPORAL)
    {
        convertMotionVectors(pRenderContext, renderData.getTexture(kMotionInput), mDenoiser.interop.motionVec.buffer, mBufferSize);
    }

    pRenderContext->waitForFalcor();

    // Compute average intensity, if needed
    if (mDenoiser.params.hdrIntensity)
    {
        optixDenoiserComputeIntensity(
            mDenoiser.denoiser,
            nullptr, // CUDA stream
            &mDenoiser.layer.input,
            mDenoiser.params.hdrIntensity,
            mDenoiser.scratchBuffer.getDevicePtr(),
            mDenoiser.scratchBuffer.getSize()
        );
    }

    // Compute average color, if needed
    if (mDenoiser.params.hdrAverageColor)
    {
        optixDenoiserComputeAverageColor(
            mDenoiser.denoiser,
            nullptr, // CUDA stream
            &mDenoiser.layer.input,
            mDenoiser.params.hdrAverageColor,
            mDenoiser.scratchBuffer.getDevicePtr(),
            mDenoiser.scratchBuffer.getSize()
        );
    }

    // On the first frame with a new denoiser, we have no prior input for temporal denoising.
    //    In this case, pass in our current frame as both the current and prior frame.
    if (mIsFirstFrame)
    {
        mDenoiser.layer.previousOutput = mDenoiser.layer.input;
    }

    // Run denoiser
    optixDenoiserInvoke(
        mDenoiser.denoiser,
        nullptr, // CUDA stream
        &mDenoiser.params,
        mDenoiser.stateBuffer.getDevicePtr(),
        mDenoiser.stateBuffer.getSize(),
        &mDenoiser.guideLayer, // Our set of normal / albedo / motion vector guides
        &mDenoiser.layer,      // Array of input or AOV layers (also contains denoised per-layer outputs)
        1u,                    // Nuumber of layers in the above array
        0u,                    // (Tile) Input offset X
        0u,                    // (Tile) Input offset Y
        mDenoiser.scratchBuffer.getDevicePtr(),
        mDenoiser.scratchBuffer.getSize()
    );

    pRenderContext->waitForCuda();

    // Copy denoised output buffer to texture for Falcor to consume
    convertBufToTex(pRenderContext, mDenoiser.interop.denoiserOutput.buffer, renderData.getTexture(kOutput), mBufferSize);

    // Make sure we set the previous frame output to the correct location for future frames.
    // Everything in this if() cluase could happen every frame, but is redundant after the first frame.
    if (mIsFirstFrame)
    {
        // Note: This is a deep copy that can dangerously point to deallocated memory when resetting denoiser settings.
        // This is (partly) why in the first frame, the layer.previousOutput is set to layer.input, above.
        mDenoiser.layer.previousOutput = mDenoiser.layer.output;

        // We're no longer in the first frame of denoising; no special processing needed now.
        mIsFirstFrame = false;
    }
}

// Basically a wrapper to handle null Falcor Buffers gracefully, which couldn't
// happen in getShareDevicePtr(), due to the bootstrapping that avoids namespace conflicts
void* OptixDenoiser_::exportBufferToCudaDevice(ref<Buffer>& buf)
{
    if (buf == nullptr)
        return nullptr;
    return cuda_utils::getSharedDevicePtr(buf->getDevice()->getType(), buf->getSharedApiHandle(), (uint32_t)buf->getSize());
}

void OptixDenoiser_::setupDenoiser()
{
    // Destroy the denoiser, if it already exists
    if (mDenoiser.denoiser)
    {
        optixDenoiserDestroy(mDenoiser.denoiser);
    }

    // Create the denoiser
    optixDenoiserCreate(mOptixContext, mDenoiser.modelKind, &mDenoiser.options, &mDenoiser.denoiser);

    // Find out how much memory is needed for the requested denoiser
    optixDenoiserComputeMemoryResources(mDenoiser.denoiser, mDenoiser.tileWidth, mDenoiser.tileHeight, &mDenoiser.sizes);

    // Allocate/resize some temporary CUDA buffers for internal OptiX processing/state
    mDenoiser.scratchBuffer.resize(mDenoiser.sizes.withoutOverlapScratchSizeInBytes);
    mDenoiser.stateBuffer.resize(mDenoiser.sizes.stateSizeInBytes);

    // Finish setup of the denoiser
    optixDenoiserSetup(
        mDenoiser.denoiser,
        nullptr,
        mDenoiser.tileWidth + 2 * mDenoiser.tileOverlap,  // Should work with tiling if parameters set appropriately
        mDenoiser.tileHeight + 2 * mDenoiser.tileOverlap, // Should work with tiling if parameters set appropriately
        mDenoiser.stateBuffer.getDevicePtr(),
        mDenoiser.stateBuffer.getSize(),
        mDenoiser.scratchBuffer.getDevicePtr(),
        mDenoiser.scratchBuffer.getSize()
    );
}

void OptixDenoiser_::convertMotionVectors(RenderContext* pRenderContext, const ref<Texture>& tex, const ref<Buffer>& buf, const uint2& size)
{
    auto var = mpConvertMotionVectors->getRootVar();
    var["GlobalCB"]["gStride"] = size.x;
    var["GlobalCB"]["gSize"] = size;
    var["gInTex"] = tex;
    var["gOutBuf"] = buf;
    mpConvertMotionVectors->execute(pRenderContext, size.x, size.y);
}

void OptixDenoiser_::convertTexToBuf(RenderContext* pRenderContext, const ref<Texture>& tex, const ref<Buffer>& buf, const uint2& size)
{
    auto var = mpConvertTexToBuf->getRootVar();
    var["GlobalCB"]["gStride"] = size.x;
    var["gInTex"] = tex;
    var["gOutBuf"] = buf;
    mpConvertTexToBuf->execute(pRenderContext, size.x, size.y);
}

void OptixDenoiser_::convertNormalsToBuf(
    RenderContext* pRenderContext,
    const ref<Texture>& tex,
    const ref<Buffer>& buf,
    const uint2& size,
    float4x4 viewIT
)
{
    auto var = mpConvertNormalsToBuf->getRootVar();
    var["GlobalCB"]["gStride"] = size.x;
    var["GlobalCB"]["gViewIT"] = viewIT;
    var["gInTex"] = tex;
    var["gOutBuf"] = buf;
    mpConvertTexToBuf->execute(pRenderContext, size.x, size.y);
}

void OptixDenoiser_::convertBufToTex(RenderContext* pRenderContext, const ref<Buffer>& buf, const ref<Texture>& tex, const uint2& size)
{
    auto var = mpConvertBufToTex->getRootVar();
    var["GlobalCB"]["gStride"] = size.x;
    var["gInBuf"] = buf;
    mpFbo->attachColorTarget(tex, 0);
    mpConvertBufToTex->execute(pRenderContext, mpFbo);
}
