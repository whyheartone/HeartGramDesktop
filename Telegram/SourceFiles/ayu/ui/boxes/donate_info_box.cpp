// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/donate_info_box.h"

#include "lang_auto.h"
#include "ayu/utils/rc_manager.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "extera/extera_text.h"
#include "window/window_controller.h"

#include <QSvgRenderer>

namespace Ui {
namespace {

QImage MakeSupportLogo(bool project = false) {
	const auto s = Size(st::supportLogoSize);
	if (project) {
		auto logo = QImage(u":/gui/art/ayu/default/app.png"_q).scaled(
			s * style::DevicePixelRatio(),
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		logo.setDevicePixelRatio(style::DevicePixelRatio());
		return logo;
	}
	auto svg = QSvgRenderer(QString(":/gui/icons/ayu/donates/support_logo.svg"));
	auto image = QImage(
		s * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		svg.render(&p, Rect(s));
	}
	return image;
}

object_ptr<Ui::RpWidget> CreateTopLogoWidget(
	not_null<Ui::RpWidget*> parent,
	bool project = false) {
	auto w = object_ptr<Ui::RpWidget>(parent);
	const auto raw = w.data();

	const auto logo = MakeSupportLogo(project);

	raw->paintRequest(
	) | rpl::on_next(
		[=](QRect)
		{
			QPainter p(raw);
			PainterHighQualityEnabler hq(p);

			const auto original = logo.size() / style::DevicePixelRatio();
			const auto maxWidth = raw->width() - rect::m::sum::h(st::boxRowPadding);
			const auto maxHeight = raw->height();
			if (original.isEmpty() || maxWidth <= 0 || maxHeight <= 0) {
				return;
			}
			const auto scale = std::min(
				double(maxWidth) / double(original.width()),
				double(maxHeight) / double(original.height()));
			const auto target = QSize(
				int(original.width() * scale),
				int(original.height() * scale));
			const auto x = (raw->width() - target.width()) / 2;
			const auto y = (raw->height() - target.height()) / 2;
			const auto rect = QRect(QPoint(x, y), target);
			p.drawImage(rect, logo);
		},
		raw->lifetime());

	return w;
}

object_ptr<Ui::RpWidget> InfoRow(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Session*> session,
	const QString &title,
	const TextWithEntities &text,
	not_null<const style::icon*> icon,
	const Text::MarkedContext &context) {
	auto row = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = row.data();

	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			rpl::single(tr::bold(title)),
			st::defaultFlatLabel),
		st::settingsPremiumRowTitlePadding);

	const auto label = raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			st::boxDividerLabel),
		st::settingsPremiumRowAboutPadding);

	label->setMarkedText(
		text,
		std::move(context)
	);

	object_ptr<Info::Profile::FloatingIcon>(
		raw,
		*icon,
		st::starrefInfoIconPosition);

	return row;
}

} // namespace

void FillDonateInfoBox(not_null<Ui::GenericBox*> box, not_null<Window::SessionController*> controller) {
	// box->setStyle(st::starrefFooterBox);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(int(st::aboutWidth * 1.1));
	box->verticalLayout()->resizeToWidth(box->width());

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	const auto logoWidget = box->verticalLayout()->add(
		CreateTopLogoWidget(box->verticalLayout()));

	logoWidget->resize(st::supportLogoSize, st::supportLogoSize);

	Ui::AddSkip(box->verticalLayout());

	box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box->verticalLayout(),
			tr::ayu_SupportBoxHeader(tr::bold),
			st::boxTitle),
		st::boxRowPadding,
		style::al_top);

	box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box->verticalLayout(),
			tr::ayu_SupportBoxInfo(),
			st::starrefCenteredText),
		st::boxRowPadding);

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	auto emojiHelper = Ui::Text::CustomEmojiHelper();
	const auto tonSymbol = emojiHelper.paletteDependent({
		.factory = [=]
		{
			return Ui::Earn::IconCurrencyColored(
				st::boxDividerLabel.style.font,
				st::boxDividerLabel.textFg->c);
		},
		.margin = st::channelEarnCurrencyLearnMargins
	});

	const auto dollarAmount = RCManager::getInstance().donateAmountUsd().prepend("$");
	const auto tonAmount = RCManager::getInstance().donateAmountTon();
	const auto rubleAmount = RCManager::getInstance().donateAmountRub().append("₽");

	const auto innerText = TextWithEntities{}.append(tonSymbol).append(tonAmount).append(", ").append(rubleAmount);
	const auto str = tr::ayu_SupportBoxMakeDonationInfo(
		tr::now,
		lt_amount1,
		TextWithEntities{dollarAmount},
		lt_amount2,
		innerText,
		tr::rich
	);

	box->verticalLayout()->add(InfoRow(
		box->verticalLayout(),
		&controller->session(),
		tr::ayu_SupportBoxMakeDonationHeader(tr::now),
		str,
		&st::menuIconEarn,
		emojiHelper.context()));

	Ui::AddSkip(box->verticalLayout());

	const auto username = RCManager::getInstance().donateUsername();
	auto usernameTrimmed = username;
	if (usernameTrimmed.startsWith('@')) {
		usernameTrimmed.remove(0, 1);
	}
	const TextWithEntities proofText = tr::ayu_SupportBoxSendProofInfo(
		tr::now,
		lt_item,
		Ui::Text::Link(username, controller->session().createInternalLinkFull(usernameTrimmed)),
		tr::rich);
	box->verticalLayout()->add(InfoRow(
		box->verticalLayout(),
		&controller->session(),
		tr::ayu_SupportBoxSendProofHeader(tr::now),
		proofText,
		&st::menuIconPhoto,
		Core::TextContext({
			.session = std::move(&controller->session()),
		})));

	Ui::AddSkip(box->verticalLayout());

	box->verticalLayout()->add(InfoRow(
		box->verticalLayout(),
		&controller->session(),
		tr::ayu_SupportBoxReceiveBadgeHeader(tr::now),
		TextWithEntities{
			tr::ayu_SupportBoxReceiveBadgeInfo(tr::now)
		},
		&st::menuIconStarRefShare,
		Core::TextContext({
			.session = std::move(&controller->session()),
		})));

	const auto closeButton = box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::starrefFooterBox.buttonPadding);
	closeButton->widthValue() | rpl::filter([=]
	{
		return (closeButton->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=]
							  {
								  closeButton->resizeToWidth(buttonWidth);
							  },
							  closeButton->lifetime());
}

} // namespace Ui

namespace Ui {

void FillProjectDonateInfoBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QString &rubles,
		const QString &ton,
		const QString &contact) {
	const auto session = &controller->session();
	const auto content = box->verticalLayout();
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(int(st::aboutWidth * 1.1));
	content->resizeToWidth(box->width());
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	const auto logo = content->add(CreateTopLogoWidget(content, true));
	logo->resize(st::supportLogoSize, st::supportLogoSize);
	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(tr::bold(Extera::Text(
				u"Поддержать HeartGram"_q,
				u"Support HeartGram"_q))),
			st::boxTitle),
		st::boxRowPadding,
		style::al_top);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(Extera::Text(
				u"Донат идёт на развитие клиента. Тем, кто поддержал, выдаётся уникальный значок рядом с именем."_q,
				u"Donations fund the client. Supporters get a unique badge next to their name."_q)),
			st::starrefCenteredText),
		st::boxRowPadding);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	auto emojiHelper = Ui::Text::CustomEmojiHelper();
	const auto tonSymbol = emojiHelper.paletteDependent({
		.factory = [=] {
			return Ui::Earn::IconCurrencyColored(
				st::boxDividerLabel.style.font,
				st::boxDividerLabel.textFg->c);
		},
		.margin = st::channelEarnCurrencyLearnMargins,
	});
	auto sum = TextWithEntities();
	if (rubles.isEmpty()) {
		sum.append(Extera::Text(u"любую сумму"_q, u"any amount"_q));
	} else {
		sum.append(rubles);
		if (!ton.isEmpty()) {
			sum.append(u" ("_q).append(tonSymbol).append(ton).append(u")"_q);
		}
	}
	auto donation = TextWithEntities();
	donation.append(Extera::Text(u"Переведите "_q, u"Send "_q));
	donation.append(sum);
	donation.append(Extera::Text(
		u" на любой из реквизитов: они в настройках HeartGram, раздел «Другое»."_q,
		u" to any of the wallets listed in HeartGram settings, under Other."_q));
	content->add(InfoRow(
		content,
		session,
		Extera::Text(u"Сделать донат"_q, u"Make a donation"_q),
		donation,
		&st::menuIconEarn,
		emojiHelper.context()));
	Ui::AddSkip(content);

	auto username = contact.isEmpty() ? u"@whyheartone"_q : contact;
	if (!username.startsWith('@')) {
		username.prepend('@');
	}
	auto receiptLink = Ui::Text::Link(
		username,
		session->createInternalLinkFull(username.mid(1)));
	receiptLink.entities.push_back(EntityInText(
		EntityType::Bold,
		0,
		int(receiptLink.text.size())));
	content->add(InfoRow(
		content,
		session,
		Extera::Text(u"Отправить чек"_q, u"Send the receipt"_q),
		TextWithEntities()
			.append(Extera::Text(
				u"Пришлите скриншот перевода в личные сообщения "_q,
				u"Send a screenshot of the transfer in a private message to "_q))
			.append(std::move(receiptLink))
			.append(Extera::Text(
				u". Должны быть видны сумма, дата и время."_q,
				u". The amount, date and time must be visible."_q)),
		&st::menuIconPhoto,
		Core::TextContext({ .session = session })));
	Ui::AddSkip(content);

	content->add(InfoRow(
		content,
		session,
		Extera::Text(u"Получить значок"_q, u"Get the badge"_q),
		TextWithEntities{ Extera::Text(
			u"Когда платёж проверен, значок появится в вашем профиле и у имени в чатах. Его увидят другие пользователи HeartGram."_q,
			u"Once the payment is checked, the badge shows in your profile and next to your name in chats. Other HeartGram users will see it."_q) },
		&st::menuIconStarRefShare,
		Core::TextContext({ .session = session })));

	const auto close = box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::starrefFooterBox.buttonPadding);
	close->widthValue() | rpl::filter([=] {
		return (close->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=] {
		close->resizeToWidth(buttonWidth);
	}, close->lifetime());
}

} // namespace Ui
