// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_other.h"
#include "settings/sections/settings_extera.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/boxes/donate_qr_box.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "lang/lang_text_entity.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "ayu/features/project_badges/project_badges.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "extera/extera_text.h"
#include "main/main_session.h"
#include "ui/basic_click_handlers.h"
#include "ui/text/text_custom_emoji.h"
#include <QFile>

#include <QDesktopServices>
#include <QGuiApplication>
#include <QSvgRenderer>

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

struct Asset {
	QString icon;
	QColor background;
};

Asset getAsset(const QString &name) {
	const auto isNightMode = Window::Theme::IsNightMode();
	const auto normalized = name.toLower();
	QString icon = QString(":/gui/icons/ayu/donates/%1.svg").arg(normalized);
	QColor background = isNightMode ? QColor(0xEEEEEE) : QColor(0x242B2C);
	return {
		.icon = std::move(icon),
		.background = std::move(background)
	};
}

QImage getImage(const QString &name) {
	const auto iconData = getAsset(name);

	const auto factor = style::DevicePixelRatio();
	const auto size = st::menuIconLink.size();
	auto image = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);

		auto svgIcon = QSvgRenderer(iconData.icon);
		svgIcon.render(&p, Rect(size));
	}

	return image;
}

[[nodiscard]] not_null<Ui::SettingsButton*> AddDonate(
		not_null<Ui::SettingsButton*> button,
		const QString &name) {
	const auto btnContainer = Ui::CreateChild<Ui::RpWidget>(button);
	const auto &buttonSt = button->st();
	const auto fullHeight = buttonSt.height
		+ rect::m::sum::v(buttonSt.padding);

	const auto iconWidget = Ui::CreateChild<Ui::RpWidget>(button.get());

	auto icon = getImage(name);
	iconWidget->resize(icon.size() / style::DevicePixelRatio());
	iconWidget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(iconWidget);
		p.drawImage(0, 0, icon);
	}, iconWidget->lifetime());

	button->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		iconWidget->moveToLeft(
			button->st().iconLeft
				+ (st::menuIconShop.width() - iconWidget->width()) / 2,
			(s.height() - iconWidget->height()) / 2);
		btnContainer->moveToLeft(
			iconWidget->x() - (fullHeight - iconWidget->height()) / 2,
			0);
	}, iconWidget->lifetime());

	btnContainer->resize(fullHeight, fullHeight);

	return button;
}

[[nodiscard]] bool HasAsset(const QString &name) {
	return QFile::exists(getAsset(name).icon);
}

void AddEmojiIcon(
		not_null<Ui::SettingsButton*> button,
		not_null<Main::Session*> session,
		DocumentId id) {
	const auto widget = Ui::CreateChild<Ui::RpWidget>(button.get());
	const auto size = st::menuIconLink.size();
	widget->resize(size);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto emoji = widget->lifetime().make_state<
		std::unique_ptr<Ui::Text::CustomEmoji>>(
			session->data().customEmojiManager().create(
				id,
				[=] { widget->update(); },
				Data::CustomEmojiSizeTag::Normal,
				size.width()));
	widget->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		(*emoji)->paint(p, {
			.textColor = st::windowFg->c,
			.now = crl::now(),
			.position = QPoint(),
			.paused = false,
		});
	}, widget->lifetime());
	button->sizeValue() | rpl::on_next([=](const QSize &s) {
		widget->moveToLeft(
			button->st().iconLeft,
			(s.height() - widget->height()) / 2);
	}, widget->lifetime());
}

void FillProjectSupport(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	container->clear();
	const auto block = ProjectBadges::Support();
	if (block.items.empty()) {
		container->resizeToWidth(container->width());
		return;
	}
	AddSubsectionTitle(
		container,
		rpl::single(Extera::Text(u"Поддержка (%1)"_q, u"Support (%1)"_q)
			.arg(ProjectBadges::Project())));
	for (const auto &item : block.items) {
		const auto name = item.name;
		const auto asset = !item.emojiId && HasAsset(name);
		auto button = AddButtonWithIcon(
			container,
			rpl::single(name),
			st::settingsButton,
			(asset || item.emojiId)
				? IconDescriptor()
				: IconDescriptor{ &st::menuIconLink });
		if (item.emojiId) {
			AddEmojiIcon(button, &controller->session(), item.emojiId);
		} else if (asset) {
			button = AddDonate(button, name);
		}
		const auto url = item.url;
		button->setClickedCallback([=] {
			if (url.startsWith(u"copy:"_q)) {
				const auto icon = HasAsset(name)
					? getAsset(name).icon
					: u":/gui/icons/ayu/donates/support_logo.svg"_q;
				controller->show(Box(Ui::FillDonateQrBox, url.mid(5), icon));
			} else {
				UrlClickHandler::Open(url);
			}
		});
	}
	AddSkip(container);
	if (!block.note.isEmpty()) {
		AddDividerText(container, rpl::single(block.note));
	}
	container->resizeToWidth(container->width());
}

void BuildDonations(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.add([=](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto inner = wctx.container->add(
				object_ptr<Ui::VerticalLayout>(wctx.container));
			FillProjectSupport(inner, controller);
			ProjectBadges::Updated() | rpl::on_next([=] {
				FillProjectSupport(inner, controller);
			}, inner->lifetime());
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"ayu/donate"_q,
				.title = Extera::Text(u"Поддержка"_q, u"Support"_q),
				.section = sctx.sectionId,
			});
		});
	});
}

void BuildCrashReporting(SectionBuilder &builder, AyuSectionBuilder &ayu) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_CategoryOther());

	ayu.addSettingToggle({
		.id = u"ayu/crashReporting"_q,
		.altIds = { u"ayu/crashlytics"_q },
		.title = tr::ayu_CrashReporting(),
		.getter = &AyuSettings::crashReporting,
		.setter = &AyuSettings::setCrashReporting,
		.icon = { &st::menuIconReport },
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_CrashReportingDescription());
#endif
}

void BuildOtherThings(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addButton({
		.id = u"ayu/registerUrlScheme"_q,
		.title = rpl::single(Extera::Text(
			u"Ассоциировать ссылки с HeartGram"_q,
			u"Associate links with HeartGram"_q)),
		.icon = { &st::menuIconLink },
		.onClick = [=] {
			Core::Application::RegisterUrlScheme();
			controller->showToast(tr::lng_box_done(tr::now));
		},
	});
	builder.addButton({
		.id = u"ayu/resetSettings"_q,
		.title = tr::ayu_ResetSettings(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_ResetSettingsConfirmation(tr::rich),
				.confirmed = [=](Fn<void()> &&close) {
					AyuSettings::reset();
					controller->showToast(tr::lng_box_done(tr::now));
					close();
				},
				.confirmText = tr::lng_box_yes(),
			}));
		},
	});
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuOther::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryOther,
	.icon = &st::menuIconFave,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildDonations(builder);
	BuildCrashReporting(builder, ayu);
	BuildOtherThings(builder);
	builder.addDivider();
	BuildExteraOther(builder);
});

} // namespace

rpl::producer<QString> AyuOther::title() {
	return tr::ayu_CategoryOther();
}

AyuOther::AyuOther(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuOther::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuOtherId() {
	return AyuOther::Id();
}

} // namespace Settings
