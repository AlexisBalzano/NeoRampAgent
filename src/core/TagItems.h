#pragma once
#include <chrono>
#include <format>
#include <string>

#include "NeoRampAgent.h"

#ifdef DEV
#define LOG_DEBUG(loglevel, message) logger_->log(loglevel, message)
#else
#define LOG_DEBUG(loglevel, message) void(0)
#endif

namespace rampAgent {
void NeoRampAgent::RegisterTagItems()
{
    PluginSDK::Tag::TagItemDefinition tagDef;

    // Tag item def
    tagDef.name = "STAND";
    tagDef.defaultValue = "N/A";
    std::string tagID = tagInterface_->RegisterTagItem(tagDef);
    standTagId_ = tagID;
}


// TAG ITEM UPDATE FUNCTIONS
void NeoRampAgent::UpdateTagItems(std::string callsign) {
    Tag::TagContext tagContext;
	tagContext.callsign = callsign;
	std::string standName = "N/A"; //FIXME: Retrieve stand name
    tagInterface_->UpdateTagValue(standTagId_, standName, tagContext);
}

// Update all tag items for all pilots
void NeoRampAgent::UpdateTagItems() {
}
}  // namespace rampAgent
