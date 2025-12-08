
#include "rtc/rtc.hpp"
#include "rc_client.h"
#include "parse_cl.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
using namespace std::chrono_literals;
using std::shared_ptr;
using std::weak_ptr;
template <class T>
weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }
using nlohmann::json;

std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>> peerConnectionMap;
std::unordered_map<std::string, shared_ptr<rtc::DataChannel>> dataChannelMap;

shared_ptr<rtc::PeerConnection> createPeerConnection(const rtc::Configuration &config,
													 weak_ptr<rtc::WebSocket> wws, std::string id);
std::string randomId(size_t length);
RCClient *global_client = nullptr;
int rtc_main(int argc, char **argv)
try
{
	Cmdline params(argc, argv);

	// Use command line parameters or generate random ID
	std::string client_id = params.clientId(); // Use new client_id parameter
	if (client_id.empty())
	{
		client_id = randomId(4);
		std::cout << "Generated client ID: " << client_id << std::endl;
	}
	else
	{
		std::cout << "Using specified client ID: " << client_id << std::endl;
	}
	std::string tty_port = params.usbDevice();
	std::string gsm_port = params.gsmPort();
	int gsm_baudrate = params.gsmBaudrate();
	
	RCClient client(tty_port, gsm_port, gsm_baudrate);
	global_client = &client;
	// rtc 初始化
	rtc::InitLogger(rtc::LogLevel::Info);
	rtc::Configuration config;
	std::string stunServer = "";
	if (params.noStun())
	{
		std::cout
			<< "No STUN server is configured. Only local hosts and public IP addresses supported."
			<< std::endl;
	}
	else
	{
		if (params.stunServer().substr(0, 5).compare("stun:") != 0)
		{
			stunServer = "stun:";
		}
		stunServer += params.stunServer() + ":" + std::to_string(params.stunPort());
		std::cout << "STUN server is " << stunServer << std::endl;
		config.iceServers.emplace_back(stunServer);
	}

	// 添加 TURN 服务器配置支持
	std::string turnServer = params.turnServer();
	if (!turnServer.empty())
	{
		std::string turnUser = params.turnUser();
		std::string turnPass = params.turnPass();
		int turnPort = params.turnPort();

		std::cout << "TURN server is " << turnServer << ":" << turnPort << std::endl;

		// TURN 服务器 - 使用带参数的构造函数
		config.iceServers.push_back(rtc::IceServer(
			turnServer,						   // hostname
			turnPort,						   // port
			turnUser,						   // username
			turnPass,						   // password
			rtc::IceServer::RelayType::TurnUdp // relay type
			));
	}

	if (params.udpMux())
	{
		std::cout << "ICE UDP mux enabled" << std::endl;
		config.enableIceUdpMux = true;
	}

	auto ws = std::make_shared<rtc::WebSocket>();

	std::promise<void> wsPromise;
	auto wsFuture = wsPromise.get_future();

	ws->onOpen([&wsPromise]()
			   {
		std::cout << "WebSocket connected, signaling ready" << std::endl;
		wsPromise.set_value(); });

	ws->onError([&wsPromise](std::string s)
				{
		std::cout << "WebSocket error" << std::endl;
		wsPromise.set_exception(std::make_exception_ptr(std::runtime_error(s))); });

	ws->onClosed([]()
				 { std::cout << "WebSocket closed" << std::endl; 
				 exit(5); });

	ws->onMessage([&config, wws = make_weak_ptr(ws)](auto data)
				  {
		// data holds either std::string or rtc::binary
		if (!std::holds_alternative<std::string>(data))
			return;

		json message = json::parse(std::get<std::string>(data));

		auto it = message.find("id");
		if (it == message.end())
			return;

		auto id = it->get<std::string>();

		it = message.find("type");
		if (it == message.end())
			return;

		auto type = it->get<std::string>();

		shared_ptr<rtc::PeerConnection> pc;
		if (auto jt = peerConnectionMap.find(id); jt != peerConnectionMap.end()) {
			if (type == "offer"){
				std::cout << "Release old pc" << std::endl;
				dataChannelMap[id].reset();
				peerConnectionMap[id].reset();
				peerConnectionMap.erase(id);
				dataChannelMap.erase(id);
				std::cout << "Answering to " + id << std::endl;
				pc = createPeerConnection(config, wws, id);
			}else{
				pc = jt->second;
			}
		} else if (type == "offer") {
			std::cout << "Answering to " + id << std::endl;
			pc = createPeerConnection(config, wws, id);
		} else {
			return;
		}

		if (type == "offer" || type == "answer") {
			auto sdp = message["description"].get<std::string>();
			pc->setRemoteDescription(rtc::Description(sdp, type));
		} else if (type == "candidate") {
			auto sdp = message["candidate"].get<std::string>();
			auto mid = message["mid"].get<std::string>();
			pc->addRemoteCandidate(rtc::Candidate(sdp, mid));
		} });

	const std::string wsPrefix =
		params.webSocketServer().find("://") == std::string::npos ? "ws://" : "";
	const std::string url = wsPrefix + params.webSocketServer() + ":" +
							std::to_string(params.webSocketPort()) + "/" + client_id;

	std::cout << "WebSocket URL is " << url << std::endl;
	ws->open(url);

	std::cout << "Waiting for signaling to be connected..." << std::endl;
	wsFuture.get();
	// 无限睡眠
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::hours(24)); // 每次睡24小时
	}
	std::cout << "Cleaning up..." << std::endl;
	dataChannelMap.clear();
	peerConnectionMap.clear();
	global_client = nullptr;
	return 0;
}
catch (const std::exception &e)
{
	std::cout << "Error: " << e.what() << std::endl;
	dataChannelMap.clear();
	peerConnectionMap.clear();
	return -1;
}

// Create and setup a PeerConnection
shared_ptr<rtc::PeerConnection> createPeerConnection(const rtc::Configuration &config,
													 weak_ptr<rtc::WebSocket> wws, std::string id)
{
	auto pc = std::make_shared<rtc::PeerConnection>(config);

	pc->onStateChange(
		[](rtc::PeerConnection::State state)
		{ std::cout << "State: " << state << std::endl; });

	pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state)
							   { std::cout << "Gathering State: " << state << std::endl; });

	pc->onLocalDescription([wws, id](rtc::Description description)
						   {
		json message = {{"id", id},
		                {"type", description.typeString()},
		                {"description", std::string(description)}};

		if (auto ws = wws.lock())
			ws->send(message.dump()); });

	pc->onLocalCandidate([wws, id](rtc::Candidate candidate)
						 {
		json message = {{"id", id},
		                {"type", "candidate"},
		                {"candidate", std::string(candidate)},
		                {"mid", candidate.mid()}};

		if (auto ws = wws.lock())
			ws->send(message.dump()); });

	pc->onDataChannel([id](shared_ptr<rtc::DataChannel> dc)
					  {
		std::cout << "DataChannel from " << id << " received with label \"" << dc->label() << "\""
		          << std::endl;

		dc->onOpen([id, dc]() {
			std::cout << "DataChannel from " << id << " open" << std::endl;
			global_client->setDataChannel(dc);
		});

		dc->onClosed([id]() { std::cout << "DataChannel from " << id << " closed" << std::endl; });

		dc->onMessage([id](auto data) {
			if (global_client && global_client->getDataChannel() != nullptr) {
				// Handle both string and binary data
				if (std::holds_alternative<rtc::binary>(data)) {
					const rtc::binary& bin_data = std::get<rtc::binary>(data);
					global_client->parseFrame(reinterpret_cast<const uint8_t*>(bin_data.data()),
											  bin_data.size());
				}
				else {
					// Text messages are currently informational only; SBUS control uses binary frames.
				}
			} else {
				std::cout << "DataChannel not ready" << std::endl;
			}
		});

		dataChannelMap.emplace(id, dc); });

	peerConnectionMap.emplace(id, pc);
	return pc;
};

// Helper function to generate a random ID
std::string randomId(size_t length)
{
	using std::chrono::high_resolution_clock;
	static thread_local std::mt19937 rng(
		static_cast<unsigned int>(high_resolution_clock::now().time_since_epoch().count()));
	static const std::string characters(
		"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
	std::string id(length, '0');
	std::uniform_int_distribution<int> uniform(0, int(characters.size() - 1));
	std::generate(id.begin(), id.end(), [&]()
				  { return characters.at(uniform(rng)); });
	return id;
}