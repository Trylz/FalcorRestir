#pragma once

#include "Singleton.h"

namespace Restir
{
struct Light
{
    Falcor::float3 mWsPosition;
    Falcor::float3 mColor;
    float mRadius;
    float mfallOff;
};

struct LightManager
{
    LightManager();
    void init(Falcor::ref<Falcor::Device> pDevice, Falcor::ref<Falcor::Scene> pScene, bool isSanMiguel);

    inline const std::vector<Light>& getLights() const { return mLights; }
    inline const Falcor::ref<Falcor::Buffer>& getLightGpuBuffer() const { return mGpuLightBuffer; }

    inline const std::vector<float>& getLightProbabilities() const { return mLightProbabilities; }
    inline const Falcor::ref<Falcor::Buffer>& getLightProbabilitiesGpuBuffer() const { return mGpuLightProbabilityBuffer; }

private:
    void createArcadeSceneLights(Falcor::ref<Falcor::Scene> pScene);
    void createSanMiguelSceneLights(Falcor::ref<Falcor::Scene> pScene);

    void spawnRandomSanMiguelLights(
        const Falcor::float3 controlPoint,
        const std::vector<Falcor::float3>& extents,
        FloatRandomNumberGenerator& rng
    );

    std::vector<Light> mLights;
    Falcor::ref<Falcor::Buffer> mGpuLightBuffer;

    std::vector<float> mLightProbabilities;
    Falcor::ref<Falcor::Buffer> mGpuLightProbabilityBuffer;
};

using LightManagerSingleton = Singleton<LightManager>;
} // namespace Restir
