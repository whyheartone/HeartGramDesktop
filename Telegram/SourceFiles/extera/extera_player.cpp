#include "extera/extera_player.h"

#include "extera/extera_config.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <cmath>

namespace Extera {
namespace {

constexpr auto kAmplitudeDuration = crl::time(320);
constexpr auto kWavePeriod = crl::time(1500);
constexpr auto kRequestTimeout = 9000;
constexpr auto kCacheLimit = 40;

[[nodiscard]] QHash<QString, LyricsLoader::Lines> &Cache() {
	static auto result = QHash<QString, LyricsLoader::Lines>();
	return result;
}

[[nodiscard]] QString CleanTitle(QString title) {
	static const auto extension = QRegularExpression(
		u"\\.(mp3|m4a|flac|ogg|oga|opus|wav|aac|wma)$"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto decoration = QRegularExpression(
		u"\\s*[(\\[][^)\\]]*(official|lyric|video|audio|visualizer|remaster|hq|hd)[^)\\]]*[)\\]]"_q,
		QRegularExpression::CaseInsensitiveOption);
	title.remove(extension);
	title.replace('_', ' ');
	const auto stripped = title.remove(decoration).trimmed();
	return stripped.isEmpty() ? title.trimmed() : stripped;
}

[[nodiscard]] LyricsLoader::Lines ParseSynced(const QString &text) {
	static const auto stamp = QRegularExpression(
		u"\\[(\\d+):(\\d+(?:[.,]\\d+)?)\\]"_q);
	auto result = LyricsLoader::Lines();
	for (const auto &raw : text.split('\n')) {
		auto times = std::vector<crl::time>();
		auto lastEnd = 0;
		auto i = stamp.globalMatch(raw);
		while (i.hasNext()) {
			const auto match = i.next();
			const auto seconds = match.captured(2).replace(',', '.').toDouble();
			times.push_back(crl::time(
				match.captured(1).toInt() * 60000 + seconds * 1000));
			lastEnd = match.capturedEnd();
		}
		if (times.empty()) {
			continue;
		}
		const auto line = raw.mid(lastEnd).trimmed();
		for (const auto at : times) {
			result.push_back({ at, line });
		}
	}
	std::stable_sort(result.begin(), result.end(), [](
			const LyricLine &a,
			const LyricLine &b) {
		return a.at < b.at;
	});
	return result;
}

[[nodiscard]] LyricsLoader::Lines ParsePlain(const QString &text) {
	auto result = LyricsLoader::Lines();
	for (const auto &raw : text.split(u'\n')) {
		const auto line = raw.trimmed();
		if (!line.isEmpty()) {
			result.push_back({ kPlainLine, line });
		}
	}
	return result;
}

} // namespace

WavySlider::WavySlider(QWidget *parent, const style::FilledSlider &st)
: Ui::FilledSlider(parent, st)
, _st(st) {
	_frames.init([=](crl::time) {
		update();
		return wavy()
			&& !isHidden()
			&& (_playing || _amplitude.animating());
	});
	Extera::Config::Instance().boolChanges(
		u"player_md3"_q
	) | rpl::on_next([=](bool) {
		update();
		refreshFrames();
	}, lifetime());
}

void WavySlider::setPlaying(bool playing) {
	if (_playing != playing) {
		_playing = playing;
		_amplitude.start(
			[=] { update(); },
			playing ? 0. : 1.,
			playing ? 1. : 0.,
			kAmplitudeDuration);
	}
	refreshFrames();
}

bool WavySlider::wavy() const {
	return _allowed && (_forced || Extera::Bool(u"player_md3"_q));
}

void WavySlider::setAllowed(bool allowed) {
	if (_allowed != allowed) {
		_allowed = allowed;
		update();
		refreshFrames();
	}
}

void WavySlider::setForced(bool forced) {
	_forced = forced;
	update();
	refreshFrames();
}

void WavySlider::setColors(QColor active, QColor inactive) {
	_active = active;
	_inactive = inactive;
	update();
}

void WavySlider::refreshFrames() {
	const auto need = wavy()
		&& !isHidden()
		&& (_playing || _amplitude.animating());
	if (need && !_frames.animating()) {
		_frames.start();
	} else if (!need && _frames.animating()) {
		_frames.stop();
	}
}

void WavySlider::paintEvent(QPaintEvent *e) {
	if (!wavy()) {
		Ui::FilledSlider::paintEvent(e);
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto seek = getSeekRect();
	const auto disabled = isDisabled();
	const auto active = _active
		? *_active
		: (disabled ? _st.disabledFg : _st.activeFg)->c;
	const auto thickness = _st.lineWidth * 0.8;
	const auto centerY = height() / 2.;
	const auto from = float64(seek.x());
	const auto end = from + seek.width();
	const auto mid = from + getCurrentValue() * seek.width();
	const auto gap = thickness * 1.6;
	const auto wavelength = _st.fullWidth * 1.9;
	const auto amplitude = _amplitude.value(_playing ? 1. : 0.)
		* _st.fullWidth * 0.2;
	const auto phase = (std::fmod(float64(crl::now()), float64(kWavePeriod))
		/ kWavePeriod) * 2 * M_PI;

	p.setOpacity(fadeOpacity());
	p.setBrush(Qt::NoBrush);

	if (end > mid + gap) {
		auto color = _inactive ? *_inactive : _st.inactiveFg->c;
		color.setAlphaF(color.alphaF() * 0.75);
		p.setPen(QPen(color, thickness, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(mid + gap, centerY), QPointF(end, centerY));
	}

	const auto waveEnd = mid - gap;
	if (waveEnd > from) {
		auto path = QPainterPath();
		path.moveTo(from, centerY + amplitude * std::sin(-phase));
		for (auto x = from + 2.; x < waveEnd; x += 2.) {
			path.lineTo(
				x,
				centerY + amplitude * std::sin(
					(x - from) / wavelength * 2 * M_PI - phase));
		}
		path.lineTo(
			waveEnd,
			centerY + amplitude * std::sin(
				(waveEnd - from) / wavelength * 2 * M_PI - phase));
		p.setPen(QPen(
			active,
			thickness,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.drawPath(path);
	}

	p.setPen(Qt::NoPen);
	p.setBrush(active);
	const auto handleWidth = thickness * 1.1;
	const auto handleHeight = _st.fullWidth * 0.95;
	p.drawRoundedRect(
		QRectF(
			mid - handleWidth / 2.,
			centerY - handleHeight / 2.,
			handleWidth,
			handleHeight),
		handleWidth / 2.,
		handleWidth / 2.);
}

LyricsLoader::LyricsLoader(Fn<void(Lines)> done)
: _done(std::move(done))
, _manager(new QNetworkAccessManager()) {
}

LyricsLoader::~LyricsLoader() {
	cancel();
	delete _manager;
}

void LyricsLoader::cancel() {
	++_generation;
	if (const auto reply = _reply.data()) {
		reply->disconnect();
		reply->abort();
		reply->deleteLater();
	}
	_reply = nullptr;
}

void LyricsLoader::request(const LyricsQuery &query) {
	cancel();
	const auto title = CleanTitle(query.title);
	auto artist = query.artist.trimmed();
	auto name = title;
	if (artist.isEmpty()) {
		const auto split = name.indexOf(u" - "_q);
		if (split > 0) {
			artist = name.left(split).trimmed();
			name = name.mid(split + 3).trimmed();
		}
	}
	if (name.isEmpty()) {
		finish({});
		return;
	}
	const auto key = (artist + QChar(1) + name + QChar(1)
		+ QString::number(query.durationSec)).toLower();
	if (const auto i = Cache().constFind(key); i != Cache().constEnd()) {
		finish(i.value());
		return;
	}
	const auto generation = _generation;
	const auto store = [=](Lines lines) {
		if (Cache().size() >= kCacheLimit) {
			Cache().clear();
		}
		Cache().insert(key, lines);
		if (generation == _generation) {
			finish(std::move(lines));
		}
	};

	auto get = QUrl(u"https://lrclib.net/api/get"_q);
	auto items = QUrlQuery();
	items.addQueryItem(u"track_name"_q, name);
	if (!artist.isEmpty()) {
		items.addQueryItem(u"artist_name"_q, artist);
	}
	if (!query.album.trimmed().isEmpty()) {
		items.addQueryItem(u"album_name"_q, query.album.trimmed());
	}
	if (query.durationSec > 0) {
		items.addQueryItem(
			u"duration"_q,
			QString::number(query.durationSec));
	}
	get.setQuery(items);

	auto search = QUrl(u"https://lrclib.net/api/search"_q);
	auto searchItems = QUrlQuery();
	searchItems.addQueryItem(u"track_name"_q, name);
	if (!artist.isEmpty()) {
		searchItems.addQueryItem(u"artist_name"_q, artist);
	}
	search.setQuery(searchItems);

	const auto duration = query.durationSec;
	fetch(get, [=](const QByteArray &data) {
		const auto object = QJsonDocument::fromJson(data).object();
		const auto synced = object.value(u"syncedLyrics"_q).toString();
		if (!synced.isEmpty()) {
			store(ParseSynced(synced));
			return;
		}
		const auto exactPlain = object.value(u"plainLyrics"_q).toString();
		if (generation != _generation) {
			return;
		}
		fetch(search, [=](const QByteArray &data) {
			const auto list = QJsonDocument::fromJson(data).array();
			const auto pick = [&](const QString &key) {
				auto best = QString();
				auto bestDelta = 1 << 30;
				for (const auto &entry : list) {
					const auto item = entry.toObject();
					const auto text = item.value(key).toString();
					if (text.isEmpty()) {
						continue;
					}
					const auto delta = (duration > 0)
						? std::abs(int(item.value(u"duration"_q).toDouble())
							- duration)
						: 0;
					if (delta < bestDelta) {
						bestDelta = delta;
						best = text;
					}
				}
				const auto fits = duration <= 0 || bestDelta <= 6;
				return fits ? best : QString();
			};
			if (const auto best = pick(u"syncedLyrics"_q); !best.isEmpty()) {
				store(ParseSynced(best));
				return;
			}
			// WHY: some tracks only have plain text on lrclib, shown as is.
			auto plain = pick(u"plainLyrics"_q);
			if (plain.isEmpty()) {
				plain = exactPlain;
			}
			store(plain.isEmpty() ? Lines() : ParsePlain(plain));
		});
	});
}

void LyricsLoader::fetch(const QUrl &url, Fn<void(const QByteArray &)> done) {
	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"HeartGram Desktop (https://t.me/whyheartone)"_q);
	request.setTransferTimeout(kRequestTimeout);
	const auto reply = _manager->get(request);
	_reply = reply;
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto data = (reply->error() == QNetworkReply::NoError)
			? reply->readAll()
			: QByteArray();
		reply->deleteLater();
		if (_reply.data() == reply) {
			_reply = nullptr;
		}
		done(data);
	});
}

void LyricsLoader::finish(Lines lines) {
	if (_done) {
		_done(std::move(lines));
	}
}

} // namespace Extera
