/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_music_common.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_saved_music.h"
#include "data/data_saved_sublist.h"
#include "history/history_item.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_music_button.h"
#include "info/saved/info_saved_music_widget.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"

// AyuGram includes
#include "logs.h"
#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "media/player/media_player_instance.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ui/components/saved_music.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_document.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "ui/widgets/popup_menu.h"


namespace Info::Saved {

namespace {

[[nodiscard]] Profile::MusicButtonData DocumentMusicButtonData(
		not_null<DocumentData*> document, not_null<HistoryItem*> message) {
	const auto name = Ui::Text::FormatSongNameFor(document);

	return {
		.name = name,
		.title = name.composedName().title,
		.performer = name.composedName().performer,
		.msgId = message->fullId(),
		.mediaView = document->createMediaView(),
	};
}

} // namespace

int MusicRowBottomSkip() {
	return AyuUiSettings::isMaterialSettings()
		? style::ConvertScale(2)
		: st::infoMusicButtonPadding.left();
}

int MusicRowSide() {
	return AyuUiSettings::isMaterialSettings()
		? style::ConvertScale(8)
		: st::infoMusicButtonPadding.left();
}

int MusicRowRadius() {
	return AyuUiSettings::isMaterialSettings()
		? style::ConvertScale(18)
		: AyuUiSettings::dialogRadius(st::boxRadius);
}

rpl::producer<bool> SetupSavedMusic(
		not_null<Ui::VerticalLayout*> container,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer,
		rpl::producer<std::optional<QColor>> topBarColor) {
	const auto hasMusic = container->lifetime().make_state<
		rpl::variable<bool>>(false);
	auto musicValue = Data::SavedMusic::Supported(peer->id)
		? rpl::merge(
			rpl::single(rpl::empty_value()),
			Data::SavedMusicList(
				peer,
				nullptr,
				1
			) | rpl::to_empty,
			peer->owner().savedMusic().changed(
			) | rpl::filter(
				rpl::mappers::_1 == peer->id
			) | rpl::to_empty
		) | rpl::map([=]() -> HistoryItem* {
			const auto &list = peer->owner().savedMusic().list(peer->id);
			return list.empty() ? nullptr : list.front().get();
		}) | rpl::distinct_until_changed() | rpl::type_erased
		: rpl::single<HistoryItem*>((HistoryItem*)(nullptr));

	const auto divider = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));

	divider->toggle(false, anim::type::instant);
	rpl::combine(
		std::move(musicValue),
		std::move(topBarColor)
	) | rpl::on_next([=](
			HistoryItem *item,
			std::optional<QColor> color) {
		// WHY: the previous button stays until the new one is ready, so the
		// row does not collapse and reopen as a hole while a cover loads.
		auto olds = std::make_shared<std::vector<QPointer<QWidget>>>();
		for (auto i = 0; i != divider->entity()->count(); ++i) {
			olds->push_back(divider->entity()->widgetAt(i));
		}
		const auto dropOlds = [=] {
			for (const auto &old : *olds) {
				if (const auto widget = old.data()) {
					delete widget;
				}
			}
			olds->clear();
		};
		auto shown = false;
		LOG(("Music: update for peer %1, supported %2, item %3, list size %4.").arg(peer->id.value).arg(Data::SavedMusic::Supported(peer->id) ? 1 : 0).arg(item ? 1 : 0).arg(peer->owner().savedMusic().list(peer->id).size()));
		if (item) {
			if (const auto document = item->media()
					? item->media()->document()
					: nullptr) {
				const auto side = MusicRowSide();
				const auto skip = st::infoMusicButtonPadding.top();
				auto musicButton = divider->entity()->add(object_ptr<Ui::SlideWrap<Profile::AyuMusicButton>>(
					divider->entity(),
					object_ptr<Profile::AyuMusicButton>(
						divider->entity(),
						DocumentMusicButtonData(document, item),
						color,
						[] {
						})),
					style::margins(side, skip, side, MusicRowBottomSkip()));

				musicButton->hide(anim::type::instant);
				musicButton->ease = anim::easeOutCubic;
				musicButton->setDuration(250);
				musicButton->entity()->setAcceptBoth(true);
				// WHY: tap plays the track, right click opens the whole list.
				musicButton->entity()->clicks(
				) | rpl::on_next([=](Qt::MouseButton mouseButton) {
					if (mouseButton == Qt::RightButton) {
						controller->showSection(Info::Saved::MakeMusic(peer));
					} else if (mouseButton == Qt::LeftButton) {
						::Media::Player::instance()->playPause(
							AudioMsgId(document, item->fullId()));
					}
				}, musicButton->lifetime());

				const auto weak = base::make_weak(musicButton);
				musicButton->entity()->onReady() | rpl::on_next(
					[=]
					{
						// fix animation glitch
						dispatchToMainThread(
							[=]
							{
								if (const auto strong = weak.get()) {
									dropOlds();
									strong->show(anim::type::normal);
									divider->toggle(true, anim::type::normal);
								}
							},
							st::widgetFadeDuration);
					},
					musicButton->lifetime());

				shown = true;
			}
		}
		if (!shown) {
			dropOlds();
		}
		*hasMusic = shown;
	}, container->lifetime());

	return hasMusic->value();
}

} // namespace Info::Saved
