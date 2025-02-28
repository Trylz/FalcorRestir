#include "LightManager.h"

namespace Restir
{
float luma(Falcor::float3 v)
{
    return 0.2126f * v.r + 0.7152f * v.g + 0.0722f * v.b;
}

LightManager::LightManager() {}

void LightManager::init(Falcor::ref<Falcor::Device> pDevice, Falcor::ref<Falcor::Scene> pScene, SceneName sceneName)
{
    //------------------------------------------------------------------------------------------------------------
    //	Create random lights
    //------------------------------------------------------------------------------------------------------------
    if (sceneName == SceneName::SanMiguel)
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

void LightManager::spawnRandomSanMiguelLights(
    const Falcor::float3 controlPoint,
    const std::vector<Falcor::float3>& extents,
    FloatRandomNumberGenerator& rng
)
{
    const uint32_t nbightsPerAxis = 4;

    for (auto& ext : extents)
    {
        const Falcor::float3 vec = ext - controlPoint;
        const Falcor::float3 delta = vec / (float)nbightsPerAxis;
        
        for (uint32_t i = 0u; i <  nbightsPerAxis; ++i)
        {
            Light light;
            light.mRadius = 0.0001f;
            light.mfallOff = std::min((light.mRadius * light.mRadius) * std::exp(1.0f / 0.0001f), 1.0f);

            light.mColor =
                Falcor::float3(rng.generateUnsignedNormalized(), rng.generateUnsignedNormalized(), rng.generateUnsignedNormalized()) *
                4000.0f;
            light.mWsPosition = controlPoint + (float)i * delta;

            mLights.push_back(light);
        }
    }
}

void LightManager::createSanMiguelSceneLights(Falcor::ref<Falcor::Scene> pScene)
{
    const Falcor::ref<Falcor::Camera>& camera = pScene->getCamera();

    FloatRandomNumberGenerator rng(444);

    {
        const Falcor::float3 controlPoint = camera->getPosition();

        std::vector<Falcor::float3> extents;
        extents.push_back(Falcor::float3(17.82f, 7.18f, -4.31f));
        extents.push_back(Falcor::float3(33.87f, 7.40f, 8.33f));

        spawnRandomSanMiguelLights(controlPoint, extents, rng);
    }

    {
        const Falcor::float3 controlPoint = Falcor::float3(34.8199f, 2.59f, -4.46f);

        std::vector<Falcor::float3> extents;
        extents.push_back(Falcor::float3(33.56f, 5.13f, 7.84141f));
        extents.push_back(Falcor::float3(18.1943f, 2.54f, -4.58f));
        extents.push_back(Falcor::float3(16.9297f, 2.932f, 7.918f));

        spawnRandomSanMiguelLights(controlPoint, extents, rng);
    }

    {
        const Falcor::float3 controlPoint = Falcor::float3(29.4029f, 9.094f, 8.977f);

        std::vector<Falcor::float3> extents;
        extents.push_back(Falcor::float3(17.47f, 8.95f, 9.00f));

        spawnRandomSanMiguelLights(controlPoint, extents, rng);
    }

    {
        const Falcor::float3 controlPoint = Falcor::float3(31.88f, 1.70f, 0.422f);

        std::vector<Falcor::float3> extents;
        extents.push_back(Falcor::float3(23.94f, 2.455f, 6.061f));
        extents.push_back(Falcor::float3(24.2696f, 2.105f, 0.277f));

        spawnRandomSanMiguelLights(controlPoint, extents, rng);
    }

    {
        const Falcor::float3 controlPoint = Falcor::float3(29.8295f, 7.587f, 2.3366f);

        std::vector<Falcor::float3> extents;
        extents.push_back(Falcor::float3(20.0454f, 7.96142f, 2.116f));

        spawnRandomSanMiguelLights(controlPoint, extents, rng);
    }

    {
        const Falcor::float3 controlPoint = Falcor::float3(23.5534f, 7.49673f, -0.798f);

        std::vector<Falcor::float3> extents;
        extents.push_back(Falcor::float3(7.0799f, 8.14723f, -1.304f));

        spawnRandomSanMiguelLights(controlPoint, extents, rng);
    }
}

} // namespace Restir
