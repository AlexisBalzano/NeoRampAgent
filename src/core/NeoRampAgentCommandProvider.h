#pragma once
#include "NeoRampAgent.h"

using namespace PluginSDK;

namespace rampAgent {

class NeoRampAgent;

class NeoRampAgentCommandProvider : public PluginSDK::Chat::CommandProvider
{
public:
    NeoRampAgentCommandProvider(rampAgent::NeoRampAgent *neoRampAgent, PluginSDK::Logger::LoggerAPI *logger, Chat::ChatAPI *chatAPI, Fsd::FsdAPI *fsdAPI)
            : neoRampAgent_(neoRampAgent), logger_(logger), chatAPI_(chatAPI), fsdAPI_(fsdAPI) {}
		
	Chat::CommandResult Execute(const std::string &commandId, const std::vector<std::string> &args) override;

private:
    Logger::LoggerAPI *logger_ = nullptr;
    Chat::ChatAPI *chatAPI_ = nullptr;
    Fsd::FsdAPI *fsdAPI_ = nullptr;
    NeoRampAgent *neoRampAgent_ = nullptr;
};
}  // namespace rampAgent