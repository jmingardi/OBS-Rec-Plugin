#include "timeline-mapper.hpp"

#include <algorithm>

namespace replay_timeline::domain {

Nanoseconds TimelineMapper::mediaTime(Nanoseconds runStarted, Nanoseconds instant,
				      const std::vector<PauseInterval> &pauses)
{
	if (instant <= runStarted)
		return 0;

	std::vector<PauseInterval> normalized;
	normalized.reserve(pauses.size());
	for (const PauseInterval &pause : pauses) {
		const Nanoseconds start = std::clamp(pause.started, runStarted, instant);
		const Nanoseconds end = std::clamp(pause.ended, start, instant);
		if (end > start)
			normalized.push_back({start, end});
	}

	std::sort(normalized.begin(), normalized.end(), [](const PauseInterval &left, const PauseInterval &right) {
		return left.started < right.started || (left.started == right.started && left.ended < right.ended);
	});

	Nanoseconds paused = 0;
	Nanoseconds mergedStart = 0;
	Nanoseconds mergedEnd = 0;
	bool haveInterval = false;
	for (const PauseInterval &pause : normalized) {
		if (!haveInterval) {
			mergedStart = pause.started;
			mergedEnd = pause.ended;
			haveInterval = true;
		} else if (pause.started <= mergedEnd) {
			mergedEnd = std::max(mergedEnd, pause.ended);
		} else {
			paused += mergedEnd - mergedStart;
			mergedStart = pause.started;
			mergedEnd = pause.ended;
		}
	}
	if (haveInterval)
		paused += mergedEnd - mergedStart;

	return std::max<Nanoseconds>(0, instant - runStarted - paused);
}

std::vector<AssociationSpan> TimelineMapper::mapReplay(Nanoseconds recordingEnd, Nanoseconds replayDuration,
						       const std::vector<RecordingSegment> &segments)
{
	std::vector<AssociationSpan> result;
	if (recordingEnd < 0 || replayDuration <= 0)
		return result;

	const Nanoseconds replayStart = std::max<Nanoseconds>(0, recordingEnd - replayDuration);
	for (const RecordingSegment &segment : segments) {
		const Nanoseconds segmentEnd = std::max(segment.mediaStart, segment.mediaEnd);
		const Nanoseconds intersectionStart = std::max(replayStart, segment.mediaStart);
		const Nanoseconds intersectionEnd = std::min(recordingEnd, segmentEnd);
		if (intersectionEnd <= intersectionStart)
			continue;

		result.push_back({segment.id, segment.path, intersectionStart, intersectionEnd,
				  intersectionStart - replayStart, intersectionEnd - replayStart,
				  intersectionStart - segment.mediaStart, intersectionEnd - segment.mediaStart});
	}
	return result;
}

} // namespace replay_timeline::domain
