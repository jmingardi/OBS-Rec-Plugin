#include "replay-timeline-dock.hpp"

#include <utility>

#include <QComboBox>
#include <QColor>
#include <QDateTime>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QSet>
#include <QTableView>
#include <QVBoxLayout>

#include <obs-module.h>

namespace replay_timeline {
namespace {
enum Column {
	TimestampColumn,
	RecordingTimeColumn,
	TagColumn,
	RatingColumn,
	NoteColumn,
	ApplicationColumn,
	AudioColumn,
	DurationColumn,
	ReplayPathColumn,
	RecordingPathColumn,
	ConfidenceColumn,
	ColumnCount
};

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString timecode(std::int64_t nanoseconds)
{
	if (nanoseconds < 0)
		return {};
	const std::int64_t totalMilliseconds = nanoseconds / 1'000'000;
	const std::int64_t milliseconds = totalMilliseconds % 1000;
	const std::int64_t totalSeconds = totalMilliseconds / 1000;
	const std::int64_t seconds = totalSeconds % 60;
	const std::int64_t minutes = (totalSeconds / 60) % 60;
	const std::int64_t hours = totalSeconds / 3600;
	return QStringLiteral("%1:%2:%3.%4")
		.arg(hours, 2, 10, QLatin1Char('0'))
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'))
		.arg(milliseconds, 3, 10, QLatin1Char('0'));
}

QStandardItem *item(const QString &value, std::int64_t replayId, bool editable = false)
{
	auto *result = new QStandardItem(value);
	result->setData(QVariant::fromValue<qlonglong>(replayId), Qt::UserRole);
	result->setEditable(editable);
	return result;
}
} // namespace

ReplayTimelineDock::ReplayTimelineDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	auto *statusLayout = new QHBoxLayout();
	recordingStatus_ = new QLabel(this);
	replayStatus_ = new QLabel(this);
	diskStatus_ = new QLabel(this);
	statusLayout->addWidget(recordingStatus_);
	statusLayout->addSpacing(16);
	statusLayout->addWidget(replayStatus_);
	statusLayout->addSpacing(16);
	statusLayout->addWidget(diskStatus_);
	statusLayout->addStretch();
	layout->addLayout(statusLayout);

	auto *controls = new QHBoxLayout();
	sessionSelector_ = new QComboBox(this);
	sessionSelector_->setMinimumContentsLength(24);
	clearSessionsButton_ = new QPushButton(text("ReplayTimeline.ClearSessions"), this);
	auto *search = new QLineEdit(this);
	search->setPlaceholderText(text("ReplayTimeline.SearchPlaceholder"));
	auto *refresh = new QPushButton(text("ReplayTimeline.Refresh"), this);
	auto *retryProbe = new QPushButton(text("ReplayTimeline.RetryProbe"), this);
	auto *configureTags = new QPushButton(text("ReplayTimeline.ConfigureTags"), this);
	auto *exportButton = new QPushButton(text("ReplayTimeline.ExportCsv"), this);
	auto *exportAllButton = new QPushButton(text("ReplayTimeline.ExportAllCsv"), this);
	controls->addWidget(new QLabel(text("ReplayTimeline.Session"), this));
	controls->addWidget(sessionSelector_);
	controls->addWidget(clearSessionsButton_);
	controls->addWidget(refresh);
	controls->addWidget(retryProbe);
	controls->addWidget(configureTags);
	controls->addStretch();
	layout->addLayout(controls);

	auto *searchAndExport = new QHBoxLayout();
	searchAndExport->addWidget(search, 1);
	searchAndExport->addWidget(exportButton);
	searchAndExport->addWidget(exportAllButton);
	layout->addLayout(searchAndExport);

	replayModel_ = new QStandardItemModel(0, ColumnCount, this);
	replayModel_->setHorizontalHeaderLabels(
		{text("ReplayTimeline.Timestamp"), text("ReplayTimeline.RecordingTime"), text("ReplayTimeline.Tag"),
		 text("ReplayTimeline.Rating"), text("ReplayTimeline.Note"), text("ReplayTimeline.Application"),
		 text("ReplayTimeline.Audio"), text("ReplayTimeline.Duration"), text("ReplayTimeline.ReplayPath"),
		 text("ReplayTimeline.RecordingPath"), text("ReplayTimeline.Confidence")});
	replayProxy_ = new QSortFilterProxyModel(this);
	replayProxy_->setSourceModel(replayModel_);
	replayProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
	replayProxy_->setFilterKeyColumn(-1);
	replayProxy_->setSortCaseSensitivity(Qt::CaseInsensitive);
	replayTable_ = new QTableView(this);
	replayTable_->setModel(replayProxy_);
	replayTable_->setSortingEnabled(true);
	replayTable_->setAlternatingRowColors(true);
	replayTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	replayTable_->setWordWrap(false);
	replayTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	replayTable_->horizontalHeader()->setSectionResizeMode(NoteColumn, QHeaderView::Stretch);
	layout->addWidget(replayTable_, 2);

	message_ = new QLabel(this);
	message_->setWordWrap(true);
	layout->addWidget(message_);

	auto *diagnosticsHeader = new QHBoxLayout();
	auto *diagnosticsTitle = new QLabel(text("ReplayTimeline.DiagnosticsTitle"), this);
	QFont titleFont = diagnosticsTitle->font();
	titleFont.setBold(true);
	diagnosticsTitle->setFont(titleFont);
	auto *clearButton = new QPushButton(text("ReplayTimeline.Clear"), this);
	diagnosticsHeader->addWidget(diagnosticsTitle);
	diagnosticsHeader->addStretch();
	diagnosticsHeader->addWidget(clearButton);
	layout->addLayout(diagnosticsHeader);

	diagnostics_ = new QPlainTextEdit(this);
	diagnostics_->setReadOnly(true);
	diagnostics_->setLineWrapMode(QPlainTextEdit::NoWrap);
	diagnostics_->setMaximumBlockCount(500);
	diagnostics_->setMaximumHeight(140);
	layout->addWidget(diagnostics_);

	connect(clearButton, &QPushButton::clicked, diagnostics_, &QPlainTextEdit::clear);
	connect(clearSessionsButton_, &QPushButton::clicked, this, [this]() {
		if (!callbacks_.clearSessionsRequested)
			return;
		const QMessageBox::StandardButton choice = QMessageBox::warning(
			this, text("ReplayTimeline.ClearSessionsTitle"), text("ReplayTimeline.ClearSessionsPrompt"),
			QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
		if (choice == QMessageBox::Yes)
			callbacks_.clearSessionsRequested();
	});
	connect(search, &QLineEdit::textChanged, replayProxy_, &QSortFilterProxyModel::setFilterFixedString);
	connect(sessionSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index >= 0 && callbacks_.sessionSelected)
			callbacks_.sessionSelected(sessionSelector_->itemData(index).toLongLong());
	});
	connect(refresh, &QPushButton::clicked, this, [this]() {
		if (callbacks_.refreshRequested)
			callbacks_.refreshRequested();
	});
	connect(retryProbe, &QPushButton::clicked, this, [this]() {
		if (!callbacks_.retryProbe)
			return;
		QSet<std::int64_t> replayIds;
		const QModelIndexList selectedRows = replayTable_->selectionModel()->selectedRows(TimestampColumn);
		for (const QModelIndex &proxyIndex : selectedRows) {
			const int sourceRow = replayProxy_->mapToSource(proxyIndex).row();
			if (QStandardItem *entry = replayModel_->item(sourceRow, TimestampColumn))
				replayIds.insert(entry->data(Qt::UserRole).toLongLong());
		}
		if (replayIds.isEmpty()) {
			showMessage(text("ReplayTimeline.SelectReplay"), true);
			return;
		}
		for (std::int64_t replayId : replayIds)
			callbacks_.retryProbe(replayId);
	});
	auto exportCsv = [this](bool allSessions) {
		if (!callbacks_.exportCsv)
			return;
		const QString title = text(allSessions ? "ReplayTimeline.ExportAllCsv" : "ReplayTimeline.ExportCsv");
		const QString defaultName = allSessions ? QStringLiteral("replay-markers-all-sessions.csv")
							: QStringLiteral("replay-markers.csv");
		const QString path =
			QFileDialog::getSaveFileName(this, title, defaultName, QStringLiteral("CSV (*.csv)"));
		if (!path.isEmpty())
			callbacks_.exportCsv(path, allSessions);
	};
	connect(exportButton, &QPushButton::clicked, this, [exportCsv]() { exportCsv(false); });
	connect(exportAllButton, &QPushButton::clicked, this, [exportCsv]() { exportCsv(true); });
	connect(configureTags, &QPushButton::clicked, this, [this]() {
		bool accepted = false;
		const QString value = QInputDialog::getText(this, text("ReplayTimeline.ConfigureTags"),
							    text("ReplayTimeline.ConfigureTagsPrompt"),
							    QLineEdit::Normal, tagNames_.join(QStringLiteral(", ")),
							    &accepted);
		if (!accepted || !callbacks_.tagsConfigured)
			return;
		QStringList tags;
		for (const QString &part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			const QString tag = part.trimmed();
			if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive))
				tags.push_back(tag);
		}
		if (!tags.isEmpty())
			callbacks_.tagsConfigured(tags.mid(0, 8));
	});
	connect(replayModel_, &QStandardItemModel::itemChanged, this,
		[this](QStandardItem *changedItem) { handleItemChanged(changedItem); });
	setOutputState(false, false, false);
}

void ReplayTimelineDock::appendDiagnostic(const QString &eventName, const QString &detail, quint64 monotonicNanoseconds)
{
	const QString wallTime = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
	const QString suffix = detail.isEmpty() ? QString() : QStringLiteral(" | ") + detail;
	diagnostics_->appendPlainText(QStringLiteral("[%1] %2 | mono=%3 ns%4")
					      .arg(wallTime, eventName, QString::number(monotonicNanoseconds), suffix));
}

void ReplayTimelineDock::setOutputState(bool recordingActive, bool recordingPaused, bool replayBufferActive)
{
	const char *recordingKey = "ReplayTimeline.RecordingInactive";
	if (recordingPaused)
		recordingKey = "ReplayTimeline.RecordingPaused";
	else if (recordingActive)
		recordingKey = "ReplayTimeline.RecordingActive";
	recordingStatus_->setText(text(recordingKey));
	replayStatus_->setText(
		text(replayBufferActive ? "ReplayTimeline.ReplayActive" : "ReplayTimeline.ReplayInactive"));
	const bool captureActive = recordingActive || replayBufferActive;
	clearSessionsButton_->setEnabled(!captureActive);
	clearSessionsButton_->setToolTip(captureActive ? text("ReplayTimeline.ClearSessionsActive")
						       : text("ReplayTimeline.ClearSessionsTooltip"));
}

void ReplayTimelineDock::setCallbacks(Callbacks callbacks)
{
	callbacks_ = std::move(callbacks);
}

void ReplayTimelineDock::setSessions(const std::vector<SessionSummary> &sessions, std::int64_t selectedSessionId)
{
	const QSignalBlocker blocker(sessionSelector_);
	sessionSelector_->clear();
	int selectedIndex = -1;
	for (const SessionSummary &session : sessions) {
		const QString label = QStringLiteral("%1  [%2]").arg(session.startedUtc, session.status);
		sessionSelector_->addItem(label, QVariant::fromValue<qlonglong>(session.id));
		if (session.id == selectedSessionId)
			selectedIndex = sessionSelector_->count() - 1;
	}
	if (selectedIndex < 0 && sessionSelector_->count() > 0)
		selectedIndex = 0;
	sessionSelector_->setCurrentIndex(selectedIndex);
}

void ReplayTimelineDock::setReplayRows(const std::vector<ReplayRow> &rows)
{
	loadingRows_ = true;
	replayModel_->removeRows(0, replayModel_->rowCount());
	for (const ReplayRow &row : rows) {
		const QString recordingTime = row.recordingStartNs >= 0
						      ? QStringLiteral("%1 - %2").arg(timecode(row.recordingStartNs),
										      timecode(row.recordingEndNs))
						      : QString();
		const QString confidence = QStringLiteral("%1 / %2").arg(row.confidence, row.probeStatus);
		auto *rating = item(QString::number(row.rating), row.id, true);
		rating->setData(row.rating, Qt::UserRole + 1);
		QString application = row.applicationName;
		if (!row.windowTitle.isEmpty() && row.windowTitle.compare(application, Qt::CaseInsensitive) != 0)
			application += application.isEmpty() ? row.windowTitle
							     : QStringLiteral(" — ") + row.windowTitle;
		auto *applicationItem = item(application, row.id);
		applicationItem->setToolTip(QStringLiteral("Application: %1\nWindow: %2\nOBS source: %3")
						    .arg(row.applicationName, row.windowTitle, row.captureSource));
		const QString audio =
			row.audioTracks >= 0
				? QStringLiteral("%1 track(s) / %2").arg(row.audioTracks).arg(row.audioStatus)
				: QStringLiteral("Unknown");
		auto *audioItem = item(audio, row.id);
		if (row.audioStatus == QStringLiteral("missing")) {
			audioItem->setForeground(QColor(QStringLiteral("#d9534f")));
			audioItem->setToolTip(text("ReplayTimeline.AudioMissing"));
		}
		QList<QStandardItem *> items{item(row.savedUtc, row.id),
					     item(recordingTime, row.id),
					     item(row.tag, row.id, true),
					     rating,
					     item(row.note, row.id, true),
					     applicationItem,
					     audioItem,
					     item(timecode(row.durationNs), row.id),
					     item(row.replayPath, row.id),
					     item(row.recordingPaths, row.id),
					     item(confidence, row.id)};
		replayModel_->appendRow(items);
	}
	loadingRows_ = false;
}

void ReplayTimelineDock::setTagNames(const QStringList &tags)
{
	tagNames_ = tags;
}

void ReplayTimelineDock::setDiskStatus(const QString &message, bool warning)
{
	diskStatus_->setText(message);
	diskStatus_->setStyleSheet(warning ? QStringLiteral("color: #d9534f; font-weight: bold;") : QString());
}

void ReplayTimelineDock::showMessage(const QString &message, bool error)
{
	message_->setText(message);
	message_->setStyleSheet(error ? QStringLiteral("color: #d9534f;") : QString());
}

void ReplayTimelineDock::handleItemChanged(QStandardItem *changedItem)
{
	if (loadingRows_ || !callbacks_.replayEdited || !changedItem)
		return;
	const int sourceRow = changedItem->row();
	QStandardItem *tag = replayModel_->item(sourceRow, TagColumn);
	QStandardItem *rating = replayModel_->item(sourceRow, RatingColumn);
	QStandardItem *note = replayModel_->item(sourceRow, NoteColumn);
	if (!tag || !rating || !note)
		return;
	bool validRating = false;
	const int ratingValue = rating->text().trimmed().toInt(&validRating);
	if (!validRating || ratingValue < 0 || ratingValue > 5) {
		loadingRows_ = true;
		rating->setText(QString::number(rating->data(Qt::UserRole + 1).toInt()));
		loadingRows_ = false;
		showMessage(text("ReplayTimeline.RatingInvalid"), true);
		return;
	}
	rating->setData(ratingValue, Qt::UserRole + 1);
	callbacks_.replayEdited(tag->data(Qt::UserRole).toLongLong(), tag->text().trimmed(), note->text(), ratingValue);
}

} // namespace replay_timeline
