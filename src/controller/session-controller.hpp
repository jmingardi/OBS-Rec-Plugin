#pragma once

#include "domain/timeline-mapper.hpp"
#include "persistence/repository.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include <QObject>
#include <QDateTime>
#include <QPointer>
#include <QStringList>

#include <obs.h>
#include <obs-hotkey.h>

namespace replay_timeline {

class ReplayTimelineDock;

class SessionController final : public QObject {
public:
	explicit SessionController(ReplayTimelineDock *dock);
	~SessionController() override;

	SessionController(const SessionController &) = delete;
	SessionController &operator=(const SessionController &) = delete;

	bool start(const QString &databasePath, QString &error);
	void stop();
	void postObsEvent(const QString &eventName, const QString &detail, std::uint64_t monotonicNanoseconds);

private:
	struct HotkeyRegistration {
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
		QString tag;
	};

	struct ActiveRun {
		std::int64_t id = 0;
		std::int64_t startedNs = 0;
		int ordinal = 0;
		std::int64_t segmentId = 0;
		int segmentOrdinal = 0;
		std::vector<domain::PauseInterval> pauses;
		std::int64_t openPauseId = 0;
		std::int64_t openPauseStartedNs = 0;
	};

	static void hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	void handleObsEvent(const QString &eventName, const QString &detail, std::int64_t monotonicNanoseconds);
	void requestReplay(const QString &tag, std::int64_t requestedNs);
	void beginRecording(std::int64_t now, const QString &path);
	void endRecording(std::int64_t now, const QString &finalPath);
	void beginPause(std::int64_t now);
	void endPause(std::int64_t now);
	void splitRecording(std::int64_t now, const QString &path);
	std::int64_t currentMediaTime(std::int64_t now) const;
	void ensureSession(std::int64_t now);
	void closeSessionIfIdle(std::int64_t now);
	void startProbe(std::int64_t replayId, const QString &path, const QString &savedUtc, std::int64_t savedNs,
			std::optional<PendingRequest> request);
	void retryProbe(std::int64_t replayId);
	void finishProbe(std::int64_t replayId, const std::optional<PendingRequest> &request, std::int64_t durationNs,
			 const QString &error, const QString &resolvedPath, const QString &resolutionDetail,
			 const QDateTime &expectedRecordingStartedUtc);
	void loadTags();
	void configureTags(const QStringList &tags);
	void registerHotkeys();
	void unregisterHotkeys();
	void refresh(std::int64_t preferredSession = 0);
	void exportCsv(const QString &path, bool allSessions);
	void clearSessions();

	QPointer<ReplayTimelineDock> dock_;
	Repository repository_;
	std::vector<HotkeyRegistration> hotkeys_;
	QStringList tags_;
	std::optional<ActiveRun> activeRun_;
	std::int64_t activeSessionId_ = 0;
	std::int64_t selectedSessionId_ = 0;
	int nextRunOrdinal_ = 1;
	int replayGeneration_ = 0;
	bool recordingActive_ = false;
	bool replayActive_ = false;
	bool started_ = false;
};

} // namespace replay_timeline
