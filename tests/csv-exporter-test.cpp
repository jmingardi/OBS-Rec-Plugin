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
		{1, 7, 1, 2, 3'661'234'000'000, 3'662'000'000'000, QStringLiteral("Bug"),
		 QStringLiteral("comma, quote \" and\nnewline ✓"), 60'000'000'000,
		 QStringLiteral("C:/replays/ação.mp4"), QStringLiteral("C:/recording,2.mkv"),
		 QStringLiteral("approximate")},
		{1,
		 8,
		 0,
		 0,
		 -1,
		 -1,
		 QStringLiteral("External"),
		 {},
		 10'000'000'000,
		 QStringLiteral("C:/replays/only.mkv"),
		 {},
		 QStringLiteral("low")},
	};
	const QByteArray csv = createCsv(rows);
	expect(csv.startsWith("session_id,replay_id"), "stable header is emitted");
	expect(csv.contains("01:01:01.234,01:01:02.000"), "nanoseconds become editor-friendly timecodes");
	expect(csv.contains("\"comma, quote \"\" and\nnewline ✓\""), "RFC 4180 quoting preserves Unicode and newlines");
	expect(csv.contains("1,8,,,,,\"External\""), "replay-only association fields are empty");
	expect(csv.count("\r\n") == 3, "CRLF record endings remain stable with an embedded newline");
	std::cout << "CSV exporter tests passed\n";
	return EXIT_SUCCESS;
}
