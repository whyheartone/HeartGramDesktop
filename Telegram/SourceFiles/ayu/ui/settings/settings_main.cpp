// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "extera/extera_update.h"
#include "extera/extera_config.h"
#include "extera/extera_text.h"
#include "ayu/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "settings/sections/settings_extera.h"
#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "extera/extera_text.h"
#include "ayu/features/heartgram_lite.h"
#include "extera/extera_version.h"
#include "ayu/ui/ayu_logo.h"
#include "ayu/ui/settings/settings_appearance.h"
#include "ayu/ui/settings/settings_ayu.h"
#include "ayu/ui/settings/settings_chats.h"
#include "ayu/ui/settings/settings_filters.h"
#include "ayu/ui/settings/settings_general.h"
#include "ayu/ui/settings/settings_other.h"
#include "core/version.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QDesktopServices>

namespace Settings {

using namespace Builder;

namespace {

void BuildLogo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto logo = object_ptr<Ui::RpWidget>(ctx.container);
		const auto logoRaw = logo.data();
		logoRaw->resize(
			QSize(st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize));
		logoRaw->setNaturalWidth(st::settingsCloudPasswordIconSize);
		logoRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(logoRaw);
			const auto image = AyuAssets::currentAppLogoPad();
			if (!image.isNull()) {
				const auto size = st::settingsCloudPasswordIconSize;
				const auto scaled = image.scaled(
					size * style::DevicePixelRatio(),
					size * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				p.drawImage(QRect(0, 0, size, size), scaled);
			}
		}, logoRaw->lifetime());
		return { .widget = std::move(logo), .align = style::al_top };
	});
}

void BuildVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(
					QString("HeartGram Desktop ")
					+ QString::fromLatin1(AppVersionStr)
					+ QString(" (%1)").arg(Extera::kBuild)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(Extera::Text(
					u"Неофициальный клиент Telegram с новыми возможностями"_q,
					u"Unofficial Telegram client with new features"_q)),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildUpdates(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(rpl::single(Extera::Text(
		u"Обновления"_q,
		u"Updates"_q)));
	builder.addButton({
		.id = u"heartgram/update"_q,
		.title = Extera::Update::AvailableValue(
		) | rpl::map([](bool available) {
			return available
				? Extera::Text(
					u"Обновить до сборки "_q,
					u"Update to build "_q)
					+ Extera::Update::AvailableVersion()
				: Extera::Text(
					u"Проверить обновления"_q,
					u"Check for updates"_q);
		}),
		.icon = { &st::menuIconInfo },
		.label = Extera::Update::StatusValue(),
		.onClick = [] {
			if (Extera::Update::AvailableVersion().isEmpty()) {
				Extera::Update::CheckNow(nullptr);
			} else {
				Extera::Update::Install();
			}
		},
	});
	builder.addButton({
		.id = u"heartgram/update_notes"_q,
		.title = rpl::single(Extera::Text(
			u"Что нового"_q,
			u"What is new"_q)),
		.icon = { &st::menuIconInfo },
		.onClick = [] { Extera::Update::OpenRelease(); },
		.shown = Extera::Update::AvailableValue(),
	});
	const auto addSwitch = [&](const QString &key, const QString &title) {
		const auto button = builder.addButton({
			.id = u"heartgram/"_q + key,
			.title = rpl::single(title),
			.st = &st::settingsButtonNoIcon,
			.toggled = rpl::single(Extera::Bool(key)),
		});
		if (button) {
			button->toggledValue(
			) | rpl::on_next([=](bool value) {
				Extera::Config::Instance().setBool(key, value);
			}, button->lifetime());
		}
	};
	addSwitch(
		u"update_prerelease"_q,
		Extera::Text(
			u"Предварительные версии"_q,
			u"Pre-releases"_q));
	addSwitch(
		u"update_auto"_q,
		Extera::Text(
			u"Проверять автоматически"_q,
			u"Check automatically"_q));
}

void BuildCategories(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::ayu_CategoriesHeader());

	if (!HeartGram::IsLite()) {
		builder.addSectionButton({
			.title = rpl::single(Extera::Text(u"Призрак и шпион"_q, u"Ghost & Spy"_q)),
			.targetSection = AyuGhost::Id(),
			.icon = { &st::menuIconGroupReactions },
		});
		builder.addSectionButton({
			.title = tr::ayu_CategoryFilters(),
			.targetSection = AyuFilters::Id(),
			.icon = { &st::menuIconTagFilter },
		});
	}
	builder.addSectionButton({
		.title = tr::ayu_CategoryGeneral(),
		.targetSection = AyuGeneral::Id(),
		.icon = { &st::menuIconShowAll },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryAppearance(),
		.targetSection = AyuAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});
	builder.addSectionButton({
		.title = tr::ayu_CategoryChats(),
		.targetSection = AyuChats::Id(),
		.icon = { &st::menuIconChatBubble },
	});
	BuildExteraCategories(builder);
	builder.addSectionButton({
		.title = tr::ayu_CategoryOther(),
		.targetSection = AyuOther::Id(),
		.icon = { &st::menuIconFave },
	});
}

void BuildLinks(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::ayu_LinksHeader());

	const auto controller = builder.controller();

	builder.addButton({
		.id = u"heartgram/repository"_q,
		.title = rpl::single(Extera::Text(u"Репозиторий"_q, u"Repository"_q)),
		.icon = { &st::menuIconIpAddress },
		.label = rpl::single(QString("GitHub")),
		.onClick = [=] {
			QDesktopServices::openUrl(
				QString("https://github.com/whyheartone/HeartGramDesktop"));
		},
	});

	if (!HeartGram::IsLite()) {
		builder.addSkip();
		builder.addSubsectionTitle(rpl::single(Extera::Text(
			u"Основано на AyuGram"_q,
			u"Based on AyuGram"_q)));
		builder.addButton({
			.id = u"ayu/channel"_q,
			.title = tr::ayu_LinksChannel(),
			.icon = { &st::menuIconChannel },
			.label = rpl::single(QString("@ayugram")),
			.onClick = [=] {
				controller->showPeerByLink(Window::PeerByLinkInfo{
					.usernameOrId = QString("ayugram"),
				});
			},
		});
	}

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuMain::Id(),
	.parentId = MainId(),
	.title = &tr::ayu_AyuPreferences,
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);
	builder.addSkip();
	BuildVersionInfo(builder);
	BuildUpdates(builder);
	BuildCategories(builder);
	BuildLinks(builder);
});

} // namespace

rpl::producer<QString> AyuMain::title() {
	return rpl::single(QString(""));
}

AyuMain::AyuMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuMainId() {
	return AyuMain::Id();
}

} // namespace Settings
