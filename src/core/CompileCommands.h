#include <algorithm>
#include <string>

#include "NeoRampAgent.h"


namespace rampAgent {
void NeoRampAgent::RegisterCommand() {
    try
    {
        CommandProvider_ = std::make_shared<NeoRampAgentCommandProvider>(this, logger_, chatAPI_, fsdAPI_);

        PluginSDK::Chat::CommandDefinition definition;
        definition.name = "COMMAND_NAME";
        definition.description = "COMMAND_DESCRIPTION";
        definition.lastParameterHasSpaces = false;
		definition.parameters.clear();

        commandId_ = chatAPI_->registerCommand(definition.name, definition, CommandProvider_);
    }
    catch (const std::exception& ex)
    {
        logger_->error("Error registering command: " + std::string(ex.what()));
    }
}

inline void NeoRampAgent::unegisterCommand()
{
    if (CommandProvider_)
    {
        chatAPI_->unregisterCommand(commandId_);
        CommandProvider_.reset();
	}
}

Chat::CommandResult NeoRampAgentCommandProvider::Execute( const std::string &commandId, const std::vector<std::string> &args)
{
    if (commandId == neoRampAgent_->commandId_)
    {
        return { true, std::nullopt };
	}
    else {
		std::string error = "Unknown command ID: " + commandId;
        return { false, error };
    }

	return { true, std::nullopt };
}
}  // namespace rampAgent