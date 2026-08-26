#pragma once

#include <QDateTime>
#include <QString>

namespace replay_timeline {

struct ReplayPathResolution {
	QString path;
	QString detail;
	bool found = false;
	bool relocated = false;
};

ReplayPathResolution resolveReplayPath(const QString &reportedPath, const QDateTime &savedUtc);
ReplayPathResolution resolveRecordingPath(const QString &reportedPath, const QDateTime &expectedStartedUtc);

} // namespace replay_timeline
