#pragma once
#include "NeoRampAgent.h"

namespace rampAgent {
void NeoRampAgent::RegisterTagActions()
{
    // Dropdown Menu def
    PluginSDK::Tag::TagActionDefinition tagDef;
    tagDef.name = "StandMenu";
    tagDef.description = "open the stand menu";
    tagDef.requiresInput = false;
    standMenuId_ = tagInterface_->RegisterTagAction(tagDef);

    PluginSDK::Tag::DropdownDefinition dropdownDef;
    dropdownDef.title = "STAND";
    dropdownDef.width = 75;
    dropdownDef.maxHeight = 150;

    PluginSDK::Tag::DropdownComponent dropdownComponent;
    PluginSDK::Tag::DropdownComponentStyle style;

    style.textAlign = PluginSDK::Tag::DropdownAlignmentType::Center;

    dropdownComponent.id = "None";
    dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::Button;
    dropdownComponent.text = "None";
    dropdownComponent.requiresInput = false;
    dropdownComponent.style = style;
    dropdownDef.components.push_back(dropdownComponent);

    tagInterface_->SetActionDropdown(standMenuId_, dropdownDef);
}

void NeoRampAgent::OnTagAction(const PluginSDK::Tag::TagActionEvent *event)
{
    if (!initialized_ || !event )
    {
        return;
    }

    std::string input;
	if (event->userInput) input = event->userInput.value();
    TagProcessing(event->callsign, event->actionId, input);
}

void NeoRampAgent::OnTagDropdownAction(const PluginSDK::Tag::DropdownActionEvent *event)
{
    if (!initialized_ || !event)
    {
        return;
    }

    if (canSendReport_ == false || isConnected_ == false) {
        logger_->warning("Ignoring manual stand assignment - not connected as controller.");
        return;
	}

	logger_->info("Trying to manually assign: " + event->componentId + " to: " + event->callsign);


   	std::string standName = event->componentId;
    
	std::optional<Flightplan::Flightplan> fpOpt = flightplanAPI_->getByCallsign(event->callsign);
	if (!fpOpt) {
		logger_->error("No flightplan found for " + event->callsign + " during manual stand assignment.");
		return;
	}
	std::string icao = fpOpt->destination;

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient cli(apiUrl_);
    httplib::Headers headers = { {"User-Agent", "NeoRampAgent"} };
    std::string apiEndpoint = "/rampagent/api/assign?stand=" + standName + "&icao=" + icao + "&callsign=" + event->callsign;

    auto res = cli.Get(apiEndpoint.c_str(), headers);

    if (!res || !(res->status >= 200 && res->status < 300)) {
        logger_->error("Failed to send manual assign to NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0));
        return;
    }
    else { // assignement processed, check response to see if successful and update tag item if so
        if (!res->body.empty()) {
            nlohmann::ordered_json dataJson = nlohmann::ordered_json::parse(res->body);
			if (!dataJson.contains("message")) return; // malformed response
            if (dataJson["message"]["action"].get<std::string>() == "assign") {
                logger_->info("Manual stand assignment successful: " + standName + " to " + event->callsign);
				lastStandTagMap_[event->callsign] = standName;
				UpdateTagItems(event->callsign, WHITE, standName);
                return;
            }
            else if (dataJson["message"]["action"].get<std::string>() == "free") {
                logger_->info("Freed stand assignment for: " + event->callsign);
				lastStandTagMap_.erase(event->callsign);
				UpdateTagItems(event->callsign, WHITE, "");
                return;
            }
            else {
				logger_->info("Manual stand rejected: " + dataJson["message"]["message"].get<std::string>());
                DisplayMessage("Manual stand rejected: " + dataJson["message"]["message"].get<std::string>());
                return;
            }
        }
    }
#endif // CPPHTTPLIB_OPENSSL_SUPPORT
	DisplayMessage("Manual stand assignment failed for " + event->callsign + " to " + standName, "");
}

void NeoRampAgent::TagProcessing(const std::string &callsign, const std::string &actionId, const std::string &userInput)
{
}

inline void NeoRampAgent::updateStandMenuButtons(const std::string& icao, const nlohmann::ordered_json& occupiedStands)
{
    if (canSendReport_ == false || isConnected_ == false) {
		return;
	}
	nlohmann::ordered_json standsJson = nlohmann::ordered_json::object();

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient cli(apiUrl_);
    httplib::Headers headers = { {"User-Agent", "NeoRampAgent"} };
    std::string apiEndpoint = "/rampagent/api/airports/" + icao + "/stands";

    auto res = cli.Get(apiEndpoint.c_str(), headers);

    if (res && res->status >= 200 && res->status < 300) {
        if (!printError) {
            printError = true; // reset error printing flag on success
            DisplayMessage("Successfully reconnected to NeoRampAgent server.", "");
            logger_->info("Successfully reconnected to NeoRampAgent server.");
        }
        try {
            if (!res->body.empty()) standsJson = nlohmann::ordered_json::parse(res->body);
        }
        catch (const std::exception& e) {
            logger_->error("Failed to parse stands data from NeoRampAgent server: " + std::string(e.what()));
        }
    }
    else {
        if (printError) {
            printError = false; // avoid spamming logs
            logger_->error("Failed to get stands information from NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0));
		}
    }

    if (standsJson.empty()) {
        if (printError) {
            printError = false; // avoid spamming logs
            logger_->error("No stands data received from NeoRampAgent server for airport " + icao);
        }
	}
#else
    logger_->error("Cannot update stand menu - HTTP client not supported (OpenSSL required).");
	return;
#endif // CPPHTTPLIB_OPENSSL_SUPPORT

	// deduct available stands list from all stands + occupied stands + blocked stands
    std::vector<Stand> availableStands;
    for (auto& [standName, standData] : standsJson.items()) {
        Stand stand;
        stand.name = standName;
        stand.occupied = false;

        // Check if stand is already Assigned
        for (const auto& occupied : occupiedStands["assignedStands"]) {
            if (occupied["name"].get<std::string>() == stand.name) {
                stand.occupied = true;
                break;
            }
        }
        // Check if stand is already occupied
        for (const auto& occupied : occupiedStands["occupiedStands"]) {
            if (occupied["name"].get<std::string>() == stand.name) {
                stand.occupied = true;
                break;
            }
        }
		// Check if stand is blocked
        for (const auto& occupied : occupiedStands["blockedStands"]) {
            if (occupied["name"].get<std::string>() == stand.name) {
                stand.occupied = true;
                break;
            }
        }
        if (!stand.occupied) {
            availableStands.push_back(stand);
        }
	}

	//Sort stands alphabetically -> 2A,2B, 3A,3B,...
	sortStandList(availableStands);

    PluginSDK::Tag::DropdownDefinition dropdownDef;
    dropdownDef.title = "STAND";
    dropdownDef.width = 75;
    dropdownDef.maxHeight = 150;

    PluginSDK::Tag::DropdownComponent scrollArea;
    scrollArea.id = "SCROLL";
    scrollArea.type = PluginSDK::Tag::DropdownComponentType::ScrollArea;


    PluginSDK::Tag::DropdownComponent dropdownComponent;
    PluginSDK::Tag::DropdownComponentStyle style;

    style.textAlign = PluginSDK::Tag::DropdownAlignmentType::Center;

    dropdownComponent.id = "None";
    dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::Button;
    dropdownComponent.text = "None";
    dropdownComponent.requiresInput = false;
    dropdownComponent.style = style;
    scrollArea.children.push_back(dropdownComponent);


    for (const auto& stand : availableStands) {
        dropdownComponent.id = stand.name;
        dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::Button;
        dropdownComponent.text = stand.name;
        dropdownComponent.requiresInput = false;
        dropdownComponent.style = style;
        scrollArea.children.push_back(dropdownComponent);
    }

    dropdownDef.components.push_back(scrollArea);

    tagInterface_->UpdateActionDropdown(standMenuId_, dropdownDef);
}

bool NeoRampAgent::OnTagShowDropdown(const std::string& actionId, const std::string& callsign)
{
    if (!initialized_) return false;
    if (actionId != standMenuId_) return false;

    std::optional<Flightplan::Flightplan> fpOpt = flightplanAPI_->getByCallsign(callsign);
    if (!fpOpt) {
        logger_->error("No flightplan found for " + callsign + " during stand menu update.");
        return false;
    }

    std::lock_guard<std::mutex> lock(reportMutex_);
    updateStandMenuButtons(fpOpt->destination, lastOccupiedStands_);
    return true;
}

}  // namespace rampAgent