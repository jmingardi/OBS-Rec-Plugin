#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace replay_timeline::domain {

using Nanoseconds = std::int64_t;

struct PauseInterval {
	Nanoseconds started = 0;
	Nanoseconds ended = 0;
};

struct RecordingSegment {
	std::int64_t id = 0;
	std::string path;
	Nanoseconds mediaStart = 0;
	Nanoseconds mediaEnd = 0;
};

struct AssociationSpan {
	std::int64_t segmentId = 0;
	std::string recordingPath;
	Nanoseconds runStart = 0;
	Nanoseconds runEnd = 0;
	Nanoseconds replayStart = 0;
	Nanoseconds replayEnd = 0;
	Nanoseconds segmentStart = 0;
	Nanoseconds segmentEnd = 0;
};

class TimelineMapper final {
public:
	static Nanoseconds mediaTime(Nanoseconds runStarted, Nanoseconds instant,
				     const std::vector<PauseInterval> &pauses);
	static std::vector<AssociationSpan> mapReplay(Nanoseconds recordingEnd, Nanoseconds replayDuration,
						      const std::vector<RecordingSegment> &segments);
};

} // namespace replay_timeline::domain
