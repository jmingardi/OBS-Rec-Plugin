#pragma once

#include <cstdint>

#include <QPointer>
#include <QString>

#include <callback/calldata.h>
#include <obs-frontend-api.h>

namespace replay_timeline {

class ReplayTimelineDock;
class SessionController;

class ObsEventBridge final {
public:
	ObsEventBridge(ReplayTimelineDock *dock, SessionController *controller);
	~ObsEventBridge();

	ObsEventBridge(const ObsEventBridge &) = delete;
	ObsEventBridge &operator=(const ObsEventBridge &) = delete;

	void start();
	void stop();

private:
	static void frontendEventCallback(obs_frontend_event event, void *privateData);
	static void recordingOutputSignalCallback(void *privateData, const char *signal, calldata_t *parameters);

	void handleFrontendEvent(obs_frontend_event event);
	void attachRecordingOutput();
	void detachRecordingOutput();
	void publish(const QString &eventName, const QString &detail, std::uint64_t monotonicNanoseconds);
	void publishState();

	QPointer<ReplayTimelineDock> dock_;
	QPointer<SessionController> controller_;
	obs_output_t *recordingOutput_ = nullptr;
	bool started_ = false;
};

} // namespace replay_timeline
