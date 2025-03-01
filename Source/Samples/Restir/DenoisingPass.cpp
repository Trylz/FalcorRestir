#include "DenoisingPass.h"
#include "Dependencies/NvidiaNRD/Integration/NRDIntegration.hpp"

#if defined(_DEBUG)
    #pragma comment(lib, __FILE__ "\\Dependencies\\NvidiaNRD\\lib\\Debug\\NRD.lib")
#else
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRD\\lib\\Release\\NRD.lib")
#endif


#if defined(_DEBUG)
    #pragma comment(lib, __FILE__ "\\Dependencies\\NvidiaNRI\\lib\\Debug\\NRI.lib")
#else
    #pragma comment(lib, __FILE__ "\\..\\Dependencies\\NvidiaNRI\\lib\\Release\\NRI.lib")
#endif

namespace Restir
{
DenoisingPass::DenoisingPass(Falcor::ref<Falcor::Device> pDevice, Falcor::ref<Falcor::Scene> pScene)
 : mpDevice(pDevice), mpScene(pScene)
{
    pDevice->getGfxCommandQueue()->
}
}
