#include "domain/timeline-mapper.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

using replay_timeline::domain::AssociationSpan;
using replay_timeline::domain::PauseInterval;
using replay_timeline::domain::RecordingSegment;
using replay_timeline::domain::TimelineMapper;

namespace {
constexpr std::int64_t second = 1'000'000'000;

void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}
} // namespace

int main()
{
	expect(TimelineMapper::mediaTime(10 * second, 70 * second, {}) == 60 * second, "ordinary recording media time");

	const std::vector<PauseInterval> pauses{{30 * second, 40 * second},
						{35 * second, 45 * second},
						{80 * second, 90 * second}};
	expect(TimelineMapper::mediaTime(10 * second, 100 * second, pauses) == 65 * second,
	       "overlapping pauses are normalized");
	expect(TimelineMapper::mediaTime(10 * second, 35 * second, {{30 * second, 50 * second}}) == 20 * second,
	       "open pause is clipped at the request instant");

	const std::vector<RecordingSegment> segments{{1, "part-1.mkv", 0, 30 * second},
						     {2, "part-2.mkv", 30 * second, 90 * second}};
	const std::vector<AssociationSpan> spans = TimelineMapper::mapReplay(42 * second, 20 * second, segments);
	expect(spans.size() == 2, "a replay crossing a split maps to two spans");
	expect(spans[0].segmentId == 1 && spans[0].segmentStart == 22 * second && spans[0].segmentEnd == 30 * second,
	       "first split span is correct");
	expect(spans[1].segmentId == 2 && spans[1].segmentStart == 0 && spans[1].segmentEnd == 12 * second,
	       "second split span is correct");

	const std::vector<AssociationSpan> clamped = TimelineMapper::mapReplay(15 * second, 60 * second, segments);
	expect(clamped.size() == 1 && clamped[0].segmentStart == 0 && clamped[0].segmentEnd == 15 * second,
	       "replay beginning is clamped to recording start");
	expect(TimelineMapper::mapReplay(10 * second, 0, segments).empty(), "unknown duration does not invent spans");
	expect(TimelineMapper::mapReplay(10 * second, second, {}).empty(), "replay-only sessions have no spans");

	std::cout << "timeline mapper tests passed\n";
	return EXIT_SUCCESS;
}
