#include "extera/extera_player_box.h"

#include "ayu/ui/utils/itunes_search.h"
#include "ayu/ui/utils/palette.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "extera/extera_config.h"
#include "extera/extera_player.h"
#include "extera/extera_text.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/media_common.h"
#include "media/player/media_player_instance.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/effects/animation_value.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

#include <QtCore/QHash>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>
#include <QtGui/QTextLayout>

#include <array>
#include <cmath>
#include <limits>

namespace Extera {
namespace {

constexpr auto kCoverCacheLimit = 8;
constexpr auto kCoverSource = 1000;
constexpr auto kSmallCover = 500;

[[nodiscard]] QHash<quint64, PlayerCover> &CoverCache() {
	static auto result = QHash<quint64, PlayerCover>();
	return result;
}

[[nodiscard]] QImage ExtractEmbeddedCover(const QString &path) {
	if (path.isEmpty()) {
		return {};
	}
	AVFormatContext *format = nullptr;
	const auto utf8 = path.toUtf8();
	if (avformat_open_input(&format, utf8.constData(), nullptr, nullptr) < 0) {
		return {};
	}
	auto result = QImage();
	for (auto i = 0U; i != format->nb_streams; ++i) {
		const auto stream = format->streams[i];
		if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
			&& stream->attached_pic.size > 0) {
			result = QImage::fromData(
				stream->attached_pic.data,
				stream->attached_pic.size);
			break;
		}
	}
	avformat_close_input(&format);
	return result;
}

struct Lab {
	double l = 0.;
	double a = 0.;
	double b = 0.;
};

[[nodiscard]] double ToLinear(double c) {
	return (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double FromLinear(double c) {
	return (c <= 0.0031308)
		? (12.92 * c)
		: (1.055 * std::pow(c, 1. / 2.4) - 0.055);
}

[[nodiscard]] Lab ToLab(QRgb rgb) {
	const auto r = ToLinear(qRed(rgb) / 255.);
	const auto g = ToLinear(qGreen(rgb) / 255.);
	const auto b = ToLinear(qBlue(rgb) / 255.);
	const auto x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047;
	const auto y = (r * 0.2126 + g * 0.7152 + b * 0.0722);
	const auto z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883;
	const auto f = [](double t) {
		return (t > 216. / 24389.)
			? std::cbrt(t)
			: ((24389. / 27. * t + 16.) / 116.);
	};
	const auto fx = f(x);
	const auto fy = f(y);
	const auto fz = f(z);
	return { 116. * fy - 16., 500. * (fx - fy), 200. * (fy - fz) };
}

[[nodiscard]] QColor FromLab(double l, double a, double b) {
	const auto fy = (l + 16.) / 116.;
	const auto fx = fy + a / 500.;
	const auto fz = fy - b / 200.;
	const auto inverse = [](double t) {
		const auto cube = t * t * t;
		return (cube > 216. / 24389.) ? cube : ((116. * t - 16.) * 27. / 24389.);
	};
	const auto x = inverse(fx) * 0.95047;
	const auto y = (l > 8.) ? std::pow(fy, 3.) : (l * 27. / 24389.);
	const auto z = inverse(fz) * 1.08883;
	const auto r = x * 3.2406 + y * -1.5372 + z * -0.4986;
	const auto g = x * -0.9689 + y * 1.8758 + z * 0.0415;
	const auto bl = x * 0.0557 + y * -0.2040 + z * 1.0570;
	const auto channel = [](double c) {
		return std::clamp(int(std::round(FromLinear(c) * 255.)), 0, 255);
	};
	return QColor(channel(r), channel(g), channel(bl));
}

// ColorTones.withTone: the seed hue with the given lightness and at most
// the given chroma.
[[nodiscard]] QColor WithTone(QRgb seed, double tone, double maxChroma) {
	auto lab = ToLab(seed);
	const auto chroma = std::hypot(lab.a, lab.b);
	if (chroma > maxChroma && chroma > 0.) {
		lab.a *= maxChroma / chroma;
		lab.b *= maxChroma / chroma;
	}
	return FromLab(tone, lab.a, lab.b);
}

[[nodiscard]] double HueDistance(double a, double b) {
	const auto d = std::fmod(std::abs(a - b), 360.);
	return (d > 180.) ? (360. - d) : d;
}

[[nodiscard]] QRgb ComputeSeed(const std::vector<Ayu::Ui::Swatch> &list) {
	if (list.empty()) {
		return 0;
	}
	const auto size = int(list.size());
	auto population = 0LL;
	for (const auto &swatch : list) {
		population += swatch.population();
	}
	if (population <= 0) {
		return 0;
	}
	auto hues = std::vector<double>(size);
	auto chromas = std::vector<double>(size);
	auto shares = std::vector<double>(size);
	for (auto i = 0; i != size; ++i) {
		const auto lab = ToLab(list[i].rgb());
		chromas[i] = std::hypot(lab.a, lab.b);
		hues[i] = std::atan2(lab.b, lab.a) * 180. / M_PI;
		shares[i] = double(list[i].population()) / population;
	}
	auto best = -1;
	auto bestScore = std::numeric_limits<double>::lowest();
	auto dominant = 0;
	for (auto i = 0; i != size; ++i) {
		if (shares[i] > shares[dominant]) {
			dominant = i;
		}
		if (chromas[i] < 8. || shares[i] < 0.01) {
			continue;
		}
		auto nearby = 0.;
		for (auto j = 0; j != size; ++j) {
			if (chromas[j] >= 8. && HueDistance(hues[i], hues[j]) <= 15.) {
				nearby += shares[j];
			}
		}
		const auto c = chromas[i];
		const auto score = nearby * 70. + (c - 48.) * ((c < 48.) ? 0.1 : 0.3);
		if (score > bestScore) {
			bestScore = score;
			best = i;
		}
	}
	return list[(best >= 0) ? best : dominant].rgb() | 0xFF000000U;
}

[[nodiscard]] PlayerCover MakeCover(
		QImage image,
		const QString &path,
		const QString &performer,
		const QString &title,
		bool upgrade) {
	if (!upgrade) {
	} else if (const auto embedded = ExtractEmbeddedCover(path);
			!embedded.isNull()) {
		image = embedded;
	} else if (image.isNull()
		|| std::min(image.width(), image.height()) < kSmallCover) {
		const auto pix = Ayu::Ui::Itunes::FetchCover(
			performer,
			title,
			kCoverSource,
			5000,
			true);
		if (!pix.isNull()) {
			image = pix.toImage();
		}
	}
	if (image.isNull()) {
		return {};
	}
	if (std::max(image.width(), image.height()) > kCoverSource) {
		image = image.scaled(
			kCoverSource,
			kCoverSource,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	if (image.width() != image.height()) {
		const auto side = std::min(image.width(), image.height());
		image = image.copy(
			(image.width() - side) / 2,
			(image.height() - side) / 2,
			side,
			side);
	}
	const auto scale = std::min(1., 112. / std::max(image.width(), image.height()));
	const auto small = image.scaled(
		std::max(1, int(std::round(image.width() * scale))),
		std::max(1, int(std::round(image.height() * scale))),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	auto builder = Ayu::Ui::Palette::from(small);
	builder.maximumColorCount(24).clearFilters();
	const auto palette = builder.generate();
	auto result = PlayerCover();
	result.image = std::move(image);
	result.has = true;
	result.seed = ComputeSeed(palette.swatches());
	if (!result.seed) {
		result.seed = qRgb(128, 128, 128);
	}
	return result;
}

} // namespace

PlayerPalette MakePalette(QRgb seed, bool light) {
	const auto dark = !light;
	const auto t = [&](double darkTone, double lightTone, double chroma) {
		return WithTone(seed, dark ? darkTone : lightTone, chroma);
	};
	const auto unlimited = 1000.;
	// PlayerColors.setGlow: four blobs around the seed hue.
	auto glow = std::array<QColor, 4>();
	{
		const auto lab = ToLab(seed);
		const auto chroma = std::min(std::hypot(lab.a, lab.b), 70.);
		const auto hue = std::atan2(lab.b, lab.a);
		constexpr auto shifts = std::array<double, 4>{ 0., 50., -35., 0. };
		constexpr auto darkTones = std::array<double, 4>{ 42., 48., 36., 60. };
		constexpr auto lightTones = std::array<double, 4>{ 78., 84., 74., 88. };
		for (auto i = 0; i != 4; ++i) {
			const auto angle = shifts[i] * M_PI / 180. + hue;
			glow[i] = FromLab(
				dark ? darkTones[i] : lightTones[i],
				std::cos(angle) * chroma,
				std::sin(angle) * chroma);
		}
	}
	return {
		.background = t(10., 98., 6.),
		.surface = t(17., 93., 8.),
		.text = t(93., 10., 4.),
		.textSecondary = t(70., 44., 10.),
		.accent = t(80., 40., unlimited),
		.onAccent = t(20., 100., unlimited),
		.tonal = t(30., 90., 24.),
		.onTonal = t(90., 30., dark ? 24. : 40.),
		.track = t(24., 90., 16.),
		.glow = glow,
	};
}

QRgb DefaultPlayerSeed() {
	return st::windowActiveTextFg->c.rgb();
}

bool PlayerLightTheme() {
	return st::windowBg->c.lightness() > 127;
}

namespace {

[[nodiscard]] QString Tr(const char *ru, const char *en) {
	return Extera::Text(QString::fromUtf8(ru), QString::fromUtf8(en));
}

void ShowPlayerSettings(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Tr("Настройки плеера", "Player settings")));
		const auto add = [&](const QString &key, const QString &title) {
			const auto button = box->addRow(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(title),
					st::settingsButtonNoIcon),
				style::margins());
			button->toggleOn(rpl::single(Extera::Bool(key)));
			button->toggledChanges(
			) | rpl::on_next([=](bool value) {
				Extera::Config::Instance().setBool(key, value);
			}, button->lifetime());
		};
		add(u"player_cover_color"_q, Tr("Цвета обложки", "Cover colors"));
		add(u"player_rhythm"_q, Tr("Фон в такт музыке", "Background to the beat"));
		add(
			u"player_live_gradient"_q,
			Tr("Живой градиент", "Live gradient"));
		add(
			u"player_gpu"_q,
			Tr(
				"Фон на видеокарте (со следующего открытия)",
				"Background on the GPU (from the next opening)"));
		add(u"player_lyrics"_q, Tr("Тексты из сети", "Lyrics from the network"));
		add(
			u"player_line_progress"_q,
			Tr("Прогресс строки", "Line progress"));
		add(u"player_change_pitch"_q, Tr("Менять тон", "Change pitch"));
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace

void LoadPlayerCover(
		not_null<DocumentData*> document,
		FullMsgId context,
		[[maybe_unused]] int sizePx,
		Fn<void(const PlayerCover &)> done) {
	const auto id = quint64(document->id);
	auto &cache = CoverCache();
	if (const auto i = cache.constFind(id); i != cache.constEnd()) {
		done(i.value());
		return;
	}
	const auto song = document->song();
	const auto performer = song ? song->performer : QString();
	const auto title = song ? song->title : QString();
	const auto path = document->filepath(true);
	const auto media = document->createMediaView();
	const auto process = [=](QImage image) {
		crl::async([=] {
			if (!image.isNull()) {
				// The small Telegram thumbnail gives the colours at once,
				// the full size cover replaces it when it is ready.
				auto quick = MakeCover(image, QString(), QString(), QString(), false);
				crl::on_main([=, quick = std::move(quick)] {
					done(quick);
				});
			}
			auto cover = MakeCover(image, path, performer, title, true);
			crl::on_main([=, cover = std::move(cover)] {
				auto &cache = CoverCache();
				if (cache.size() >= kCoverCacheLimit) {
					cache.clear();
				}
				cache.insert(id, cover);
				done(cover);
			});
		});
	};
	if (const auto thumbnail = media->thumbnail()) {
		process(thumbnail->original());
		return;
	}
	if (!document->isSongWithCover()) {
		process(QImage());
		return;
	}
	media->thumbnailWanted(Data::FileOrigin(context));
	const auto holder = std::make_shared<rpl::lifetime>();
	const auto tries = std::make_shared<int>(0);
	document->session().downloaderTaskFinished(
	) | rpl::take_while([=] {
		if (!holder) {
			return false;
		}
		if (const auto thumbnail = media->thumbnail()) {
			process(thumbnail->original());
			return false;
		}
		return (++*tries < 400);
	}) | rpl::start(*holder);
}

void ShowPlayerSettingsBox(
		not_null<Window::SessionController*> controller) {
	ShowPlayerSettings(controller);
}

void ShowPlayerBox(not_null<Window::SessionController*> controller) {
	ShowPlayerScreen(controller);
}

} // namespace Extera
