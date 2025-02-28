#include "LightManager.h"
#include "FloatRandomNumberGenerator.h"

namespace Restir
{
float luma(Falcor::float3 v)
{
    return 0.2126f * v.r + 0.7152f * v.g + 0.0722f * v.b;
}

LightManager::LightManager() {}

void LightManager::init(Falcor::ref<Falcor::Device> pDevice, Falcor::ref<Falcor::Scene> pScene, bool isSanMiguel)
{
    //------------------------------------------------------------------------------------------------------------
    //	Create random lights
    //------------------------------------------------------------------------------------------------------------
    if (isSanMiguel)
    {
        createSanMiguelSceneLights(pScene);
    }
    else
    {
        createArcadeSceneLights(pScene);
    }

    //------------------------------------------------------------------------------------------------------------
    //	Compute light probabilities
    //------------------------------------------------------------------------------------------------------------
    mLightProbabilities.reserve(mLights.size());

    float totalWeight = 0.0f;
    for (const Light& light : mLights)
    {
        const float lightLuma = luma(light.mColor);

        mLightProbabilities.push_back(lightLuma);
        totalWeight += lightLuma;
    }

    for (float& prob : mLightProbabilities)
        prob /= totalWeight;

    //------------------------------------------------------------------------------------------------------------
    //	Create lights gpu buffer
    //------------------------------------------------------------------------------------------------------------

    mGpuLightBuffer = pDevice->createStructuredBuffer(
        sizeof(Light), mLights.size(), Falcor::ResourceBindFlags::ShaderResource, Falcor::MemoryType::DeviceLocal, mLights.data(), false
    );

    //------------------------------------------------------------------------------------------------------------
    //	Create light probalities gpu buffer
    //------------------------------------------------------------------------------------------------------------

    mGpuLightProbabilityBuffer = pDevice->createStructuredBuffer(
        sizeof(float),
        mLightProbabilities.size(),
        Falcor::ResourceBindFlags::ShaderResource,
        Falcor::MemoryType::DeviceLocal,
        mLightProbabilities.data(),
        false
    );
}


void LightManager::createSanMiguelSceneLights(Falcor::ref<Falcor::Scene> pScene)
{
    Light light;

    // Init light.
    light.mRadius = 0.0001f;
    light.mfallOff = std::min((light.mRadius * light.mRadius) * std::exp(1.0f / 0.0001f), 1.0f);

    // Color
    light.mColor = Falcor::float3(1.0f, 0.0f, 0.0f) * 5000.0f;

    // Position
    light.mWsPosition = pScene->getCamera()->getPosition();

    mLights.push_back(light);
}

void LightManager::createArcadeSceneLights(Falcor::ref<Falcor::Scene> pScene)
{
    static const uint32_t nbLights = 2u;

    const Falcor::AABB& aabb = pScene->getSceneBounds();

    const float epsilon = aabb.radius() / 10.0f;

    const Falcor::float3 minPoint = aabb.minPoint + (aabb.maxPoint - aabb.minPoint) * epsilon;
    const Falcor::float3 maxPoint = aabb.maxPoint + (aabb.minPoint - aabb.maxPoint) * epsilon;
    const Falcor::float3 extent = maxPoint - minPoint;

    FloatRandomNumberGenerator rng(333);

    for (uint32_t i = 0u; i < nbLights; ++i)
    {
        Light light;

        // Init light.
        light.mRadius = 0.0001f;
        light.mfallOff = std::min((light.mRadius * light.mRadius) * std::exp(1.0f / 0.0001f), 1.0f);

        // Color
        light.mColor =
            Falcor::float3(rng.generateUnsignedNormalized(), rng.generateUnsignedNormalized(), rng.generateUnsignedNormalized()) * 50.0f /
            (float)nbLights;

        // Position
        const float dx = rng.generateUnsignedNormalized();
        const float dy = rng.generateUnsignedNormalized();
        const float dz = rng.generateUnsignedNormalized();
        light.mWsPosition = minPoint + extent * Falcor::float3(dx, dy, dz);

        mLights.push_back(light);
    }
}

} // namespace Restir
