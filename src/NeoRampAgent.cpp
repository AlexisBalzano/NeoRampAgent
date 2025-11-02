#include "NeoRampAgent.h"
#include <numeric>
#include <chrono>
#include <httplib.h>
#include <fstream>
#include <iomanip>
#include <openssl/sha.h>

#include "Version.h"
#include "core/CompileCommands.h"
#include "core/TagFunctions.h"
#include "core/TagItems.h"
#include "Secret.h"

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
	try {
		std::pair<bool, std::string> updateAvailable = newVersionAvailable();
		if (updateAvailable.first) {
			DisplayMessage("A new version of NeoRampAgent is available: " + updateAvailable.second + " (current version: " + NEORAMPAGENT_VERSION + ")", "");
		}
	} catch (const std::exception& e) {
		logger_->error("Error checking for updates: " + std::string(e.what()));
	}
#endif // !DEV


	try
	{
		this->RegisterTagActions();
		this->RegisterTagItems();
		this->RegisterCommand();

		initialized_ = true;
		isConnected_ = isConnected();
		isController_ = isController();
		logger_->info("NeoRampAgent initialized successfully");
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
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	httplib::SSLClient cli("api.github.com");
	cli.set_connection_timeout(2); // seconds
	cli.set_read_timeout(5);
	cli.set_write_timeout(5);
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
#else
	logger_->warning("OpenSSL not available; skipping online version check.");
	return { false, "" };
#endif
}

void NeoRampAgent::Shutdown()
{
	if (initialized_)
	{
		initialized_ = false;
		LOG_DEBUG(Logger::LogLevel::Info, "NeoRampAgent shutdown complete");
	}


	this->m_stop = true;
	if (this->m_worker.joinable()) {
		this->m_worker.join();
	}

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
	std::transform(str.begin(), str.end(), str.begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
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
	if (!connectionInfo.has_value()) {
		return false;
	}
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

nlohmann::ordered_json rampAgent::NeoRampAgent::getAllAssignedStands()
{
	nlohmann::ordered_json assignedStandsJson = nlohmann::ordered_json::object();
	
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	httplib::SSLClient cli(apiUrl_, 443);
	httplib::Headers headers = { {"User-Agent", "NeoRampAgent"} };

	auto res = cli.Get("/rampagent/api/occupancy/", headers);

	if (res && res->status >= 200 && res->status < 300) {
		if (!printError) {
			printError = true; // reset error printing flag on success
			DisplayMessage("Successfully reconnected to NeoRampAgent server.", "");
			logger_->info("Successfully reconnected to NeoRampAgent server.");
		}
		try {
			if (!res->body.empty()) return assignedStandsJson = nlohmann::ordered_json::parse(res->body);
		}
		catch (const std::exception& e) {
			logger_->error("Failed to parse assigned stands data from NeoRampAgent server: " + std::string(e.what()));
			return assignedStandsJson;
		}
	}
	else {
		if (printError) {
			printError = false; // avoid spamming logs
			DisplayMessage("Failed to retrieve assigned stands data from NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0), "");
			logger_->error("Failed to retrieve assigned stands data from NeoRampAgent server. HTTP status: " + std::to_string(res ? res->status : 0));
		}
		return assignedStandsJson;
	}
#else
	logger_->warning("OpenSSL not available; cannot retrieve assigned stands from NeoRampAgent server.");
#endif // #ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	return assignedStandsJson;
}

bool rampAgent::NeoRampAgent::changeApiUrl(const std::string& newUrl)
{
	apiUrl_ = newUrl;
	return true;
}

std::string rampAgent::NeoRampAgent::generateToken(const std::string& callsign)
{
	std::string s = AUTH_SECRET + callsign_;
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), hash);
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');
	for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
		oss << std::setw(2) << static_cast<int>(hash[i]);
	}
	return oss.str();
}	

void NeoRampAgent::runScopeUpdate() {
	std::lock_guard<std::mutex> lock(occupiedStandstMutex_);

	LOG_DEBUG(Logger::LogLevel::Info, "Running scope update for stand assignments.");
	lastOccupiedStands_ = getAllAssignedStands();
	LOG_DEBUG(Logger::LogLevel::Info, "Retrieved occupied stands data: " + lastOccupiedStands_.dump());

	if (lastOccupiedStands_.empty()) {
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

	std::map<std::string, std::string> standTagMap;

	LOG_DEBUG(Logger::LogLevel::Info, "Processing assigned stands for tag updates.");

	auto& assigned = lastOccupiedStands_["assignedStands"];
	lastOccupiedStands_["assignedStands"].insert(
		lastOccupiedStands_["assignedStands"].end(),
		lastOccupiedStands_["occupiedStands"].begin(),
		lastOccupiedStands_["occupiedStands"].end()
	); // display tag item on occupied stands as well

	LOG_DEBUG(Logger::LogLevel::Info, "Combined assigned and occupied stands for processing.");

	try {
		for (const auto& stand : assigned) {
			if (!stand.is_object()) continue;

			// callsign
			const auto csIt = stand.find("callsign");
			if (csIt == stand.end() || !csIt->is_string()) continue;
			const std::string callsign = csIt->get<std::string>();

			// aircraft must exist on scope
			std::optional<Aircraft::Aircraft> acOpt = aircraftAPI_->getByCallsign(callsign);
			if (!acOpt.has_value()) continue;

			// stand name
			const auto nameIt = stand.find("name");
			if (nameIt == stand.end() || !nameIt->is_string()) continue;
			const std::string standName = nameIt->get<std::string>();
			standTagMap[callsign] = standName;

			// remark: accept string only; treat null/other as empty
			std::string remark;
			if (auto rIt = stand.find("remark"); rIt != stand.end() && rIt->is_string()) {
				remark = rIt->get<std::string>();
			}
			else {
				remark.clear();
			}

			// Update only if changed or new
			if (auto it = lastStandTagMap_.find(callsign);
				it != lastStandTagMap_.end() && it->second == standName) {
				UpdateTagItems(callsign, WHITE, standName, remark);
			}
			else {
				UpdateTagItems(callsign, YELLOW, standName, remark);
			}
		}
	}
	catch (const std::exception& e) {
		logger_->error(std::string("runScopeUpdate: failed to process assigned stands: ") + e.what());
	}

	// Clear tags for aircraft that are no longer assigned
	for (const auto& [callsign, standName] : lastStandTagMap_) {
		if (standTagMap.find(callsign) == standTagMap.end()) {
			UpdateTagItems(callsign, WHITE, "");
		}
	}

	lastStandTagMap_ = standTagMap;
	LOG_DEBUG(Logger::LogLevel::Info, "Scope update completed.");
}

void rampAgent::NeoRampAgent::OnFsdConnectionStateChange(const Fsd::FsdConnectionStateChangeEvent* event)
{
	// recheck connection status to determine if we can send reports
	isConnected_ = isConnected();
	isController_ = isController();
}

void NeoRampAgent::OnTimer(int Counter) {
	if (Counter % 15 == 0 && isConnected_) this->runScopeUpdate();
}

PluginSDK::PluginMetadata NeoRampAgent::GetMetadata() const
{
	return { "NeoRampAgent", PLUGIN_VERSION, "French vACC" };
}
