#pragma once

#include <memory>

namespace replay_timeline {

class ObsEventBridge;
class ReplayTimelineDock;
class SessionController;

class PluginController final {
public:
	PluginController();
	~PluginController();

	PluginController(const PluginController &) = delete;
	PluginController &operator=(const PluginController &) = delete;

	bool start();
	void stop();

private:
	ReplayTimelineDock *dock_ = nullptr;
	std::unique_ptr<SessionController> sessionController_;
	std::unique_ptr<ObsEventBridge> eventBridge_;
	bool dockRegistered_ = false;
};

} // namespace replay_timeline
