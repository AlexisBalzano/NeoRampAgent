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

    struct Stand {
        std::string name;
        std::string icao;
        bool occupied;
    };

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
		virtual void OnFsdConnectionStateChange(const Fsd::FsdConnectionStateChangeEvent* event) override;
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
		bool isConnected();
        bool isController();
        void sortStandList(std::vector<Stand>& standList);

    public:
		std::string toUpper(std::string str);
        void generateReport(nlohmann::ordered_json& reportJson);
        nlohmann::ordered_json sendReport();
        nlohmann::ordered_json getAllOccupiedStands(); //used to update tags when not sending reports
        nlohmann::ordered_json getAllBlockedStands();
		//void assignStandToAircraft(std::string callsign, std::string standName, std::string icao);
        //nlohmann::ordered_json getAllStands(std::string icao);
		std::string getMenuICAO() const { return menuICAO_; }
		std::string changeMenuICAO(const std::string& newICAO) { menuICAO_ = newICAO; return menuICAO_; }

    public:
        // Command IDs
        std::string versionId_;
		std::string menuId_;

    private:
        // Plugin state
        bool initialized_ = false;
        std::thread m_worker;
		bool canSendReport_ = false;
        bool isConnected_ = false;
        bool m_stop;
		std::string menuICAO_ = "LFPG"; //default airport for menu

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
        void updateStandMenuButtons(const std::string& icao, const nlohmann::ordered_json& occupiedStands);

	    // TAG Items IDs
		std::string standTagId_;
        std::string standMenuId_;

        // TAG Action IDs
    };
} // namespace rampAgent