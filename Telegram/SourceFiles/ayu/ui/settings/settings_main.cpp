// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
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
				rpl::single(HeartGram::IsLite()
					? Extera::Text(
						u"Клиент Telegram с плагинами и функциями exteraGram. Сборка и изменения: @whyheartone."_q,
						u"Telegram client with plugins and exteraGram features. Build and changes by @whyheartone."_q)
					: Extera::Text(
						u"Неофициальная сборка AyuGram Desktop с функциями exteraGram. Сборка и изменения: @whyheartone."_q,
						u"Unofficial AyuGram Desktop build with exteraGram features. Build and changes by @whyheartone."_q)),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
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
		.id = u"heartgram/developer"_q,
		.title = rpl::single(Extera::Text(u"Разработчик"_q, u"Developer"_q)),
		.icon = { &st::menuIconProfile },
		.label = rpl::single(QString("@whyheartone")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("whyheartone"),
			});
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
		builder.addButton({
			.id = u"ayu/chat"_q,
			.title = tr::ayu_LinksChats(),
			.icon = { &st::menuIconChats },
			.label = rpl::single(QString("@ayugramchat")),
			.onClick = [=] {
				controller->showPeerByLink(Window::PeerByLinkInfo{
					.usernameOrId = QString("ayugramchat"),
				});
			},
		});
		builder.addButton({
			.id = u"ayu/website"_q,
			.title = tr::ayu_LinksDocumentation(),
			.icon = { &st::menuIconIpAddress },
			.label = rpl::single(QString("docs.ayugram.one")),
			.onClick = [=] {
				QDesktopServices::openUrl(
					QString("https://docs.ayugram.one"));
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
