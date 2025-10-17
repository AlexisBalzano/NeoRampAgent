#include "NeoRampAgent.h"

extern "C" PLUGIN_API PluginSDK::BasePlugin *CreatePluginInstance()
{
    try
    {
        return new rampAgent::NeoRampAgent();
    }
    catch (const std::exception &e)
    {
        return nullptr;
    }
}