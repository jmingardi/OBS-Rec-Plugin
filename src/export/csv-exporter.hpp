#pragma once

#include "persistence/repository.hpp"

#include <vector>

#include <QByteArray>
#include <QString>

namespace replay_timeline {

QByteArray createCsv(const std::vector<CsvRow> &rows);
bool writeCsv(const QString &path, const std::vector<CsvRow> &rows, QString &error);

} // namespace replay_timeline
