#pragma once

#include "domain/timeline-mapper.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include <QString>

struct sqlite3;

namespace replay_timeline {

struct SessionSummary {
	std::int64_t id = 0;
	QString startedUtc;
	QString status;
};

struct ReplayRow {
	std::int64_t id = 0;
	std::int64_t sessionId = 0;
	QString savedUtc;
	std::int64_t recordingStartNs = -1;
	std::int64_t recordingEndNs = -1;
	QString tag;
	QString note;
	std::int64_t durationNs = -1;
	QString replayPath;
	QString recordingPaths;
	QString confidence;
	QString probeStatus;
};

struct CsvRow {
	std::int64_t sessionId = 0;
	std::int64_t replayId = 0;
	std::int64_t recordingRun = 0;
	std::int64_t recordingSegment = 0;
	std::int64_t recordingStartNs = -1;
	std::int64_t recordingEndNs = -1;
	QString tag;
	QString note;
	std::int64_t durationNs = -1;
	QString replayPath;
	QString recordingPath;
	QString confidence;
};

struct PendingRequest {
	std::int64_t id = 0;
	std::int64_t runId = 0;
	std::int64_t recordingEndNs = -1;
	std::int64_t requestedNs = 0;
	QString tag;
};

class Repository final {
public:
	Repository() = default;
	~Repository();

	Repository(const Repository &) = delete;
	Repository &operator=(const Repository &) = delete;

	bool open(const QString &path, QString &error);
	void close();
	bool isOpen() const;
	QString lastError() const;

	bool recoverInterrupted();
	std::int64_t createSession(std::int64_t startedNs, const QString &startedUtc);
	bool closeSession(std::int64_t sessionId, std::int64_t endedNs, const QString &endedUtc,
			  const QString &status = QStringLiteral("complete"));
	std::int64_t createRecordingRun(std::int64_t sessionId, int ordinal, std::int64_t startedNs);
	bool closeRecordingRun(std::int64_t runId, std::int64_t endedNs);
	std::int64_t createSegment(std::int64_t runId, int ordinal, const QString &path, std::int64_t mediaStartNs,
				   std::int64_t startedNs);
	bool closeSegment(std::int64_t segmentId, std::int64_t mediaEndNs, std::int64_t endedNs);
	std::int64_t createPause(std::int64_t runId, std::int64_t startedNs);
	bool closePause(std::int64_t pauseId, std::int64_t endedNs);
	std::int64_t createRequest(std::int64_t sessionId, int generation, std::int64_t runId,
				   std::int64_t recordingEndNs, std::int64_t requestedNs, const QString &tag);
	std::optional<PendingRequest> resolveOldestPending(int generation, std::int64_t savedNs);
	std::int64_t createReplay(std::int64_t sessionId, const std::optional<PendingRequest> &request,
				  std::int64_t savedNs, const QString &savedUtc, const QString &path);
	bool replayProbeTarget(std::int64_t replayId, QString &path, std::optional<PendingRequest> &request) const;
	bool completeProbe(std::int64_t replayId, std::int64_t durationNs, const QString &probeStatus,
			   const QString &confidence, const QString &reason,
			   const std::vector<domain::AssociationSpan> &spans);
	std::vector<domain::RecordingSegment> segmentsForRun(std::int64_t runId, std::int64_t openEndNs) const;

	std::vector<SessionSummary> sessions() const;
	std::vector<ReplayRow> replays(std::int64_t sessionId) const;
	std::vector<CsvRow> csvRows(std::int64_t sessionId) const;
	bool updateReplay(std::int64_t replayId, const QString &tag, const QString &note);
	QString setting(const QString &key, const QString &fallback) const;
	bool setSetting(const QString &key, const QString &value);

private:
	bool execute(const char *sql) const;
	bool migrate();
	std::int64_t lastInsertId() const;

	sqlite3 *database_ = nullptr;
	mutable QString lastError_;
};

} // namespace replay_timeline
