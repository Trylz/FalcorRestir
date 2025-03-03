add_falcor_executable(Restir)

target_sources(Restir PRIVATE
    CudaUtils.h
    DenoisingPass.h
    GBuffer.h
    LightManager.h
    ReservoirManager.h
    RestirApp.h
    RISPass.h
    SceneName.h
    SceneSettings.h
    Singleton.h
    ShadingPass.h
    TemporalFilteringPass.h    
    VisibilityPass.h

    CudaUtils.cpp
	DenoisingPass.cpp
    FloatRandomNumberGenerator.h
    GBuffer.cpp
    LightManager.cpp
    ReservoirManager.cpp
	RestirApp.cpp
	RISPass.cpp
    ShadingPass.cpp
	TemporalFilteringPass.cpp
	VisibilityPass.cpp

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
