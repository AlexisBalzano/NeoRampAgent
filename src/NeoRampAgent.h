#pragma once
#include <memory>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>


#include "NeoRadarSDK/SDK.h"
#include "core/NeoRampAgentCommandProvider.h"

constexpr const char* NEORAMPAGENT_VERSION = "v0.0.1";

using namespace PluginSDK;

namespace rampAgent {

    class NeoRampAgentCommandProvider;

    class NeoRampAgent : public BasePlugin
    {
    public:
        NeoRampAgent();
        ~NeoRampAgent();

		// Plugin lifecycle methods
        void Initialize(const PluginMetadata& metadata, CoreAPI* coreAPI, ClientInformation info) override;
        std::pair<bool, std::string> newVersionAvailable();
        void Shutdown() override;
        void Reset();
        PluginMetadata GetMetadata() const override;

        // Radar commands
        void DisplayMessage(const std::string& message, const std::string& sender = "");
		
        // Scope events
        void OnTimer(int Counter);

        // Command handling
        void TagProcessing(const std::string& callsign, const std::string& actionId, const std::string& userInput = "");

		// API Accessors
        PluginSDK::Logger::LoggerAPI* GetLogger() const { return logger_; }
        Aircraft::AircraftAPI* GetAircraftAPI() const { return aircraftAPI_; }
        Airport::AirportAPI* GetAirportAPI() const { return airportAPI_; }
        Chat::ChatAPI* GetChatAPI() const { return chatAPI_; }
        Flightplan::FlightplanAPI* GetFlightplanAPI() const { return flightplanAPI_; }
        Fsd::FsdAPI* GetFsdAPI() const { return fsdAPI_; }
        PluginSDK::ControllerData::ControllerDataAPI* GetControllerDataAPI() const { return controllerDataAPI_; }
		Tag::TagInterface* GetTagInterface() const { return tagInterface_; }
		Package::PackageAPI* GetPackageAPI() const { return packageAPI_; }

    private:
        void runScopeUpdate();
        void run();
		std::string toUpper(std::string str);
        void generateReport(nlohmann::ordered_json& reportJson);
        void sendReport();
		//void assignStandToAircraft(std::string callsign, std::string standName, std::string icao);
        //nlohmann::ordered_json getAllStands(std::string icao);

    public:
        // Command IDs
        std::string versionId_;

    private:
        // Plugin state
        bool initialized_ = false;
        std::thread m_worker;
        bool m_stop;

        // APIs
        PluginMetadata metadata_;
        ClientInformation clientInfo_;
        Aircraft::AircraftAPI* aircraftAPI_ = nullptr;
        Airport::AirportAPI* airportAPI_ = nullptr;
        Chat::ChatAPI* chatAPI_ = nullptr;
        Flightplan::FlightplanAPI* flightplanAPI_ = nullptr;
        Fsd::FsdAPI* fsdAPI_ = nullptr;
        PluginSDK::Logger::LoggerAPI* logger_ = nullptr;
        PluginSDK::ControllerData::ControllerDataAPI* controllerDataAPI_ = nullptr;
        Tag::TagInterface* tagInterface_ = nullptr;
		Package::PackageAPI* packageAPI_ = nullptr;
        std::shared_ptr<NeoRampAgentCommandProvider> CommandProvider_;

        // Tag Items
        void RegisterTagItems();
        void RegisterTagActions();
        void RegisterCommand();
        void unegisterCommand();
        void OnTagAction(const Tag::TagActionEvent* event) override;
        void OnTagDropdownAction(const Tag::DropdownActionEvent* event) override;
        void UpdateTagItems();
        void UpdateTagItems(std::string Callsign, std::string standName = "N/A");
        void updateStandMenuButtons(const std::string& icao);

	    // TAG Items IDs
		std::string standTagId_;
        std::string standMenuId_;

        // TAG Action IDs
    };
} // namespace rampAgent