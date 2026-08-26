#include "plugin-controller.hpp"

#include "../controller/session-controller.hpp"
#include "../obs/obs-event-bridge.hpp"
#include "../ui/replay-timeline-dock.hpp"

#include <QDir>
#include <QFileInfo>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>

#include <plugin-support.h>

namespace replay_timeline {
namespace {
constexpr const char *kDockId = "obs-replay-timeline";
}

PluginController::PluginController() = default;

PluginController::~PluginController()
{
	stop();
}

bool PluginController::start()
{
	if (dockRegistered_)
		return true;

	dock_ = new ReplayTimelineDock();
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("ReplayTimeline.DockTitle"), dock_)) {
		delete dock_;
		dock_ = nullptr;
		return false;
	}

	dockRegistered_ = true;
	char *rawDatabasePath = obs_module_config_path("replay-timeline.sqlite3");
	const QString databasePath = rawDatabasePath ? QString::fromUtf8(rawDatabasePath) : QString();
	bfree(rawDatabasePath);
	if (databasePath.isEmpty() || !QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
		obs_log(LOG_ERROR, "failed to create the plugin configuration directory");
		return false;
	}

	sessionController_ = std::make_unique<SessionController>(dock_);
	QString error;
	if (!sessionController_->start(databasePath, error)) {
		obs_log(LOG_ERROR, "failed to open timeline database: %s", error.toUtf8().constData());
		return false;
	}

	eventBridge_ = std::make_unique<ObsEventBridge>(dock_, sessionController_.get());
	eventBridge_->start();
	obs_log(LOG_INFO, "registered dock '%s'", kDockId);
	return true;
}

void PluginController::stop()
{
	if (eventBridge_) {
		eventBridge_->stop();
		eventBridge_.reset();
	}
	if (sessionController_) {
		sessionController_->stop();
		sessionController_.reset();
	}

	if (dockRegistered_) {
		obs_frontend_remove_dock(kDockId);
		dockRegistered_ = false;
		dock_ = nullptr;
	}
}

} // namespace replay_timeline
