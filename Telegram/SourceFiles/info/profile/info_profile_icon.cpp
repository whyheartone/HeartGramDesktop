/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_icon.h"

// AyuGram includes
#include "ayu/ayu_ui_settings.h"
#include "ui/widgets/buttons.h"

namespace Info {
namespace Profile {
namespace {

[[nodiscard]] Ui::SettingsButton *FindButton(QWidget *parent) {
	if (!parent) {
		return nullptr;
	}
	if (const auto self = dynamic_cast<Ui::SettingsButton*>(parent)) {
		return self;
	}
	for (const auto child : parent->findChildren<QWidget*>()) {
		if (const auto button = dynamic_cast<Ui::SettingsButton*>(child)) {
			return button;
		}
	}
	return nullptr;
}

} // namespace

FloatingIcon::FloatingIcon(
	RpWidget *parent,
	const style::icon &icon,
	QPoint position)
: FloatingIcon(parent, icon, position, Tag{}) {
}

FloatingIcon::FloatingIcon(
	RpWidget *parent,
	const style::icon &icon,
	QPoint position,
	const Tag &)
: RpWidget(parent)
, _icon(&icon)
, _point(position) {
	setGeometry(QRect(
		QPoint(0, 0),
		QSize(
			_point.x() + _icon->width(),
			_point.y() + _icon->height()
				+ 2 * AyuUiSettings::settingsRowExtra())));
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void FloatingIcon::paintEvent(QPaintEvent *e) {
	auto extra = 0;
	if (const auto button = FindButton(parentWidget())) {
		const auto &st = button->st();
		const auto base = st.padding.top() + st.height + st.padding.bottom();
		extra = std::min(
			std::max(0, (button->height() - base) / 2),
			AyuUiSettings::settingsRowExtra());
	}
	auto p = QPainter(this);
	_icon->paint(p, _point + QPoint(0, extra), width());
}

} // namespace Profile
} // namespace Info
