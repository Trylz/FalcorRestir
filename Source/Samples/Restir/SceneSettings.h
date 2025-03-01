#pragma once

#include "Singleton.h"

namespace Restir
{
struct SceneSettings
{
    uint32_t RISSamplesCount = 32u;
    uint32_t nbReservoirPerPixel = 4u;
    float temporalWsRadiusThreshold = 999999999.0f;
    float temporalNormalThreshold = 0.12f;
};

using SceneSettingsSingleton = Singleton<SceneSettings>;
} // namespace Restir
