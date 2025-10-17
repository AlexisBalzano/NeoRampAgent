#include "NeoRampAgent.h"
#include <numeric>
#include <chrono>
#include <httplib.h>

#include "Version.h"
#include "core/CompileCommands.h"
#include "core/TagFunctions.h"
#include "core/TagItems.h"

#ifdef DEV
#define LOG_DEBUG(loglevel, message) logger_->log(loglevel, message)
#else
#define LOG_DEBUG(loglevel, message) void(0)
#endif

using namespace rampAgent;

NeoRampAgent::NeoRampAgent() : m_stop(false), controllerDataAPI_(nullptr) {};
NeoRampAgent::~NeoRampAgent() = default;

void NeoRampAgent::Initialize(const PluginMetadata &metadata, CoreAPI *coreAPI, ClientInformation info)
{
    metadata_ = metadata;
    clientInfo_ = info;
    CoreAPI *lcoreAPI = coreAPI;
    aircraftAPI_ = &lcoreAPI->aircraft();
    airportAPI_ = &lcoreAPI->airport();
    chatAPI_ = &lcoreAPI->chat();
    flightplanAPI_ = &lcoreAPI->flightplan();
    fsdAPI_ = &lcoreAPI->fsd();
    controllerDataAPI_ = &lcoreAPI->controllerData();
    logger_ = &lcoreAPI->logger();
    tagInterface_ = lcoreAPI->tag().getInterface();
	packageAPI_ = &lcoreAPI->package();

#ifndef DEV
	std::pair<bool, std::string> updateAvailable = newVersionAvailable();
	if (updateAvailable.first) {
		DisplayMessage("A new version of NeoRampAgent is available: " + updateAvailable.second + " (current version: " + NEORAMPAGENT_VERSION + ")", "");
	}
#endif // !DEV

    try
    {
        this->RegisterTagItems();
        this->RegisterTagActions();
        this->RegisterCommand();

        initialized_ = true;
    }
    catch (const std::exception &e)
    {
        logger_->error("Failed to initialize NeoRampAgent: " + std::string(e.what()));
    }

    this->m_stop = false;
    this->m_worker = std::thread(&NeoRampAgent::run, this);
}

std::pair<bool, std::string> rampAgent::NeoRampAgent::newVersionAvailable()
{
    httplib::SSLClient cli("api.github.com");
    httplib::Headers headers = { {"User-Agent", "NeoRampAgentVersionChecker"} };
    std::string apiEndpoint = "/repos/AlexisBalzano/NeoRampAgent/releases/latest";

    auto res = cli.Get(apiEndpoint.c_str(), headers);
    if (res && res->status == 200) {
        try
        {
            auto json = nlohmann::json::parse(res->body);
            std::string latestVersion = json["tag_name"];
            if (latestVersion != NEORAMPAGENT_VERSION) {
                logger_->warning("A new version of NeoRampAgent is available: " + latestVersion + " (current version: " + NEORAMPAGENT_VERSION + ")");
                return { true, latestVersion };
            }
            else {
                logger_->log(Logger::LogLevel::Info, "NeoRampAgent is up to date.");
                return { false, "" };
            }
        }
        catch (const std::exception& e)
        {
            logger_->error("Failed to parse version information from GitHub: " + std::string(e.what()));
            return { false, "" };
        }
    }
    else {
        logger_->error("Failed to check for NeoRampAgent updates. HTTP status: " + std::to_string(res ? res->status : 0));
        return { false, "" };
    }
}

void NeoRampAgent::Shutdown()
{
    if (initialized_)
    {
        initialized_ = false;
        LOG_DEBUG(Logger::LogLevel::Info, "NeoRampAgent shutdown complete");
    }


    this->m_stop = true;
    this->m_worker.join();

    this->unegisterCommand();
}

void rampAgent::NeoRampAgent::Reset()
{
}

void NeoRampAgent::DisplayMessage(const std::string &message, const std::string &sender) {
    Chat::ClientTextMessageEvent textMessage;
    textMessage.sentFrom = "NeoRampAgent";
    (sender.empty()) ? textMessage.message = ": " + message : textMessage.message = sender + ": " + message;
    textMessage.useDedicatedChannel = true;

    chatAPI_->sendClientMessage(textMessage);
}

void NeoRampAgent::run() {
    int counter = 0;
    while (!this->m_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        counter++;
        OnTimer(counter);
    }
}

std::string rampAgent::NeoRampAgent::toUpper(std::string str)
{
	std::transform(str.begin(), str.end(), str.begin(), ::toupper);
	return str;
}

void rampAgent::NeoRampAgent::generateReport(nlohmann::ordered_json& reportJson)
{
    reportJson.clear();

    // need to retrieve all aircraft in range and format json report
    std::vector<Aircraft::Aircraft> aircrafts = aircraftAPI_->getAll();

    // Filter for ground aircraft
    std::vector<Aircraft::Aircraft> groundAircrafts;
    for (const auto& ac : aircrafts) {
        if (ac.position.onGround && ac.position.groundSpeed == 0) {
            groundAircrafts.push_back(ac);
        }
    }

    // Filter for airborn aircraft
    std::vector<Aircraft::Aircraft> airbornAircrafts;
    for (const auto& ac : aircrafts) {
        if (!ac.position.onGround || ac.position.groundSpeed != 0) {
            std::optional<Flightplan::Flightplan> fp = flightplanAPI_->getByCallsign(ac.callsign);
            if (!fp.has_value()) continue; // Skip if no flightplan found
            airbornAircrafts.push_back(ac);
        }
    }

    std::optional<Fsd::ConnectionInfo> connectionInfo = fsdAPI_->getConnection();
    if (!connectionInfo.has_value()) {
        DisplayMessage("Not connected to FSD server. Cannot send report.", "NeoRampAgent");
        logger_->log(Logger::LogLevel::Warning, "Not connected to FSD server. Cannot send report.");
        return;
    }
    std::string currentATC = connectionInfo->callsign;

    reportJson["client"] = currentATC;
	reportJson["aircrafts"]["onGround"] = nlohmann::ordered_json::object();
	reportJson["aircrafts"]["airborne"] = nlohmann::ordered_json::object();

    for (const auto& ac : groundAircrafts) {
        std::string callsign = toUpper(ac.callsign);
        std::optional<Flightplan::Flightplan> fp = flightplanAPI_->getByCallsign(ac.callsign);
        std::string origin = "N/A";
        std::string aircraftType = "ZZZZ";
        if (fp.has_value()) {
            origin = toUpper(fp->origin);
            aircraftType = toUpper(fp->acType);
        }

        reportJson["aircrafts"]["onGround"][callsign]["origin"] = origin;
        reportJson["aircrafts"]["onGround"][callsign]["aircraftType"] = aircraftType;
        reportJson["aircrafts"]["onGround"][callsign]["position"]["lat"] = ac.position.latitude;
        reportJson["aircrafts"]["onGround"][callsign]["position"]["lon"] = ac.position.longitude;
    }

    for (const auto& ac : airbornAircrafts) {
        std::string callsign = toUpper(ac.callsign);
        std::optional<Flightplan::Flightplan> fp = flightplanAPI_->getByCallsign(ac.callsign);
        std::string origin = "N/A";
        std::string destination = "N/A";
        std::string aircraftType = "ZZZZ";
        if (fp.has_value()) {
            origin = toUpper(fp->origin);
            destination = toUpper(fp->destination);
            aircraftType = toUpper(fp->acType);
        }

        std::optional<double> distOpt = aircraftAPI_->getDistanceToDestination(ac.callsign);
        double dist = distOpt.value_or(-1);

        reportJson["aircrafts"]["airborne"][callsign]["origin"] = origin;
        reportJson["aircrafts"]["airborne"][callsign]["destination"] = destination;
        reportJson["aircrafts"]["airborne"][callsign]["aircraftType"] = aircraftType;
        reportJson["aircrafts"]["airborne"][callsign]["position"]["lat"] = ac.position.latitude;
        reportJson["aircrafts"]["airborne"][callsign]["position"]["lon"] = ac.position.longitude;
        reportJson["aircrafts"]["airborne"][callsign]["position"]["alt"] = ac.position.altitude;
        reportJson["aircrafts"]["airborne"][callsign]["position"]["dist"] = dist;
    }
}

void rampAgent::NeoRampAgent::sendReport()
{
    nlohmann::ordered_json reportJson;
    generateReport(reportJson);

    if (reportJson.empty()) {
        logger_->log(Logger::LogLevel::Info, "Skipping report: no data to send.");
        return;
    }

    // HTTP (no TLS) to localhost:3000
    httplib::Client cli("127.0.0.1", 3000);
    cli.set_connection_timeout(2); // seconds
    cli.set_read_timeout(5);
    cli.set_write_timeout(5);
    
    auto res = cli.Post("/report", reportJson.dump(), "application/json");

    if (res && res->status >= 200 && res->status < 300) {
		//Use response to update tags/return response
    } else {
        logger_->error(
            "Failed to send report to NeoRampAgent server. HTTP status: " +
            std::to_string(res ? res->status : 0) +
            " (no detailed error available with this httplib version)"
        );
    }
}

void NeoRampAgent::runScopeUpdate() {
	sendReport(); // Use response to update tags
    //UpdateTagItems(); //FIXME:
}

void NeoRampAgent::OnTimer(int Counter) {
    if (Counter % 10 == 0) this->runScopeUpdate();
}

PluginSDK::PluginMetadata NeoRampAgent::GetMetadata() const
{
    return {"NeoRampAgent", PLUGIN_VERSION, "French vACC"};
}
