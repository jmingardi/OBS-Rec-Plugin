#pragma once

#include <cstdint>

#include <QString>

namespace replay_timeline {

struct ThumbnailResult {
	QString path;
	QString error;
	bool cacheHit = false;

	bool succeeded() const { return !path.isEmpty() && error.isEmpty(); }
};

QString replayThumbnailCachePath(const QString &cacheDirectory, const QString &mediaPath);
ThumbnailResult generateReplayThumbnail(const QString &mediaPath, const QString &cacheDirectory,
					std::int64_t durationNs);

} // namespace replay_timeline
