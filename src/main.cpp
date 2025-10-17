#include "NeoRampAgent.h"

extern "C" PLUGIN_API PluginSDK::BasePlugin *CreatePluginInstance()
{
    try
    {
        return new ramp::NeoRampAgent();
    }
    catch (const std::exception &e)
    {
        return nullptr;
    }
}