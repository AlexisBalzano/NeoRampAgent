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

    if (isController_ == false || isConnected_ == false) {
        logger_->warning("Ignoring manual stand assignment - not connected as controller.");
        return;
	}



   	std::string standName;
    if (event->componentId == "ENTERED") {
		standName = event->userInput.value_or("");
		std::transform(standName.begin(), standName.end(), standName.begin(), ::toupper);
    }
    else {
		standName = event->componentId;
    }

	logger_->info("Trying to manually assign: " + standName + " to: " + event->callsign);
    
	std::optional<Flightplan::Flightplan> fpOpt = flightplanAPI_->getByCallsign(event->callsign);
	if (!fpOpt) {
		logger_->error("No flightplan found for " + event->callsign + " during manual stand assignment.");
		return;
	}
	std::string icao = fpOpt->destination;

	std::string token = generateToken(callsign_);

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient cli(apiUrl_);
    httplib::Headers headers = { {"User-Agent", "NeoRampAgent"} };
    std::string apiEndpoint = "/rampagent/api/assign?stand=" + standName + "&icao=" + icao + "&callsign=" + event->callsign + "&token=" + token + "&client=" + callsign_;

    auto res = cli.Get(apiEndpoint.c_str(), headers);

    if (!res || !(res->status >= 200 && res->status < 300)) {
        logger_->error("Failed to send manual assign to NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0));
        return;
    }
    else { // assignement processed, check response to see if successful and update tag item if so
        if (!res->body.empty()) {
			std::lock_guard<std::mutex> lock(occupiedStandstMutex_);
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
    if (isController_ == false || isConnected_ == false) {
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
#else
    logger_->error("Cannot update stand menu - HTTP client not supported (OpenSSL required).");
    return;
#endif // CPPHTTPLIB_OPENSSL_SUPPORT

    // If standsJson is missing or not an object, publish a minimal dropdown and return safely
    if (!standsJson.is_object() || standsJson.empty() || occupiedStands.empty()) {
        PluginSDK::Tag::DropdownDefinition dropdownDef;
        dropdownDef.title = "STAND";
        dropdownDef.width = 75;
        dropdownDef.maxHeight = 200;

        // Divider
        PluginSDK::Tag::DropdownComponent divider;
        divider.id = "DIVIDER";
        divider.type = PluginSDK::Tag::DropdownComponentType::Divider;

        PluginSDK::Tag::DropdownComponent dropdownComponent;
        PluginSDK::Tag::DropdownComponentStyle style;
        style.textAlign = PluginSDK::Tag::DropdownAlignmentType::Center;

        dropdownComponent.id = "None";
        dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::Button;
        dropdownComponent.text = "None";
        dropdownComponent.requiresInput = false;
        dropdownComponent.style = style;
        dropdownDef.components.push_back(dropdownComponent);

        dropdownDef.components.push_back(divider);

        // Manual entry
        dropdownComponent.id = "ENTERED";
        dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::InputArea;
        dropdownComponent.text = "Enter";
        dropdownComponent.requiresInput = true;
        style.border = true;
        style.backgroundColor = std::array<unsigned int, 3>{ 47, 53, 57 }; // Darker grey
        dropdownComponent.style = style;
        dropdownDef.components.push_back(dropdownComponent);

        tagInterface_->UpdateActionDropdown(standMenuId_, dropdownDef);

        if (printError) {
            printError = false; // avoid spamming logs
            logger_->error("No stands data received from NeoRampAgent server for airport " + icao);
        }
        return;
    }

    std::vector<Stand> availableStands;

    const auto itAssigned = occupiedStands.find("assignedStands");
    const auto itOccupied = occupiedStands.find("occupiedStands");
    const auto itBlocked  = occupiedStands.find("blockedStands");

    for (auto& [standName, standData] : standsJson.items()) {
        Stand stand;
        stand.name = standName;
        stand.occupied = false;

        // Assigned
        if (itAssigned != occupiedStands.end() && itAssigned->is_array()) {
            for (const auto& occ : *itAssigned) {
                const std::string occName = occ.value("name", "");
                if (!occName.empty() && occName == stand.name) { stand.occupied = true; break; }
            }
        }
        if (stand.occupied) { continue; }

        // Currently occupied
        if (itOccupied != occupiedStands.end() && itOccupied->is_array()) {
            for (const auto& occ : *itOccupied) {
                const std::string occName = occ.value("name", "");
                if (!occName.empty() && occName == stand.name) { stand.occupied = true; break; }
            }
        }
        if (stand.occupied) { continue; }

        // Blocked
        if (itBlocked != occupiedStands.end() && itBlocked->is_array()) {
            for (const auto& occ : *itBlocked) {
                const std::string occName = occ.value("name", "");
                if (!occName.empty() && occName == stand.name) { stand.occupied = true; break; }
            }
        }

        if (!stand.occupied) {
            availableStands.push_back(stand);
        }
    }

    // Sort stands alphabetically -> 2A,2B, 3A,3B,...
    sortStandList(availableStands);

    PluginSDK::Tag::DropdownDefinition dropdownDef;
    dropdownDef.title = "STAND";
    dropdownDef.width = 75;
    dropdownDef.maxHeight = 200;

    PluginSDK::Tag::DropdownComponent dropdownComponent;
    PluginSDK::Tag::DropdownComponentStyle style;
    style.textAlign = PluginSDK::Tag::DropdownAlignmentType::Center;

    // Divider
    PluginSDK::Tag::DropdownComponent divider;
    divider.id = "DIVIDER";
    PluginSDK::Tag::DropdownComponentStyle dividerStyle;
    dividerStyle.height = 10;
    divider.style = dividerStyle;
    divider.type = PluginSDK::Tag::DropdownComponentType::Divider;

    // "None" button
    dropdownComponent.id = "None";
    dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::Button;
    dropdownComponent.text = "None";
    dropdownComponent.requiresInput = false;
    dropdownComponent.style = style;
    dropdownDef.components.push_back(dropdownComponent);
    dropdownDef.components.push_back(divider);

    // Scroll area with available stands
    PluginSDK::Tag::DropdownComponent scrollArea;
    scrollArea.id = "SCROLL";
    scrollArea.type = PluginSDK::Tag::DropdownComponentType::ScrollArea;

    for (const auto& stand : availableStands) {
        dropdownComponent.id = stand.name;
        dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::Button;
        dropdownComponent.text = stand.name;
        dropdownComponent.requiresInput = false;
        dropdownComponent.style = style;
        scrollArea.children.push_back(dropdownComponent);
    }

    dropdownDef.components.push_back(scrollArea);
    dropdownDef.components.push_back(divider);

    // Manual entry
    dropdownComponent.id = "ENTERED";
    dropdownComponent.type = PluginSDK::Tag::DropdownComponentType::InputArea;
    dropdownComponent.text = "Enter";
    dropdownComponent.requiresInput = true;
    style.border = true;
    style.backgroundColor = std::array<unsigned int, 3>{ 47, 53, 57 }; // Darker grey
    dropdownComponent.style = style;
    dropdownDef.components.push_back(dropdownComponent);

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

    std::lock_guard<std::mutex> lock(occupiedStandstMutex_);
    updateStandMenuButtons(fpOpt->destination, lastOccupiedStands_);
    return true;
}

}  // namespace rampAgent