#include "obs-event-bridge.hpp"

#include "../controller/session-controller.hpp"
#include "../ui/replay-timeline-dock.hpp"

#include <cstring>

#include <QByteArray>
#include <QMetaObject>

#include <callback/signal.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <plugin-support.h>

namespace replay_timeline {
namespace {
QString eventName(obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTING:
		return QStringLiteral("RECORDING_STARTING");
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		return QStringLiteral("RECORDING_STARTED");
	case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
		return QStringLiteral("RECORDING_STOPPING");
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		return QStringLiteral("RECORDING_STOPPED");
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		return QStringLiteral("RECORDING_PAUSED");
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		return QStringLiteral("RECORDING_UNPAUSED");
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
		return QStringLiteral("REPLAY_BUFFER_STARTING");
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		return QStringLiteral("REPLAY_BUFFER_STARTED");
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING:
		return QStringLiteral("REPLAY_BUFFER_STOPPING");
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		return QStringLiteral("REPLAY_BUFFER_STOPPED");
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		return QStringLiteral("REPLAY_BUFFER_SAVED");
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		return QStringLiteral("OBS_FINISHED_LOADING");
	case OBS_FRONTEND_EVENT_EXIT:
		return QStringLiteral("OBS_EXIT");
	default:
		return {};
	}
}

QString takeFrontendString(char *value)
{
	const QString result = value ? QString::fromUtf8(value) : QString();
	bfree(value);
	return result;
}

QString outputFilePath(obs_output_t *output)
{
	if (!output)
		return {};
	obs_data_t *settings = obs_output_get_settings(output);
	if (!settings)
		return {};
	const char *rawPath = obs_data_get_string(settings, "path");
	const QString path = rawPath ? QString::fromUtf8(rawPath) : QString();
	obs_data_release(settings);
	return path;
}
} // namespace

ObsEventBridge::ObsEventBridge(ReplayTimelineDock *dock, SessionController *controller)
	: dock_(dock),
	  controller_(controller)
{
}

ObsEventBridge::~ObsEventBridge()
{
	stop();
}

void ObsEventBridge::start()
{
	if (started_)
		return;

	started_ = true;
	obs_frontend_add_event_callback(frontendEventCallback, this);
	if (obs_frontend_recording_active())
		attachRecordingOutput();

	publish(QStringLiteral("PLUGIN_STARTED"), QStringLiteral("frontend callback registered"), os_gettime_ns());
	publishState();
}

void ObsEventBridge::stop()
{
	if (!started_)
		return;

	obs_frontend_remove_event_callback(frontendEventCallback, this);
	detachRecordingOutput();
	started_ = false;
}

void ObsEventBridge::frontendEventCallback(obs_frontend_event event, void *privateData)
{
	static_cast<ObsEventBridge *>(privateData)->handleFrontendEvent(event);
}

void ObsEventBridge::recordingOutputSignalCallback(void *privateData, const char *signal, calldata_t *parameters)
{
	if (!signal || std::strcmp(signal, "file_changed") != 0)
		return;

	auto *bridge = static_cast<ObsEventBridge *>(privateData);
	const char *nextFile = calldata_string(parameters, "next_file");
	const QString detail = nextFile ? QStringLiteral("next_file=%1").arg(QString::fromUtf8(nextFile))
					: QStringLiteral("next_file=<missing>");
	bridge->publish(QStringLiteral("RECORDING_FILE_CHANGED"), detail, os_gettime_ns());
}

void ObsEventBridge::handleFrontendEvent(obs_frontend_event event)
{
	const std::uint64_t now = os_gettime_ns();
	QString name = eventName(event);
	if (name.isEmpty())
		return;

	QString detail;
	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		attachRecordingOutput();
		QString path = outputFilePath(recordingOutput_);
		if (path.isEmpty())
			path = takeFrontendString(obs_frontend_get_current_record_output_path());
		if (!path.isEmpty())
			detail = QStringLiteral("path=%1").arg(path);
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		const QString path = takeFrontendString(obs_frontend_get_last_recording());
		if (!path.isEmpty())
			detail = QStringLiteral("path=%1").arg(path);
	} else if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED) {
		const QString path = takeFrontendString(obs_frontend_get_last_replay());
		detail = path.isEmpty() ? QStringLiteral("path=<unavailable>") : QStringLiteral("path=%1").arg(path);
	}

	publish(name, detail, now);
	publishState();

	if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED)
		detachRecordingOutput();
}

void ObsEventBridge::attachRecordingOutput()
{
	detachRecordingOutput();
	recordingOutput_ = obs_frontend_get_recording_output();
	if (!recordingOutput_) {
		publish(QStringLiteral("RECORDING_OUTPUT_UNAVAILABLE"), {}, os_gettime_ns());
		return;
	}

	signal_handler_t *handler = obs_output_get_signal_handler(recordingOutput_);
	if (!handler) {
		publish(QStringLiteral("RECORDING_SIGNAL_HANDLER_UNAVAILABLE"), {}, os_gettime_ns());
		obs_output_release(recordingOutput_);
		recordingOutput_ = nullptr;
		return;
	}

	signal_handler_connect_global(handler, recordingOutputSignalCallback, this);
	publish(QStringLiteral("RECORDING_OUTPUT_ATTACHED"), QStringLiteral("listening for file_changed"),
		os_gettime_ns());
}

void ObsEventBridge::detachRecordingOutput()
{
	if (!recordingOutput_)
		return;

	if (signal_handler_t *handler = obs_output_get_signal_handler(recordingOutput_))
		signal_handler_disconnect_global(handler, recordingOutputSignalCallback, this);

	obs_output_release(recordingOutput_);
	recordingOutput_ = nullptr;
}

void ObsEventBridge::publish(const QString &name, const QString &detail, std::uint64_t monotonicNanoseconds)
{
	QPointer<SessionController> controller = controller_;
	if (controller)
		controller->postObsEvent(name, detail, monotonicNanoseconds);

	QPointer<ReplayTimelineDock> dock = dock_;
	if (dock) {
		QMetaObject::invokeMethod(
			dock,
			[dock, name, detail, monotonicNanoseconds]() {
				if (dock)
					dock->appendDiagnostic(name, detail, monotonicNanoseconds);
			},
			Qt::QueuedConnection);
	}

	const QByteArray nameUtf8 = name.toUtf8();
	const QByteArray detailUtf8 = detail.toUtf8();
	obs_log(LOG_INFO, "%s at %llu ns", nameUtf8.constData(), static_cast<unsigned long long>(monotonicNanoseconds));
	if (!detail.isEmpty())
		obs_log(LOG_DEBUG, "%s detail: %s", nameUtf8.constData(), detailUtf8.constData());
}

void ObsEventBridge::publishState()
{
	const bool recordingActive = obs_frontend_recording_active();
	const bool recordingPaused = recordingActive && obs_frontend_recording_paused();
	const bool replayBufferActive = obs_frontend_replay_buffer_active();
	QPointer<ReplayTimelineDock> dock = dock_;
	if (!dock)
		return;

	QMetaObject::invokeMethod(
		dock,
		[dock, recordingActive, recordingPaused, replayBufferActive]() {
			if (dock)
				dock->setOutputState(recordingActive, recordingPaused, replayBufferActive);
		},
		Qt::QueuedConnection);
}

} // namespace replay_timeline
