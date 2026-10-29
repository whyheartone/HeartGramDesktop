#include "core/launcher.h"
#include "extera/extera_welcome.h"
#include "ayu/features/heartgram_lite.h"

#include "base/call_delayed.h"
#include "core/application.h"
#include "core/version.h"
#include "extera/extera_config.h"
#include "extera/extera_text.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/basic_click_handlers.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Extera {
namespace {

rpl::lifetime &Lifetime() {
	static auto result = rpl::lifetime();
	return result;
}

void AddPoint(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st::boxLabel),
		st::exteraWelcomePointPadding);
}

void AddLinkButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		const QString &label,
		const QString &url,
		const style::icon *icon) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		std::move(title),
		st::settingsButton));
	Settings::AddButtonIcon(button, st::settingsButton, { icon });
	Settings::CreateRightLabel(
		button,
		label,
		st::settingsButton,
		rpl::single(QString()));
	button->setClickedCallback([=] { UrlClickHandler::Open(url); });
}

} // namespace

void FillUnofficialInfo(not_null<Ui::VerticalLayout*> container) {
	const auto lite = HeartGram::IsLite();
	AddPoint(container, rpl::single(lite
		? Text(
			u"HeartGram — клиент Telegram с плагинами и функциями exteraGram для Windows. Он неофициальный: командами Telegram и exteraGram не разрабатывается и не поддерживается."_q,
			u"HeartGram is a Telegram client with plugins and exteraGram features for Windows. It is unofficial and is not developed or supported by the Telegram or exteraGram teams."_q)
		: Text(
			u"HeartGram — неофициальная сборка AyuGram Desktop с функциями exteraGram для Windows. Командами AyuGram и exteraGram она не разрабатывается и не поддерживается."_q,
			u"HeartGram is an unofficial AyuGram Desktop build with exteraGram features for Windows. It is not developed or supported by the AyuGram or exteraGram teams."_q)));
	AddPoint(container, rpl::single(lite
		? Text(
			u"Открытый исходный код, лицензия GPL-3.0, порт exteraDesktop (@metawka). Сборка и изменения: @whyheartone."_q,
			u"Open source, GPL-3.0, based on the exteraDesktop port (@metawka). Build and changes: @whyheartone."_q)
		: Text(
			u"Основа: AyuGram Desktop (Radolyn Labs) и порт exteraDesktop (@metawka), лицензия GPL-3.0. Сборка и изменения: @whyheartone."_q,
			u"Based on AyuGram Desktop (Radolyn Labs) and the exteraDesktop port (@metawka), GPL-3.0. Build and changes: @whyheartone."_q)));
	Ui::AddSkip(container);
	AddLinkButton(
		container,
		rpl::single(Text(u"Разработчик"_q, u"Developer"_q)),
		u"@whyheartone"_q,
		u"https://t.me/whyheartone"_q,
		&st::exteraIconDeveloper);
	if (!lite) {
		AddLinkButton(
			container,
			rpl::single(Text(u"Основа: AyuGram"_q, u"Based on AyuGram"_q)),
			u"@ayugram"_q,
			u"https://t.me/ayugram"_q,
			&st::exteraIconChannel);
	}
	AddLinkButton(
		container,
		rpl::single(Text(u"Порт exteraGram"_q, u"exteraGram port"_q)),
		u"@metawka"_q,
		u"https://t.me/metawka"_q,
		&st::exteraIconDeveloper);
}

void ShowWelcomeBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		const auto size = st::exteraHeaderLogoSize;
		const auto logo = content->add(
			object_ptr<Ui::FixedHeightWidget>(content, size),
			st::exteraHeaderLogoPadding);
		logo->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(logo);
			auto hq = PainterHighQualityEnabler(p);
			p.drawImage(
				QRect((logo->width() - size) / 2, 0, size, size),
				Window::Logo());
		}, logo->lifetime());
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(QString(u"HeartGram"_q)),
				st::exteraHeaderTitle),
			st::exteraHeaderTitlePadding,
			style::al_top);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(Text(
					u"Неофициальная сборка"_q,
					u"Unofficial build"_q)),
				st::exteraWelcomeBadge),
			st::exteraHeaderVersionPadding,
			style::al_top);
		FillUnofficialInfo(content);
		box->addButton(tr::lng_extera_welcome_continue(), [=] {
			box->closeBox();
		});
	}));
}

void StartWelcome() {
	static auto started = false;
	if (started) {
		return;
	}
	started = true;
	Core::App().domain().activeSessionValue(
	) | rpl::filter([](Main::Session *session) {
		return session != nullptr;
	}) | rpl::take(1) | rpl::on_next([](Main::Session *session) {
		const auto forced = Core::Launcher::Instance().arguments().contains(
			u"-welcome"_q);
		if (Bool(u"welcome_shown_heartgram"_q) && !forced) {
			return;
		}
		base::call_delayed(1500, [] {
			const auto window = Core::App().activePrimaryWindow();
			const auto controller = window
				? window->sessionController()
				: nullptr;
			if (!controller) {
				return;
			}
			Config::Instance().setBool(u"welcome_shown_heartgram"_q, true);
			ShowWelcomeBox(controller);
		});
	}, Lifetime());
}

} // namespace Extera
