// Full screen music player, ported from exteraGram's MusicPlayerSheet:
// the geometry of PlayerLayout, the springs of Material3Tokens and the
// shapes of PlayerControlsView / ExpressiveStateDrawable / WavySeekBar.
#include "extera/extera_player_box.h"

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "extera/extera_config.h"
#include "extera/extera_player.h"
#include "extera/extera_rhythm.h"
#include "extera/extera_text.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "media/audio/media_audio.h"
#include "media/media_common.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_dropdown.h"
#include "styles/style_media_player.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/gl/gl_shader.h"
#include "ui/gl/gl_surface.h"
#include "ui/effects/animation_value.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "lottie/lottie_icon.h"
#include "data/data_document_media.h"
#include "ui/image/image.h"

#include <QtSvg/QSvgRenderer>
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

#include <QtCore/QPointer>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QVector2D>
#include <QtGui/QVector3D>
#include <QtGui/QVector4D>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>
#include <QtGui/QLinearGradient>
#include <QtGui/QTextLayout>

#include <array>
#include <cmath>

namespace Extera {
namespace {

constexpr auto kHoldDelay = crl::time(300);
constexpr auto kColorDuration = crl::time(450);
constexpr auto kShowDuration = crl::time(260);
constexpr auto kLineFadeDuration = crl::time(600);
constexpr auto kManualScrollHold = crl::time(2500);
constexpr auto kRewindTick = crl::time(16);

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] float64 D(float64 value) {
	return style::ConvertScaleExact(value);
}

[[nodiscard]] style::font Font(int size, bool bold) {
	const auto &base = bold ? st::semiboldFont : st::normalFont;
	const auto family = Extera::String(u"player_font"_q);
	return family.isEmpty()
		? style::font(S(size), base->flags(), base->family())
		: style::font(S(size), base->flags(), family);
}

[[nodiscard]] float64 Lerp(float64 a, float64 b, float64 t) {
	return a + (b - a) * t;
}

[[nodiscard]] float64 Clamp01(float64 value) {
	return std::clamp(value, 0., 1.);
}

[[nodiscard]] float64 Smooth(float64 value) {
	const auto t = Clamp01(value);
	return t * t * (3. - 2. * t);
}

[[nodiscard]] QColor Mix(QColor a, QColor b, float64 k) {
	return QColor::fromRgbF(
		a.redF() + (b.redF() - a.redF()) * k,
		a.greenF() + (b.greenF() - a.greenF()) * k,
		a.blueF() + (b.blueF() - a.blueF()) * k,
		a.alphaF() + (b.alphaF() - a.alphaF()) * k);
}

[[nodiscard]] QColor Alpha(QColor color, float64 alpha) {
	color.setAlphaF(Clamp01(color.alphaF() * alpha));
	return color;
}

[[nodiscard]] PlayerPalette MixPalette(
		const PlayerPalette &a,
		const PlayerPalette &b,
		float64 k) {
	return {
		Mix(a.background, b.background, k),
		Mix(a.surface, b.surface, k),
		Mix(a.text, b.text, k),
		Mix(a.textSecondary, b.textSecondary, k),
		Mix(a.accent, b.accent, k),
		Mix(a.onAccent, b.onAccent, k),
		Mix(a.tonal, b.tonal, k),
		Mix(a.onTonal, b.onTonal, k),
		Mix(a.track, b.track, k),
		{
			Mix(a.glow[0], b.glow[0], k),
			Mix(a.glow[1], b.glow[1], k),
			Mix(a.glow[2], b.glow[2], k),
			Mix(a.glow[3], b.glow[3], k),
		},
	};
}

[[nodiscard]] float64 Approach(
		float64 current,
		float64 target,
		float64 dt,
		float64 up,
		float64 down) {
	const auto tau = (target > current) ? up : down;
	return current + (target - current) * (1. - std::exp(-dt / tau));
}

// White masks of the exteraGram icons, tinted when drawn.
[[nodiscard]] const QImage &IconMask(const QString &name, int size) {
	static auto cache = QHash<QString, QImage>();
	const auto key = name + '/' + QString::number(size);
	if (const auto i = cache.constFind(key); i != cache.constEnd()) {
		return *i;
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto path = u":/gui/art/extera/player/"_q + name;
		if (name.endsWith(u".svg"_q)) {
			auto renderer = QSvgRenderer(path);
			renderer.render(&p, QRectF(0, 0, image.width(), image.height()));
		} else {
			p.drawImage(QRect(QPoint(), image.size()), QImage(path));
		}
	}
	image.setDevicePixelRatio(ratio);
	return *cache.insert(key, std::move(image));
}

void DrawIcon(
		QPainter &p,
		const QString &name,
		QPointF center,
		int size,
		QColor color) {
	const auto &mask = IconMask(name, size);
	auto tinted = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
	tinted.fill(Qt::transparent);
	{
		auto q = QPainter(&tinted);
		q.drawImage(0, 0, mask);
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.fillRect(tinted.rect(), color);
	}
	tinted.setDevicePixelRatio(mask.devicePixelRatio());
	p.drawImage(
		QRectF(center.x() - size / 2., center.y() - size / 2., size, size),
		tinted);
}

// Grain: on its lowest level it only dithers the dark gradients, which
// otherwise show bands on 8-bit screens.
[[nodiscard]] const QPixmap &NoiseTile(int level) {
	static auto tiles = std::array<QPixmap, 4>();
	level = std::clamp(level, 0, 3);
	auto &result = tiles[level];
	if (result.isNull() && level > 0) {
		constexpr auto kSide = 128;
		constexpr auto kStrength = std::array<int, 4>{ 0, 1, 3, 5 };
		auto image = QImage(kSide, kSide, QImage::Format_ARGB32_Premultiplied);
		auto seed = quint32(0x9E3779B9U);
		for (auto y = 0; y != kSide; ++y) {
			auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (auto x = 0; x != kSide; ++x) {
				seed = seed * 1664525U + 1013904223U;
				const auto value = int(seed >> 24);
				const auto alpha = (4 + (value % 5)) * kStrength[level];
				line[x] = (value & 1)
					? qPremultiply(qRgba(255, 255, 255, alpha))
					: qPremultiply(qRgba(0, 0, 0, alpha));
			}
		}
		result = QPixmap::fromImage(image);
	}
	return result;
}

[[nodiscard]] QString Tr(const char *ru, const char *en) {
	return Extera::Text(QString::fromUtf8(ru), QString::fromUtf8(en));
}

[[nodiscard]] QPainterPath RoundedPath(
		QRectF rect,
		float64 left,
		float64 right) {
	auto path = QPainterPath();
	const auto l = std::min(left, std::min(rect.width(), rect.height()) / 2.);
	const auto r = std::min(right, std::min(rect.width(), rect.height()) / 2.);
	path.moveTo(rect.left() + l, rect.top());
	path.lineTo(rect.right() - r, rect.top());
	path.arcTo(QRectF(rect.right() - 2 * r, rect.top(), 2 * r, 2 * r), 90, -90);
	path.lineTo(rect.right(), rect.bottom() - r);
	path.arcTo(
		QRectF(rect.right() - 2 * r, rect.bottom() - 2 * r, 2 * r, 2 * r),
		0,
		-90);
	path.lineTo(rect.left() + l, rect.bottom());
	path.arcTo(
		QRectF(rect.left(), rect.bottom() - 2 * l, 2 * l, 2 * l),
		270,
		-90);
	path.lineTo(rect.left(), rect.top() + l);
	path.arcTo(QRectF(rect.left(), rect.top(), 2 * l, 2 * l), 180, -90);
	path.closeSubpath();
	return path;
}

// Material3Tokens.createSpring: a damped spring with unit mass.
struct Spring {
	float64 value = 0.;
	float64 velocity = 0.;
	float64 target = 0.;
	float64 damping = 1.;
	float64 stiffness = 800.;
	float64 epsilon = 0.001;
	bool running = false;

	void setup(float64 dampingRatio, float64 stiff, float64 eps) {
		damping = dampingRatio;
		stiffness = stiff;
		epsilon = eps;
	}
	void to(float64 value) {
		if (target != value || !running) {
			target = value;
			running = (std::abs(this->value - target) > epsilon)
				|| (std::abs(velocity) > epsilon);
		}
	}
	void jump(float64 value) {
		this->value = target = value;
		velocity = 0.;
		running = false;
	}
	void step(float64 dt) {
		if (!running) {
			return;
		}
		const auto c = 2. * damping * std::sqrt(stiffness);
		const auto steps = std::max(1, int(std::ceil(dt / 0.004)));
		const auto h = dt / steps;
		for (auto i = 0; i != steps; ++i) {
			const auto a = -stiffness * (value - target) - c * velocity;
			velocity += a * h;
			value += velocity * h;
		}
		if (std::abs(value - target) < epsilon
			&& std::abs(velocity) < epsilon * 10.) {
			value = target;
			velocity = 0.;
			running = false;
		}
	}
};

// The speed menu of the promo: a slider, presets and the two toggles
// that matter while listening, drawn in the colours of the cover.
class SpeedPanel final : public Ui::RpWidget {
public:
	SpeedPanel(
		not_null<QWidget*> parent,
		QRectF anchor,
		Fn<PlayerPalette()> palette,
		Fn<void(float64)> apply);

	void hideAnimated();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	enum class Part {
		None,
		Inside,
		Slider,
		Chip,
		Row,
	};
	struct Hit {
		Part part = Part::None;
		int index = 0;
	};

	[[nodiscard]] QRectF panelRect() const;
	[[nodiscard]] QRectF sliderRect() const;
	[[nodiscard]] QRectF chipRect(int index) const;
	[[nodiscard]] QRectF rowRect(int index) const;
	[[nodiscard]] Hit hitTest(QPointF point) const;
	[[nodiscard]] float64 shownSpeed() const;
	void setSpeed(float64 speed, bool animated);
	void setFromSlider(float64 x);
	void toggleRow(int index);

	QRectF _anchor;
	Fn<PlayerPalette()> _palette;
	Fn<void(float64)> _apply;
	float64 _speed = 1.;
	float64 _speedFrom = 1.;
	Ui::Animations::Simple _shown;
	Ui::Animations::Simple _speedAnimation;
	std::array<Ui::Animations::Simple, 2> _checkAnimations;
	std::array<bool, 2> _checked = { false, false };
	bool _hiding = false;
	bool _dragging = false;

};

constexpr auto kPanelSpeedMin = 0.5;
constexpr auto kPanelSpeedMax = 2.;
constexpr auto kPanelPresets = std::array<float64, 4>{ 0.5, 1., 1.5, 2. };

SpeedPanel::SpeedPanel(
	not_null<QWidget*> parent,
	QRectF anchor,
	Fn<PlayerPalette()> palette,
	Fn<void(float64)> apply)
: RpWidget(parent)
, _anchor(anchor)
, _palette(std::move(palette))
, _apply(std::move(apply))
, _speed(Core::App().settings().audioPlaybackSpeed(false))
, _speedFrom(_speed) {
	_checked[0] = Extera::Bool(u"player_change_pitch"_q);
	_checked[1] = Extera::Bool(u"player_rhythm"_q);
	setGeometry(parent->rect());
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	show();
	raise();
	setFocus();
	_shown.start([=] { update(); }, 0., 1., crl::time(220), anim::easeOutCirc);
}

void SpeedPanel::hideAnimated() {
	if (_hiding) {
		return;
	}
	_hiding = true;
	if (const auto parent = parentWidget()) {
		parent->setFocus();
	}
	_shown.start([=] {
		update();
		if (!_shown.animating()) {
			deleteLater();
		}
	}, 1., 0., crl::time(160), anim::easeOutCirc);
}

QRectF SpeedPanel::panelRect() const {
	const auto w = float64(S(300));
	const auto h = float64(S(248));
	const auto margin = float64(S(12));
	const auto x = std::clamp(
		_anchor.center().x() - w / 2.,
		margin,
		std::max(margin, width() - w - margin));
	auto y = _anchor.top() - S(10) - h;
	if (y < margin) {
		y = _anchor.bottom() + S(10);
	}
	return QRectF(x, y, w, h);
}

QRectF SpeedPanel::sliderRect() const {
	const auto r = panelRect();
	return QRectF(r.x() + S(20), r.y() + S(58), r.width() - S(40), S(20));
}

QRectF SpeedPanel::chipRect(int index) const {
	const auto r = panelRect();
	const auto gap = float64(S(8));
	const auto w = (r.width() - S(40) - gap * 3) / 4.;
	return QRectF(r.x() + S(20) + index * (w + gap), r.y() + S(94), w, S(36));
}

QRectF SpeedPanel::rowRect(int index) const {
	const auto r = panelRect();
	return QRectF(r.x() + S(8), r.y() + S(150) + index * S(44), r.width() - S(16), S(44));
}

SpeedPanel::Hit SpeedPanel::hitTest(QPointF point) const {
	if (sliderRect().adjusted(-S(6), -S(10), S(6), S(10)).contains(point)) {
		return { Part::Slider };
	}
	for (auto i = 0; i != 4; ++i) {
		if (chipRect(i).contains(point)) {
			return { Part::Chip, i };
		}
	}
	for (auto i = 0; i != 2; ++i) {
		if (rowRect(i).contains(point)) {
			return { Part::Row, i };
		}
	}
	return { panelRect().contains(point) ? Part::Inside : Part::None };
}

float64 SpeedPanel::shownSpeed() const {
	return _speedAnimation.value(_speed);
}

void SpeedPanel::setSpeed(float64 speed, bool animated) {
	speed = std::clamp(std::round(speed * 20.) / 20., kPanelSpeedMin, kPanelSpeedMax);
	if (std::abs(speed - _speed) < 0.001) {
		return;
	}
	const auto from = shownSpeed();
	_speed = speed;
	if (animated) {
		_speedAnimation.start([=] { update(); }, from, speed, crl::time(240), anim::easeOutCirc);
	} else {
		_speedAnimation.stop();
	}
	_apply(speed);
	update();
}

void SpeedPanel::setFromSlider(float64 x) {
	const auto r = sliderRect();
	const auto k = Clamp01((x - r.left()) / r.width());
	setSpeed(Lerp(kPanelSpeedMin, kPanelSpeedMax, k), false);
}

void SpeedPanel::toggleRow(int index) {
	_checked[index] = !_checked[index];
	const auto now = _checked[index];
	_checkAnimations[index].start(
		[=] { update(); },
		now ? 0. : 1.,
		now ? 1. : 0.,
		crl::time(200),
		anim::easeOutCirc);
	if (index == 0) {
		Extera::Config::Instance().setBool(u"player_change_pitch"_q, now);
		Media::Player::instance()->updatePlaybackSpeed();
	} else {
		Extera::Config::Instance().setBool(u"player_rhythm"_q, now);
	}
}

void SpeedPanel::mousePressEvent(QMouseEvent *e) {
	if (_hiding) {
		return;
	}
	const auto hit = hitTest(QPointF(e->pos()));
	switch (hit.part) {
	case Part::None: hideAnimated(); break;
	case Part::Slider:
		_dragging = true;
		setFromSlider(e->pos().x());
		break;
	case Part::Chip: setSpeed(kPanelPresets[hit.index], true); break;
	case Part::Row: toggleRow(hit.index); break;
	default: break;
	}
}

void SpeedPanel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		setFromSlider(e->pos().x());
		return;
	}
	const auto part = hitTest(QPointF(e->pos())).part;
	setCursor((part == Part::Slider || part == Part::Chip || part == Part::Row)
		? style::cur_pointer
		: style::cur_default);
}

void SpeedPanel::mouseReleaseEvent(QMouseEvent *e) {
	_dragging = false;
}

void SpeedPanel::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		hideAnimated();
	} else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Down) {
		setSpeed(_speed - 0.05, true);
	} else if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Up) {
		setSpeed(_speed + 0.05, true);
	} else {
		RpWidget::keyPressEvent(e);
	}
}

void SpeedPanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto shown = _shown.value(_hiding ? 0. : 1.);
	if (shown <= 0.) {
		return;
	}
	const auto c = _palette();
	const auto r = panelRect();
	const auto above = (r.bottom() <= _anchor.top());
	const auto origin = QPointF(
		std::clamp(_anchor.center().x(), r.left(), r.right()),
		above ? r.bottom() : r.top());
	const auto scale = 0.85 + 0.15 * shown;
	p.setOpacity(shown);
	p.translate(origin);
	p.scale(scale, scale);
	p.translate(-origin);

	const auto radius = D(24.);
	p.setPen(Qt::NoPen);
	for (auto i = 4; i != 0; --i) {
		p.setBrush(QColor(0, 0, 0, 9 * (5 - i)));
		p.drawRoundedRect(
			r.adjusted(-i * D(2.), -i * D(1.5), i * D(2.), i * D(3.)),
			radius + i * D(2.),
			radius + i * D(2.));
	}
	p.setBrush(Mix(c.surface, c.text, 0.06));
	p.drawRoundedRect(r, radius, radius);

	const auto speed = shownSpeed();
	const auto pad = float64(S(20));
	p.setFont(Font(15, false));
	p.setPen(c.textSecondary);
	p.drawText(QRectF(r.x() + pad, r.y() + S(16), r.width() / 2., S(28)), Qt::AlignLeft | Qt::AlignVCenter, Tr("Скорость", "Speed"));
	p.setFont(Font(21, true));
	p.setPen(c.text);
	p.drawText(QRectF(r.x() + r.width() / 2., r.y() + S(14), r.width() / 2. - pad, S(30)), Qt::AlignRight | Qt::AlignVCenter, QString::number(speed, 'f', 2) + 'x');

	const auto slider = sliderRect();
	const auto k = Clamp01((speed - kPanelSpeedMin) / (kPanelSpeedMax - kPanelSpeedMin));
	const auto trackHeight = D(16.);
	const auto track = QRectF(slider.x(), slider.center().y() - trackHeight / 2., slider.width(), trackHeight);
	p.setPen(Qt::NoPen);
	p.setBrush(c.track);
	p.drawRoundedRect(track, trackHeight / 2., trackHeight / 2.);
	const auto thumbX = slider.x() + slider.width() * k;
	p.setBrush(c.accent);
	p.drawRoundedRect(
		QRectF(track.x(), track.y(), std::max(trackHeight, thumbX - track.x() - D(5.)), trackHeight),
		trackHeight / 2.,
		trackHeight / 2.);
	const auto thumbHeight = _dragging ? D(36.) : D(32.);
	p.setBrush(c.text);
	p.drawRoundedRect(
		QRectF(thumbX - D(3.), slider.center().y() - thumbHeight / 2., D(6.), thumbHeight),
		D(3.),
		D(3.));

	p.setFont(Font(14, true));
	for (auto i = 0; i != 4; ++i) {
		const auto chip = chipRect(i);
		const auto on = std::abs(_speed - kPanelPresets[i]) < 0.001;
		p.setPen(Qt::NoPen);
		p.setBrush(on ? c.accent : c.tonal);
		p.drawRoundedRect(chip, chip.height() / 2., chip.height() / 2.);
		p.setPen(on ? c.onAccent : c.onTonal);
		p.drawText(chip, Qt::AlignCenter, QString::number(kPanelPresets[i], 'g', 2) + 'x');
	}
	p.setPen(Qt::NoPen);
	p.setBrush(Alpha(c.textSecondary, 0.2));
	p.drawRect(QRectF(r.x() + S(16), r.y() + S(143), r.width() - S(32), D(1.)));

	const auto labels = std::array<QString, 2>{
		Tr("Менять тон", "Change pitch"),
		Tr("Фон в такт музыке", "Background to the beat"),
	};
	p.setFont(Font(15, false));
	for (auto i = 0; i != 2; ++i) {
		const auto row = rowRect(i);
		p.setPen(c.text);
		p.drawText(row.adjusted(S(12), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, labels[i]);
		const auto value = _checkAnimations[i].value(_checked[i] ? 1. : 0.);
		const auto side = D(22.);
		const auto box = QRectF(row.right() - S(12) - side, row.center().y() - side / 2., side, side);
		if (value < 1.) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(Alpha(c.textSecondary, 1. - value), D(2.)));
			p.drawRoundedRect(box.adjusted(D(1.), D(1.), -D(1.), -D(1.)), D(6.), D(6.));
		}
		if (value > 0.) {
			p.setPen(Qt::NoPen);
			p.setBrush(c.accent);
			const auto inner = side * value;
			p.drawRoundedRect(
				QRectF(box.center().x() - inner / 2., box.center().y() - inner / 2., inner, inner),
				D(6.),
				D(6.));
			auto check = QPainterPath();
			check.moveTo(box.x() + side * 0.25, box.y() + side * 0.52);
			check.lineTo(box.x() + side * 0.43, box.y() + side * 0.7);
			check.lineTo(box.x() + side * 0.76, box.y() + side * 0.32);
			auto pen = QPen(Alpha(c.onAccent, value), D(2.4));
			pen.setCapStyle(Qt::RoundCap);
			pen.setJoinStyle(Qt::RoundJoin);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawPath(check);
		}
	}
}


// WHY: the backdrop is a sibling under the player: when OpenGL works every
// pixel of the blobs, the live gradient and the grain is computed by the
// GPU, and the player widget itself is repainted only when its content changes.
struct BackdropState {
	QColor background;
	QColor accent;
	QColor mixed;
	QColor tonal;
	std::array<QColor, 4> glow;
	QPointF cover;
	std::array<float, 4> blobAlpha = {};
	std::array<float, 4> blobScale = {};
	float presence = 0.f;
	float pulse = 0.f;
	float shift = 0.f;
	float bloom = 0.f;
	float angle = 0.f;
	float time = 0.f;
	float grain = 0.f;
	QPointF grainOffset;
	float light = 0.f;
	bool hasCover = false;
	bool live = false;
};

[[nodiscard]] Ui::GL::ShaderPart BackdropFragment() {
	return {
		.header = R"glsl(
uniform vec2 u_size;
uniform vec3 u_bg;
uniform vec3 u_accent;
uniform vec3 u_mixed;
uniform vec3 u_tonal;
uniform vec3 u_g0;
uniform vec3 u_g1;
uniform vec3 u_g2;
uniform vec3 u_g3;
uniform vec2 u_cover;
uniform vec4 u_blobAlpha;
uniform vec4 u_blobScale;
uniform float u_presence;
uniform float u_pulse;
uniform float u_shift;
uniform float u_bloom;
uniform float u_angle;
uniform float u_time;
uniform float u_grain;
uniform vec2 u_grainOffset;
uniform float u_light;
uniform float u_hasCover;
uniform float u_live;

float fall(float t) {
	float s = clamp(t, 0., 1.) * 5.;
	if (s < 1.) return mix(1., 0.88, s);
	if (s < 2.) return mix(0.88, 0.6, s - 1.);
	if (s < 3.) return mix(0.6, 0.32, s - 2.);
	if (s < 4.) return mix(0.32, 0.12, s - 3.);
	return mix(0.12, 0., s - 4.);
}
float hash(vec2 p) {
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}
vec3 glow(vec3 c, vec2 frag, vec2 center, float radius, vec3 color, float alpha) {
	float d = clamp(length(frag - center) / radius, 0., 1.);
	return mix(c, color, alpha * (1. - d));
}
vec3 blob(vec3 c, vec2 frag, vec2 center, float radius, vec3 color, float alpha) {
	float d = length(frag - center) / max(radius, 1.);
	return mix(c, color, alpha * fall(d));
}
vec3 spot(vec3 c, vec2 uv, float i, vec2 base, vec2 r, vec3 color, float alpha) {
	vec2 center = base + vec2(
		sin(u_time * 0.13 + i * 2.1) * 0.07,
		cos(u_time * 0.11 + i * 1.3) * 0.06);
	float breathe = 1. + 0.08 * sin(u_time * 0.21 + i * 1.7);
	float d = length((uv - center) / (r * breathe));
	float k = (d < 0.35)
		? mix(1., 0.55, d / 0.35)
		: ((d < 0.7) ? mix(0.55, 0., (d - 0.35) / 0.35) : 0.);
	return mix(c, color, alpha * k);
}
vec3 pick(float k) {
	k = mod(k, 4.);
	if (k < 0.5) return u_g0;
	if (k < 1.5) return u_g1;
	if (k < 2.5) return u_g2;
	return u_g3;
}
vec3 orbit(vec3 c, vec2 frag, float angle, vec2 o, float rad, float scale, float index, float alpha) {
	vec2 center = vec2(0.5 + cos(angle) * o.x, 0.42 + sin(angle) * o.y) * u_size;
	float base = floor(u_shift);
	float part = u_shift - base;
	float r = rad * u_size.x * scale;
	c = blob(c, frag, center, r, pick(index + base), alpha * (1. - part));
	if (part > 0.003) {
		c = blob(c, frag, center, r, pick(index + base + 1.), alpha * part);
	}
	return c;
}
)glsl",
		.body = R"glsl(
	vec2 frag = vec2(gl_FragCoord.x, u_size.y - gl_FragCoord.y);
	vec2 uv = frag / u_size;
	vec3 c = u_bg;
	if (u_hasCover > 0.5) {
		float big = max(u_size.x, u_size.y) * 0.55;
		c = glow(c, frag, u_cover, big, u_accent, 0.12);
		c = glow(c, frag, u_size * vec2(0.9, 0.15), big, u_mixed, 0.07);
		c = glow(c, frag, u_size * vec2(0.15, 0.95), big, u_tonal, 0.12);
	}
	if (u_live > 0.5) {
		float k = (u_light > 0.5) ? 0.6 : 1.;
		c = spot(c, uv, 0., vec2(0.1, 0.08), vec2(0.7, 0.86), u_g0, 0.5 * k);
		c = spot(c, uv, 1., vec2(0.92, 0.2), vec2(0.7, 0.86), u_g1, 0.45 * k);
		c = spot(c, uv, 2., vec2(0.5, 1.05), vec2(1., 0.69), u_g2, 0.38 * k);
	}
	if (u_presence > 0.003) {
		c = orbit(c, frag, u_angle * 1. + 0., vec2(0.3, 0.26), 1.02, u_blobScale.x, 0., u_blobAlpha.x);
		c = orbit(c, frag, u_angle * 0.75 + 1.5707963, vec2(0.34, 0.3), 0.94, u_blobScale.y, 1., u_blobAlpha.y);
		c = orbit(c, frag, u_angle * -1.25 + 3.1415927, vec2(0.26, 0.22), 0.96, u_blobScale.z, 2., u_blobAlpha.z);
		c = orbit(c, frag, u_angle * 0.6 + 4.712389, vec2(0.38, 0.34), 0.86, u_blobScale.w, 3., u_blobAlpha.w);
		if (u_pulse > 0.003) {
			float d = length(frag - u_cover) / (0.7 * u_size.x * (1. + u_pulse * 0.12));
			c = mix(c, pick(floor(u_shift)), u_pulse * 0.14 * u_presence * fall(d));
		}
		if (u_bloom > 0.003) {
			float d = length(frag - vec2(0.5, 0.42) * u_size) / (u_size.x * ((1. - u_bloom) * 0.9 + 0.6));
			c = mix(c, mix(u_g0, vec3(1.), 0.45), u_bloom * 0.5 * u_presence * fall(d));
		}
		float s = clamp((uv.y - 0.52) / 0.48, 0., 1.);
		c = mix(c, u_bg, 0.718 * u_presence * s);
	}
	vec2 g = floor(frag + u_grainOffset);
	float ga = (4. + floor(hash(g + vec2(37.2, 11.9)) * 5.)) / 255. * u_grain;
	c = mix(c, vec3(step(0.5, hash(g))), ga);
	c += (hash(g + vec2(5.3, 91.7)) - 0.5) / 255.;
	result = vec4(clamp(c, 0., 1.), 1.);
)glsl",
	};
}

class BackdropRenderer final : public Ui::GL::Renderer {
public:
	BackdropRenderer(
		Fn<BackdropState()> state,
		Fn<void(QPainter&)> paintCpu)
	: _state(std::move(state))
	, _paintCpu(std::move(paintCpu)) {
	}

	void init(QOpenGLFunctions &f) override {
		_buffer.emplace();
		_buffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
		_buffer->create();
		_buffer->bind();
		constexpr auto vertices = std::array<GLfloat, 8>{
			-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f,
		};
		_buffer->allocate(vertices.data(), int(sizeof(vertices)));
		_program.emplace();
		Ui::GL::LinkProgram(
			&*_program,
			Ui::GL::VertexShader({}),
			Ui::GL::FragmentShader({ BackdropFragment() }));
	}

	void deinit(QOpenGLFunctions *f) override {
		_program = std::nullopt;
		_buffer = std::nullopt;
	}

	void paint(
			not_null<QOpenGLWidget*> widget,
			QOpenGLFunctions &f) override {
		const auto s = _state();
		const auto ratio = widget->devicePixelRatio();
		const auto w = float(widget->width() * ratio);
		const auto h = float(widget->height() * ratio);
		if (!_program || !_program->isLinked() || !_buffer) {
			f.glClearColor(
				s.background.redF(),
				s.background.greenF(),
				s.background.blueF(),
				1.f);
			f.glClear(GL_COLOR_BUFFER_BIT);
			return;
		}
		f.glViewport(0, 0, int(w), int(h));
		_program->bind();
		const auto rgb = [](const QColor &c) {
			return QVector3D(c.redF(), c.greenF(), c.blueF());
		};
		_program->setUniformValue("u_size", QVector2D(w, h));
		_program->setUniformValue("u_bg", rgb(s.background));
		_program->setUniformValue("u_accent", rgb(s.accent));
		_program->setUniformValue("u_mixed", rgb(s.mixed));
		_program->setUniformValue("u_tonal", rgb(s.tonal));
		_program->setUniformValue("u_g0", rgb(s.glow[0]));
		_program->setUniformValue("u_g1", rgb(s.glow[1]));
		_program->setUniformValue("u_g2", rgb(s.glow[2]));
		_program->setUniformValue("u_g3", rgb(s.glow[3]));
		_program->setUniformValue(
			"u_cover",
			QVector2D(float(s.cover.x() * ratio), float(s.cover.y() * ratio)));
		_program->setUniformValue(
			"u_blobAlpha",
			QVector4D(s.blobAlpha[0], s.blobAlpha[1], s.blobAlpha[2], s.blobAlpha[3]));
		_program->setUniformValue(
			"u_blobScale",
			QVector4D(s.blobScale[0], s.blobScale[1], s.blobScale[2], s.blobScale[3]));
		_program->setUniformValue("u_presence", s.presence);
		_program->setUniformValue("u_pulse", s.pulse);
		_program->setUniformValue("u_shift", s.shift);
		_program->setUniformValue("u_bloom", s.bloom);
		_program->setUniformValue("u_angle", s.angle);
		_program->setUniformValue("u_time", s.time);
		_program->setUniformValue("u_grain", s.grain);
		_program->setUniformValue(
			"u_grainOffset",
			QVector2D(float(s.grainOffset.x()), float(s.grainOffset.y())));
		_program->setUniformValue("u_light", s.light);
		_program->setUniformValue("u_hasCover", s.hasCover ? 1.f : 0.f);
		_program->setUniformValue("u_live", s.live ? 1.f : 0.f);

		_buffer->bind();
		const auto position = _program->attributeLocation("position");
		f.glVertexAttribPointer(
			position,
			2,
			GL_FLOAT,
			GL_FALSE,
			2 * sizeof(GLfloat),
			nullptr);
		f.glEnableVertexAttribArray(position);
		f.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		f.glDisableVertexAttribArray(position);
	}

	void paintFallback(
			Painter &p,
			const QRegion &clip,
			Ui::GL::Backend backend) override {
		_paintCpu(p);
	}

private:
	Fn<BackdropState()> _state;
	Fn<void(QPainter&)> _paintCpu;
	std::optional<QOpenGLShaderProgram> _program;
	std::optional<QOpenGLBuffer> _buffer;

};


enum class Id {
	None,
	Collapse,
	More,
	Profile,
	Seek,
	Duration,
	Shuffle,
	Repeat,
	Previous,
	Play,
	Next,
	Speed,
	Lyrics,
	Queue,
	Cover,
	LyricsArea,
	QueueSearch,
	QueueSort,
	QueueRow,
	Count,
};
constexpr auto kIds = int(Id::Count);

enum class LyricsState {
	Loading,
	Ready,
	NotFound,
	Offline,
};

class Screen final : public Ui::RpWidget {
public:
	Screen(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller);

	void close();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Geometry {
		bool landscape = false;
		int contentLeft = 0;
		int contentTop = 0;
		int contentWidth = 0;
		int paneLeft = 0;
		int paneWidth = 0;
		int stageLeft = 0;
		int stageWidth = 0;
		int stageTop = 0;
		int stageBottom = 0;
		int headerBottom = 0;
		int toolbarTop = 0;
		int transportTop = 0;
		int timesTop = 0;
		int lineCenter = 0;
		int titleTop = 0;
		int textWidth = 0;
		float64 coverLeft = 0.;
		float64 coverTop = 0.;
		float64 coverSize = 0.;
		int compactCoverSize = 0;
	};

	void computeGeometry();
	void applyState(const Media::Player::TrackState &state);
	void loadTrack(const AudioMsgId &id);
	void setCover(const PlayerCover &cover);
	void refreshQueue();
	void setMode(int mode);
	void updateLyricIndex(bool animated);
	void updateControlTargets();
	void ensureFrames();
	bool frame(crl::time now);
	void pressed(Id id, QPoint point);
	void released(Id id, QPoint point, bool inside);
	void click(Id id);
	void longPress(Id id);
	void showMoreMenu();
	void showOrderMenu();
	void showRepeatMenu();
	void showSpeedMenu();
	void toggleSaved();
	void seekToProgress(float64 progress);
	void startRewind(int direction);
	void stopRewind();
	void applySeek(float64 progress);
	void layoutLyrics(int width);
	[[nodiscard]] Image *thumbnailFor(not_null<DocumentData*> document, FullMsgId id);
	[[nodiscard]] HistoryItem *currentItem() const;
	[[nodiscard]] bool saved() const;
	[[nodiscard]] Id hitTest(QPoint point) const;
	[[nodiscard]] QRectF rectOf(Id id) const;
	[[nodiscard]] QRectF queueRectOf(Id id) const;
	[[nodiscard]] QRectF controlRect(int index) const;
	[[nodiscard]] QRectF stageRect() const;
	[[nodiscard]] QRectF seekRect() const;
	[[nodiscard]] QRectF coverRect() const;
	[[nodiscard]] float64 seekProgressAt(float64 x) const;
	[[nodiscard]] int queueRowAt(QPoint point) const;
	[[nodiscard]] std::vector<int> filteredQueue() const;
	[[nodiscard]] int lyricLineAt(QPoint point) const;
	[[nodiscard]] PlayerPalette pal() const;
	[[nodiscard]] crl::time shownPosition() const;

	void createBackdrop();
	void stepRhythm();
	void paintBackdropCpu(QPainter &p);
	[[nodiscard]] BackdropState backdropState();
	void paintBackground(QPainter &p, const PlayerPalette &c);
	void paintRhythm(QPainter &p, const PlayerPalette &c);
	void paintLive(QPainter &p, const PlayerPalette &c);
	[[nodiscard]] bool rhythmActive() const;
	[[nodiscard]] bool liveActive() const;
	void paintHeader(QPainter &p, const PlayerPalette &c);
	void paintCover(QPainter &p, const PlayerPalette &c);
	void paintInfo(QPainter &p, const PlayerPalette &c);
	void paintSeek(QPainter &p, const PlayerPalette &c);
	void paintTransport(QPainter &p, const PlayerPalette &c);
	void paintToolbar(QPainter &p, const PlayerPalette &c);
	void paintLyrics(QPainter &p, const PlayerPalette &c, float64 alpha);
	void paintQueue(QPainter &p, const PlayerPalette &c, float64 alpha);
	void paintButtonShape(
		QPainter &p,
		Id id,
		QRectF bounds,
		QColor content,
		QColor container,
		QColor selectedContainer,
		bool alwaysContainer,
		float64 pressedRadius,
		float64 selectedRadius,
		int innerSide = 0,
		float64 innerRadius = 0.);

	const not_null<Window::SessionController*> _controller;
	Geometry _g;

	PlayerPalette _from;
	PlayerPalette _to;
	Ui::Animations::Simple _colorAnimation;
	Ui::Animations::Simple _showAnimation;
	Ui::Animations::Basic _frames;
	crl::time _lastFrame = 0;

	std::array<Spring, kIds> _press;
	std::array<Spring, kIds> _selectShape;
	std::array<Spring, kIds> _selectContainer;
	std::array<Spring, 3> _widths;
	std::array<Spring, 3> _radii;
	Spring _mode;
	Spring _lyricsAlpha;
	Spring _queueAlpha;
	Spring _pause;
	Spring _amplitude;
	Spring _handle;
	Spring _bubble;
	Spring _lyricScroll;
	Spring _queueScroll;
	Spring _drag;
	Spring _playIcon;
	float64 _wavePhase = 0.;
	std::unique_ptr<Lottie::Icon> _prevIcon;
	std::unique_ptr<Lottie::Icon> _nextIcon;
	base::flat_map<DocumentId, std::shared_ptr<Data::DocumentMedia>> _thumbs;
	crl::time _lastSeekApplied = 0;
	bool _wasPlayingOnRewind = false;
	std::vector<std::unique_ptr<QTextLayout>> _layouts;
	std::vector<float64> _layoutTop;
	std::vector<float64> _layoutHeight;
	int _layoutWidth = 0;
	QImage _background;
	QString _backgroundKey;
	QImage _rhythm;
	QImage _live;
	std::unique_ptr<Ui::RpWidgetWrap> _backdrop;
	QPixmap _coverScaled;
	int _coverScaledSize = 0;
	crl::time _positionAt = 0;
	crl::time _rhythmLast = 0;
	float64 _rhythmAngle = 0.;
	float64 _presence = 0.;
	float64 _energy = 0.;
	float64 _bass = 0.;
	float64 _mid = 0.;
	float64 _high = 0.;
	float64 _pulse = 0.;
	float64 _surge = 0.;
	float64 _bloom = 0.;
	float64 _colorShift = 0.;
	int _colorTarget = 0;
	float64 _bpm = 0.;
	float64 _intensity = 0.5;
	float64 _targetIntensity = 0.5;
	float64 _spin = 0.;
	Rhythm::State _rhythmState;
	float64 _layoutTotal = 0.;

	AudioMsgId _id;
	QString _title;
	QString _artist;
	QString _source;
	QPixmap _coverPix;
	bool _hasCover = false;
	bool _coverLoading = false;
	crl::time _position = 0;
	crl::time _length = 0;
	bool _playing = false;
	bool _loading = false;
	bool _showRemaining = false;
	int _modeValue = 0;

	bool _seeking = false;
	float64 _seekValue = 0.;
	int _rewindDirection = 0;
	int _rewindCount = 0;
	float64 _rewindProgress = 0.;
	crl::time _rewindStarted = 0;
	crl::time _rewindLast = 0;
	base::Timer _rewindTimer;
	base::Timer _holdTimer;
	bool _holding = false;

	std::unique_ptr<LyricsLoader> _lyricsLoader;
	std::vector<LyricLine> _lyrics;
	LyricsState _lyricsState = LyricsState::Loading;
	int _lyricIndex = -1;
	std::vector<float64> _lineFrom;
	std::vector<float64> _lineLevel;
	std::vector<crl::time> _lineChanged;
	crl::time _manualScrollUntil = 0;
	float64 _lyricsContentHeight = 0.;
	std::vector<QRectF> _lineRects;

	Media::Player::Instance::PlaylistView _queue;
	QString _search;
	bool _reversed = false;

	base::unique_qptr<Ui::PopupMenu> _menu;
	QPointer<SpeedPanel> _speedPanel;
	QPointer<QWidget> _previousFocus;
	Id _down = Id::None;
	QPoint _downPoint;
	float64 _dragStartX = 0.;
	bool _dragging = false;
	int _downRow = -1;

};

Screen::Screen(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _rewindTimer([=] {
	const auto now = crl::now();
	const auto dt = now - _rewindLast;
	_rewindLast = now;
	if (_rewindDirection == 0 || _length <= 0) {
		return;
	}
	const auto elapsed = now - _rewindStarted;
	_rewindCount = (elapsed > 4000) ? 3 : (elapsed > 2000) ? 2 : 1;
	const auto multiplier = (_rewindDirection < 0)
		? ((_rewindCount <= 1) ? 3. : (_rewindCount == 2) ? 6. : 12.)
		: ((_rewindCount <= 1) ? 4. : (_rewindCount == 2) ? 7. : 13.);
	_rewindProgress = Clamp01(_rewindProgress
		+ _rewindDirection * (dt * multiplier) / float64(_length));
	_seekValue = _rewindProgress;
	if (now - _lastSeekApplied > 250) {
		applySeek(_rewindProgress);
	}
	update();
})
, _holdTimer([=] {
	if (_down == Id::Previous || _down == Id::Next) {
		_holding = true;
		startRewind((_down == Id::Previous) ? -1 : 1);
	} else if (_down == Id::Shuffle
		|| _down == Id::Repeat
		|| _down == Id::Speed) {
		_holding = true;
		longPress(_down);
	}
}) {
	for (auto i = 0; i != kIds; ++i) {
		_press[i].setup(0.6, 800., 0.002);
		_selectShape[i].setup(0.6, 800., 0.002);
		_selectContainer[i].setup(1., 1600., 0.002);
	}
	for (auto i = 0; i != 3; ++i) {
		_widths[i].setup(0.6, 800., 0.1);
		_radii[i].setup(0.6, 800., 0.1);
	}
	_mode.setup(0.8, 380., 0.002);
	_lyricsAlpha.setup(1., 1600., 0.002);
	_queueAlpha.setup(1., 1600., 0.002);
	_pause.setup(0.8, 380., 0.0005);
	_pause.jump(1.);
	_amplitude.setup(1., 260., 0.01);
	_handle.setup(0.6, 800., 0.01);
	_handle.jump(D(4.));
	_bubble.setup(0.6, 800., 0.01);
	_lyricScroll.setup(1., 70., 0.3);
	_queueScroll.setup(1., 600., 0.5);
	_drag.setup(0.8, 380., 0.5);
	_playIcon.setup(1., 700., 0.002);
	const auto skipIcon = [&] {
		return std::make_unique<Lottie::Icon>(Lottie::IconDescriptor{
			.path = u":/gui/art/extera/player/player_prev.json"_q,
			.color = &st::windowFg,
			.sizeOverride = QSize(S(20), S(20)),
			.colorizeUsingAlpha = true,
		});
	};
	_prevIcon = skipIcon();
	_nextIcon = skipIcon();
	_controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (_modeValue == 2) {
			update();
		}
	}, lifetime());

	_from = _to = MakePalette(DefaultPlayerSeed(), PlayerLightTheme());
	rpl::merge(
		Extera::Config::Instance().boolChanges(u"player_live_gradient"_q) | rpl::to_empty,
		Extera::Config::Instance().boolChanges(u"player_rhythm"_q) | rpl::to_empty
	) | rpl::on_next([=] {
		ensureFrames();
		update();
	}, lifetime());

	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);

	_frames.init([=](crl::time now) { return frame(now); });

	_lyricsLoader = std::make_unique<LyricsLoader>([=](
			std::vector<LyricLine> lines) {
		_lyrics = std::move(lines);
		_layouts.clear();
		_layoutWidth = 0;
		_lyricsState = _lyrics.empty()
			? LyricsState::NotFound
			: LyricsState::Ready;
		_lineFrom.assign(_lyrics.size(), 0.);
		_lineChanged.assign(_lyrics.size(), 0);
		_lyricIndex = -1;
		updateLyricIndex(false);
		ensureFrames();
		update();
	});

	parent->sizeValue(
	) | rpl::on_next([=](QSize size) {
		setGeometry(QRect(QPoint(), size));
	}, lifetime());

	const auto instance = Media::Player::instance();
	instance->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &state) {
		if (state.id.type() == AudioMsgId::Type::Song && state.id.audio()) {
			applyState(state);
		}
	}, lifetime());
	instance->trackChanged(
	) | rpl::on_next([=](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Song) {
			applyState(Media::Player::instance()->getState(type));
		}
	}, lifetime());
	instance->stops(
		AudioMsgId::Type::Song
	) | rpl::on_next([=] {
		if (!Media::Player::instance()->current(AudioMsgId::Type::Song)) {
			close();
		}
	}, lifetime());
	instance->playlistChanges(
		AudioMsgId::Type::Song
	) | rpl::on_next([=] {
		refreshQueue();
		update();
	}, lifetime());
	rpl::merge(
		Core::App().settings().playerOrderModeValue() | rpl::to_empty,
		Core::App().settings().playerRepeatModeValue() | rpl::to_empty
	) | rpl::on_next([=] {
		const auto &settings = Core::App().settings();
		_selectShape[int(Id::Shuffle)].to(
			(settings.playerOrderMode() != Media::OrderMode::Default)
				? 1. : 0.);
		_selectContainer[int(Id::Shuffle)].to(
			_selectShape[int(Id::Shuffle)].target);
		_selectShape[int(Id::Repeat)].to(
			(settings.playerRepeatMode() != Media::RepeatMode::None)
				? 1. : 0.);
		_selectContainer[int(Id::Repeat)].to(
			_selectShape[int(Id::Repeat)].target);
		ensureFrames();
		update();
	}, lifetime());

	applyState(instance->getState(AudioMsgId::Type::Song));
	for (auto &spring : _selectShape) {
		spring.jump(spring.target);
	}
	for (auto &spring : _selectContainer) {
		spring.jump(spring.target);
	}
	updateControlTargets();
	for (auto i = 0; i != 3; ++i) {
		_widths[i].jump(_widths[i].target);
		_radii[i].jump(_radii[i].target);
	}
	_amplitude.jump(_amplitude.target);
	_pause.jump(_pause.target);
	_playIcon.jump(_playIcon.target);

	if (const auto saved = Extera::Int(u"player_mode"_q); saved == 1 || saved == 2) {
		setMode(saved);
		_mode.jump(_mode.target);
		_lyricsAlpha.jump(_lyricsAlpha.target);
		_queueAlpha.jump(_queueAlpha.target);
		for (const auto id : { Id::Lyrics, Id::Queue }) {
			_selectShape[int(id)].jump(_selectShape[int(id)].target);
			_selectContainer[int(id)].jump(_selectContainer[int(id)].target);
		}
	}
	_showAnimation.start([=] { update(); }, 0., 1., kShowDuration, anim::easeOutCubic);
	_previousFocus = QApplication::focusWidget();
	createBackdrop();
	show();
	raise();
	setFocus();
}

void Screen::close() {
	stopRewind();
	// Without this the focus chain hands the focus to the chats search
	// field, which then opens the search.
	const auto previous = _previousFocus;
	if (_backdrop) {
		_backdrop->rpWidget()->hide();
	}
	hide();
	if (previous && previous->isVisible()) {
		previous->setFocus();
	} else if (const auto content = _controller->content().get()) {
		content->setFocus();
	}
	deleteLater();
}

void Screen::resizeEvent(QResizeEvent *e) {
	computeGeometry();
	if (_backdrop) {
		_backdrop->rpWidget()->setGeometry(geometry());
	}
}

void Screen::createBackdrop() {
	auto backend = Ui::GL::Backend::Raster;
	if (Extera::Bool(u"player_gpu"_q)) {
		backend = Ui::GL::ChooseBackendDefault(
			Ui::GL::CheckCapabilities(nullptr));
		if (backend == Ui::GL::Backend::QRhi) {
			backend = Ui::GL::Backend::Raster;
		}
	}
	_backdrop = Ui::GL::CreateSurface(parentWidget(), Ui::GL::ChosenRenderer{
		.renderer = std::make_unique<BackdropRenderer>(
			[=] { return backdropState(); },
			[=](QPainter &p) { paintBackdropCpu(p); }),
		.backend = backend,
	});
	const auto widget = _backdrop->rpWidget();
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	widget->setGeometry(geometry());
	widget->stackUnder(this);
	widget->show();
}

BackdropState Screen::backdropState() {
	stepRhythm();
	const auto c = pal();
	auto result = BackdropState();
	result.background = c.background;
	result.accent = c.accent;
	result.mixed = Mix(c.accent, c.tonal, 0.5);
	result.tonal = c.tonal;
	result.glow = c.glow;
	result.cover = QPointF(
		_g.coverLeft + _g.coverSize / 2.,
		_g.coverTop + _g.coverSize / 2.);
	const auto light = PlayerLightTheme();
	constexpr auto kAlphaDark = std::array<float64, 4>{ 0.31, 0.26, 0.29, 0.23 };
	constexpr auto kAlphaLight = std::array<float64, 4>{ 0.22, 0.19, 0.2, 0.16 };
	const auto f7 = _intensity * 0.45 + 0.7;
	const auto bands = std::array<float64, 4>{ _bass, _mid, _bass, _high };
	for (auto i = 0; i != 4; ++i) {
		result.blobScale[i] = float(1. + _pulse * 0.05 + bands[i] * 0.1);
		result.blobAlpha[i] = float(std::min(
			0.95,
			((light ? kAlphaLight[i] : kAlphaDark[i])
				+ _pulse * 0.08
				+ _energy * 0.12) * f7) * _presence);
	}
	result.shift = float(_colorShift);
	result.bloom = float(_bloom);
	result.presence = float(_presence);
	result.pulse = float(_pulse);
	result.angle = float(_rhythmAngle);
	result.time = float(std::fmod(crl::now() / 1000., 100000.));
	result.grain = 0.f;
	result.light = light ? 1.f : 0.f;
	result.hasCover = _hasCover;
	result.live = liveActive();
	return result;
}

void Screen::computeGeometry() {
	auto &g = _g;
	const auto w = width();
	const auto h = height();
	g.landscape = (w > h);
	g.contentLeft = 0;
	g.contentTop = S(8);
	g.contentWidth = w;
	const auto controlsHeight = S(72);
	if (!g.landscape) {
		g.stageLeft = g.paneLeft = 0;
		g.stageWidth = g.paneWidth = w;
		g.headerBottom = g.contentTop + S(56);
		g.stageTop = g.headerBottom + S(8);
		const auto coverMax = w - S(20) * 2;
		const auto coverTopMin = g.headerBottom + S(16);
		const auto below = S(159) + controlsHeight;
		const auto bottom = h - S(64);
		g.toolbarTop = int(std::round(bottom - std::min(
			std::max(0, ((bottom - coverTopMin) - below) - coverMax) * 0.25,
			float64(S(12)))));
		g.coverSize = std::max(
			0,
			std::min(coverMax, (g.toolbarTop - below) - coverTopMin));
		g.coverLeft = (w - g.coverSize) / 2.;
		const auto extra = std::max(
			0,
			(g.toolbarTop - int(std::round(coverTopMin + g.coverSize)))
				- below);
		const auto small = std::min(extra, S(12));
		g.transportTop = ((g.toolbarTop - S(8))
			- std::min(extra - small, S(32)))
			- controlsHeight;
		g.timesTop = ((g.transportTop - S(12)) - small) - S(18);
		g.lineCenter = g.timesTop - S(14);
		g.titleTop = g.lineCenter - S(83);
		g.coverTop = g.headerBottom
			+ ((g.titleTop - g.headerBottom) - g.coverSize) / 2.;
		g.stageBottom = g.titleTop - S(16);
	} else {
		const auto half = int(std::round(w * 0.5));
		g.paneWidth = std::min(w - half, S(460));
		g.paneLeft = half + ((w - half) - g.paneWidth) / 2;
		g.stageWidth = std::min(half, S(720));
		g.stageLeft = (half - g.stageWidth) / 2 + S(16);
		g.headerBottom = g.contentTop + S(56);
		g.stageTop = g.headerBottom + S(8);
		const auto bottom = h - S(8);
		g.toolbarTop = bottom - S(56);
		g.transportTop = (g.toolbarTop - S(8)) - controlsHeight;
		g.timesTop = (g.transportTop - S(12)) - S(18);
		g.lineCenter = g.timesTop - S(14);
		g.titleTop = g.lineCenter - S(83);
		const auto lift = std::max(0, (g.titleTop - g.headerBottom) / 2);
		g.titleTop -= lift;
		g.lineCenter -= lift;
		g.timesTop -= lift;
		g.transportTop -= lift;
		g.toolbarTop -= lift;
		const auto room = bottom - g.headerBottom;
		g.coverSize = std::max(
			0,
			std::min({ g.stageWidth - S(20) * 2, room - S(16), S(560) }));
		g.coverLeft = g.stageLeft + (g.stageWidth - g.coverSize) / 2.;
		g.coverTop = g.headerBottom + (room - g.coverSize) / 2.;
		g.stageBottom = h - S(16);
	}
	g.compactCoverSize = S(50);
	g.textWidth = g.paneWidth - S(20) * 2 - S(48);
	update();
}

PlayerPalette Screen::pal() const {
	return MixPalette(_from, _to, _colorAnimation.value(1.));
}

HistoryItem *Screen::currentItem() const {
	const auto document = _id.audio();
	return document ? document->owner().message(_id.contextId()) : nullptr;
}

bool Screen::saved() const {
	const auto document = _id.audio();
	return document && document->owner().savedMusic().has(document);
}

void Screen::applyState(const Media::Player::TrackState &state) {
	if (!state.id.audio()) {
		return;
	}
	if (state.id.audio() != _id.audio()
		|| state.id.contextId() != _id.contextId()) {
		loadTrack(state.id);
	}
	const auto frequency = state.frequency ? state.frequency : 1000;
	_position = (state.position * 1000LL) / frequency;
	_positionAt = crl::now();
	_length = state.length
		? (state.length * 1000LL) / frequency
		: state.id.audio()->duration();
	_loading = state.id.audio()->loading();
	const auto playing = Media::Player::ShowPauseIcon(state.state);
	if (_playing != playing && _rewindDirection == 0) {
		_playing = playing;
		updateControlTargets();
	}
	_playIcon.to(_playing ? 1. : 0.);
	_pause.to(_playing ? 1. : 0.94);
	_amplitude.to(_playing ? D(3.) : 0.);
	const auto isSaved = saved();
	_selectShape[int(Id::Profile)].to(isSaved ? 1. : 0.);
	_selectContainer[int(Id::Profile)].to(isSaved ? 1. : 0.);
	const auto speed = Core::App().settings().audioPlaybackSpeed(false);
	const auto speedOn = std::abs(speed - 1.) > 0.01;
	_selectShape[int(Id::Speed)].to(speedOn ? 1. : 0.);
	_selectContainer[int(Id::Speed)].to(speedOn ? 1. : 0.);
	updateLyricIndex(true);
	ensureFrames();
	update();
}

void Screen::loadTrack(const AudioMsgId &id) {
	_id = id;
	const auto document = id.audio();
	const auto song = document->song();
	_artist = song ? song->performer : QString();
	_title = (song && !song->title.isEmpty())
		? song->title
		: document->filename();
	_source = QString();
	if (const auto item = currentItem()) {
		_source = item->history()->peer->name();
	}
	_lyrics.clear();
	_layouts.clear();
	_layoutWidth = 0;
	_lineFrom.clear();
	_lineChanged.clear();
	_lineLevel.clear();
	_lyricIndex = -1;
	_lyricScroll.jump(0.);
	if (!song) {
		_lyricsLoader->cancel();
		_lyricsState = LyricsState::NotFound;
	} else if (!Extera::Bool(u"player_lyrics"_q)) {
		_lyricsLoader->cancel();
		_lyricsState = LyricsState::Offline;
	} else {
		_lyricsState = LyricsState::Loading;
		_lyricsLoader->request({
			.artist = song->performer,
			.title = song->title,
			.durationSec = int(document->duration() / 1000),
		});
	}
	_coverLoading = true;
	LoadPlayerCover(
		document,
		id.contextId(),
		1000,
		crl::guard(this, [=](const PlayerCover &cover) {
			if (_id.audio() == document) {
				setCover(cover);
			}
		}));
	refreshQueue();
	update();
}

void Screen::setCover(const PlayerCover &cover) {
	_coverLoading = false;
	_hasCover = cover.has;
	_coverPix = cover.has ? QPixmap::fromImage(cover.image) : QPixmap();
	_coverScaled = QPixmap();
	_coverScaledSize = 0;
	const auto light = PlayerLightTheme();
	const auto next = (cover.has && Extera::Bool(u"player_cover_color"_q))
		? MakePalette(cover.seed, light)
		: MakePalette(DefaultPlayerSeed(), light);
	_from = pal();
	_to = next;
	_colorAnimation.start([=] { update(); }, 0., 1., kColorDuration);
	update();
}

void Screen::refreshQueue() {
	_queue = Media::Player::instance()->playlistView(AudioMsgId::Type::Song);
}

std::vector<int> Screen::filteredQueue() const {
	auto result = std::vector<int>();
	const auto owner = _id.audio() ? &_id.audio()->owner() : nullptr;
	const auto needle = _search.trimmed().toLower();
	const auto count = int(_queue.items.size());
	for (auto k = 0; k != count; ++k) {
		const auto i = _reversed ? (count - 1 - k) : k;
		if (!needle.isEmpty()) {
			auto text = QString();
			if (owner) {
				if (const auto item = owner->message(_queue.items[i])) {
					if (const auto media = item->media()) {
						if (const auto document = media->document()) {
							if (const auto song = document->song()) {
								text = song->title + ' ' + song->performer;
							} else {
								text = document->filename();
							}
						}
					}
				}
			}
			if (!text.toLower().contains(needle)) {
				continue;
			}
		}
		result.push_back(i);
	}
	return result;
}

void Screen::setMode(int mode) {
	if (_modeValue == mode) {
		mode = 0;
	}
	if (_modeValue == 2 && mode != 2) {
		_search = QString();
	}
	_modeValue = mode;
	Extera::Config::Instance().setInt(u"player_mode"_q, mode);
	_mode.to(mode ? 1. : 0.);
	_lyricsAlpha.to(mode == 1 ? 1. : 0.);
	_queueAlpha.to(mode == 2 ? 1. : 0.);
	_selectShape[int(Id::Lyrics)].to(mode == 1 ? 1. : 0.);
	_selectContainer[int(Id::Lyrics)].to(mode == 1 ? 1. : 0.);
	_selectShape[int(Id::Queue)].to(mode == 2 ? 1. : 0.);
	_selectContainer[int(Id::Queue)].to(mode == 2 ? 1. : 0.);
	if (mode == 1) {
		_manualScrollUntil = 0;
		updateLyricIndex(false);
	} else if (mode == 2) {
		refreshQueue();
		const auto rows = filteredQueue();
		auto position = 0;
		for (auto i = 0; i != int(rows.size()); ++i) {
			if (rows[i] == _queue.index) {
				position = i;
			}
		}
		_queueScroll.jump(std::max(0., float64(position - 2) * S(56)));
	}
	ensureFrames();
	update();
}

void Screen::updateLyricIndex(bool animated) {
	if (_lyrics.empty()) {
		_lyricIndex = -1;
		return;
	}
	const auto i = std::upper_bound(
		_lyrics.begin(),
		_lyrics.end(),
		shownPosition(),
		[](crl::time value, const LyricLine &line) {
			return value < line.at;
		});
	const auto index = int(i - _lyrics.begin()) - 1;
	if (index == _lyricIndex) {
		return;
	}
	const auto now = crl::now();
	const auto mark = [&](int line) {
		if (line >= 0 && line < int(_lineChanged.size())) {
			_lineChanged[line] = animated ? now : 0;
		}
	};
	mark(_lyricIndex);
	mark(index);
	_lyricIndex = index;
	ensureFrames();
}

void Screen::updateControlTargets() {
	const auto skip = D(56.);
	const auto playWidth = _playing ? D(96.) : D(72.);
	auto prev = skip;
	auto play = playWidth;
	auto next = skip;
	const auto down = (_down == Id::Previous)
		? 0
		: (_down == Id::Play)
		? 1
		: (_down == Id::Next)
		? 2
		: -1;
	if (down == 1) {
		play = 1.15 * playWidth;
		prev = next = skip - (playWidth * 0.15) / 2.;
	} else if (down == 0) {
		play = playWidth - 0.15 * skip;
		prev = 1.15 * skip;
	} else if (down == 2) {
		play = playWidth - 0.15 * skip;
		next = 1.15 * skip;
	}
	const auto prevRadius = (down == 0) ? D(12.) : skip / 2.;
	const auto nextRadius = (down == 2) ? D(12.) : skip / 2.;
	const auto playRadius = (down == 1)
		? D(16.)
		: _playing
		? D(24.)
		: D(72.) / 2.;
	_widths[0].to(prev);
	_widths[1].to(play);
	_widths[2].to(next);
	_radii[0].to(prevRadius);
	_radii[1].to(playRadius);
	_radii[2].to(nextRadius);
	ensureFrames();
}

void Screen::ensureFrames() {
	if (!_frames.animating()) {
		_lastFrame = crl::now();
		_frames.start();
	}
}

bool Screen::frame(crl::time now) {
	const auto dt = std::clamp((now - _lastFrame) / 1000., 0., 0.05);
	_lastFrame = now;
	auto running = false;
	const auto step = [&](Spring &spring) {
		spring.step(dt);
		running |= spring.running;
	};
	for (auto &spring : _press) {
		step(spring);
	}
	for (auto &spring : _selectShape) {
		step(spring);
	}
	for (auto &spring : _selectContainer) {
		step(spring);
	}
	for (auto &spring : _widths) {
		step(spring);
	}
	for (auto &spring : _radii) {
		step(spring);
	}
	step(_mode);
	step(_lyricsAlpha);
	step(_queueAlpha);
	step(_pause);
	step(_amplitude);
	step(_handle);
	step(_bubble);
	step(_drag);
	step(_playIcon);
	step(_queueScroll);
	if (_modeValue == 1 || _lyricsAlpha.value > 0.) {
		step(_lyricScroll);
	}
	const auto speed = Core::App().settings().audioPlaybackSpeed(false);
	if (_playing && !_lyrics.empty()) {
		updateLyricIndex(true);
	}
	auto levelsMoving = false;
	if (!_lyrics.empty() && _lyricsAlpha.value > 0.001) {
		if (_lineLevel.size() != _lyrics.size()) {
			_lineLevel.assign(_lyrics.size(), 0.28);
		}
		for (auto i = 0; i != int(_lineLevel.size()); ++i) {
			const auto target = (i == _lyricIndex)
				? 1.
				: (_lyricIndex < 0 && i == 0)
				? 0.62
				: 0.28;
			const auto next = _lineLevel[i]
				+ (target - _lineLevel[i]) * (1. - std::exp(-dt / 0.16));
			if (std::abs(target - next) < 0.003) {
				_lineLevel[i] = target;
			} else {
				_lineLevel[i] = next;
				levelsMoving = true;
			}
		}
	}
	const auto waving = _playing && _amplitude.value > 0.01;
	if (waving) {
		_wavePhase += dt * 2. * M_PI * 0.9 * std::max(0.1, speed);
	}
	auto lines = false;
	for (const auto changed : _lineChanged) {
		if (changed && now - changed < kLineFadeDuration) {
			lines = true;
			break;
		}
	}
	if (levelsMoving) {
		lines = true;
	}
	const auto spinner = !_hasCover && _coverLoading;
	const auto rhythm = rhythmActive() || (_presence > 0.003) || liveActive();
	if (rhythm && _backdrop) {
		_backdrop->rpWidget()->update();
	}
	if (running || lines || spinner) {
		update();
	} else if (waving || _seeking || _rewindDirection) {
		update(seekRect().adjusted(-S(8), -S(48), S(8), S(8)).toRect());
		if (_lyricsAlpha.value > 0.01 && _lyricIndex >= 0) {
			update(stageRect().toRect());
		}
	}
	return running || waving || lines || spinner || rhythm || _seeking || _rewindDirection;
}

crl::time Screen::shownPosition() const {
	if (!_playing || _loading || _seeking || _rewindDirection) {
		return _position;
	}
	const auto speed = Core::App().settings().audioPlaybackSpeed(false);
	const auto passed = std::min(
		crl::time((crl::now() - _positionAt) * speed),
		crl::time(600));
	const auto result = _position + std::max(passed, crl::time(0));
	return (_length > 0) ? std::min(result, _length) : result;
}

QRectF Screen::stageRect() const {
	return QRectF(
		_g.stageLeft,
		_g.stageTop,
		_g.stageWidth,
		std::max(S(64), _g.stageBottom - _g.stageTop));
}

QRectF Screen::seekRect() const {
	const auto left = _g.paneLeft + S(20);
	const auto width = _g.paneWidth - S(40);
	return QRectF(left, _g.lineCenter - S(52), width, S(68));
}

float64 Screen::seekProgressAt(float64 x) const {
	const auto rect = seekRect();
	const auto from = rect.left() + D(2.);
	const auto to = rect.right() - D(2.);
	return Clamp01((x - from) / std::max(1., to - from));
}

QRectF Screen::coverRect() const {
	const auto k = Clamp01(_mode.value);
	const auto left = Lerp(_g.coverLeft, _g.paneLeft + S(20), k);
	const auto top = Lerp(_g.coverTop, _g.titleTop, k);
	const auto size = Lerp(_g.coverSize, _g.compactCoverSize, k);
	return QRectF(left, top, size, size);
}

QRectF Screen::controlRect(int index) const {
	const auto left = _g.paneLeft + S(20) + S(40) + S(8);
	const auto width = _g.paneWidth - S(68) * 2;
	const auto centerX = left + width / 2.;
	const auto centerY = _g.transportTop + S(72) / 2.;
	const auto gap = D(8.);
	const auto total = _widths[0].value + _widths[1].value + _widths[2].value
		+ gap * 2.;
	auto x = centerX - total / 2.;
	const auto skip = D(56.);
	const auto play = D(72.);
	if (index == 0) {
		return QRectF(x, centerY - skip / 2., _widths[0].value, skip);
	}
	x += _widths[0].value + gap;
	if (index == 1) {
		return QRectF(x, centerY - play / 2., _widths[1].value, play);
	}
	x += _widths[1].value + gap;
	return QRectF(x, centerY - skip / 2., _widths[2].value, skip);
}

QRectF Screen::queueRectOf(Id id) const {
	const auto stage = stageRect();
	switch (id) {
	case Id::QueueSearch:
		return QRectF(
			stage.left() + S(20),
			stage.top(),
			stage.width() - S(40) - S(48),
			S(40));
	case Id::QueueSort:
		return QRectF(stage.right() - S(20) - S(40), stage.top(), S(40), S(40));
	case Id::QueueRow:
		return QRectF(
			stage.left(),
			stage.top() + S(52),
			stage.width(),
			stage.height() - S(52));
	default:
		return QRectF();
	}
}

QRectF Screen::rectOf(Id id) const {
	const auto button = S(40);
	const auto headerY = _g.contentTop + (S(56) - button) / 2;
	const auto paneLeft = _g.paneLeft + S(20);
	const auto paneRight = _g.paneLeft + _g.paneWidth - S(20);
	const auto transportY = _g.transportTop + (S(72) - button) / 2;
	const auto modeTop = _g.toolbarTop + (S(56) - S(48)) / 2;
	switch (id) {
	case Id::Collapse:
		return QRectF(_g.contentLeft + S(20), headerY, button, button);
	case Id::More:
		return QRectF(
			_g.contentLeft + _g.contentWidth - S(20) - button,
			headerY,
			button,
			button);
	case Id::Profile:
		return QRectF(
			paneRight - button,
			_g.titleTop + (S(50) - button) / 2,
			button,
			button);
	case Id::Seek: return seekRect();
	case Id::Duration:
		return QRectF(paneRight - S(120), _g.timesTop, S(120), S(18));
	case Id::Shuffle: return QRectF(paneLeft, transportY, button, button);
	case Id::Repeat:
		return QRectF(paneRight - button, transportY, button, button);
	case Id::Previous: return controlRect(0);
	case Id::Play: return controlRect(1);
	case Id::Next: return controlRect(2);
	case Id::Speed: return QRectF(paneLeft, modeTop, S(64), S(48));
	case Id::Queue: {
		const auto width = S(64) + D(8.) * std::max(0., _press[int(Id::Queue)].value);
		return QRectF(paneRight - width, modeTop, width, S(48));
	}
	case Id::Lyrics: {
		const auto queue = rectOf(Id::Queue);
		const auto width = S(64) + D(8.) * std::max(0., _press[int(Id::Lyrics)].value);
		return QRectF(queue.left() - D(2.) - width, modeTop, width, S(48));
	}
	case Id::Cover: return (_modeValue == 0) ? coverRect() : QRectF();
	case Id::LyricsArea: return (_modeValue == 1) ? stageRect() : QRectF();
	case Id::QueueSearch:
	case Id::QueueSort:
	case Id::QueueRow:
		return (_modeValue == 2) ? queueRectOf(id) : QRectF();
	case Id::None:
	case Id::Count:
		break;
	}
	return QRectF();
}

Id Screen::hitTest(QPoint point) const {
	for (const auto id : {
		Id::Collapse,
		Id::More,
		Id::Profile,
		Id::Duration,
		Id::Shuffle,
		Id::Repeat,
		Id::Previous,
		Id::Play,
		Id::Next,
		Id::Speed,
		Id::Lyrics,
		Id::Queue,
		Id::QueueSearch,
		Id::QueueSort,
		Id::QueueRow,
		Id::LyricsArea,
		Id::Cover,
	}) {
		if (rectOf(id).contains(point)) {
			return id;
		}
	}
	const auto seek = seekRect();
	if (seek.adjusted(0, S(24), 0, 0).contains(point)) {
		return Id::Seek;
	}
	return Id::None;
}

int Screen::queueRowAt(QPoint point) const {
	const auto area = rectOf(Id::QueueRow);
	if (!area.contains(point)) {
		return -1;
	}
	const auto rows = filteredQueue();
	const auto index = int((point.y() - area.top() + _queueScroll.value)
		/ S(56));
	return (index >= 0 && index < int(rows.size())) ? rows[index] : -1;
}

int Screen::lyricLineAt(QPoint point) const {
	for (auto i = 0; i != int(_lineRects.size()); ++i) {
		if (_lineRects[i].contains(point)) {
			return i;
		}
	}
	return -1;
}

void Screen::mousePressEvent(QMouseEvent *e) {
	const auto id = hitTest(e->pos());
	_down = id;
	_downPoint = e->pos();
	_holding = false;
	if (e->button() == Qt::RightButton) {
		_down = Id::None;
		if (id == Id::Shuffle || id == Id::Repeat || id == Id::Speed) {
			longPress(id);
		}
		return;
	}
	pressed(id, e->pos());
}

void Screen::pressed(Id id, QPoint point) {
	if (id == Id::None) {
		return;
	}
	_press[int(id)].to(1.);
	if (id == Id::Previous || id == Id::Play || id == Id::Next) {
		updateControlTargets();
	}
	if (id == Id::Previous
		|| id == Id::Next
		|| id == Id::Shuffle
		|| id == Id::Repeat
		|| id == Id::Speed) {
		_holdTimer.callOnce(kHoldDelay);
	}
	if (id == Id::Seek) {
		_seeking = true;
		_seekValue = seekProgressAt(point.x());
		_handle.to(D(2.));
		_bubble.to(1.);
	} else if (id == Id::Cover) {
		_dragging = false;
		_dragStartX = point.x() - _drag.value;
	} else if (id == Id::QueueRow) {
		_downRow = queueRowAt(point);
	}
	ensureFrames();
	update();
}

void Screen::mouseMoveEvent(QMouseEvent *e) {
	const auto point = e->pos();
	if (_down == Id::Seek && _seeking) {
		_seekValue = seekProgressAt(point.x());
		update();
		return;
	} else if (_down == Id::Cover) {
		const auto delta = point.x() - _dragStartX;
		if (!_dragging && std::abs(point.x() - _downPoint.x()) > S(8)) {
			_dragging = true;
		}
		if (_dragging) {
			const auto limit = D(220.);
			const auto rubber = [&](float64 value) {
				return std::copysign(
					limit * (1. - 1. / ((std::abs(value) / limit) * 0.55 + 1.)),
					value);
			};
			const auto canDrag = _queue.items.size() > 1;
			_drag.jump(canDrag ? delta : rubber(delta));
			update();
		}
		return;
	}
	const auto id = hitTest(point);
	const auto pointer = (id != Id::None
		&& id != Id::Cover
		&& id != Id::LyricsArea)
		|| (id == Id::LyricsArea && lyricLineAt(point) >= 0);
	setCursor(pointer ? style::cur_pointer : style::cur_default);
}

void Screen::mouseReleaseEvent(QMouseEvent *e) {
	const auto id = std::exchange(_down, Id::None);
	_holdTimer.cancel();
	const auto inside = (id != Id::None) && rectOf(id).contains(e->pos());
	released(id, e->pos(), inside);
}

void Screen::released(Id id, QPoint point, bool inside) {
	if (id == Id::None) {
		return;
	}
	_press[int(id)].to(0.);
	if (id == Id::Previous || id == Id::Play || id == Id::Next) {
		updateControlTargets();
	}
	const auto wasHolding = std::exchange(_holding, false);
	if (id == Id::Seek) {
		if (_seeking) {
			_seeking = false;
			_handle.to(D(4.));
			_bubble.to(0.);
			applySeek(seekProgressAt(point.x()));
		}
	} else if (id == Id::Cover && _dragging) {
		_dragging = false;
		const auto page = std::max(1., coverRect().width());
		if (std::abs(_drag.value) > page * 0.3 && _queue.items.size() > 1) {
			const auto type = AudioMsgId::Type::Song;
			if (_drag.value < 0) {
				Media::Player::instance()->next(type);
			} else {
				Media::Player::instance()->previous(type);
			}
		}
		_drag.to(0.);
	} else if (wasHolding) {
		if (id == Id::Previous || id == Id::Next) {
			stopRewind();
		}
	} else if (inside) {
		click(id);
	}
	if (id == Id::QueueRow && inside) {
		const auto row = queueRowAt(point);
		if (row >= 0 && row == _downRow) {
			const auto fullId = _queue.items[row];
			if (const auto document = _id.audio()) {
				if (const auto item = document->owner().message(fullId)) {
					if (const auto media = item->media()) {
						if (const auto target = media->document()) {
							Media::Player::instance()->play(
								AudioMsgId(target, fullId));
						}
					}
				}
			}
		}
	}
	if (id == Id::LyricsArea && inside) {
		const auto line = lyricLineAt(point);
		if (line >= 0 && _length > 0) {
			seekToProgress(float64(_lyrics[line].at) / _length);
			if (!_playing) {
				Media::Player::instance()->playPause(AudioMsgId::Type::Song);
			}
		}
	}
	ensureFrames();
	update();
}

void Screen::seekToProgress(float64 progress) {
	applySeek(progress);
}

// finishSeeking restarts the stream at the position and plays it; a track
// that was paused is paused again so seeking never changes the play state.
void Screen::applySeek(float64 progress) {
	const auto instance = Media::Player::instance();
	const auto wasPlaying = _playing;
	instance->finishSeeking(AudioMsgId::Type::Song, Clamp01(progress));
	if (!wasPlaying) {
		instance->pause(AudioMsgId::Type::Song);
	}
	if (_length > 0) {
		_position = crl::time(Clamp01(progress) * _length);
		updateLyricIndex(false);
	}
	_lastSeekApplied = crl::now();
	update();
}

void Screen::startRewind(int direction) {
	if (_length <= 0) {
		return;
	}
	_rewindDirection = direction;
	_rewindStarted = _rewindLast = crl::now();
	_rewindProgress = Clamp01(float64(_position) / _length);
	_seekValue = _rewindProgress;
	_lastSeekApplied = 0;
	_rewindTimer.callEach(kRewindTick);
	ensureFrames();
}

void Screen::stopRewind() {
	if (!_rewindDirection) {
		return;
	}
	_rewindDirection = 0;
	_rewindTimer.cancel();
	applySeek(_rewindProgress);
}

void Screen::click(Id id) {
	const auto instance = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	auto &settings = Core::App().settings();
	switch (id) {
	case Id::Collapse: close(); break;
	case Id::More: showMoreMenu(); break;
	case Id::Profile: toggleSaved(); break;
	case Id::Duration: _showRemaining = !_showRemaining; break;
	case Id::Previous:
		_prevIcon->animate([=] { update(); }, 0, _prevIcon->framesCount() - 1);
		instance->previous(type);
		break;
	case Id::Next:
		_nextIcon->animate([=] { update(); }, 0, _nextIcon->framesCount() - 1);
		instance->next(type);
		break;
	case Id::Play: instance->playPause(type); break;
	case Id::Shuffle:
		settings.setPlayerOrderMode(
			(settings.playerOrderMode() == Media::OrderMode::Shuffle)
				? Media::OrderMode::Default
				: Media::OrderMode::Shuffle);
		Core::App().saveSettingsDelayed();
		break;
	case Id::Repeat:
		settings.setPlayerRepeatMode([&] {
			switch (settings.playerRepeatMode()) {
			case Media::RepeatMode::None: return Media::RepeatMode::All;
			case Media::RepeatMode::All: return Media::RepeatMode::One;
			case Media::RepeatMode::One: return Media::RepeatMode::None;
			}
			return Media::RepeatMode::None;
		}());
		Core::App().saveSettingsDelayed();
		break;
	case Id::Speed: {
		const auto current = settings.audioPlaybackSpeed(false);
		constexpr auto speeds = std::array<float64, 4>{ 1., 1.5, 2., 0.5 };
		// TOGGLE_SPEEDS of MusicPlayerSheet
		auto index = -1;
		for (auto i = 0; i != int(speeds.size()); ++i) {
			if (current - 0.1 <= speeds[i]) {
				index = i;
				break;
			}
		}
		const auto next = speeds[(index + 1) % int(speeds.size())];
		settings.setAudioPlaybackSpeed(next);
		Core::App().saveSettingsDelayed();
		instance->updatePlaybackSpeed();
		applyState(instance->getState(type));
	} break;
	case Id::Lyrics: setMode(1); break;
	case Id::Queue: setMode(2); break;
	case Id::QueueSort: _reversed = !_reversed; break;
	default: break;
	}
	update();
}

void Screen::longPress(Id id) {
	if (id == Id::Shuffle) {
		showOrderMenu();
	} else if (id == Id::Repeat) {
		showRepeatMenu();
	} else if (id == Id::Speed) {
		showSpeedMenu();
	}
	_press[int(id)].to(0.);
	ensureFrames();
}

void Screen::toggleSaved() {
	const auto document = _id.audio();
	if (!document) {
		return;
	}
	auto &music = document->owner().savedMusic();
	if (music.has(document)) {
		music.remove(document);
	} else {
		music.save(document, Data::FileOrigin(_id.contextId()));
	}
	const auto isSaved = !_selectShape[int(Id::Profile)].target;
	_selectShape[int(Id::Profile)].to(isSaved ? 1. : 0.);
	_selectContainer[int(Id::Profile)].to(isSaved ? 1. : 0.);
	ensureFrames();
}

void Screen::showMoreMenu() {
	const auto item = currentItem();
	if (!item) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto controller = _controller;
	const auto fullId = item->fullId();
	const auto weak = QPointer<Screen>(this);
	_menu->addAction(
		saved()
			? Tr("Убрать из профиля", "Remove from profile")
			: Tr("Сохранить в профиль", "Save to profile"),
		[=] { if (weak) { weak->toggleSaved(); } },
		&st::menuIconProfile);
	_menu->addAction(
		Tr("Переслать", "Forward"),
		[=] {
			Window::ShowForwardMessagesBox(
				controller,
				MessageIdsList{ fullId });
		},
		&st::menuIconForward);
	_menu->addAction(
		Tr("Показать в чате", "Show in chat"),
		[=] {
			if (const auto message = controller->session().data().message(
					fullId)) {
				if (weak) {
					weak->close();
				}
				controller->showMessage(message);
			}
		},
		&st::menuIconShowInChat);
	_menu->addSeparator();
	_menu->addAction(
		Tr("Настройки плеера", "Player settings"),
		[=] { ShowPlayerSettingsBox(controller); },
		&st::menuIconSettings);
	_menu->popup(mapToGlobal(rectOf(Id::More).bottomRight().toPoint()));
}

void Screen::showOrderMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::defaultPopupMenu);
	const auto set = [](Media::OrderMode mode) {
		Core::App().settings().setPlayerOrderMode(mode);
		Core::App().saveSettingsDelayed();
	};
	_menu->addAction(Tr("По порядку", "In order"), [=] {
		set(Media::OrderMode::Default);
	}, nullptr);
	_menu->addAction(Tr("Обратный порядок", "Reverse order"), [=] {
		set(Media::OrderMode::Reverse);
	}, nullptr);
	_menu->addAction(Tr("Перемешать", "Shuffle"), [=] {
		set(Media::OrderMode::Shuffle);
	}, nullptr);
	_menu->popup(mapToGlobal(rectOf(Id::Shuffle).topLeft().toPoint()));
}

void Screen::showRepeatMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::defaultPopupMenu);
	const auto set = [](Media::RepeatMode mode) {
		Core::App().settings().setPlayerRepeatMode(mode);
		Core::App().saveSettingsDelayed();
	};
	_menu->addAction(Tr("Не повторять", "No repeat"), [=] {
		set(Media::RepeatMode::None);
	}, nullptr);
	_menu->addAction(Tr("Повторять плейлист", "Repeat playlist"), [=] {
		set(Media::RepeatMode::All);
	}, nullptr);
	_menu->addAction(Tr("Повторять трек", "Repeat track"), [=] {
		set(Media::RepeatMode::One);
	}, nullptr);
	_menu->popup(mapToGlobal(rectOf(Id::Repeat).topLeft().toPoint()));
}

void Screen::showSpeedMenu() {
	if (_speedPanel) {
		_speedPanel->hideAnimated();
		return;
	}
	const auto weak = QPointer<Screen>(this);
	_speedPanel = Ui::CreateChild<SpeedPanel>(
		this,
		rectOf(Id::Speed),
		[=] { return weak ? weak->pal() : PlayerPalette(); },
		[=](float64 speed) {
			Core::App().settings().setAudioPlaybackSpeed(speed);
			Core::App().saveSettingsDelayed();
			Media::Player::instance()->updatePlaybackSpeed();
			if (weak) {
				weak->applyState(Media::Player::instance()->getState(
					AudioMsgId::Type::Song));
			}
		});
}

void Screen::wheelEvent(QWheelEvent *e) {
	const auto delta = e->angleDelta().y();
	if (_modeValue == 2 && stageRect().contains(e->position())) {
		const auto rows = int(filteredQueue().size());
		const auto max = std::max(
			0.,
			float64(rows * S(56)) - rectOf(Id::QueueRow).height());
		_queueScroll.to(std::clamp(
			_queueScroll.target - delta * 0.8,
			0.,
			max));
		ensureFrames();
	} else if (_modeValue == 1 && stageRect().contains(e->position())) {
		_manualScrollUntil = crl::now() + kManualScrollHold;
		_lyricScroll.to(std::clamp(
			_lyricScroll.target - delta * 0.8,
			0.,
			std::max(0., _lyricsContentHeight)));
		ensureFrames();
	}
}

void Screen::keyPressEvent(QKeyEvent *e) {
	const auto key = e->key();
	if (_modeValue == 2) {
		if (key == Qt::Key_Backspace && !_search.isEmpty()) {
			_search.chop(1);
			_queueScroll.jump(0.);
			update();
			return;
		} else if (key == Qt::Key_Escape && !_search.isEmpty()) {
			_search = QString();
			update();
			return;
		}
		const auto text = e->text();
		if (!text.isEmpty()
			&& text.at(0).isPrint()
			&& !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
			&& !(key == Qt::Key_Space && _search.isEmpty())) {
			_search += text;
			_queueScroll.jump(0.);
			update();
			return;
		}
	}
	const auto instance = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	if (key == Qt::Key_Escape) {
		close();
	} else if (key == Qt::Key_Space) {
		instance->playPause(type);
	} else if ((key == Qt::Key_Left || key == Qt::Key_Right) && _length > 0) {
		const auto delta = (key == Qt::Key_Left) ? -5000 : 5000;
		seekToProgress(float64(_position + delta) / _length);
	} else {
		RpWidget::keyPressEvent(e);
	}
}

void Screen::paintButtonShape(
		QPainter &p,
		Id id,
		QRectF bounds,
		QColor content,
		QColor container,
		QColor selectedContainer,
		bool alwaysContainer,
		float64 pressedRadius,
		float64 selectedRadius,
		int innerSide,
		float64 innerRadius) {
	const auto press = _press[int(id)].value;
	const auto shape = _selectShape[int(id)].value;
	const auto select = Clamp01(_selectContainer[int(id)].value);
	const auto max = std::min(bounds.width(), bounds.height()) / 2.;
	const auto radius = std::clamp(
		Lerp(Lerp(max, std::min(selectedRadius, max), shape),
			std::min(pressedRadius, max),
			press),
		0.,
		max);
	const auto inner = innerSide
		? std::clamp(Lerp(std::min(innerRadius, radius), radius, shape), 0., radius)
		: radius;
	const auto path = (innerSide == 1)
		? RoundedPath(bounds, inner, radius)
		: (innerSide == 2)
		? RoundedPath(bounds, radius, inner)
		: RoundedPath(bounds, radius, radius);
	p.setPen(Qt::NoPen);
	const auto base = alwaysContainer ? 1. : select;
	if (base > 0. && container.isValid() && container.alpha()) {
		p.setBrush(Alpha(container, base));
		p.drawPath(path);
	}
	if (select > 0. && selectedContainer.isValid() && selectedContainer.alpha()) {
		p.setBrush(Alpha(selectedContainer, select));
		p.drawPath(path);
	}
	if (press > 0.01) {
		p.setBrush(Alpha(content, 0.1 * Clamp01(press)));
		p.drawPath(path);
	}
}

void Screen::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto shown = _showAnimation.value(1.);
	if (shown < 1.) {
		p.setOpacity(shown);
		p.translate(0, (1. - shown) * D(40.));
	}
	const auto c = pal();
	paintHeader(p, c);
	const auto mode = Smooth(_mode.value);
	if (_lyricsAlpha.value > 0.01) {
		paintLyrics(p, c, Clamp01(_lyricsAlpha.value) * mode);
	}
	if (_queueAlpha.value > 0.01) {
		paintQueue(p, c, Clamp01(_queueAlpha.value) * mode);
	}
	paintCover(p, c);
	paintInfo(p, c);
	paintSeek(p, c);
	paintTransport(p, c);
	paintToolbar(p, c);
}

void Screen::paintBackdropCpu(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	paintBackground(p, pal());
}

void Screen::paintBackground(QPainter &p, const PlayerPalette &c) {
	const auto key = QString::number(width()) + 'x' + QString::number(height())
		+ '/' + QString::number(c.background.rgba(), 16)
		+ '/' + QString::number(c.accent.rgba(), 16)
		+ '/' + QString::number(c.tonal.rgba(), 16)
		+ (_hasCover ? u"/c"_q : QString());
	if (_backgroundKey != key) {
		_backgroundKey = key;
		const auto ratio = style::DevicePixelRatio();
		_background = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
		_background.setDevicePixelRatio(ratio);
		auto q = QPainter(&_background);
		q.fillRect(rect(), c.background);
		if (_hasCover) {
			const auto w = float64(width());
			const auto h = float64(height());
			const auto radius = std::max(w, h) * 0.55;
			const auto glow = [&](QPointF center, QColor color, float64 alpha) {
				auto gradient = QRadialGradient(center, radius);
				gradient.setColorAt(0., Alpha(color, alpha));
				gradient.setColorAt(1., Alpha(color, 0.));
				q.fillRect(rect(), gradient);
			};
			const auto cover = QPointF(
				_g.coverLeft + _g.coverSize / 2.,
				_g.coverTop + _g.coverSize / 2.);
			glow(cover, c.accent, 0.12);
			glow(QPointF(w * 0.9, h * 0.15), Mix(c.accent, c.tonal, 0.5), 0.07);
			glow(QPointF(w * 0.15, h * 0.95), c.tonal, 0.12);
		}
	}
	p.drawImage(0, 0, _background);
	paintLive(p, c);
	paintRhythm(p, c);
	const auto &grain = NoiseTile(0);
	if (!grain.isNull()) {
		p.drawTiledPixmap(rect(), grain);
	}
}

bool Screen::rhythmActive() const {
	return _hasCover && _playing && Extera::Bool(u"player_rhythm"_q);
}

bool Screen::liveActive() const {
	return Extera::Bool(u"player_live_gradient"_q);
}

// The backdrop of the PulseSync heart mod: three soft spots in the cover
// colours at the corners and the bottom, here slowly drifting.
void Screen::paintLive(QPainter &p, const PlayerPalette &c) {
	if (!liveActive()) {
		return;
	}
	const auto factor = 8;
	const auto small = QSize(
		std::max(1, width() / factor),
		std::max(1, height() / factor));
	if (_live.size() != small) {
		_live = QImage(small, QImage::Format_ARGB32_Premultiplied);
	}
	_live.fill(Qt::transparent);
	{
		auto q = QPainter(&_live);
		q.setRenderHint(QPainter::Antialiasing);
		const auto w = float64(small.width());
		const auto h = float64(small.height());
		const auto t = crl::now() / 1000.;
		constexpr auto kX = std::array<float64, 3>{ 0.1, 0.92, 0.5 };
		constexpr auto kY = std::array<float64, 3>{ 0.08, 0.2, 1.05 };
		constexpr auto kRx = std::array<float64, 3>{ 0.7, 0.7, 1. };
		constexpr auto kRy = std::array<float64, 3>{ 0.86, 0.86, 0.69 };
		constexpr auto kAlpha = std::array<float64, 3>{ 0.5, 0.45, 0.38 };
		const auto light = PlayerLightTheme();
		for (auto i = 0; i != 3; ++i) {
			const auto x = (kX[i] + std::sin(t * 0.13 + i * 2.1) * 0.07) * w;
			const auto y = (kY[i] + std::cos(t * 0.11 + i * 1.3) * 0.06) * h;
			const auto breathe = 1. + 0.08 * std::sin(t * 0.21 + i * 1.7);
			const auto rx = kRx[i] * w * breathe;
			const auto ry = kRy[i] * h * breathe;
			const auto alpha = kAlpha[i] * (light ? 0.6 : 1.);
			auto gradient = QRadialGradient(QPointF(), 1.);
			gradient.setColorAt(0., Alpha(c.glow[i], alpha));
			gradient.setColorAt(0.35, Alpha(c.glow[i], alpha * 0.55));
			gradient.setColorAt(0.7, Alpha(c.glow[i], 0.));
			q.save();
			q.translate(x, y);
			q.scale(rx, ry);
			q.fillRect(QRectF(-x / rx, -y / ry, w / rx, h / ry), gradient);
			q.restore();
		}
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(rect(), _live);
	p.restore();
}

// RhythmBackground of exteraGram: four soft blobs orbiting the middle,
// breathing with the bass, mids and highs, flaring on beats.
void Screen::stepRhythm() {
	const auto now = crl::now();
	const auto dt = _rhythmLast
		? std::clamp((now - _rhythmLast) / 1000., 0., 0.05)
		: 0.;
	_rhythmLast = now;
	const auto animating = rhythmActive();
	auto energy = 0.;
	auto bass = 0.;
	auto mid = 0.;
	auto high = 0.;
	if (animating) {
		const auto speed = Core::App().settings().audioPlaybackSpeed(false);
		const auto position = _position
			+ crl::time((now - _positionAt) * speed);
		_rhythmState.clearEvents();
		Rhythm::Read(position, _rhythmState);
		const auto &state = _rhythmState;
		if (state.beat > 0.f) {
			const auto strength = std::min(1., state.beat / 0.7);
			_pulse = std::max(
				_pulse,
				state.downbeat ? 0.75 * strength : state.beat * 0.28);
			_surge = std::max(
				_surge,
				state.downbeat ? strength * 1.6 : state.beat * 0.6);
		}
		_colorTarget += state.phrases;
		if (state.drop) {
			_bloom = 1.;
			_surge = std::max(_surge, 3.);
		}
		_bpm = state.bpm;
		energy = state.energy;
		bass = state.bass;
		mid = state.mid;
		high = state.high;
		_targetIntensity = state.intensity;
	}
	_presence = Approach(_presence, animating ? 1. : 0., dt, 0.25, 0.25);
	_energy = Approach(_energy, energy, dt, 0.12, 0.35);
	_bass = Approach(_bass, bass, dt, 0.04, 0.22);
	_mid = Approach(_mid, mid, dt, 0.08, 0.3);
	_high = Approach(_high, high, dt, 0.05, 0.25);
	_intensity = Approach(
		_intensity,
		animating ? _targetIntensity : 0.5,
		dt,
		1.2,
		1.2);
	_pulse *= std::exp(-dt / 0.22);
	_surge *= std::exp(-dt / 0.3);
	_bloom *= std::exp(-dt / 0.9);
	_colorShift = Approach(_colorShift, _colorTarget, dt, 0.7, 0.7);
	if (_colorTarget >= 4 && _colorShift >= 4.) {
		_colorTarget -= 4;
		_colorShift -= 4.;
	}
	const auto factor = _intensity * 0.8 + 0.6;
	const auto spin = animating
		? ((_bpm > 0.)
			? (_bpm / 60. / 16.) * factor
			: _energy * 0.06 + 0.02)
		: 0.;
	_spin = Approach(_spin, spin, dt, 0.6, 0.6);
	_rhythmAngle = std::fmod(
		_rhythmAngle + dt * _spin * (_surge * 1.6 + 1.) * 2. * M_PI,
		120. * M_PI);
}

void Screen::paintRhythm(QPainter &p, const PlayerPalette &c) {
	stepRhythm();
	if (_presence <= 0.003) {
		return;
	}
	constexpr auto kOrbitX = std::array<float64, 4>{ 0.3, 0.34, 0.26, 0.38 };
	constexpr auto kOrbitY = std::array<float64, 4>{ 0.26, 0.3, 0.22, 0.34 };
	constexpr auto kAngle = std::array<float64, 4>{ 0., M_PI / 2., M_PI, M_PI * 1.5 };
	constexpr auto kSpin = std::array<float64, 4>{ 1., 0.75, -1.25, 0.6 };
	constexpr auto kRadius = std::array<float64, 4>{ 1.02, 0.94, 0.96, 0.86 };
	constexpr auto kAlphaDark = std::array<float64, 4>{ 0.31, 0.26, 0.29, 0.23 };
	constexpr auto kAlphaLight = std::array<float64, 4>{ 0.22, 0.19, 0.2, 0.16 };
	constexpr auto kStops = std::array<float64, 6>{ 0., 0.2, 0.4, 0.6, 0.8, 1. };
	constexpr auto kFalloff = std::array<float64, 6>{ 1., 0.88, 0.6, 0.32, 0.12, 0. };
	const auto light = PlayerLightTheme();
	// Drawn small and stretched: blobs are blurry anyway, so this is cheap.
	const auto factor = 8;
	const auto small = QSize(
		std::max(1, width() / factor),
		std::max(1, height() / factor));
	if (_rhythm.size() != small) {
		_rhythm = QImage(small, QImage::Format_ARGB32_Premultiplied);
	}
	_rhythm.fill(Qt::transparent);
	{
		auto q = QPainter(&_rhythm);
		q.setRenderHint(QPainter::Antialiasing);
		const auto w = float64(small.width());
		const auto h = float64(small.height());
		const auto cx = w / 2.;
		const auto cy = h * 0.42;
		const auto bands = std::array<float64, 4>{ _bass, _mid, _bass, _high };
		for (auto i = 0; i != 4; ++i) {
			const auto angle = _rhythmAngle * kSpin[i] + kAngle[i];
			const auto x = cx + std::cos(angle) * kOrbitX[i] * w;
			const auto y = cy + std::sin(angle) * kOrbitY[i] * h;
			const auto radius = kRadius[i] * w
				* (1. + _pulse * 0.05 + bands[i] * 0.1);
			const auto alpha = std::min(
				0.95,
				((light ? kAlphaLight[i] : kAlphaDark[i])
					+ _pulse * 0.08
					+ _energy * 0.12) * (_intensity * 0.45 + 0.7)) * _presence;
			auto gradient = QRadialGradient(QPointF(x, y), radius);
			for (auto s = 0; s != 6; ++s) {
				gradient.setColorAt(kStops[s], Alpha(c.glow[i], alpha * kFalloff[s]));
			}
			q.fillRect(QRectF(0, 0, w, h), gradient);
		}
		if (_pulse > 0.003) {
			auto gradient = QRadialGradient(QPointF(cx, cy), 0.7 * w * (1. + _pulse * 0.12));
			for (auto s = 0; s != 6; ++s) {
				gradient.setColorAt(kStops[s], Alpha(c.glow[0], _pulse * 0.14 * _presence * kFalloff[s]));
			}
			q.fillRect(QRectF(0, 0, w, h), gradient);
		}
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(rect(), _rhythm);
	auto scrim = QLinearGradient(0, height() * 0.52, 0, height());
	scrim.setColorAt(0., Alpha(c.background, 0.));
	scrim.setColorAt(1., Alpha(c.background, 0.718 * _presence));
	p.fillRect(QRectF(0, height() * 0.52, width(), height() * 0.48), scrim);
	p.restore();
}

void Screen::paintHeader(QPainter &p, const PlayerPalette &c) {
	const auto collapse = rectOf(Id::Collapse);
	paintButtonShape(p, Id::Collapse, collapse, c.text, QColor(), QColor(), false, D(12.), D(12.));
	DrawIcon(p, u"arrow_more.png"_q, collapse.center(), S(24), c.text);
	if (currentItem()) {
		const auto more = rectOf(Id::More);
		paintButtonShape(p, Id::More, more, c.text, QColor(), QColor(), false, D(12.), D(12.));
		DrawIcon(p, u"ic_ab_other.png"_q, more.center(), S(24), c.text);
	}
	const auto width = (_g.landscape ? _g.paneWidth : _g.contentWidth)
		- S(68) * 2;
	const auto left = (_g.landscape ? _g.paneLeft : _g.contentLeft) + S(68);
	const auto count = int(_queue.items.size());
	const auto subtitle = (_queue.index >= 0 && count > 1)
		? Extera::Text(
			QString::fromUtf8("Трек %1 из %2").arg(_queue.index + 1).arg(count),
			QString("Track %1 of %2").arg(_queue.index + 1).arg(count))
		: QString();
	const auto titleFont = Font(16, true);
	const auto subFont = Font(13, false);
	const auto total = titleFont->height + (subtitle.isEmpty() ? 0 : subFont->height);
	const auto top = _g.contentTop + (S(56) - total) / 2;
	p.setPen(c.text);
	p.setFont(titleFont->f);
	p.drawText(
		QRect(left, top, width, titleFont->height),
		Qt::AlignCenter,
		titleFont->elided(_source, width));
	if (!subtitle.isEmpty()) {
		p.setPen(c.textSecondary);
		p.setFont(subFont->f);
		p.drawText(
			QRect(left, top + titleFont->height, width, subFont->height),
			Qt::AlignCenter,
			subtitle);
	}
}

void Screen::paintCover(QPainter &p, const PlayerPalette &c) {
	const auto mode = Clamp01(_mode.value);
	auto rect = coverRect();
	if (rect.width() <= 1.) {
		return;
	}
	const auto pause = Lerp(_pause.value, 1., mode);
	const auto dragK = Clamp01(std::abs(_drag.value) / std::max(1., rect.width()));
	const auto scale = pause * Lerp(1., 0.86, dragK);
	const auto center = rect.center() + QPointF(_drag.value * (1. - mode), 0.);
	rect = QRectF(
		center.x() - rect.width() * scale / 2.,
		center.y() - rect.height() * scale / 2.,
		rect.width() * scale,
		rect.height() * scale);
	const auto radius = Lerp(D(28.), D(14.), mode) * scale;
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.save();
	p.setOpacity(p.opacity() * Lerp(1., 0.55, dragK));
	if (_hasCover && !_coverPix.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		const auto want = int(std::ceil(std::max(_g.coverSize, 1.) * ratio));
		if (std::abs(want - _coverScaledSize) > 2 || _coverScaled.isNull()) {
			_coverScaledSize = want;
			_coverScaled = _coverPix.scaled(
				want,
				want,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		}
		p.setClipPath(path);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawPixmap(rect, _coverScaled, QRectF(_coverScaled.rect()));
	} else {
		p.setPen(Qt::NoPen);
		p.setBrush(c.tonal);
		p.drawPath(path);
		const auto note = rect.width() * 0.33;
		const auto spin = _coverLoading
			? (crl::now() % 1600) / 1600. * 2 * M_PI
			: 0.;
		if (_coverLoading && rect.width() > D(80.)) {
			auto petals = QPainterPath();
			const auto center = rect.center();
			const auto base = rect.width() * 0.08;
			for (auto i = 0; i <= 96; ++i) {
				const auto t = i / 96. * 2 * M_PI;
				const auto r = base * (1. + 0.16 * std::cos(9 * t));
				const auto point = QPointF(
					center.x() + std::cos(t + spin) * r,
					center.y() + std::sin(t + spin) * r);
				if (i) {
					petals.lineTo(point);
				} else {
					petals.moveTo(point);
				}
			}
			p.setBrush(c.onTonal);
			p.drawPath(petals);
		} else {
			p.setPen(c.onTonal);
			p.setFont(Font(std::max(8, int(note / style::Scale() * 100)), true)->f);
			p.drawText(rect, Qt::AlignCenter, QString(QChar(0x266A)));
		}
	}
	p.restore();
}

void Screen::paintInfo(QPainter &p, const PlayerPalette &c) {
	const auto mode = Clamp01(_mode.value);
	const auto shift = (_g.compactCoverSize + S(16)) * mode;
	const auto left = _g.paneLeft + S(20) + shift;
	const auto width = int(_g.textWidth - shift);
	const auto titleFont = Font(20, true);
	p.setPen(c.text);
	p.setFont(titleFont->f);
	p.drawText(
		QRectF(left, _g.titleTop, width, S(28)),
		Qt::AlignLeft | Qt::AlignVCenter,
		titleFont->elided(_title, width));
	const auto artistFont = Font(16, false);
	p.setPen(c.textSecondary);
	p.setFont(artistFont->f);
	p.drawText(
		QRectF(left, _g.titleTop + S(26), width, S(28)),
		Qt::AlignLeft | Qt::AlignVCenter,
		artistFont->elided(_artist, width));

	const auto profile = rectOf(Id::Profile);
	paintButtonShape(p, Id::Profile, profile, c.text, QColor(), c.tonal, false, D(12.), D(12.));
	const auto on = Clamp01(_selectContainer[int(Id::Profile)].value);
	const auto ink = Mix(c.text, c.onTonal, on);
	DrawIcon(
		p,
		(on > 0.5) ? u"media_like_active.png"_q : u"media_like.png"_q,
		profile.center(),
		S(24),
		ink);
}

void Screen::paintSeek(QPainter &p, const PlayerPalette &c) {
	const auto rect = seekRect();
	const auto centerY = rect.bottom() - D(16.);
	const auto from = rect.left() + D(2.);
	const auto to = rect.right() - D(2.);
	const auto progress = (_seeking || _rewindDirection)
		? _seekValue
		: ((_length > 0) ? Clamp01(float64(shownPosition()) / _length) : 0.);
	const auto x = Lerp(from, to, progress);
	const auto handle = _handle.value;
	const auto gap = handle / 2. + D(4.);
	const auto stroke = D(4.);

	p.setBrush(Qt::NoBrush);
	if (x + gap < to) {
		p.setPen(QPen(c.track, stroke, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(x + gap, centerY), QPointF(to, centerY));
	}
	if (x - gap > from) {
		const auto fade = (progress < 0.1)
			? progress / 0.1
			: (progress > 0.9)
			? (1. - progress) / 0.1
			: 1.;
		const auto amplitude = std::max(0., _amplitude.value) * Clamp01(fade);
		const auto k = 2. * M_PI / D(40.);
		auto path = QPainterPath();
		const auto end = x - gap;
		path.moveTo(from, centerY + amplitude * std::sin(-_wavePhase));
		for (auto px = from + 1.5; px < end; px += 1.5) {
			path.lineTo(px, centerY + amplitude * std::sin((px - from) * k - _wavePhase));
		}
		path.lineTo(end, centerY + amplitude * std::sin((end - from) * k - _wavePhase));
		p.setPen(QPen(c.accent, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(path);
	}
	p.setPen(Qt::NoPen);
	p.setBrush(c.accent);
	const auto handleHeight = D(18.);
	p.drawRoundedRect(
		QRectF(x - handle / 2., centerY - handleHeight / 2., handle, handleHeight),
		handle / 2.,
		handle / 2.);

	const auto bubble = Clamp01(_bubble.value);
	if (bubble > 0.01 && _length > 0) {
		const auto label = Ui::FormatDurationText(crl::time(progress * _length) / 1000);
		const auto font = Font(13, true);
		const auto w = font->width(label) + D(20.);
		const auto h = D(26.);
		const auto left = std::clamp(x - w / 2., rect.left() - D(4.), rect.right() - w + D(4.));
		const auto top = centerY - D(30.) - h + D(2.);
		const auto scale = bubble * 0.4 + 0.6;
		p.save();
		p.translate(x, centerY - D(16.));
		p.scale(scale, scale);
		p.translate(-x, -(centerY - D(16.)));
		p.setOpacity(p.opacity() * bubble);
		p.setBrush(c.accent);
		p.drawRoundedRect(QRectF(left, top, w, h), h / 2., h / 2.);
		p.setPen(c.onAccent);
		p.setFont(font->f);
		p.drawText(QRectF(left, top, w, h), Qt::AlignCenter, label);
		p.restore();
	}

	const auto timeFont = Font(12, false);
	p.setFont(timeFont->f);
	p.setPen(c.textSecondary);
	const auto shown = crl::time(progress * _length);
	p.drawText(
		QRectF(_g.paneLeft + S(20), _g.timesTop, S(120), S(18)),
		Qt::AlignLeft | Qt::AlignVCenter,
		Ui::FormatDurationText(shown / 1000));
	const auto right = _showRemaining
		? (u"-"_q + Ui::FormatDurationText(std::max(crl::time(0), _length - shown) / 1000))
		: Ui::FormatDurationText(_length / 1000);
	p.drawText(rectOf(Id::Duration), Qt::AlignRight | Qt::AlignVCenter, right);
}

void Screen::paintTransport(QPainter &p, const PlayerPalette &c) {
	const auto &settings = Core::App().settings();
	const auto instance = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;

	const auto toggle = [&](Id id, bool on) {
		const auto rect = rectOf(id);
		paintButtonShape(p, id, rect, c.text, QColor(), c.tonal, false, D(12.), D(12.));
		const auto ink = Mix(c.text, c.onTonal, Clamp01(_selectContainer[int(id)].value));
		const auto name = (id == Id::Shuffle)
			? u"player_new_shuffle.svg"_q
			: (on && settings.playerRepeatMode() == Media::RepeatMode::One)
			? u"player_new_repeatone.svg"_q
			: u"player_new_repeatall.svg"_q;
		DrawIcon(p, name, rect.center(), S(28), ink);
	};
	toggle(Id::Shuffle, settings.playerOrderMode() != Media::OrderMode::Default);
	toggle(Id::Repeat, settings.playerRepeatMode() != Media::RepeatMode::None);

	const auto skip = [&](int index, bool enabled) {
		const auto rect = controlRect(index);
		const auto radius = std::min(_radii[index].value, std::min(rect.width(), rect.height()) / 2.);
		p.setPen(Qt::NoPen);
		p.setBrush(c.tonal);
		p.drawRoundedRect(rect, radius, radius);
		const auto icon = index ? _nextIcon.get() : _prevIcon.get();
		const auto color = Alpha(c.onTonal, enabled ? 1. : 0.4);
		const auto size = S(20);
		p.save();
		p.translate(rect.center());
		if (index) {
			p.rotate(180.);
		}
		icon->paint(p, -size / 2, -size / 2, color);
		p.restore();
	};
	skip(0, instance->previousAvailable(type));
	skip(2, instance->nextAvailable(type));

	const auto rect = controlRect(1);
	const auto radius = std::min(_radii[1].value, std::min(rect.width(), rect.height()) / 2.);
	p.setPen(Qt::NoPen);
	p.setBrush(c.accent);
	p.drawRoundedRect(rect, radius, radius);
	const auto center = rect.center();
	p.setBrush(c.onAccent);
	const auto k = Clamp01(_playIcon.value);
	if (_loading && !_playing) {
		const auto spin = (crl::now() % 1000) / 1000. * 360.;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(c.onAccent, D(3.), Qt::SolidLine, Qt::RoundCap));
		p.drawArc(QRectF(center.x() - D(13.), center.y() - D(13.), D(26.), D(26.)), int(-spin * 16), 270 * 16);
		ensureFrames();
		return;
	}
	// The triangle splits in two halves that slide into the pause bars.
	const auto size = D(28.);
	const auto x0 = center.x() - size * 0.30;
	const auto x1 = center.x() + size * 0.40;
	const auto xm = (x0 + x1) / 2.;
	const auto top = center.y() - size * 0.36;
	const auto bottom = center.y() + size * 0.36;
	const auto mid = center.y();
	const auto midTop = top + (mid - top) * 0.5;
	const auto midBottom = bottom - (bottom - mid) * 0.5;
	const auto bar = size * 0.2;
	const auto gap = size * 0.15;
	const auto lerp = [&](QPointF a, QPointF b) {
		return QPointF(Lerp(a.x(), b.x(), k), Lerp(a.y(), b.y(), k));
	};
	const auto barLeft = center.x() - gap / 2. - bar;
	const auto barRight = center.x() + gap / 2.;
	const auto barTop = center.y() - size * 0.33;
	const auto barBottom = center.y() + size * 0.33;
	auto left = QPainterPath();
	left.moveTo(lerp({ x0, top }, { barLeft, barTop }));
	left.lineTo(lerp({ xm, midTop }, { barLeft + bar, barTop }));
	left.lineTo(lerp({ xm, midBottom }, { barLeft + bar, barBottom }));
	left.lineTo(lerp({ x0, bottom }, { barLeft, barBottom }));
	left.closeSubpath();
	auto right = QPainterPath();
	right.moveTo(lerp({ xm, midTop }, { barRight, barTop }));
	right.lineTo(lerp({ x1, mid }, { barRight + bar, barTop }));
	right.lineTo(lerp({ x1, mid }, { barRight + bar, barBottom }));
	right.lineTo(lerp({ xm, midBottom }, { barRight, barBottom }));
	right.closeSubpath();
	p.setPen(QPen(c.onAccent, D(2.), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(c.onAccent);
	p.drawPath(left);
	p.drawPath(right);
}

void Screen::paintToolbar(QPainter &p, const PlayerPalette &c) {
	const auto speedBounds = rectOf(Id::Speed);
	const auto speed = speedBounds.adjusted(0, D(6.), 0, -D(6.));
	paintButtonShape(p, Id::Speed, speed, c.text, QColor(), c.tonal, false, D(12.), D(24.));
	const auto outline = 1. - Clamp01(_selectContainer[int(Id::Speed)].value);
	if (outline > 0.01) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(Alpha(c.textSecondary, 0.6 * outline), D(1.5)));
		const auto r = speed.height() / 2.;
		p.drawRoundedRect(speed.adjusted(D(0.75), D(0.75), -D(0.75), -D(0.75)), r, r);
	}
	const auto value = Core::App().settings().audioPlaybackSpeed(false);
	p.setPen(Mix(c.text, c.onTonal, 1. - outline));
	p.setFont(Font(14, true)->f);
	p.drawText(speed, Qt::AlignCenter, QString::number(value, 'g', 3) + u"x"_q);

	const auto segment = [&](Id id, int side) {
		const auto rect = rectOf(id);
		paintButtonShape(p, id, rect, c.onTonal, c.tonal, c.accent, true, D(12.), D(24.), side, D(6.));
		const auto ink = Mix(c.onTonal, c.onAccent, Clamp01(_selectContainer[int(id)].value));
		DrawIcon(
			p,
			(id == Id::Lyrics)
				? u"formatting_quote.svg"_q
				: u"player_new_queue.svg"_q,
			rect.center(),
			S(24),
			ink);
	};
	segment(Id::Lyrics, 2);
	segment(Id::Queue, 1);
}

void Screen::paintLyrics(QPainter &p, const PlayerPalette &c, float64 alpha) {
	const auto stage = stageRect();
	p.save();
	p.setOpacity(p.opacity() * alpha);
	p.translate(0, (1. - Clamp01(_lyricsAlpha.value)) * D(16.));
	p.setClipRect(stage);
	_lineRects.clear();

	const auto message = [&](const QString &title, const QString &text) {
		const auto titleFont = Font(20, true);
		const auto textFont = Font(14, false);
		p.setPen(c.text);
		p.setFont(titleFont->f);
		const auto middle = stage.center().y();
		p.drawText(QRectF(stage.left() + S(32), middle - S(30), stage.width() - S(64), S(28)), Qt::AlignCenter, title);
		p.setPen(c.textSecondary);
		p.setFont(textFont->f);
		p.drawText(QRectF(stage.left() + S(32), middle + S(2), stage.width() - S(64), S(60)), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, text);
	};
	if (_lyricsState == LyricsState::Loading) {
		const auto now = crl::now();
		const auto center = stage.center();
		const auto size = D(9.) / 2.;
		for (auto i = 0; i != 3; ++i) {
			const auto t = (((now - i * 180) % 1400) + 1400) % 1400 / 1400.;
			const auto pulse = 0.5 - std::cos(t * 2 * M_PI) * 0.5;
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(c.text, pulse * 0.55 + 0.45));
			p.drawEllipse(QPointF(center.x() + (i - 1) * (D(9.) + D(7.)), center.y()), size * (pulse * 0.45 + 0.7), size * (pulse * 0.45 + 0.7));
		}
		p.restore();
		ensureFrames();
		return;
	} else if (_lyricsState == LyricsState::NotFound) {
		message(Tr("Текст не найден", "Lyrics not found"), Tr("Для этого трека пока нет текста.", "There are no lyrics for this track yet."));
		p.restore();
		return;
	} else if (_lyricsState == LyricsState::Offline) {
		message(Tr("Тексты из сети выключены", "Online lyrics are off"), Tr("Включите их в настройках плеера.", "Turn them on in the player settings."));
		p.restore();
		return;
	}

	const auto side = S(20);
	const auto width = int(stage.width()) - side * 2;
	layoutLyrics(width);
	const auto padding = D(7.);
	const auto now = crl::now();
	const auto current = std::max(_lyricIndex, 0);
	const auto currentCenter = (current < int(_layoutTop.size()))
		? (_layoutTop[current] + _layoutHeight[current] / 2.)
		: 0.;
	_lyricsContentHeight = std::max(0., _layoutTotal - stage.height() * 0.5);
	if (now >= _manualScrollUntil) {
		const auto target = std::max(0., currentCenter - stage.height() * 0.42);
		if (std::abs(_lyricScroll.target - target) > 0.5) {
			_lyricScroll.to(target);
			ensureFrames();
		}
	}
	const auto scroll = _lyricScroll.value;

	const auto progressMode = Extera::Bool(u"player_line_progress"_q);
	auto progress = 0.;
	if (_lyricIndex >= 0 && _lyricIndex + 1 < int(_lyrics.size())) {
		const auto from = _lyrics[_lyricIndex].at;
		const auto to = _lyrics[_lyricIndex + 1].at;
		if (to > from) {
			progress = Clamp01(float64(shownPosition() - from) / (to - from));
		}
	}

	const auto fade = D(48.);
	_lineRects.resize(_layouts.size());
	for (auto i = 0; i != int(_layouts.size()); ++i) {
		const auto &layout = *_layouts[i];
		const auto height = _layoutHeight[i];
		const auto top = stage.top() + _layoutTop[i] - scroll;
		_lineRects[i] = QRectF(stage.left() + side, top, width, height);
		if (top + height < stage.top() || top > stage.bottom()) {
			continue;
		}
		const auto edge = std::min(
			(top + height / 2.) - stage.top(),
			stage.bottom() - (top + height / 2.));
		const auto edgeAlpha = Clamp01(edge / fade);
		if (edgeAlpha <= 0.) {
			continue;
		}
		const auto active = (i == _lyricIndex);
		const auto level = (i < int(_lineLevel.size())) ? _lineLevel[i] : 0.28;
		const auto opacity = level * edgeAlpha;
		const auto scale = Lerp(0.95, 1., Clamp01((level - 0.28) / 0.72));
		p.save();
		const auto origin = QPointF(stage.left() + side, top + padding);
		p.translate(origin);
		p.scale(scale, scale);
		p.translate(-origin);
		if (active && progressMode) {
			p.setPen(Alpha(c.text, 0.45 * edgeAlpha));
			auto total = 0.;
			for (auto l = 0; l != layout.lineCount(); ++l) {
				layout.lineAt(l).draw(&p, origin);
				total += layout.lineAt(l).naturalTextWidth();
			}
			auto remain = progress * total;
			p.setPen(Alpha(c.text, edgeAlpha));
			for (auto l = 0; l != layout.lineCount(); ++l) {
				const auto text = layout.lineAt(l);
				const auto natural = text.naturalTextWidth();
				const auto fill = std::clamp(remain, 0., natural);
				remain -= natural;
				if (fill <= 0.) {
					continue;
				}
				p.save();
				p.setClipRect(QRectF(origin.x(), origin.y() + text.y(), fill + 1., text.height()), Qt::IntersectClip);
				text.draw(&p, origin);
				p.restore();
			}
		} else {
			p.setPen(Alpha(c.text, opacity));
			for (auto l = 0; l != layout.lineCount(); ++l) {
				layout.lineAt(l).draw(&p, origin);
			}
		}
		p.restore();
	}
	p.restore();
}

void Screen::layoutLyrics(int width) {
	if (_layoutWidth == width && _layouts.size() == _lyrics.size()) {
		return;
	}
	_layoutWidth = width;
	_layouts.clear();
	_layoutTop.clear();
	_layoutHeight.clear();
	const auto font = Font(22, true)->f;
	const auto padding = D(7.);
	auto y = 0.;
	for (const auto &line : _lyrics) {
		auto layout = std::make_unique<QTextLayout>(
			line.text.isEmpty() ? QString(QChar(0x266A)) : line.text,
			font);
		auto option = QTextOption(Qt::AlignLeft);
		option.setWrapMode(QTextOption::WordWrap);
		layout->setTextOption(option);
		layout->setCacheEnabled(true);
		layout->beginLayout();
		auto h = 0.;
		while (true) {
			auto text = layout->createLine();
			if (!text.isValid()) {
				break;
			}
			text.setLineWidth(width);
			text.setPosition(QPointF(0, h));
			h += text.height();
		}
		layout->endLayout();
		_layoutTop.push_back(y);
		_layoutHeight.push_back(h + padding * 2);
		y += h + padding * 2;
		_layouts.push_back(std::move(layout));
	}
	_layoutTotal = y;
}

Image *Screen::thumbnailFor(not_null<DocumentData*> document, FullMsgId id) {
	if (!document->hasThumbnail()) {
		return nullptr;
	}
	auto &media = _thumbs[document->id];
	if (!media) {
		media = document->createMediaView();
		media->thumbnailWanted(Data::FileOrigin(id));
	}
	return media->thumbnail();
}

void Screen::paintQueue(QPainter &p, const PlayerPalette &c, float64 alpha) {
	const auto stage = stageRect();
	p.save();
	p.setOpacity(p.opacity() * alpha);
	p.translate(0, (1. - Clamp01(_queueAlpha.value)) * D(16.));

	const auto field = queueRectOf(Id::QueueSearch);
	p.setPen(Qt::NoPen);
	p.setBrush(c.surface);
	p.drawRoundedRect(field, D(20.), D(20.));
	DrawIcon(
		p,
		u"msg_search.png"_q,
		QPointF(field.left() + D(20.), field.center().y()),
		S(20),
		c.textSecondary);
	const auto fieldFont = Font(15, false);
	p.setFont(fieldFont->f);
	const auto textRect = field.adjusted(D(36.), 0, -D(12.), 0);
	if (_search.isEmpty()) {
		p.setPen(c.textSecondary);
		p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, Tr("Поиск по очереди", "Search the queue"));
	} else {
		p.setPen(c.text);
		p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, fieldFont->elided(_search, int(textRect.width()), Qt::ElideLeft));
	}
	DrawIcon(
		p,
		u"player_new_order.png"_q,
		queueRectOf(Id::QueueSort).center(),
		S(24),
		_reversed ? c.accent : c.textSecondary);

	const auto area = queueRectOf(Id::QueueRow);
	p.setClipRect(area);
	const auto rows = filteredQueue();
	const auto owner = _id.audio() ? &_id.audio()->owner() : nullptr;
	const auto rowHeight = S(56);
	const auto side = int(area.left()) + S(20);
	const auto titleFont = Font(15, true);
	const auto subFont = Font(13, false);
	for (auto k = 0; k != int(rows.size()); ++k) {
		const auto top = area.top() + k * rowHeight - _queueScroll.value;
		if (top + rowHeight < area.top() || top > area.bottom()) {
			continue;
		}
		const auto index = rows[k];
		const auto current = (index == _queue.index);
		if (current) {
			p.setPen(Qt::NoPen);
			p.setBrush(c.surface);
			p.drawRoundedRect(QRectF(side - S(8), top + S(2), area.width() - S(12) * 2, rowHeight - S(4)), D(16.), D(16.));
		}
		const auto cover = QRectF(side, top + (rowHeight - S(44)) / 2., S(44), S(44));
		const auto radius = current ? D(12.) : cover.width() / 2.;
		Image *thumb = nullptr;
		if (owner) {
			if (const auto item = owner->message(_queue.items[index])) {
				if (const auto media = item->media()) {
					if (const auto document = media->document()) {
						thumb = thumbnailFor(document, _queue.items[index]);
					}
				}
			}
		}
		p.setPen(Qt::NoPen);
		auto clip = QPainterPath();
		clip.addRoundedRect(cover, radius, radius);
		if (thumb) {
			p.save();
			p.setClipPath(clip, Qt::IntersectClip);
			p.drawImage(cover, thumb->original());
			p.restore();
			p.setBrush(QColor(0, 0, 0, 90));
			p.drawPath(clip);
		} else {
			p.setBrush(current ? c.accent : c.tonal);
			p.drawPath(clip);
		}
		p.setBrush(thumb ? QColor(Qt::white) : (current ? c.onAccent : c.onTonal));
		const auto center = cover.center();
		if (current && _playing) {
			const auto now = crl::now();
			for (auto b = 0; b != 3; ++b) {
				const auto phase = (now % 900) / 900. * 2 * M_PI + b * 1.7;
				const auto h = D(6.) + D(5.) * (0.5 + 0.5 * std::sin(phase));
				p.drawRoundedRect(QRectF(center.x() - D(7.) + b * D(5.), center.y() + D(7.) - h, D(3.), h), D(1.5), D(1.5));
			}
			ensureFrames();
		} else {
			const auto h = D(6.);
			auto tri = QPainterPath();
			tri.moveTo(center.x() + h, center.y());
			tri.lineTo(center.x() - h * 0.7, center.y() - h);
			tri.lineTo(center.x() - h * 0.7, center.y() + h);
			tri.closeSubpath();
			p.drawPath(tri);
		}
		auto title = QString();
		auto subtitle = QString();
		if (owner) {
			if (const auto item = owner->message(_queue.items[index])) {
				if (const auto media = item->media()) {
					if (const auto document = media->document()) {
						if (const auto song = document->song()) {
							title = song->title;
							subtitle = song->performer;
						}
						if (title.isEmpty()) {
							title = document->filename();
						}
					}
				}
			}
		}
		const auto textLeft = side + S(60);
		const auto textWidth = int(area.right()) - textLeft - S(20);
		p.setPen(current ? c.accent : c.text);
		p.setFont(titleFont->f);
		p.drawText(QRectF(textLeft, top + S(9), textWidth, titleFont->height), Qt::AlignLeft | Qt::AlignVCenter, titleFont->elided(title, textWidth));
		p.setPen(c.textSecondary);
		p.setFont(subFont->f);
		p.drawText(QRectF(textLeft, top + S(29), textWidth, subFont->height), Qt::AlignLeft | Qt::AlignVCenter, subFont->elided(subtitle, textWidth));
	}
	p.restore();
}

QPointer<Screen> &CurrentScreen() {
	static auto result = QPointer<Screen>();
	return result;
}

} // namespace

void ShowPlayerScreen(not_null<Window::SessionController*> controller) {
	auto &current = CurrentScreen();
	if (current) {
		current->raise();
		current->setFocus();
		return;
	}
	const auto body = controller->widget()->bodyWidget();
	const auto parent = body
		? not_null<Ui::RpWidget*>(body)
		: not_null<Ui::RpWidget*>(controller->content().get());
	current = Ui::CreateChild<Screen>(parent.get(), controller);
}

} // namespace Extera
