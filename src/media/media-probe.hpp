#pragma once

#include <cstdint>

#include <QString>

namespace replay_timeline {

struct MediaProbeResult {
	std::int64_t durationNs = -1;
	QString error;

	bool succeeded() const { return durationNs > 0; }
};

MediaProbeResult probeMediaDuration(const QString &path);

} // namespace replay_timeline
