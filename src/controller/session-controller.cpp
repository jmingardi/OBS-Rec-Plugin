#include "session-controller.hpp"

#include "export/csv-exporter.hpp"
#include "media/media-probe.hpp"
#include "ui/replay-timeline-dock.hpp"

#include <algorithm>

#include <QDateTime>
#include <QMetaObject>
#include <QThreadPool>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <plugin-support.h>

namespace replay_timeline {
namespace {
QString utcNow()
{
	return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString detailValue(const QString &detail)
{
	const qsizetype separator = detail.indexOf(QLatin1Char('='));
	return separator >= 0 ? detail.mid(separator + 1) : detail;
}

} // namespace

SessionController::SessionController(ReplayTimelineDock *dock) : QObject(dock), dock_(dock) {}

SessionController::~SessionController()
{
	stop();
}

bool SessionController::start(const QString &databasePath, QString &error)
{
	if (started_)
		return true;
	if (!repository_.open(databasePath, error) || !repository_.recoverInterrupted()) {
		if (error.isEmpty())
			error = repository_.lastError();
		return false;
	}

	started_ = true;
	loadTags();
	registerHotkeys();
	if (dock_) {
		dock_->setCallbacks({
			[this](std::int64_t sessionId) {
				selectedSessionId_ = sessionId;
				refresh();
			},
			[this](std::int64_t replayId, const QString &tag, const QString &note) {
				if (!repository_.updateReplay(replayId, tag, note) && dock_)
					dock_->showMessage(repository_.lastError(), true);
			},
			[this](const QString &path) { exportCsv(path); },
			[this](const QStringList &tags) { configureTags(tags); },
			[this](std::int64_t replayId) { retryProbe(replayId); },
			[this]() { refresh(); },
		});
		dock_->setTagNames(tags_);
	}

	const std::int64_t now = static_cast<std::int64_t>(os_gettime_ns());
	recordingActive_ = obs_frontend_recording_active();
	replayActive_ = obs_frontend_replay_buffer_active();
	if (replayActive_)
		replayGeneration_ = 1;
	if (recordingActive_ || replayActive_)
		ensureSession(now);
	if (recordingActive_) {
		char *rawPath = obs_frontend_get_current_record_output_path();
		const QString path = rawPath ? QString::fromUtf8(rawPath) : QStringLiteral("<unavailable>");
		bfree(rawPath);
		beginRecording(now, path);
		if (obs_frontend_recording_paused())
			beginPause(now);
	}
	refresh(activeSessionId_);
	return true;
}

void SessionController::stop()
{
	if (!started_)
		return;
	unregisterHotkeys();
	repository_.close();
	started_ = false;
}

void SessionController::postObsEvent(const QString &eventName, const QString &detail,
				     std::uint64_t monotonicNanoseconds)
{
	QPointer<SessionController> self(this);
	QMetaObject::invokeMethod(
		this,
		[self, eventName, detail, monotonicNanoseconds]() {
			if (self)
				self->handleObsEvent(eventName, detail,
						     static_cast<std::int64_t>(monotonicNanoseconds));
		},
		Qt::QueuedConnection);
}

void SessionController::hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *controller = static_cast<SessionController *>(data);
	const auto registration = std::find_if(controller->hotkeys_.begin(), controller->hotkeys_.end(),
					       [id](const HotkeyRegistration &entry) { return entry.id == id; });
	if (registration == controller->hotkeys_.end())
		return;
	const QString tag = registration->tag;
	const std::int64_t now = static_cast<std::int64_t>(os_gettime_ns());
	QPointer<SessionController> self(controller);
	QMetaObject::invokeMethod(
		controller,
		[self, tag, now]() {
			if (self)
				self->requestReplay(tag, now);
		},
		Qt::QueuedConnection);
}

void SessionController::handleObsEvent(const QString &eventName, const QString &detail, std::int64_t now)
{
	if (!started_)
		return;
	if (eventName == QStringLiteral("RECORDING_STARTED")) {
		recordingActive_ = true;
		ensureSession(now);
		beginRecording(now, detailValue(detail));
	} else if (eventName == QStringLiteral("RECORDING_PAUSED")) {
		beginPause(now);
	} else if (eventName == QStringLiteral("RECORDING_UNPAUSED")) {
		endPause(now);
	} else if (eventName == QStringLiteral("RECORDING_FILE_CHANGED")) {
		splitRecording(now, detailValue(detail));
	} else if (eventName == QStringLiteral("RECORDING_STOPPED")) {
		endRecording(now);
		recordingActive_ = false;
		closeSessionIfIdle(now);
	} else if (eventName == QStringLiteral("REPLAY_BUFFER_STARTED")) {
		replayActive_ = true;
		++replayGeneration_;
		ensureSession(now);
	} else if (eventName == QStringLiteral("REPLAY_BUFFER_STOPPED")) {
		replayActive_ = false;
		closeSessionIfIdle(now);
	} else if (eventName == QStringLiteral("REPLAY_BUFFER_SAVED")) {
		ensureSession(now);
		const QString path = detailValue(detail);
		const std::optional<PendingRequest> request = repository_.resolveOldestPending(replayGeneration_, now);
		const std::int64_t replayId = repository_.createReplay(activeSessionId_, request, now, utcNow(), path);
		if (replayId)
			startProbe(replayId, path, request);
		refresh(activeSessionId_);
		closeSessionIfIdle(now);
	}
}

void SessionController::requestReplay(const QString &tag, std::int64_t requestedNs)
{
	replayActive_ = obs_frontend_replay_buffer_active();
	if (!replayActive_) {
		if (dock_)
			dock_->showMessage(
				QStringLiteral("Replay Buffer is not active; no replay request was created."), true);
		return;
	}
	ensureSession(requestedNs);
	const std::int64_t runId = activeRun_ ? activeRun_->id : 0;
	const std::int64_t recordingEnd = activeRun_ ? currentMediaTime(requestedNs) : -1;
	if (!repository_.createRequest(activeSessionId_, replayGeneration_, runId, recordingEnd, requestedNs, tag)) {
		if (dock_)
			dock_->showMessage(repository_.lastError(), true);
		return;
	}
	obs_frontend_replay_buffer_save();
	if (dock_)
		dock_->showMessage(QStringLiteral("Requested replay tagged “%1”.").arg(tag));
}

void SessionController::beginRecording(std::int64_t now, const QString &path)
{
	if (activeRun_)
		return;
	ensureSession(now);
	ActiveRun run;
	run.startedNs = now;
	run.ordinal = nextRunOrdinal_++;
	run.id = repository_.createRecordingRun(activeSessionId_, run.ordinal, now);
	run.segmentOrdinal = 1;
	run.segmentId = repository_.createSegment(run.id, run.segmentOrdinal,
						  path.isEmpty() ? QStringLiteral("<unavailable>") : path, 0, now);
	activeRun_ = std::move(run);
}

void SessionController::endRecording(std::int64_t now)
{
	if (!activeRun_)
		return;
	endPause(now);
	const std::int64_t mediaEnd = currentMediaTime(now);
	if (activeRun_->segmentId)
		repository_.closeSegment(activeRun_->segmentId, mediaEnd, now);
	repository_.closeRecordingRun(activeRun_->id, now);
	activeRun_.reset();
}

void SessionController::beginPause(std::int64_t now)
{
	if (!activeRun_ || activeRun_->openPauseId)
		return;
	activeRun_->openPauseStartedNs = now;
	activeRun_->openPauseId = repository_.createPause(activeRun_->id, now);
}

void SessionController::endPause(std::int64_t now)
{
	if (!activeRun_ || !activeRun_->openPauseId)
		return;
	repository_.closePause(activeRun_->openPauseId, now);
	activeRun_->pauses.push_back({activeRun_->openPauseStartedNs, now});
	activeRun_->openPauseId = 0;
	activeRun_->openPauseStartedNs = 0;
}

void SessionController::splitRecording(std::int64_t now, const QString &path)
{
	if (!activeRun_)
		return;
	const std::int64_t mediaBoundary = currentMediaTime(now);
	if (activeRun_->segmentId)
		repository_.closeSegment(activeRun_->segmentId, mediaBoundary, now);
	++activeRun_->segmentOrdinal;
	activeRun_->segmentId = repository_.createSegment(activeRun_->id, activeRun_->segmentOrdinal,
							  path.isEmpty() ? QStringLiteral("<unavailable>") : path,
							  mediaBoundary, now);
}

std::int64_t SessionController::currentMediaTime(std::int64_t now) const
{
	if (!activeRun_)
		return -1;
	std::vector<domain::PauseInterval> pauses = activeRun_->pauses;
	if (activeRun_->openPauseId)
		pauses.push_back({activeRun_->openPauseStartedNs, now});
	return domain::TimelineMapper::mediaTime(activeRun_->startedNs, now, pauses);
}

void SessionController::ensureSession(std::int64_t now)
{
	if (activeSessionId_)
		return;
	activeSessionId_ = repository_.createSession(now, utcNow());
	selectedSessionId_ = activeSessionId_;
	nextRunOrdinal_ = 1;
}

void SessionController::closeSessionIfIdle(std::int64_t now)
{
	if (!activeSessionId_ || recordingActive_ || replayActive_)
		return;
	repository_.closeSession(activeSessionId_, now, utcNow());
	selectedSessionId_ = activeSessionId_;
	activeSessionId_ = 0;
	refresh(selectedSessionId_);
}

void SessionController::startProbe(std::int64_t replayId, const QString &path, std::optional<PendingRequest> request)
{
	QPointer<SessionController> self(this);
	QThreadPool::globalInstance()->start([self, replayId, path, request = std::move(request)]() {
		const MediaProbeResult result = probeMediaDuration(path);
		if (!self)
			return;
		QMetaObject::invokeMethod(
			self,
			[self, replayId, request, result]() {
				if (self)
					self->finishProbe(replayId, request, result.durationNs, result.error);
			},
			Qt::QueuedConnection);
	});
}

void SessionController::retryProbe(std::int64_t replayId)
{
	QString path;
	std::optional<PendingRequest> request;
	if (!repository_.replayProbeTarget(replayId, path, request)) {
		if (dock_)
			dock_->showMessage(QStringLiteral("Unable to reload replay metadata for probing."), true);
		return;
	}
	startProbe(replayId, path, request);
	if (dock_)
		dock_->showMessage(QStringLiteral("Retrying media duration probe…"));
}

void SessionController::finishProbe(std::int64_t replayId, const std::optional<PendingRequest> &request,
				    std::int64_t durationNs, const QString &error)
{
	std::vector<domain::AssociationSpan> spans;
	QString probeStatus = QStringLiteral("failed");
	QString confidence = request ? QStringLiteral("unmapped") : QStringLiteral("low");
	QString reason = error;
	if (durationNs > 0) {
		probeStatus = QStringLiteral("complete");
		if (request && request->runId && request->recordingEndNs >= 0) {
			const auto segments = repository_.segmentsForRun(request->runId, request->recordingEndNs);
			spans = domain::TimelineMapper::mapReplay(request->recordingEndNs, durationNs, segments);
			if (!spans.empty()) {
				confidence = QStringLiteral("approximate");
				reason = QStringLiteral("plugin request timestamp and probed replay duration");
			} else {
				reason = QStringLiteral("recording interval did not overlap a known segment");
			}
		} else if (request) {
			reason = QStringLiteral("Replay Buffer-only request; no recording run was active");
		} else {
			reason = QStringLiteral("native/external save has no precise request timestamp");
		}
	}
	repository_.completeProbe(replayId, durationNs, probeStatus, confidence, reason, spans);
	refresh();
}

void SessionController::loadTags()
{
	tags_ = repository_.setting(QStringLiteral("tag_names"), QStringLiteral("Funny\nKill\nBug\nKeep"))
			.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	if (tags_.isEmpty())
		tags_ = {QStringLiteral("Keep")};
}

void SessionController::configureTags(const QStringList &tags)
{
	if (tags.isEmpty())
		return;
	tags_ = tags;
	repository_.setSetting(QStringLiteral("tag_names"), tags_.join(QLatin1Char('\n')));
	unregisterHotkeys();
	registerHotkeys();
	if (dock_) {
		dock_->setTagNames(tags_);
		dock_->showMessage(QStringLiteral("Tag hotkeys updated. Assign them in OBS Settings → Hotkeys."));
	}
}

void SessionController::registerHotkeys()
{
	for (int index = 0; index < tags_.size(); ++index) {
		const QByteArray name = QStringLiteral("ReplayTimeline.TagSlot%1").arg(index + 1).toUtf8();
		const QByteArray description =
			QStringLiteral("Replay Timeline: Save Replay — %1").arg(tags_[index]).toUtf8();
		const obs_hotkey_id id =
			obs_hotkey_register_frontend(name.constData(), description.constData(), hotkeyCallback, this);
		if (id != OBS_INVALID_HOTKEY_ID)
			hotkeys_.push_back({id, tags_[index]});
	}
}

void SessionController::unregisterHotkeys()
{
	for (const HotkeyRegistration &registration : hotkeys_)
		obs_hotkey_unregister(registration.id);
	hotkeys_.clear();
}

void SessionController::refresh(std::int64_t preferredSession)
{
	if (!dock_ || !repository_.isOpen())
		return;
	const std::vector<SessionSummary> sessions = repository_.sessions();
	if (preferredSession)
		selectedSessionId_ = preferredSession;
	if (!selectedSessionId_ && !sessions.empty())
		selectedSessionId_ = sessions.front().id;
	dock_->setSessions(sessions, selectedSessionId_);
	dock_->setReplayRows(selectedSessionId_ ? repository_.replays(selectedSessionId_) : std::vector<ReplayRow>{});
}

void SessionController::exportCsv(const QString &path)
{
	QString error;
	if (!writeCsv(path, repository_.csvRows(selectedSessionId_), error)) {
		if (dock_)
			dock_->showMessage(error, true);
		return;
	}
	if (dock_)
		dock_->showMessage(QStringLiteral("CSV exported to %1").arg(path));
}

} // namespace replay_timeline
