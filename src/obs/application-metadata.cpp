#include "application-metadata.hpp"

#include <cstring>
#include <utility>

#include <QFileInfo>
#include <QStringList>

#include <callback/calldata.h>
#include <callback/proc.h>
#include <obs-frontend-api.h>
#include <obs.h>

namespace replay_timeline {
namespace {
struct Candidate {
	ApplicationMetadata metadata;
	int priority = 0;
	bool hooked = false;
};

QString decodeWindowPart(QString value)
{
	return value.replace(QStringLiteral("#3A"), QStringLiteral(":"))
		.replace(QStringLiteral("#22"), QStringLiteral("#"));
}

ApplicationMetadata configuredMetadata(obs_source_t *source)
{
	ApplicationMetadata result;
	obs_data_t *settings = obs_source_get_settings(source);
	if (!settings)
		return result;
	const QString serialized = QString::fromUtf8(obs_data_get_string(settings, "window"));
	obs_data_release(settings);
	const QStringList parts = serialized.split(QLatin1Char(':'));
	if (parts.size() >= 3) {
		result.windowTitle = decodeWindowPart(parts[0]);
		result.applicationName = QFileInfo(decodeWindowPart(parts[2])).fileName();
	}
	return result;
}

void inspectSource(obs_source_t *, obs_source_t *source, void *parameter)
{
	auto &best = *static_cast<Candidate *>(parameter);
	const char *rawId = obs_source_get_unversioned_id(source);
	if (!rawId)
		return;
	const bool gameCapture = std::strcmp(rawId, "game_capture") == 0;
	const bool windowCapture = std::strcmp(rawId, "window_capture") == 0 ||
				   std::strcmp(rawId, "xcomposite_input") == 0;
	if (!gameCapture && !windowCapture)
		return;

	Candidate candidate;
	candidate.priority = gameCapture ? 2 : 1;
	if (const char *sourceName = obs_source_get_name(source))
		candidate.metadata.sourceName = QString::fromUtf8(sourceName);
	calldata_t callData{};
	proc_handler_t *handler = obs_source_get_proc_handler(source);
	if (handler && proc_handler_call(handler, "get_hooked", &callData) && calldata_bool(&callData, "hooked")) {
		candidate.hooked = true;
		if (const char *title = calldata_string(&callData, "title"))
			candidate.metadata.windowTitle = QString::fromUtf8(title);
		if (const char *executable = calldata_string(&callData, "executable"))
			candidate.metadata.applicationName = QFileInfo(QString::fromUtf8(executable)).fileName();
	}
	calldata_free(&callData);
	if (!candidate.hooked) {
		const ApplicationMetadata configured = configuredMetadata(source);
		candidate.metadata.applicationName = configured.applicationName;
		candidate.metadata.windowTitle = configured.windowTitle;
	}

	if (candidate.metadata.applicationName.isEmpty() && candidate.metadata.windowTitle.isEmpty())
		return;
	if (candidate.priority > best.priority ||
	    (candidate.priority == best.priority && candidate.hooked && !best.hooked))
		best = std::move(candidate);
}
} // namespace

ApplicationMetadata currentApplicationMetadata()
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (!scene)
		return {};
	Candidate candidate;
	obs_source_enum_active_tree(scene, inspectSource, &candidate);
	obs_source_release(scene);
	return candidate.metadata;
}

} // namespace replay_timeline
