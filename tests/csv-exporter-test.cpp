#include "export/csv-exporter.hpp"

#include <cstdlib>
#include <iostream>

using replay_timeline::CsvRow;
using replay_timeline::createCsv;

namespace {
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
	const std::vector<CsvRow> rows{
		{1, 7, QStringLiteral("2026-08-26T16:16:27.000Z"), 1, 2, 3'661'234'000'000,
		 3'662'000'000'000, 1'234'000'000, 2'000'000'000, QStringLiteral("Bug"),
		 QStringLiteral("comma, quote \" and\nnewline ✓"), 60'000'000'000,
		 QStringLiteral("C:/replays/ação.mp4"), QStringLiteral("C:/recording,2.mkv"),
		 QStringLiteral("approximate"), QStringLiteral("complete"), QStringLiteral("timestamp + duration")},
		{1,
		 8,
		 QStringLiteral("2026-08-26T16:17:00.000Z"),
		 0,
		 0,
		 -1,
		 -1,
		 -1,
		 -1,
		 QStringLiteral("External"),
		 {},
		 10'000'000'000,
		 QStringLiteral("C:/replays/only.mkv"),
		 {},
		 QStringLiteral("low"),
		 QStringLiteral("complete"),
		 QStringLiteral("Replay Buffer-only")},
	};
	const QByteArray csv = createCsv(rows);
	expect(csv.startsWith("session_id,replay_id,saved_utc,recording_run,recording_segment,run_start,run_end,"
			      "segment_start,segment_end"),
	       "run-global and segment-local columns are explicit");
	expect(csv.contains("01:01:01.234,01:01:02.000"), "nanoseconds become editor-friendly timecodes");
	expect(csv.contains("00:00:01.234,00:00:02.000"), "segment-local split-file timecodes are exported");
	expect(csv.contains("\"comma, quote \"\" and\nnewline ✓\""), "RFC 4180 quoting preserves Unicode and newlines");
	expect(csv.contains("1,8,\"2026-08-26T16:17:00.000Z\",,,,,,,\"External\""),
	       "replay-only association fields are empty");
	expect(csv.contains("\"complete\",\"timestamp + duration\""), "probe status and mapping reason are exported");
	expect(csv.count("\r\n") == 3, "CRLF record endings remain stable with an embedded newline");
	std::cout << "CSV exporter tests passed\n";
	return EXIT_SUCCESS;
}
