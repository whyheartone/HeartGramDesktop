#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/continuous_sliders.h"

#include <QtCore/QPointer>
#include <QtGui/QColor>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace Extera {

// Draws like the stock slider unless the Material 3 player style is on.
class WavySlider final : public Ui::FilledSlider {
public:
	WavySlider(QWidget *parent, const style::FilledSlider &st);

	void setPlaying(bool playing);
	void setForced(bool forced);
	void setAllowed(bool allowed);
	void setColors(QColor active, QColor inactive);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void refreshFrames();
	[[nodiscard]] bool wavy() const;

	const style::FilledSlider &_st;
	Ui::Animations::Simple _amplitude;
	Ui::Animations::Basic _frames;
	std::optional<QColor> _active;
	std::optional<QColor> _inactive;
	bool _playing = false;
	bool _forced = false;
	bool _allowed = true;

};

struct LyricLine {
	crl::time at = 0;
	QString text;
};

struct LyricsQuery {
	QString artist;
	QString title;
	QString album;
	int durationSec = 0;
};

class LyricsLoader final {
public:
	using Lines = std::vector<LyricLine>;

	explicit LyricsLoader(Fn<void(Lines)> done);
	~LyricsLoader();

	void request(const LyricsQuery &query);
	void cancel();

private:
	void fetch(const QUrl &url, Fn<void(const QByteArray &)> done);
	void finish(Lines lines);

	const Fn<void(Lines)> _done;
	QNetworkAccessManager *_manager = nullptr;
	QPointer<QNetworkReply> _reply;
	int _generation = 0;

};

} // namespace Extera
