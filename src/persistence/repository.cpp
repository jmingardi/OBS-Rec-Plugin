#include "repository.hpp"

#include <limits>
#include <utility>

#include <QByteArray>

#include <sqlite3.h>

namespace replay_timeline {
namespace {

class Statement final {
public:
	Statement(sqlite3 *database, const char *sql) : database_(database)
	{
		if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) != SQLITE_OK)
			statement_ = nullptr;
	}

	~Statement() { sqlite3_finalize(statement_); }

	Statement(const Statement &) = delete;
	Statement &operator=(const Statement &) = delete;

	bool valid() const { return statement_ != nullptr; }
	bool bind(std::int32_t index, std::int64_t value)
	{
		return sqlite3_bind_int64(statement_, index, value) == SQLITE_OK;
	}
	bool bind(std::int32_t index, const QString &value)
	{
		const QByteArray utf8 = value.toUtf8();
		return sqlite3_bind_text(statement_, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) ==
		       SQLITE_OK;
	}
	bool bindNull(std::int32_t index) { return sqlite3_bind_null(statement_, index) == SQLITE_OK; }
	int step() { return sqlite3_step(statement_); }
	std::int64_t integer(std::int32_t column) const { return sqlite3_column_int64(statement_, column); }
	bool isNull(std::int32_t column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }
	QString text(std::int32_t column) const
	{
		const auto *value = sqlite3_column_text(statement_, column);
		return value ? QString::fromUtf8(reinterpret_cast<const char *>(value)) : QString();
	}

private:
	sqlite3 *database_ = nullptr;
	sqlite3_stmt *statement_ = nullptr;
};

bool succeeded(Statement &statement)
{
	return statement.valid() && statement.step() == SQLITE_DONE;
}

} // namespace

Repository::~Repository()
{
	close();
}

bool Repository::open(const QString &path, QString &error)
{
	close();
	const QByteArray utf8 = path.toUtf8();
	if (sqlite3_open_v2(utf8.constData(), &database_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
	    SQLITE_OK) {
		lastError_ = database_ ? QString::fromUtf8(sqlite3_errmsg(database_))
				       : QStringLiteral("sqlite open failed");
		error = lastError_;
		close();
		return false;
	}

	sqlite3_busy_timeout(database_, 5000);
	if (!execute("PRAGMA foreign_keys=ON;") || !execute("PRAGMA journal_mode=WAL;") || !migrate()) {
		error = lastError_;
		close();
		return false;
	}
	return true;
}

void Repository::close()
{
	if (database_)
		sqlite3_close(database_);
	database_ = nullptr;
}

bool Repository::isOpen() const
{
	return database_ != nullptr;
}

QString Repository::lastError() const
{
	if (!lastError_.isEmpty())
		return lastError_;
	return database_ ? QString::fromUtf8(sqlite3_errmsg(database_)) : QStringLiteral("database is closed");
}

bool Repository::execute(const char *sql) const
{
	char *message = nullptr;
	const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &message);
	if (result == SQLITE_OK)
		return true;
	lastError_ = message ? QString::fromUtf8(message) : QString::fromUtf8(sqlite3_errmsg(database_));
	sqlite3_free(message);
	return false;
}

bool Repository::migrate()
{
	static constexpr const char *schema = R"sql(
BEGIN IMMEDIATE;
CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY, applied_utc TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS sessions(
  id INTEGER PRIMARY KEY, started_ns INTEGER NOT NULL, started_utc TEXT NOT NULL,
  ended_ns INTEGER, ended_utc TEXT, status TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS recording_runs(
  id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  ordinal INTEGER NOT NULL, started_ns INTEGER NOT NULL, ended_ns INTEGER, status TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS recording_segments(
  id INTEGER PRIMARY KEY, run_id INTEGER NOT NULL REFERENCES recording_runs(id) ON DELETE CASCADE,
  ordinal INTEGER NOT NULL, path TEXT NOT NULL, media_start_ns INTEGER NOT NULL, media_end_ns INTEGER,
  started_ns INTEGER NOT NULL, ended_ns INTEGER
);
CREATE TABLE IF NOT EXISTS recording_pauses(
  id INTEGER PRIMARY KEY, run_id INTEGER NOT NULL REFERENCES recording_runs(id) ON DELETE CASCADE,
  started_ns INTEGER NOT NULL, ended_ns INTEGER
);
CREATE TABLE IF NOT EXISTS replay_requests(
  id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  generation INTEGER NOT NULL, run_id INTEGER, recording_end_ns INTEGER,
  requested_ns INTEGER NOT NULL, tag TEXT NOT NULL, source TEXT NOT NULL, status TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS replays(
  id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  request_id INTEGER REFERENCES replay_requests(id), tag TEXT NOT NULL, note TEXT NOT NULL DEFAULT '',
  replay_path TEXT NOT NULL, duration_ns INTEGER, requested_ns INTEGER, saved_ns INTEGER NOT NULL,
  saved_utc TEXT NOT NULL, probe_status TEXT NOT NULL, confidence TEXT NOT NULL, reason TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS replay_recording_spans(
  id INTEGER PRIMARY KEY, replay_id INTEGER NOT NULL REFERENCES replays(id) ON DELETE CASCADE,
  segment_id INTEGER NOT NULL REFERENCES recording_segments(id), recording_path TEXT NOT NULL,
  run_start_ns INTEGER NOT NULL, run_end_ns INTEGER NOT NULL,
  replay_start_ns INTEGER NOT NULL, replay_end_ns INTEGER NOT NULL,
  segment_start_ns INTEGER NOT NULL, segment_end_ns INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS replay_requests_pending ON replay_requests(generation, status, requested_ns);
CREATE INDEX IF NOT EXISTS replays_session ON replays(session_id, saved_ns DESC);
INSERT OR IGNORE INTO schema_migrations(version, applied_utc) VALUES(1, datetime('now'));
COMMIT;
)sql";
	if (!execute(schema))
		return false;
	Statement version(database_, "SELECT COALESCE(MAX(version),0) FROM schema_migrations");
	if (!version.valid() || version.step() != SQLITE_ROW)
		return false;
	if (version.integer(0) >= 2)
		return true;
	static constexpr const char *version2 = R"sql(
BEGIN IMMEDIATE;
ALTER TABLE replay_requests ADD COLUMN application_name TEXT NOT NULL DEFAULT '';
ALTER TABLE replay_requests ADD COLUMN window_title TEXT NOT NULL DEFAULT '';
ALTER TABLE replay_requests ADD COLUMN capture_source TEXT NOT NULL DEFAULT '';
ALTER TABLE replays ADD COLUMN rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5);
ALTER TABLE replays ADD COLUMN audio_tracks INTEGER;
ALTER TABLE replays ADD COLUMN audio_status TEXT NOT NULL DEFAULT 'unknown';
ALTER TABLE replays ADD COLUMN application_name TEXT NOT NULL DEFAULT '';
ALTER TABLE replays ADD COLUMN window_title TEXT NOT NULL DEFAULT '';
ALTER TABLE replays ADD COLUMN capture_source TEXT NOT NULL DEFAULT '';
INSERT INTO schema_migrations(version, applied_utc) VALUES(2, datetime('now'));
COMMIT;
)sql";
	return execute(version2);
}

bool Repository::recoverInterrupted()
{
	return execute("BEGIN IMMEDIATE;"
		       "UPDATE sessions SET status='interrupted' WHERE status='active';"
		       "UPDATE recording_runs SET status='interrupted' WHERE status='active';"
		       "UPDATE replay_requests SET status='abandoned' WHERE status='pending';"
		       "COMMIT;");
}

std::int64_t Repository::lastInsertId() const
{
	return sqlite3_last_insert_rowid(database_);
}

std::int64_t Repository::createSession(std::int64_t startedNs, const QString &startedUtc)
{
	Statement statement(database_, "INSERT INTO sessions(started_ns,started_utc,status) VALUES(?1,?2,'active')");
	if (!statement.valid() || !statement.bind(1, startedNs) || !statement.bind(2, startedUtc) ||
	    !succeeded(statement))
		return 0;
	return lastInsertId();
}

bool Repository::closeSession(std::int64_t sessionId, std::int64_t endedNs, const QString &endedUtc,
			      const QString &status)
{
	Statement statement(database_, "UPDATE sessions SET ended_ns=?1,ended_utc=?2,status=?3 WHERE id=?4");
	return statement.valid() && statement.bind(1, endedNs) && statement.bind(2, endedUtc) &&
	       statement.bind(3, status) && statement.bind(4, sessionId) && succeeded(statement);
}

std::int64_t Repository::createRecordingRun(std::int64_t sessionId, int ordinal, std::int64_t startedNs)
{
	Statement statement(
		database_,
		"INSERT INTO recording_runs(session_id,ordinal,started_ns,status) VALUES(?1,?2,?3,'active')");
	if (!statement.valid() || !statement.bind(1, sessionId) || !statement.bind(2, ordinal) ||
	    !statement.bind(3, startedNs) || !succeeded(statement))
		return 0;
	return lastInsertId();
}

bool Repository::closeRecordingRun(std::int64_t runId, std::int64_t endedNs)
{
	Statement statement(database_, "UPDATE recording_runs SET ended_ns=?1,status='complete' WHERE id=?2");
	return statement.valid() && statement.bind(1, endedNs) && statement.bind(2, runId) && succeeded(statement);
}

std::int64_t Repository::createSegment(std::int64_t runId, int ordinal, const QString &path, std::int64_t mediaStartNs,
				       std::int64_t startedNs)
{
	Statement statement(database_, "INSERT INTO recording_segments(run_id,ordinal,path,media_start_ns,started_ns) "
				       "VALUES(?1,?2,?3,?4,?5)");
	if (!statement.valid() || !statement.bind(1, runId) || !statement.bind(2, ordinal) ||
	    !statement.bind(3, path) || !statement.bind(4, mediaStartNs) || !statement.bind(5, startedNs) ||
	    !succeeded(statement))
		return 0;
	return lastInsertId();
}

bool Repository::closeSegment(std::int64_t segmentId, std::int64_t mediaEndNs, std::int64_t endedNs)
{
	Statement statement(database_, "UPDATE recording_segments SET media_end_ns=?1,ended_ns=?2 WHERE id=?3");
	return statement.valid() && statement.bind(1, mediaEndNs) && statement.bind(2, endedNs) &&
	       statement.bind(3, segmentId) && succeeded(statement);
}

bool Repository::updateSegmentPath(std::int64_t segmentId, const QString &path)
{
	if (path.isEmpty())
		return false;
	if (!execute("BEGIN IMMEDIATE;"))
		return false;
	Statement segment(database_, "UPDATE recording_segments SET path=?1 WHERE id=?2");
	bool okay = segment.valid() && segment.bind(1, path) && segment.bind(2, segmentId) && succeeded(segment);
	Statement spans(database_, "UPDATE replay_recording_spans SET recording_path=?1 WHERE segment_id=?2");
	okay = okay && spans.valid() && spans.bind(1, path) && spans.bind(2, segmentId) && succeeded(spans);
	if (okay)
		return execute("COMMIT;");
	execute("ROLLBACK;");
	return false;
}

std::int64_t Repository::createPause(std::int64_t runId, std::int64_t startedNs)
{
	Statement statement(database_, "INSERT INTO recording_pauses(run_id,started_ns) VALUES(?1,?2)");
	if (!statement.valid() || !statement.bind(1, runId) || !statement.bind(2, startedNs) || !succeeded(statement))
		return 0;
	return lastInsertId();
}

bool Repository::closePause(std::int64_t pauseId, std::int64_t endedNs)
{
	Statement statement(database_, "UPDATE recording_pauses SET ended_ns=?1 WHERE id=?2");
	return statement.valid() && statement.bind(1, endedNs) && statement.bind(2, pauseId) && succeeded(statement);
}

std::int64_t Repository::createRequest(std::int64_t sessionId, int generation, std::int64_t runId,
				       std::int64_t recordingEndNs, std::int64_t requestedNs, const QString &tag,
				       const ApplicationMetadata &metadata)
{
	Statement statement(database_, "INSERT INTO replay_requests(session_id,generation,run_id,recording_end_ns,"
				       "requested_ns,tag,source,status,application_name,window_title,capture_source) "
				       "VALUES(?1,?2,?3,?4,?5,?6,'plugin','pending',?7,?8,?9)");
	if (!statement.valid() || !statement.bind(1, sessionId) || !statement.bind(2, generation) ||
	    (runId ? !statement.bind(3, runId) : !statement.bindNull(3)) ||
	    (recordingEndNs >= 0 ? !statement.bind(4, recordingEndNs) : !statement.bindNull(4)) ||
	    !statement.bind(5, requestedNs) || !statement.bind(6, tag) ||
	    !statement.bind(7, metadata.applicationName) || !statement.bind(8, metadata.windowTitle) ||
	    !statement.bind(9, metadata.sourceName) || !succeeded(statement))
		return 0;
	return lastInsertId();
}

std::optional<PendingRequest> Repository::resolveOldestPending(int generation, std::int64_t savedNs)
{
	constexpr std::int64_t requestTimeoutNs = 120'000'000'000;
	Statement expire(database_, "UPDATE replay_requests SET status='abandoned' WHERE generation=?1 AND "
				    "status='pending' AND requested_ns<?2");
	if (expire.valid() && expire.bind(1, generation) && expire.bind(2, savedNs - requestTimeoutNs))
		expire.step();

	Statement query(
		database_,
		"SELECT id,COALESCE(run_id,0),COALESCE(recording_end_ns,-1),requested_ns,tag,"
		"application_name,window_title,capture_source "
		"FROM replay_requests WHERE generation=?1 AND status='pending' AND requested_ns BETWEEN ?3 AND ?2 "
		"ORDER BY requested_ns,id LIMIT 1");
	if (!query.valid() || !query.bind(1, generation) || !query.bind(2, savedNs) ||
	    !query.bind(3, savedNs - requestTimeoutNs) || query.step() != SQLITE_ROW)
		return std::nullopt;

	PendingRequest result{query.integer(0), query.integer(1), query.integer(2),
			      query.integer(3), query.text(4),    {query.text(5), query.text(6), query.text(7)}};
	Statement update(database_, "UPDATE replay_requests SET status='resolved' WHERE id=?1 AND status='pending'");
	if (!update.valid() || !update.bind(1, result.id) || !succeeded(update))
		return std::nullopt;
	return result;
}

std::int64_t Repository::createReplay(std::int64_t sessionId, const std::optional<PendingRequest> &request,
				      std::int64_t savedNs, const QString &savedUtc, const QString &path,
				      const ApplicationMetadata &metadata)
{
	const ApplicationMetadata &captured = request ? request->metadata : metadata;
	Statement statement(database_,
			    "INSERT INTO replays(session_id,request_id,tag,replay_path,requested_ns,saved_ns,"
			    "saved_utc,probe_status,confidence,reason,application_name,window_title,capture_source) "
			    "VALUES(?1,?2,?3,?4,?5,?6,?7,'pending',?8,?9,?10,?11,?12)");
	const QString tag = request ? request->tag : QStringLiteral("External");
	const QString confidence = request ? QStringLiteral("pending") : QStringLiteral("low");
	const QString reason = request ? QStringLiteral("duration probe pending")
				       : QStringLiteral("saved without a matching plugin request");
	if (!statement.valid() || !statement.bind(1, sessionId) ||
	    (request ? !statement.bind(2, request->id) : !statement.bindNull(2)) || !statement.bind(3, tag) ||
	    !statement.bind(4, path) || (request ? !statement.bind(5, request->requestedNs) : !statement.bindNull(5)) ||
	    !statement.bind(6, savedNs) || !statement.bind(7, savedUtc) || !statement.bind(8, confidence) ||
	    !statement.bind(9, reason) || !statement.bind(10, captured.applicationName) ||
	    !statement.bind(11, captured.windowTitle) || !statement.bind(12, captured.sourceName) ||
	    !succeeded(statement))
		return 0;
	return lastInsertId();
}

bool Repository::replayProbeTarget(std::int64_t replayId, QString &path, QString &savedUtc, std::int64_t &savedNs,
				   std::optional<PendingRequest> &request) const
{
	Statement query(database_, R"sql(
SELECT r.replay_path,r.saved_utc,r.saved_ns,q.id,COALESCE(q.run_id,0),COALESCE(q.recording_end_ns,-1),
       COALESCE(q.requested_ns,0),COALESCE(q.tag,''),COALESCE(q.application_name,''),
       COALESCE(q.window_title,''),COALESCE(q.capture_source,'')
FROM replays r LEFT JOIN replay_requests q ON q.id=r.request_id WHERE r.id=?1
)sql");
	if (!query.valid() || !query.bind(1, replayId) || query.step() != SQLITE_ROW)
		return false;
	path = query.text(0);
	savedUtc = query.text(1);
	savedNs = query.integer(2);
	if (query.isNull(3))
		request.reset();
	else
		request = PendingRequest{query.integer(3), query.integer(4),
					 query.integer(5), query.integer(6),
					 query.text(7),    {query.text(8), query.text(9), query.text(10)}};
	return true;
}

bool Repository::updateReplayPath(std::int64_t replayId, const QString &path)
{
	Statement statement(database_, "UPDATE replays SET replay_path=?1 WHERE id=?2");
	return statement.valid() && statement.bind(1, path) && statement.bind(2, replayId) && succeeded(statement);
}

bool Repository::completeProbe(std::int64_t replayId, std::int64_t durationNs, int audioTracks,
			       const QString &audioStatus, const QString &probeStatus, const QString &confidence,
			       const QString &reason, const std::vector<domain::AssociationSpan> &spans)
{
	if (!execute("BEGIN IMMEDIATE;"))
		return false;
	Statement remove(database_, "DELETE FROM replay_recording_spans WHERE replay_id=?1");
	bool okay = remove.valid() && remove.bind(1, replayId) && succeeded(remove);

	Statement update(database_, "UPDATE replays SET duration_ns=?1,audio_tracks=?2,audio_status=?3,probe_status=?4,"
				    "confidence=?5,reason=?6 WHERE id=?7");
	okay = okay && update.valid() && (durationNs >= 0 ? update.bind(1, durationNs) : update.bindNull(1)) &&
	       (audioTracks >= 0 ? update.bind(2, audioTracks) : update.bindNull(2)) && update.bind(3, audioStatus) &&
	       update.bind(4, probeStatus) && update.bind(5, confidence) && update.bind(6, reason) &&
	       update.bind(7, replayId) && succeeded(update);
	for (const domain::AssociationSpan &span : spans) {
		if (!okay)
			break;
		Statement insert(database_, "INSERT INTO replay_recording_spans(replay_id,segment_id,recording_path,"
					    "run_start_ns,run_end_ns,replay_start_ns,replay_end_ns,segment_start_ns,"
					    "segment_end_ns) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)");
		okay = insert.valid() && insert.bind(1, replayId) && insert.bind(2, span.segmentId) &&
		       insert.bind(3, QString::fromStdString(span.recordingPath)) && insert.bind(4, span.runStart) &&
		       insert.bind(5, span.runEnd) && insert.bind(6, span.replayStart) &&
		       insert.bind(7, span.replayEnd) && insert.bind(8, span.segmentStart) &&
		       insert.bind(9, span.segmentEnd) && succeeded(insert);
	}

	if (okay)
		return execute("COMMIT;");
	execute("ROLLBACK;");
	return false;
}

std::vector<domain::RecordingSegment> Repository::segmentsForRun(std::int64_t runId, std::int64_t openEndNs) const
{
	std::vector<domain::RecordingSegment> result;
	Statement query(database_, "SELECT id,path,media_start_ns,COALESCE(media_end_ns,?2) FROM recording_segments "
				   "WHERE run_id=?1 ORDER BY ordinal,id");
	if (!query.valid() || !query.bind(1, runId) || !query.bind(2, openEndNs))
		return result;
	while (query.step() == SQLITE_ROW)
		result.push_back({query.integer(0), query.text(1).toStdString(), query.integer(2), query.integer(3)});
	return result;
}

std::vector<SessionSummary> Repository::sessions() const
{
	std::vector<SessionSummary> result;
	Statement query(database_, "SELECT id,started_utc,status FROM sessions ORDER BY started_ns DESC,id DESC");
	if (!query.valid())
		return result;
	while (query.step() == SQLITE_ROW)
		result.push_back({query.integer(0), query.text(1), query.text(2)});
	return result;
}

std::vector<ReplayRow> Repository::replays(std::int64_t sessionId) const
{
	std::vector<ReplayRow> result;
	Statement query(database_, R"sql(
SELECT r.id,r.session_id,r.saved_utc,
       MIN(s.run_start_ns),MAX(s.run_end_ns),r.tag,r.note,COALESCE(r.duration_ns,-1),r.replay_path,
       COALESCE(GROUP_CONCAT(DISTINCT s.recording_path),''),r.confidence,r.probe_status,r.rating,
       COALESCE(r.audio_tracks,-1),r.audio_status,r.application_name,r.window_title,r.capture_source
FROM replays r LEFT JOIN replay_recording_spans s ON s.replay_id=r.id
WHERE r.session_id=?1 GROUP BY r.id ORDER BY r.saved_ns DESC,r.id DESC
)sql");
	if (!query.valid() || !query.bind(1, sessionId))
		return result;
	while (query.step() == SQLITE_ROW) {
		result.push_back({query.integer(0), query.integer(1), query.text(2),
				  query.isNull(3) ? -1 : query.integer(3), query.isNull(4) ? -1 : query.integer(4),
				  query.text(5), query.text(6), query.integer(7), query.text(8), query.text(9),
				  query.text(10), query.text(11), static_cast<int>(query.integer(12)),
				  static_cast<int>(query.integer(13)), query.text(14), query.text(15), query.text(16),
				  query.text(17)});
	}
	return result;
}

std::vector<CsvRow> Repository::csvRows(std::int64_t sessionId) const
{
	std::vector<CsvRow> result;
	Statement query(database_, R"sql(
SELECT r.session_id,r.id,r.saved_utc,COALESCE(rr.ordinal,0),COALESCE(rs.ordinal,0),
       COALESCE(s.run_start_ns,-1),COALESCE(s.run_end_ns,-1),
       COALESCE(s.segment_start_ns,-1),COALESCE(s.segment_end_ns,-1),r.tag,r.note,r.rating,
       r.application_name,r.window_title,r.capture_source,COALESCE(r.duration_ns,-1),
       COALESCE(r.audio_tracks,-1),r.audio_status,r.replay_path,COALESCE(s.recording_path,''),
       r.confidence,r.probe_status,r.reason
FROM replays r
LEFT JOIN replay_recording_spans s ON s.replay_id=r.id
LEFT JOIN recording_segments rs ON rs.id=s.segment_id
LEFT JOIN recording_runs rr ON rr.id=rs.run_id
WHERE (?1=0 OR r.session_id=?1) ORDER BY r.saved_ns,r.id,s.id
)sql");
	if (!query.valid() || !query.bind(1, sessionId))
		return result;
	while (query.step() == SQLITE_ROW) {
		result.push_back({query.integer(0),
				  query.integer(1),
				  query.text(2),
				  query.integer(3),
				  query.integer(4),
				  query.integer(5),
				  query.integer(6),
				  query.integer(7),
				  query.integer(8),
				  query.text(9),
				  query.text(10),
				  static_cast<int>(query.integer(11)),
				  query.text(12),
				  query.text(13),
				  query.text(14),
				  query.integer(15),
				  static_cast<int>(query.integer(16)),
				  query.text(17),
				  query.text(18),
				  query.text(19),
				  query.text(20),
				  query.text(21),
				  query.text(22)});
	}
	return result;
}

bool Repository::clearSessions()
{
	if (!execute("BEGIN IMMEDIATE;"))
		return false;
	Statement remove(database_, "DELETE FROM sessions");
	if (succeeded(remove) && execute("COMMIT;"))
		return true;
	execute("ROLLBACK;");
	return false;
}

bool Repository::updateReplay(std::int64_t replayId, const QString &tag, const QString &note, int rating)
{
	if (rating < 0 || rating > 5)
		return false;
	Statement statement(database_, "UPDATE replays SET tag=?1,note=?2,rating=?3 WHERE id=?4");
	return statement.valid() && statement.bind(1, tag) && statement.bind(2, note) && statement.bind(3, rating) &&
	       statement.bind(4, replayId) && succeeded(statement);
}

QString Repository::setting(const QString &key, const QString &fallback) const
{
	Statement query(database_, "SELECT value FROM settings WHERE key=?1");
	if (!query.valid() || !query.bind(1, key) || query.step() != SQLITE_ROW)
		return fallback;
	return query.text(0);
}

bool Repository::setSetting(const QString &key, const QString &value)
{
	Statement statement(database_, "INSERT INTO settings(key,value) VALUES(?1,?2) "
				       "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
	return statement.valid() && statement.bind(1, key) && statement.bind(2, value) && succeeded(statement);
}

} // namespace replay_timeline
