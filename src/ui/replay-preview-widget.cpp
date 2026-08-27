#include "replay-preview-widget.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

#include <QFileInfo>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEngine>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <graphics/graphics.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#elif defined(__linux__) || defined(__FreeBSD__)
#include <obs-nix-platform.h>
#endif

namespace replay_timeline {
namespace {
QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString playbackTime(std::int64_t milliseconds)
{
	if (milliseconds < 0)
		milliseconds = 0;
	const std::int64_t seconds = milliseconds / 1000;
	return QStringLiteral("%1:%2")
		.arg(seconds / 60, 2, 10, QLatin1Char('0'))
		.arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString monitoringDeviceName()
{
	config_t *profile = obs_frontend_get_profile_config();
	const char *name = profile ? config_get_string(profile, "Audio", "MonitoringDeviceName") : nullptr;
	return name && *name ? QString::fromUtf8(name) : text("ReplayTimeline.PreviewDefaultDevice");
}
} // namespace

class ReplayVideoSurface final : public QWidget {
public:
	explicit ReplayVideoSurface(QWidget *parent) : QWidget(parent)
	{
		setAttribute(Qt::WA_PaintOnScreen);
		setAttribute(Qt::WA_StaticContents);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setAttribute(Qt::WA_DontCreateNativeAncestors);
		setAttribute(Qt::WA_NativeWindow);
		setMinimumSize(256, 144);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	~ReplayVideoSurface() override
	{
		setSource(nullptr);
		if (display_) {
			obs_display_remove_draw_callback(display_, draw, this);
			obs_display_destroy(display_);
		}
	}

	void setSource(obs_source_t *source)
	{
		std::lock_guard<std::mutex> lock(sourceMutex_);
		source_ = source;
	}

	bool displayAvailable() const { return display_ != nullptr; }

protected:
	QPaintEngine *paintEngine() const override { return nullptr; }

	void paintEvent(QPaintEvent *) override { createDisplay(); }

	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		createDisplay();
		if (display_)
			obs_display_resize(display_, pixelWidth(), pixelHeight());
	}

	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		createDisplay();
		if (display_)
			obs_display_set_enabled(display_, true);
	}

	void hideEvent(QHideEvent *event) override
	{
		if (display_)
			obs_display_set_enabled(display_, false);
		QWidget::hideEvent(event);
	}

private:
	std::uint32_t pixelWidth() const
	{
		return static_cast<std::uint32_t>(std::max(1, qRound(width() * devicePixelRatioF())));
	}

	std::uint32_t pixelHeight() const
	{
		return static_cast<std::uint32_t>(std::max(1, qRound(height() * devicePixelRatioF())));
	}

	void createDisplay()
	{
		if (display_ || !isVisible() || !windowHandle() || !windowHandle()->isExposed())
			return;
		gs_init_data info{};
		info.cx = pixelWidth();
		info.cy = pixelHeight();
		info.format = GS_BGRA;
		info.zsformat = GS_ZS_NONE;
#ifdef _WIN32
		info.window.hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(winId()));
#elif defined(__linux__) || defined(__FreeBSD__)
		if (obs_get_nix_platform() != OBS_NIX_PLATFORM_X11_EGL)
			return;
		info.window.id = static_cast<std::uint32_t>(winId());
		info.window.display = obs_get_nix_platform_display();
#else
		return;
#endif
		display_ = obs_display_create(&info, 0xFF101010);
		if (display_)
			obs_display_add_draw_callback(display_, draw, this);
	}

	static void draw(void *data, std::uint32_t width, std::uint32_t height)
	{
		auto *surface = static_cast<ReplayVideoSurface *>(data);
		std::lock_guard<std::mutex> lock(surface->sourceMutex_);
		if (!surface->source_)
			return;
		const std::uint32_t sourceWidth = std::max(obs_source_get_width(surface->source_), 1U);
		const std::uint32_t sourceHeight = std::max(obs_source_get_height(surface->source_), 1U);
		const float scale = std::min(static_cast<float>(width) / static_cast<float>(sourceWidth),
					     static_cast<float>(height) / static_cast<float>(sourceHeight));
		const int targetWidth = static_cast<int>(std::lround(scale * static_cast<float>(sourceWidth)));
		const int targetHeight = static_cast<int>(std::lround(scale * static_cast<float>(sourceHeight)));
		const int x = (static_cast<int>(width) - targetWidth) / 2;
		const int y = (static_cast<int>(height) - targetHeight) / 2;

		gs_viewport_push();
		gs_projection_push();
		const bool previousLinearSrgb = gs_set_linear_srgb(true);
		gs_ortho(0.0F, static_cast<float>(sourceWidth), 0.0F, static_cast<float>(sourceHeight), -100.0F,
			 100.0F);
		gs_set_viewport(x, y, targetWidth, targetHeight);
		obs_source_video_render(surface->source_);
		gs_set_linear_srgb(previousLinearSrgb);
		gs_projection_pop();
		gs_viewport_pop();
	}

	obs_display_t *display_ = nullptr;
	obs_source_t *source_ = nullptr;
	std::mutex sourceMutex_;
};

ReplayPreviewWidget::ReplayPreviewWidget(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	pathLabel_ = new QLabel(text("ReplayTimeline.PreviewSelectReplay"), this);
	pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	pathLabel_->setWordWrap(true);
	layout->addWidget(pathLabel_);

	videoSurface_ = new ReplayVideoSurface(this);
	layout->addWidget(videoSurface_, 1);

	auto *controls = new QHBoxLayout();
	playPauseButton_ = new QPushButton(text("ReplayTimeline.PreviewPlay"), this);
	stopButton_ = new QPushButton(text("ReplayTimeline.PreviewStop"), this);
	seekSlider_ = new QSlider(Qt::Horizontal, this);
	seekSlider_->setRange(0, 0);
	timeLabel_ = new QLabel(QStringLiteral("00:00 / 00:00"), this);
	muteButton_ = new QPushButton(this);
	muteButton_->setCheckable(true);
	muteButton_->setChecked(true);
	muteButton_->setToolTip(text("ReplayTimeline.PreviewMuteTooltip"));
	const int muteButtonWidth =
		std::max(muteButton_->fontMetrics().horizontalAdvance(text("ReplayTimeline.PreviewMuted")),
			 muteButton_->fontMetrics().horizontalAdvance(text("ReplayTimeline.PreviewAudioOn"))) +
		28;
	muteButton_->setMinimumWidth(muteButtonWidth);
	controls->addWidget(playPauseButton_);
	controls->addWidget(stopButton_);
	controls->addWidget(seekSlider_, 1);
	controls->addWidget(timeLabel_);
	controls->addWidget(muteButton_);
	layout->addLayout(controls);
	statusLabel_ = new QLabel(text("ReplayTimeline.PreviewMutedStatus"), this);
	statusLabel_->setWordWrap(true);
	layout->addWidget(statusLabel_);

	playbackTimer_ = new QTimer(this);
	playbackTimer_->setInterval(100);
	connect(playbackTimer_, &QTimer::timeout, this, [this]() { updatePlaybackState(); });
	connect(playPauseButton_, &QPushButton::clicked, this, [this]() { togglePlayback(); });
	connect(stopButton_, &QPushButton::clicked, this, [this]() {
		if (!source_)
			return;
		obs_source_media_stop(source_);
		obs_source_media_set_time(source_, 0);
		updatePlaybackState();
	});
	connect(seekSlider_, &QSlider::sliderPressed, this, [this]() { seeking_ = true; });
	connect(seekSlider_, &QSlider::sliderReleased, this, [this]() {
		if (source_)
			obs_source_media_set_time(source_, seekSlider_->value());
		seeking_ = false;
		updatePlaybackState();
	});
	connect(muteButton_, &QPushButton::toggled, this, [this](bool muted) {
		muted_ = muted;
		applyAudioState();
		updateMuteButton();
	});
	updateMuteButton();
	setControlsEnabled(false);
}

ReplayPreviewWidget::~ReplayPreviewWidget()
{
	releaseSource();
}

void ReplayPreviewWidget::loadReplay(const QString &path)
{
	if (source_ && replayPath_ == path)
		return;
	releaseSource();
	replayPath_ = path;
	const QFileInfo media(path);
	pathLabel_->setText(media.fileName().isEmpty() ? path : media.fileName());
	pathLabel_->setToolTip(path);
	if (!media.exists() || !media.isFile()) {
		statusLabel_->setText(text("ReplayTimeline.PreviewMissing"));
		setControlsEnabled(false);
		return;
	}

	obs_data_t *settings = obs_data_create();
	const QByteArray utf8 = path.toUtf8();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", utf8.constData());
	obs_data_set_bool(settings, "looping", false);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "close_when_inactive", true);
	obs_data_set_bool(settings, "clear_on_media_end", false);
	obs_data_set_bool(settings, "log_changes", false);
	source_ = obs_source_create_private("ffmpeg_source", "Replay Timeline Preview", settings);
	obs_data_release(settings);
	if (!source_) {
		statusLabel_->setText(text("ReplayTimeline.PreviewUnavailable"));
		setControlsEnabled(false);
		return;
	}

	muted_ = true;
	muteButton_->setChecked(true);
	applyAudioState();
	obs_source_inc_active(source_);
	sourceActive_ = true;
	obs_source_inc_showing(source_);
	sourceShowing_ = true;
	videoSurface_->setSource(source_);
	obs_source_media_restart(source_);
	obs_source_media_play_pause(source_, true);
	setControlsEnabled(true);
	statusLabel_->setText(text("ReplayTimeline.PreviewAudioDeviceStatus").arg(monitoringDeviceName()));
	playbackTimer_->start();
	updatePlaybackState();
}

void ReplayPreviewWidget::clearReplay()
{
	releaseSource();
	replayPath_.clear();
	pathLabel_->setText(text("ReplayTimeline.PreviewSelectReplay"));
	pathLabel_->setToolTip({});
	statusLabel_->setText(text("ReplayTimeline.PreviewMutedStatus"));
}

void ReplayPreviewWidget::hideEvent(QHideEvent *event)
{
	if (source_) {
		obs_source_media_play_pause(source_, true);
		muted_ = true;
		muteButton_->setChecked(true);
		applyAudioState();
	}
	QWidget::hideEvent(event);
}

void ReplayPreviewWidget::releaseSource()
{
	playbackTimer_->stop();
	videoSurface_->setSource(nullptr);
	if (source_) {
		obs_source_set_monitoring_type(source_, OBS_MONITORING_TYPE_NONE);
		obs_source_set_muted(source_, true);
		obs_source_media_stop(source_);
		if (sourceShowing_)
			obs_source_dec_showing(source_);
		if (sourceActive_)
			obs_source_dec_active(source_);
		obs_source_release(source_);
	}
	source_ = nullptr;
	sourceActive_ = false;
	sourceShowing_ = false;
	seekSlider_->setRange(0, 0);
	timeLabel_->setText(QStringLiteral("00:00 / 00:00"));
	playPauseButton_->setText(text("ReplayTimeline.PreviewPlay"));
	setControlsEnabled(false);
}

void ReplayPreviewWidget::togglePlayback()
{
	if (!source_)
		return;
	const obs_media_state state = obs_source_media_get_state(source_);
	if (state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_OPENING ||
	    state == OBS_MEDIA_STATE_BUFFERING) {
		obs_source_media_play_pause(source_, true);
	} else if (state == OBS_MEDIA_STATE_ENDED || state == OBS_MEDIA_STATE_STOPPED ||
		   state == OBS_MEDIA_STATE_ERROR) {
		obs_source_media_restart(source_);
	} else {
		obs_source_media_play_pause(source_, false);
	}
	updatePlaybackState();
}

void ReplayPreviewWidget::updatePlaybackState()
{
	if (!source_)
		return;
	const std::int64_t duration = std::max<std::int64_t>(obs_source_media_get_duration(source_), 0);
	const std::int64_t position = std::clamp<std::int64_t>(obs_source_media_get_time(source_), 0, duration);
	const int sliderMaximum = static_cast<int>(std::min<std::int64_t>(duration, std::numeric_limits<int>::max()));
	seekSlider_->setMaximum(sliderMaximum);
	if (!seeking_)
		seekSlider_->setValue(static_cast<int>(std::min<std::int64_t>(position, sliderMaximum)));
	timeLabel_->setText(QStringLiteral("%1 / %2").arg(playbackTime(position), playbackTime(duration)));
	const obs_media_state state = obs_source_media_get_state(source_);
	playPauseButton_->setText(text(state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_OPENING ||
					    state == OBS_MEDIA_STATE_BUFFERING
					    ? "ReplayTimeline.PreviewPause"
					    : "ReplayTimeline.PreviewPlay"));
	if (state == OBS_MEDIA_STATE_ERROR)
		statusLabel_->setText(text("ReplayTimeline.PreviewError"));
}

void ReplayPreviewWidget::applyAudioState()
{
	if (!source_)
		return;
	if (muted_) {
		obs_source_set_monitoring_type(source_, OBS_MONITORING_TYPE_NONE);
		obs_source_set_muted(source_, true);
	} else {
		obs_source_set_muted(source_, false);
		obs_source_set_monitoring_type(source_, OBS_MONITORING_TYPE_MONITOR_ONLY);
	}
}

void ReplayPreviewWidget::updateMuteButton()
{
	muteButton_->setText(text(muted_ ? "ReplayTimeline.PreviewMuted" : "ReplayTimeline.PreviewAudioOn"));
}

void ReplayPreviewWidget::setControlsEnabled(bool enabled)
{
	playPauseButton_->setEnabled(enabled);
	stopButton_->setEnabled(enabled);
	seekSlider_->setEnabled(enabled);
	muteButton_->setEnabled(enabled);
}

} // namespace replay_timeline
