// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/icon_picker.h"

#include "tray.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_controller.h"

#include <algorithm>
#include "ayu/ayu_settings.h"
#include "ayu/ui/ayu_logo.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "styles/style_ayu_styles.h"
#include "ui/painter.h"
#include "window/main_window.h"
#include "extera/extera_text.h"
#include "lang/lang_keys.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"

#ifdef Q_OS_WIN
#include "ayu/utils/windows_utils.h"
#endif

namespace {

const QVector<QString> ayuIcons{
	AyuAssets::DEFAULT_ICON,
	AyuAssets::ALT_ICON,
	AyuAssets::DISCORD_ICON,
	AyuAssets::SPOTIFY_ICON,
	AyuAssets::EXTERA_ICON,
	AyuAssets::NOTHING_ICON,
	AyuAssets::BARD_ICON,
	AyuAssets::YAPLUS_ICON,
	AyuAssets::WIN95_ICON,
	AyuAssets::CHIBI_ICON,
	AyuAssets::CHIBI2_ICON,
	AyuAssets::EXTERA2_ICON,
};

const QVector<QString> exteraIcons = [] {
	auto result = QVector<QString>();
	for (const auto id : {
		"amethyst", "aurora", "avatar", "beta", "candy", "cyberpunk",
		"echpochmak", "editor", "fish", "flappy", "frog", "google",
		"google26", "guitar", "hamster", "human", "ice_age", "invincible",
		"jellyfish", "mole", "nasa", "nyan_cat", "orbit", "osu",
		"psychosis", "round", "space", "sponge", "sunset", "sus",
		"winter", "xp" }) {
		result.push_back(AyuAssets::EXTERA_PREFIX + QString::fromLatin1(id));
	}
	return result;
}();

[[nodiscard]] int RowsFor(const QVector<QString> &icons) {
	return int(icons.size()) / IconPicker::kColumns
		+ std::min(1, int(icons.size()) % IconPicker::kColumns);
}

void applyIcon() {
#ifdef Q_OS_WIN
	AyuAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif

	Window::OverrideApplicationIcon(AyuAssets::currentAppLogo());
	Core::App().refreshApplicationIcon();
	Core::App().tray().updateIconCounters();
	Core::App().domain().notifyUnreadBadgeChanged();
}

} // namespace

std::vector<IconPicker*> &Pickers() {
	static auto result = std::vector<IconPicker*>();
	return result;
}

IconPicker::IconPicker(QWidget *parent, bool extera)
	: RpWidget(parent)
	, _icons(extera ? &exteraIcons : &ayuIcons)
	, _rows(RowsFor(*_icons)) {
	Pickers().push_back(this);
	lifetime().add([=] {
		auto &list = Pickers();
		list.erase(std::remove(list.begin(), list.end(), this), list.end());
	});
	widthValue() | rpl::on_next([=](int w) {
		const auto cell = w / kColumns;
		const auto iconSize = st::iconPickerIconSize;
		const auto contentSize = iconSize + st::iconPickerImagePadding * 2;
		const auto h = _rows * cell - (cell - contentSize);
		resize(w, h);
	}, lifetime());
}

void IconPicker::drawIcon(QPainter &p, const QImage &icon, int x, int y, float strokeOpacity) {
	{
		PainterHighQualityEnabler hq(p);
		p.save();
		p.setPen(QPen(st::boxDividerBg, 0));
		p.setBrush(QBrush(st::boxDividerBg));
		p.setOpacity(strokeOpacity);
		p.drawRoundedRect(
			x + st::iconPickerSelectedPadding,
			y + st::iconPickerSelectedPadding,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerSelectedRounding,
			st::iconPickerSelectedRounding
		);
		p.restore();
	}

	const auto rect = QRect(
		x + st::iconPickerImagePadding,
		y + st::iconPickerImagePadding,
		st::iconPickerIconSize,
		st::iconPickerIconSize
	);
	p.drawImage(rect, icon);
}

int IconPicker::cellWidth() const {
	return width() / kColumns;
}

void IconPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;

	for (int row = 0; row < _rows; row++) {
		const auto columns = std::min(kColumns, static_cast<int>(_icons->size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto &iconName = (*_icons)[idx];
			if (iconName.isEmpty()) {
				continue;
			}
			QImage icon;
			if (const auto cached = _cachedIcons.find(iconName); cached != _cachedIcons.end()) {
				icon = cached->second;
			} else {
				icon = _cachedIcons[iconName] = AyuAssets::loadPreview(iconName);
			}
			const auto opacity = (iconName == AyuSettings::getInstance().appIcon())
				? 1.0f
				: 0.0f;

			const auto x = i * cell + (cell - iconSize) / 2;
			const auto y = row * cell;

			drawIcon(p, icon, x, y, opacity);
		}
	}
}

void IconPicker::mousePressEvent(QMouseEvent *e) {
	const auto &settings = AyuSettings::getInstance();
	auto changed = false;

	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;

	for (int row = 0; row < _rows; row++) {
		const auto columns = std::min(kColumns, static_cast<int>(_icons->size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto x = i * cell + (cell - iconSize) / 2;
			const auto y = row * cell;

			if (e->pos().x() >= x && e->pos().x() <= x + iconSize
				&& e->pos().y() >= y && e->pos().y() <= y + iconSize) {
				const auto &iconName = (*_icons)[idx];
				if (iconName.isEmpty()) {
					break;
				}

				if (settings.appIcon() != iconName) {
					_wasSelected = settings.appIcon();
					_animation.start(
						[=]
						{
							update();
						},
						0.0,
						1.0,
						200,
						anim::easeOutCubic
					);

					AyuSettings::getInstance().setAppIcon(iconName);
					changed = true;
					break;
				}
			}
		}
	}

	if (changed) {
		applyIcon();

		for (const auto picker : Pickers()) {
			picker->update();
		}
		if (const auto window = Core::App().activeWindow()) {
			window->show(Ui::MakeConfirmBox({
				.text = Extera::Text(
					u"Чтобы иконка поменялась везде, перезапустите HeartGram."_q,
					u"Restart HeartGram so the new icon is applied everywhere."_q),
				.confirmed = [](Fn<void()> &&close) {
					Core::Restart();
				},
				.confirmText = Extera::Text(
					u"Перезапустить"_q,
					u"Restart"_q),
			}));
		}
	}
}

void ShowIconPickerBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(Extera::Text(
		u"Иконка приложения"_q,
		u"App icon"_q)));
	const auto content = box->verticalLayout();
	Ui::AddSubsectionTitle(content, rpl::single(u"AyuGram"_q));
	content->add(
		object_ptr<IconPicker>(content, false),
		st::settingsButtonNoIcon.padding);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(u"exteraGram"_q));
	content->add(
		object_ptr<IconPicker>(content, true),
		st::settingsButtonNoIcon.padding);
	Ui::AddSkip(content);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}
