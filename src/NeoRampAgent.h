#pragma once
#include <memory>
#include <thread>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <map>

#include "NeoRadarSDK/SDK.h"
#include "core/NeoRampAgentCommandProvider.h"

constexpr const char* NEORAMPAGENT_VERSION = "v1.0.2";
constexpr const char* RAMPAGENT_API = "pintade.vatsim.fr";

using namespace PluginSDK;

namespace rampAgent {

    struct Stand {
        std::string name;
        std::string icao;
        bool occupied = false;
    };

    typedef std::optional<std::array<unsigned int, 3>> Colour;
    inline Colour YELLOW = std::array<unsigned int, 3>({ 255, 220, 3 });
    inline Colour WHITE = std::array<unsigned int, 3>({ 255, 255, 255 });


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
        virtual bool OnTagShowDropdown(const std::string& actionId, const std::string& callsign) override;

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
        nlohmann::ordered_json getAllAssignedStands(); //used to update tags when not sending reports
        bool printToFile(const std::vector<std::string>& lines, const std::string& fileName);
		bool dumpReportToLogFile();
		bool changeApiUrl(const std::string& newUrl);
        std::string generateToken(const std::string& callsign, const std::string& cid);

    public:
        // Command IDs
        std::string versionId_;
		std::string dumpId_;
		std::string urlId_;

    private:
        // Plugin state
        bool initialized_ = false;
        std::thread m_worker;
		bool canSendReport_ = false;
        bool isConnected_ = false;
        bool m_stop;
        bool printError = true;
		std::filesystem::path configPath_;
		nlohmann::ordered_json lastReportJson_;
		nlohmann::ordered_json lastOccupiedStands_;
		std::mutex reportMutex_;
		std::map<std::string, std::string> lastStandTagMap_; // maps callsign to stand tag ID
		std::string apiUrl_ = RAMPAGENT_API;
        std::string callsign_;
		std::string cid_;

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
        void UpdateTagItems(std::string Callsign, Colour colour = WHITE, std::string standName = "", std::string remark = "");
        void updateStandMenuButtons(const std::string& icao, const nlohmann::ordered_json& occupiedStands);

	    // TAG Items IDs
		std::string standTagId_;
		std::string remarkTagId_;
        std::string standMenuId_;

        // TAG Action IDs
    };
} // namespace rampAgent