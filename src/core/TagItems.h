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
    tagDef.defaultValue = "";
    std::string tagID = tagInterface_->RegisterTagItem(tagDef);
    standTagId_ = tagID;

    tagDef.name = "REMARK";
    tagDef.defaultValue = "";
    tagID = tagInterface_->RegisterTagItem(tagDef);
    remarkTagId_ = tagID;
}


// TAG ITEM UPDATE FUNCTIONS
void NeoRampAgent::UpdateTagItems(std::string callsign, Colour colour, std::string standName, std::string remark) {
    Tag::TagContext tagContext;
	tagContext.callsign = callsign;
    tagContext.colour = colour;
    tagInterface_->UpdateTagValue(standTagId_, standName, tagContext);
    tagInterface_->UpdateTagValue(remarkTagId_, remark, tagContext);
}
}  // namespace rampAgent
