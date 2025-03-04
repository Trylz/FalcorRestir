add_falcor_executable(Restir)

target_sources(Restir PRIVATE

    YannCudaUtils.h
    YannCudaRuntime.h
	DenoisingPass.h
    GBuffer.h
    LightManager.h
    YannOptixDenoiser.h
    OptixUtils.h
    ReservoirManager.h
    RestirApp.h
    RISPass.h
    SceneName.h
    SceneSettings.h
    Singleton.h
    ShadingPass.h
    TemporalFilteringPass.h    
    VisibilityPass.h

    YannCudaUtils.cpp
	DenoisingPass.cpp
    FloatRandomNumberGenerator.h
    GBuffer.cpp
    LightManager.cpp
    YannOptixDenoiser.cpp
    OptixUtils.cpp
    ReservoirManager.cpp
	RestirApp.cpp
	RISPass.cpp
    ShadingPass.cpp
	TemporalFilteringPass.cpp
	VisibilityPass.cpp

    ConvertBufToTex.slang
    ConvertMotionVectorInputs.slang
    ConvertNormalsToBuf.slang
    ConvertTexToBuf.slang

    DenoisingPass_PackNRD.slang
    DenoisingPass_UnpackNRD.slang

    GBuffer.slang
    RISPass.slang
    ShadingPass.slang
    TemporalFilteringPass.slang
    VisibilityPass.slang

    BRDF.slangh
    Light.slangh
    Reservoir.slangh
)

target_copy_shaders(Restir Samples/Restir)

target_source_group(Restir "Samples")