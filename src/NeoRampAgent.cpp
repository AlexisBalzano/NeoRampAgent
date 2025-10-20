#include "NeoRampAgent.h"
#include <numeric>
#include <chrono>
#include <httplib.h>
#include <fstream>

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

void NeoRampAgent::Initialize(const PluginMetadata& metadata, CoreAPI* coreAPI, ClientInformation info)
{
	metadata_ = metadata;
	clientInfo_ = info;
	CoreAPI* lcoreAPI = coreAPI;
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
		isConnected_ = isConnected();
		canSendReport_ = isController();
		configPath_ = clientInfo_.documentsPath;
		LOG_DEBUG(Logger::LogLevel::Info, "NeoRampAgent initialized successfully");
	}
	catch (const std::exception& e)
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

void NeoRampAgent::DisplayMessage(const std::string& message, const std::string& sender) {
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

bool rampAgent::NeoRampAgent::isConnected()
{
	std::optional<Fsd::ConnectionInfo> connectionInfo = fsdAPI_->getConnection();
	if (connectionInfo.has_value() && connectionInfo->isConnected) {
		return true;
	}
	return false;
}

bool rampAgent::NeoRampAgent::isController()
{
	std::optional<Fsd::ConnectionInfo> connectionInfo = fsdAPI_->getConnection();
#ifdef DEV
	callsign_ = connectionInfo->callsign;
	return true;
#endif // DEV

	if (isConnected_) {
		if (connectionInfo->facility >= Fsd::NetworkFacility::DEL) {
			callsign_ = connectionInfo->callsign;
			return true;
		}
	}
	return false;
}

void rampAgent::NeoRampAgent::sortStandList(std::vector<Stand>& standList)
{
	std::sort(standList.begin(), standList.end(), [](const Stand& a, const Stand& b) {
		auto key = [](const std::string& s) {
			size_t i = 0, n = s.size();

			// Trim leading spaces
			while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;

			// Leading number
			int num = 0;
			bool hasNum = false;
			while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) {
				hasNum = true;
				int digit = s[i] - '0';
				if (num > (std::numeric_limits<int>::max() - digit) / 10) {
					num = std::numeric_limits<int>::max(); // clamp on overflow
				}
				else {
					num = num * 10 + digit;
				}
				++i;
			}

			// Immediate letter suffix (A, B, AB, ...)
			std::string letters;
			while (i < n && std::isalpha(static_cast<unsigned char>(s[i]))) {
				letters.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
				++i;
			}

			// Remainder (for stable tie-breaking, case-insensitive)
			std::string tailUpper;
			tailUpper.reserve(n - i);
			for (; i < n; ++i) {
				unsigned char c = static_cast<unsigned char>(s[i]);
				tailUpper.push_back(static_cast<char>(std::toupper(c)));
			}

			// Bare names (no numeric prefix) go to the end.
			return std::tuple<int, std::string, std::string, std::string>(
				hasNum ? num : std::numeric_limits<int>::max(), letters, tailUpper, s
			);
			};

		const auto [an, al, ar, as] = key(a.name);
		const auto [bn, bl, br, bs] = key(b.name);

		if (an != bn) return an < bn;

		// If numbers equal, empty suffix (e.g., "2") comes before non-empty (e.g., "2A")
		if (al != bl) {
			if (al.empty() != bl.empty()) return al.empty();
			return al < bl; // case-insensitive compare via uppercased letters
		}

		// Fallback to case-insensitive remainder, then original (stable) name
		if (ar != br) return ar < br;
		return as < bs;
		});
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
			if (ac.position.altitude > 20000) continue; // Skip aircraft above 20,000 ft
			airbornAircrafts.push_back(ac);
		}
	}

	if (!isConnected_) {
		if (printError) {
			printError = false; // avoid spamming logs
			DisplayMessage("Not connected to FSD server. Cannot send report.", "NeoRampAgent");
			logger_->log(Logger::LogLevel::Warning, "Not connected to FSD server. Cannot send report.");
		}
		return;
	}
	std::string currentATC = connectionInfo->callsign;

	reportJson["client"] = callsign_;
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
		if (!fp.has_value()) continue; // Skip if no flightplan found
		if (fp->destination.substr(0, 2) != "LF") continue; // Skip if destination is not in France
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

nlohmann::ordered_json rampAgent::NeoRampAgent::sendReport()
{
	nlohmann::ordered_json reportJson;
	generateReport(reportJson);
	lastReportJson_ = reportJson; // store last report for logging/dumping

	if (reportJson.empty()) {
		logger_->log(Logger::LogLevel::Info, "Skipping report: no data to send.");
		return nlohmann::ordered_json::object();
	}

	httplib::SSLClient cli(apiUrl_, 443);
	httplib::Headers headers = { {"User-Agent", "NeoRampAgent"} };


	auto res = cli.Post("/api/report", reportJson.dump(), "application/json");

	if (res && res->status >= 200 && res->status < 300) {
		printError = true; // reset error printing flag on success
		try {
			return res->body.empty() ? nlohmann::ordered_json::object() : nlohmann::ordered_json::parse(res->body);
		}
		catch (const std::exception& e) {
			logger_->error("Failed to parse response from NeoRampAgent server: " + std::string(e.what()));
			return nlohmann::ordered_json::object();
		}
	}
	else {
		if (printError) {
			printError = false; // avoid spamming logs
			DisplayMessage("Failed to send report to NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0), "");
			logger_->error("Failed to send report to NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0));
		}
	}
	return nlohmann::ordered_json::object();
}

nlohmann::ordered_json rampAgent::NeoRampAgent::getAllAssignedStands()
{
	nlohmann::ordered_json assignedStandsJson = nlohmann::ordered_json::object();
	
	httplib::SSLClient cli(apiUrl_, 443);
	httplib::Headers headers = { {"User-Agent", "NeoRampAgent"} };

	auto res = cli.Get("/api/occupancy/assigned", headers);

	if (res && res->status >= 200 && res->status < 300) {
		printError = true; // reset error printing flag on success
		try {
			if (!res->body.empty()) assignedStandsJson["assignedStands"] = nlohmann::ordered_json::parse(res->body);
			return assignedStandsJson;
		}
		catch (const std::exception& e) {
			logger_->error("Failed to parse occupied stands data from NeoRampAgent server: " + std::string(e.what()));
			return nlohmann::ordered_json::object();
		}
	}
	else {
		if (printError) {
			printError = false; // avoid spamming logs
			DisplayMessage("Failed to retrieve occupied stands data from NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0), "");
			logger_->error("Failed to send report to NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0));
		}
	}
	return nlohmann::ordered_json::object();
}

bool rampAgent::NeoRampAgent::printToFile(const std::vector<std::string>& lines, const std::string& fileName)
{
	{
		std::filesystem::path dir = configPath_ / "logs" / "plugins" / "NeoRampAgent";
		std::error_code ec;
		if (!std::filesystem::exists(dir))
		{
			if (!std::filesystem::create_directories(dir, ec))
			{
				logger_->log(Logger::LogLevel::Error,
					"Failed to create log directory: " + dir.string() + " ec=" + ec.message());
				return false;
			}
		}
		std::filesystem::path filePath = dir / fileName;
		std::ofstream outFile(filePath);
		if (!outFile.is_open()) {
			logger_->log(Logger::LogLevel::Error, "Could not open file to write: " + filePath.string());
			return false;
		}
		for (const auto& line : lines) {
			outFile << line << std::endl;
		}
		outFile.close();
		return true;
	}
}

bool rampAgent::NeoRampAgent::dumpReportToLogFile()
{
	// generate filName containing timestamp
	std::string fileName = "report_" + std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())) + ".txt";
	std::vector<std::string> content;
	content.push_back("--- NeoRampAgent last Report Dump ---");
	content.push_back("Is Connected: " + std::string(isConnected_ ? "Yes" : "No"));
	content.push_back("Can send report: " + std::string(canSendReport_ ? "Yes" : "No"));
	content.push_back("Report:");
	content.push_back(lastReportJson_.dump(4).empty() ? "{}" : lastReportJson_.dump(4));
	return printToFile(content, fileName);
}

bool rampAgent::NeoRampAgent::changeApiUrl(const std::string& newUrl)
{
	apiUrl_ = newUrl;
	return true;
}

void NeoRampAgent::runScopeUpdate() {
	nlohmann::ordered_json assignedStands;
	if (canSendReport_) assignedStands = sendReport(); // Use response to update tags
	else assignedStands = getAllAssignedStands();

	if (assignedStands.empty()) {
		if (printError) {
			DisplayMessage("No occupied stands data received to update tags.", "");
			logger_->log(Logger::LogLevel::Warning, "No occupied stands data received to update tags.");
			printError = false; // avoid spamming logs
		}
		// Clear All Tag Items
		for (const auto& [callsign, standName] : lastStandTagMap_) {
			UpdateTagItems(callsign, WHITE, "");
		}
		lastStandTagMap_.clear();
		return;
	}

	updateStandMenuButtons(menuICAO_, assignedStands);

	std::map<std::string, std::string> standTagMap;

	auto& assigned = assignedStands["assignedStands"];
	for (auto& stand : assigned) {
		std::string callsign = stand["callsign"].get<std::string>();
		std::optional<Aircraft::Aircraft> acOpt = aircraftAPI_->getByCallsign(callsign);
		if (!acOpt.has_value()) {
			continue; // Aircraft not found, skip
		}

		std::string standName = stand["name"].get<std::string>();
		standTagMap[callsign] = standName;

		// Update only if changed or new
		if (lastStandTagMap_.find(callsign) != lastStandTagMap_.end() && lastStandTagMap_[callsign] == standName) {
			UpdateTagItems(callsign, WHITE, standName);
			continue;
		}
		else {
			UpdateTagItems(callsign, ORANGE, standName);
		}
	}

	// Clear tags for aircraft that are no longer assigned
	for (const auto& [callsign, standName] : lastStandTagMap_) {
		if (standTagMap.find(callsign) == standTagMap.end()) {
			UpdateTagItems(callsign, WHITE, "");
		}
	}

	lastStandTagMap_ = standTagMap;
}

void rampAgent::NeoRampAgent::OnFsdConnectionStateChange(const Fsd::FsdConnectionStateChangeEvent* event)
{
	// recheck connection status to determine if we can send reports
	isConnected_ = isConnected();
	canSendReport_ = isController();
}

void NeoRampAgent::OnTimer(int Counter) {
	if (Counter % 10 == 0 && isConnected_) this->runScopeUpdate();
}

PluginSDK::PluginMetadata NeoRampAgent::GetMetadata() const
{
	return { "NeoRampAgent", PLUGIN_VERSION, "French vACC" };
}
