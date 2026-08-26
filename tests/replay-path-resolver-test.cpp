#include "media/replay-path-resolver.hpp"

#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using replay_timeline::resolveReplayPath;
using replay_timeline::resolveRecordingPath;

namespace {
void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

void touch(const QString &path)
{
	QFile file(path);
	expect(file.open(QIODevice::WriteOnly), "test media file can be created");
	file.write("test");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	QTemporaryDir directory;
	expect(directory.isValid(), "temporary directory is available");
	expect(QDir(directory.path()).mkpath(QStringLiteral("Phasmophobia")), "game directory is created");

	const QString actual = directory.filePath(QStringLiteral("Phasmophobia/Phasmophobia_26.08.2026_13-16-26.mkv"));
	touch(actual);
	const QString reported = directory.filePath(QStringLiteral("Replay 2026-08-26 13-16-26.mkv"));
	const auto relocated = resolveReplayPath(reported, QDateTime::currentDateTimeUtc());
	expect(relocated.found && relocated.relocated && QFileInfo(relocated.path) == QFileInfo(actual),
	       "renamed replay is recovered recursively by filename time");

	const QString present = directory.filePath(QStringLiteral("Replay 2026-08-26 13-17-00.mkv"));
	touch(present);
	const auto unchanged = resolveReplayPath(present, QDateTime::currentDateTimeUtc());
	expect(unchanged.found && !unchanged.relocated && QFileInfo(unchanged.path) == QFileInfo(present),
	       "an existing reported replay wins without searching");

	QTemporaryDir recordingDirectory;
	expect(recordingDirectory.isValid(), "recording temporary directory is available");
	const QString recording = recordingDirectory.filePath(QStringLiteral("2026-08-26 13-15-33.mkv"));
	touch(recording);
	const auto finalized = resolveRecordingPath(recordingDirectory.path(), QDateTime::currentDateTimeUtc());
	expect(finalized.found && finalized.relocated && QFileInfo(finalized.path) == QFileInfo(recording),
	       "a directory placeholder is finalized to its root-level recording");

	std::cout << "replay path resolver tests passed\n";
	return EXIT_SUCCESS;
}
