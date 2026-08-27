#include "replay-timeline-dock.hpp"
#include "replay-preview-widget.hpp"

#include <utility>

#include <QComboBox>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QSet>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <obs-module.h>
#include <util/bmem.h>

namespace replay_timeline {
namespace {
enum Column {
	ThumbnailColumn,
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

QString uiSettingsPath()
{
	char *path = obs_module_config_path("ui.ini");
	if (!path)
		return {};
	const QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
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
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(6);
	statusBar_ = new QWidget(this);
	auto *statusLayout = new QHBoxLayout(statusBar_);
	statusLayout->setContentsMargins(0, 0, 0, 0);
	recordingStatus_ = new QLabel(this);
	replayStatus_ = new QLabel(this);
	diskStatus_ = new QLabel(this);
	diskRefreshActivity_ = new QLabel(text("ReplayTimeline.DiskRefreshing"), this);
	diskRefreshActivity_->setStyleSheet(QStringLiteral("color: #5bc0de; font-weight: bold;"));
	diskRefreshActivity_->hide();
	diskRefreshActivityTimer_ = new QTimer(this);
	diskRefreshActivityTimer_->setInterval(1'500);
	diskRefreshActivityTimer_->setSingleShot(true);
	connect(diskRefreshActivityTimer_, &QTimer::timeout, diskRefreshActivity_, &QWidget::hide);
	statusLayout->addWidget(recordingStatus_);
	statusLayout->addSpacing(16);
	statusLayout->addWidget(replayStatus_);
	statusLayout->addSpacing(16);
	statusLayout->addWidget(diskStatus_);
	statusLayout->addWidget(diskRefreshActivity_);
	statusLayout->addStretch();
	layout->addWidget(statusBar_);

	toolbar_ = new QWidget(this);
	auto *controls = new QHBoxLayout(toolbar_);
	controls->setContentsMargins(0, 0, 0, 0);
	sessionSelector_ = new QComboBox(this);
	sessionSelector_->setMinimumContentsLength(24);
	clearSessionsButton_ = new QPushButton(text("ReplayTimeline.ClearSessions"), this);
	auto *search = new QLineEdit(this);
	search->setPlaceholderText(text("ReplayTimeline.SearchPlaceholder"));
	auto *refresh = new QPushButton(text("ReplayTimeline.Refresh"), this);
	auto *retryProbe = new QPushButton(text("ReplayTimeline.RetryProbe"), this);
	auto *configureTags = new QPushButton(text("ReplayTimeline.ConfigureTags"), this);
	auto *diagnosticsButton = new QPushButton(text("ReplayTimeline.DiagnosticsButton"), this);
	auto *exportButton = new QPushButton(text("ReplayTimeline.ExportCsv"), this);
	auto *exportAllButton = new QPushButton(text("ReplayTimeline.ExportAllCsv"), this);
	controls->addWidget(new QLabel(text("ReplayTimeline.Session"), this));
	controls->addWidget(sessionSelector_);
	controls->addWidget(clearSessionsButton_);
	controls->addWidget(refresh);
	controls->addWidget(retryProbe);
	controls->addWidget(configureTags);
	controls->addWidget(diagnosticsButton);
	controls->addStretch();
	layout->addWidget(toolbar_);

	searchBar_ = new QWidget(this);
	auto *searchAndExport = new QHBoxLayout(searchBar_);
	searchAndExport->setContentsMargins(0, 0, 0, 0);
	searchAndExport->addWidget(search, 1);
	searchAndExport->addWidget(exportButton);
	searchAndExport->addWidget(exportAllButton);
	layout->addWidget(searchBar_);

	replayModel_ = new QStandardItemModel(0, ColumnCount, this);
	replayModel_->setHorizontalHeaderLabels(
		{text("ReplayTimeline.Thumbnail"), text("ReplayTimeline.Timestamp"), text("ReplayTimeline.RecordingTime"),
		 text("ReplayTimeline.Tag"),
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
	replayTable_->setSelectionMode(QAbstractItemView::SingleSelection);
	replayTable_->setWordWrap(false);
	replayTable_->setIconSize(QSize(96, 54));
	replayTable_->verticalHeader()->setDefaultSectionSize(60);
	auto *replayHeader = replayTable_->horizontalHeader();
	replayHeader->setSectionResizeMode(QHeaderView::Interactive);
	replayHeader->setMinimumSectionSize(48);
	replayHeader->setStretchLastSection(false);
	replayTable_->setColumnWidth(ThumbnailColumn, 104);
	replayTable_->setColumnWidth(TimestampColumn, 170);
	replayTable_->setColumnWidth(RecordingTimeColumn, 190);
	replayTable_->setColumnWidth(TagColumn, 90);
	replayTable_->setColumnWidth(RatingColumn, 90);
	replayTable_->setColumnWidth(NoteColumn, 220);
	replayTable_->setColumnWidth(ApplicationColumn, 180);
	replayTable_->setColumnWidth(AudioColumn, 140);
	replayTable_->setColumnWidth(DurationColumn, 110);
	replayTable_->setColumnWidth(ReplayPathColumn, 240);
	replayTable_->setColumnWidth(RecordingPathColumn, 240);
	replayTable_->setColumnWidth(ConfidenceColumn, 150);
	contentSplitter_ = new QSplitter(Qt::Vertical, this);
	contentSplitter_->setChildrenCollapsible(false);
	contentSplitter_->setHandleWidth(8);
	contentSplitter_->setOpaqueResize(true);
	contentSplitter_->setStyleSheet(
		QStringLiteral("QSplitter::handle:vertical { background: palette(mid); margin: 2px 0; }"));
	contentSplitter_->addWidget(replayTable_);
	auto *previewGroup = new QFrame(contentSplitter_);
	previewGroup->setFrameShape(QFrame::StyledPanel);
	auto *previewLayout = new QVBoxLayout(previewGroup);
	previewLayout->setContentsMargins(8, 6, 8, 6);
	auto *previewHeader = new QHBoxLayout();
	auto *previewTitle = new QLabel(text("ReplayTimeline.PreviewTitle"), previewGroup);
	QFont previewTitleFont = previewTitle->font();
	previewTitleFont.setBold(true);
	previewTitle->setFont(previewTitleFont);
	previewHeader->addWidget(previewTitle);
	previewHeader->addStretch();
	previewFocusButton_ = new QPushButton(text("ReplayTimeline.ExpandPreview"), previewGroup);
	previewFocusButton_->setToolTip(text("ReplayTimeline.ExpandPreviewTooltip"));
	previewHeader->addWidget(previewFocusButton_);
	previewLayout->addLayout(previewHeader);
	preview_ = new ReplayPreviewWidget(previewGroup);
	previewLayout->addWidget(preview_, 1);
	contentSplitter_->addWidget(previewGroup);
	contentSplitter_->setStretchFactor(0, 2);
	contentSplitter_->setStretchFactor(1, 3);
	contentSplitter_->setSizes({320, 420});
	contentSplitter_->handle(1)->setToolTip(text("ReplayTimeline.ResizePreviewTooltip"));
	layout->addWidget(contentSplitter_, 2);

	const QString settingsPath = uiSettingsPath();
	if (!settingsPath.isEmpty()) {
		QDir().mkpath(QFileInfo(settingsPath).absolutePath());
		uiSettings_ = new QSettings(settingsPath, QSettings::IniFormat, this);
		const QByteArray splitterState = uiSettings_->value(QStringLiteral("layout/contentSplitter")).toByteArray();
		if (!splitterState.isEmpty())
			contentSplitter_->restoreState(splitterState);
		const QByteArray headerState = uiSettings_->value(QStringLiteral("layout/replayTableHeader")).toByteArray();
		if (!headerState.isEmpty())
			replayHeader->restoreState(headerState);
	}

	message_ = new QLabel(this);
	message_->setWordWrap(true);
	layout->addWidget(message_);
	message_->hide();

	diagnosticsDialog_ = new QDialog(this);
	diagnosticsDialog_->setWindowTitle(text("ReplayTimeline.DiagnosticsTitle"));
	diagnosticsDialog_->setModal(false);
	diagnosticsDialog_->resize(760, 420);
	auto *diagnosticsLayout = new QVBoxLayout(diagnosticsDialog_);
	diagnostics_ = new QPlainTextEdit(diagnosticsDialog_);
	diagnostics_->setReadOnly(true);
	diagnostics_->setLineWrapMode(QPlainTextEdit::NoWrap);
	diagnostics_->setMaximumBlockCount(500);
	diagnosticsLayout->addWidget(diagnostics_, 1);
	auto *diagnosticsActions = new QHBoxLayout();
	auto *clearButton = new QPushButton(text("ReplayTimeline.Clear"), diagnosticsDialog_);
	auto *closeDiagnosticsButton = new QPushButton(text("ReplayTimeline.Close"), diagnosticsDialog_);
	diagnosticsActions->addStretch();
	diagnosticsActions->addWidget(clearButton);
	diagnosticsActions->addWidget(closeDiagnosticsButton);
	diagnosticsLayout->addLayout(diagnosticsActions);

	connect(clearButton, &QPushButton::clicked, diagnostics_, &QPlainTextEdit::clear);
	connect(closeDiagnosticsButton, &QPushButton::clicked, diagnosticsDialog_, &QDialog::close);
	connect(diagnosticsButton, &QPushButton::clicked, this, [this]() {
		diagnosticsDialog_->show();
		diagnosticsDialog_->raise();
		diagnosticsDialog_->activateWindow();
	});
	connect(previewFocusButton_, &QPushButton::clicked, this,
		[this]() { setPreviewFocusMode(!previewFocusMode_); });
	auto *leaveFocusShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	leaveFocusShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(leaveFocusShortcut, &QShortcut::activated, this, [this]() {
		if (previewFocusMode_)
			setPreviewFocusMode(false);
	});
	connect(contentSplitter_, &QSplitter::splitterMoved, this, [this](int, int) {
		if (uiSettings_ && !previewFocusMode_)
			uiSettings_->setValue(QStringLiteral("layout/contentSplitter"), contentSplitter_->saveState());
	});
	connect(replayHeader, &QHeaderView::sectionResized, this, [this, replayHeader](int, int, int) {
		if (uiSettings_)
			uiSettings_->setValue(QStringLiteral("layout/replayTableHeader"), replayHeader->saveState());
	});
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
	connect(replayTable_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
		[this](const QModelIndex &current) {
			if (!current.isValid()) {
				preview_->clearReplay();
				return;
			}
			const int sourceRow = replayProxy_->mapToSource(current).row();
			if (QStandardItem *path = replayModel_->item(sourceRow, ReplayPathColumn))
				preview_->loadReplay(path->text());
		});
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
	preview_->clearReplay();
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
		auto *thumbnail = item(text("ReplayTimeline.ThumbnailPending"), row.id);
		thumbnail->setTextAlignment(Qt::AlignCenter);
		QList<QStandardItem *> items{thumbnail,
					     item(row.savedUtc, row.id),
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

void ReplayTimelineDock::setReplayThumbnail(std::int64_t replayId, const QString &replayPath,
					    const QString &thumbnailPath, const QString &error)
{
	for (int row = 0; row < replayModel_->rowCount(); ++row) {
		QStandardItem *thumbnail = replayModel_->item(row, ThumbnailColumn);
		QStandardItem *path = replayModel_->item(row, ReplayPathColumn);
		if (!thumbnail || !path || thumbnail->data(Qt::UserRole).toLongLong() != replayId ||
		    path->text() != replayPath)
			continue;
		const QPixmap image(thumbnailPath);
		if (!image.isNull()) {
			thumbnail->setText({});
			thumbnail->setIcon(QIcon(image));
			thumbnail->setToolTip(text("ReplayTimeline.ThumbnailTooltip"));
		} else {
			thumbnail->setText(text("ReplayTimeline.ThumbnailUnavailable"));
			thumbnail->setToolTip(error);
		}
		return;
	}
}

void ReplayTimelineDock::clearPreview()
{
	preview_->clearReplay();
}

void ReplayTimelineDock::setTagNames(const QStringList &tags)
{
	tagNames_ = tags;
}

void ReplayTimelineDock::showDiskRefreshActivity()
{
	diskRefreshActivity_->show();
	diskRefreshActivityTimer_->start();
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
	message_->setVisible(!message.isEmpty() && !previewFocusMode_);
}

void ReplayTimelineDock::setPreviewFocusMode(bool enabled)
{
	if (previewFocusMode_ == enabled)
		return;
	previewFocusMode_ = enabled;
	if (enabled) {
		normalSplitterSizes_ = contentSplitter_->sizes();
		statusBar_->hide();
		toolbar_->hide();
		searchBar_->hide();
		replayTable_->hide();
		message_->hide();
		previewFocusButton_->setText(text("ReplayTimeline.RestoreLayout"));
		previewFocusButton_->setToolTip(text("ReplayTimeline.RestoreLayoutTooltip"));
		return;
	}

	statusBar_->show();
	toolbar_->show();
	searchBar_->show();
	replayTable_->show();
	message_->setVisible(!message_->text().isEmpty());
	previewFocusButton_->setText(text("ReplayTimeline.ExpandPreview"));
	previewFocusButton_->setToolTip(text("ReplayTimeline.ExpandPreviewTooltip"));
	if (!normalSplitterSizes_.isEmpty()) {
		const QList<int> sizes = normalSplitterSizes_;
		QTimer::singleShot(0, this, [this, sizes]() { contentSplitter_->setSizes(sizes); });
	}
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
