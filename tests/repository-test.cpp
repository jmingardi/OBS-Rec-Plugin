#include "domain/timeline-mapper.hpp"
#include "persistence/repository.hpp"

#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QTemporaryDir>

using replay_timeline::PendingRequest;
using replay_timeline::Repository;
using replay_timeline::domain::RecordingSegment;
using replay_timeline::domain::TimelineMapper;

namespace {
void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	QTemporaryDir directory;
	expect(directory.isValid(), "temporary directory is available");
	const QString databasePath = directory.filePath(QStringLiteral("metadata.sqlite3"));

	Repository repository;
	QString error;
	expect(repository.open(databasePath, error), "database opens and migrates");
	expect(repository.recoverInterrupted(), "initial recovery succeeds");
	const std::int64_t session = repository.createSession(100, QStringLiteral("2026-08-26T00:00:00.000Z"));
	const std::int64_t run = repository.createRecordingRun(session, 1, 100);
	const std::int64_t first = repository.createSegment(run, 1, QStringLiteral("C:/vídeo/parte,1.mkv"), 0, 100);
	const std::int64_t second =
		repository.createSegment(run, 2, QStringLiteral("C:/vídeo/parte-2.mkv"), 30'000, 30'100);
	expect(first > 0 && second > 0, "recording segments persist");
	expect(repository.closeSegment(first, 30'000, 30'100), "first split closes");
	expect(repository.closeSegment(second, 90'000, 90'100), "second split closes");
	expect(repository.updateSegmentPath(first, QStringLiteral("C:/vídeo/final-1.mkv")),
	       "final recording path can replace an initial placeholder");

	const replay_timeline::ApplicationMetadata metadata{QStringLiteral("game.exe"), QStringLiteral("Game Window"),
							    QStringLiteral("Game Capture")};
	const std::int64_t requestId =
		repository.createRequest(session, 1, run, 42'000, 42'100, QStringLiteral("Bug"), metadata);
	expect(requestId > 0, "tagged request persists");
	const std::optional<PendingRequest> request = repository.resolveOldestPending(1, 42'200);
	expect(request && request->id == requestId && request->tag == QStringLiteral("Bug"),
	       "oldest compatible request resolves");
	const std::int64_t replay = repository.createReplay(session, request, 42'200,
							    QStringLiteral("2026-08-26T00:00:42.200Z"),
							    QStringLiteral("C:/replays/ação.mp4"));
	const std::vector<RecordingSegment> segments = repository.segmentsForRun(run, 42'000);
	const auto spans = TimelineMapper::mapReplay(42'000, 20'000, segments);
	expect(spans.size() == 2, "repository segments feed split-aware mapping");
	expect(repository.completeProbe(replay, 20'000, 2, QStringLiteral("ok"), QStringLiteral("complete"),
					QStringLiteral("approximate"), QStringLiteral("test"), spans),
	       "probe result and spans commit atomically");
	expect(repository.updateReplay(replay, QStringLiteral("Falha"), QStringLiteral("nota, com\nnova linha ✓"), 4),
	       "Unicode editable metadata persists");
	expect(!repository.updateReplay(replay, QStringLiteral("Falha"), QString(), 6),
	       "ratings outside zero through five are rejected");

	const auto rows = repository.replays(session);
	expect(rows.size() == 1 && rows.front().tag == QStringLiteral("Falha") &&
		       rows.front().recordingStartNs == 22'000 && rows.front().recordingEndNs == 42'000 &&
		       rows.front().rating == 4 && rows.front().audioTracks == 2 &&
		       rows.front().audioStatus == QStringLiteral("ok") &&
		       rows.front().applicationName == QStringLiteral("game.exe") &&
		       rows.front().windowTitle == QStringLiteral("Game Window") &&
		       rows.front().captureSource == QStringLiteral("Game Capture"),
	       "review query reconstructs mapped replay");
	const auto selectedCsv = repository.csvRows(session);
	expect(selectedCsv.size() == 2 && selectedCsv.front().recordingPath == QStringLiteral("C:/vídeo/final-1.mkv") &&
		       selectedCsv.front().probeStatus == QStringLiteral("complete") &&
		       selectedCsv.front().rating == 4 && selectedCsv.front().audioTracks == 2 &&
		       selectedCsv.front().applicationName == QStringLiteral("game.exe"),
	       "split replay exports finalized paths and probe diagnostics");
	expect(selectedCsv[0].runStartNs == 22'000 && selectedCsv[0].runEndNs == 30'000 &&
		       selectedCsv[0].segmentStartNs == 22'000 && selectedCsv[0].segmentEndNs == 30'000 &&
		       selectedCsv[1].runStartNs == 30'000 && selectedCsv[1].runEndNs == 42'000 &&
		       selectedCsv[1].segmentStartNs == 0 && selectedCsv[1].segmentEndNs == 12'000,
	       "split replay exports both run-global and file-local intervals");

	const std::int64_t interrupted = repository.createSession(100'000, QStringLiteral("2026-08-26T01:00:00.000Z"));
	expect(repository.createRequest(interrupted, 2, 0, -1, 100'100, QStringLiteral("Keep")) > 0,
	       "pending request exists before restart");
	repository.close();
	expect(repository.open(databasePath, error) && repository.recoverInterrupted(), "restart recovery succeeds");
	expect(!repository.resolveOldestPending(2, 200'000), "recovery abandons unresolved requests");
	const auto sessions = repository.sessions();
	expect(!sessions.empty() && sessions.front().status == QStringLiteral("interrupted"),
	       "recovery marks open session interrupted");
	expect(repository.csvRows().size() == 2, "all-session CSV query includes every replay span");
	expect(repository.setSetting(QStringLiteral("tag_names"), QStringLiteral("Funny\nKeep")),
	       "settings persist before clearing sessions");
	expect(repository.clearSessions(), "session metadata clears transactionally");
	expect(repository.sessions().empty() && repository.csvRows().empty(),
	       "clearing removes every session and replay");
	expect(repository.setting(QStringLiteral("tag_names"), {}) == QStringLiteral("Funny\nKeep"),
	       "clearing sessions preserves plugin settings");

	std::cout << "repository tests passed\n";
	return EXIT_SUCCESS;
}
