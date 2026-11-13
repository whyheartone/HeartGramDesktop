/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_inner_widget.h"
#include "extera/extera_streamer.h"

#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_wrap_widget.h"
#include "info/profile/info_profile_widget.h"
#include "info/profile/tabs/adapters/info_profile_tab_chats.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "info/profile/tabs/adapters/info_profile_tab_members.h"
#include "info/profile/tabs/adapters/info_profile_tab_peer_lists.h"
#include "info/profile/tabs/adapters/info_profile_tab_polls.h"
#include "info/profile/tabs/adapters/info_profile_tab_saved.h"
#include "info/profile/tabs/adapters/info_profile_tab_stories.h"
#include "info/profile/tabs/info_profile_tabs_host.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_members.h"
#include "info/profile/info_profile_shared_media_classic.h"
#include "info/profile/info_profile_top_bar.h"
#include "info/profile/info_profile_actions.h"
#include "info/profile/info_profile_values.h"
#include "info/saved/info_saved_music_widget.h"
#include "info/saved/info_saved_music_common.h"
#include "info/profile/info_profile_balance_pill.h"
#include "wallet/wallet_card_view.h"
#include "window/window_session_controller.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_file_origin.h"
#include "data/data_user.h"
#include "data/data_user_wallet_card.h"
#include "data/data_saved_sublist.h"
#include "info_profile_actions.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "lang/lang_keys.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/color_contrast.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"
#include "styles/style_info_profile_top_bar.h"
#include "styles/style_layers.h"

// AyuGram includes
#include "logs.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "settings/settings_tiles.h"
#include "extera/extera_streamer.h"


namespace Info {
namespace Profile {

namespace {

constexpr auto kMembersInlineMax = 5;

[[nodiscard]] rpl::producer<bool> MembersInTabValue(not_null<PeerData*> peer) {
	const auto channel = peer->asChannel();
	return MembersCountValue(
		peer
	) | rpl::filter([=](int count) {
		return (count > 0) && (!channel || channel->membersCountKnown());
	}) | rpl::map([](int count) {
		return (count > kMembersInlineMax);
	}) | rpl::take(1);
}

void AddMusicAndWallet(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Controller*> controller,
		not_null<PeerData*> musicPeer,
		UserData *walletUser,
		rpl::producer<std::optional<QColor>> topBarColor) {
	const auto padding = st::infoMusicButtonPadding;
	const auto side = padding.left();
	const auto skip = padding.top();

	struct State {
		rpl::variable<bool> hasMusic;
		rpl::variable<bool> hasWallet;
		std::optional<QColor> edge;
		int walletNatural = 0;
		bool relaying = false;
	};
	const auto row = layout->add(object_ptr<Ui::RpWidget>(layout));
	const auto state = row->lifetime().make_state<State>();
	const auto music = Ui::CreateChild<Ui::VerticalLayout>(row);
	const auto showMusic = !(musicPeer->isSelf()
		&& Extera::Streamer::HideProfile());
	if (showMusic) {
		state->hasMusic = Info::Saved::SetupSavedMusic(
			music,
			controller,
			musicPeer,
			rpl::duplicate(topBarColor));
	}

	Ui::RpWidget *wallet = nullptr;
	if (walletUser) {
		wallet = Ui::CreateChild<Ui::RpWidget>(row);
		wallet->hide();
		const auto pill = Ui::CreateChild<BalancePill>(
			wallet,
			walletUser,
			rpl::duplicate(topBarColor));
		pill->setFlat(true);
		pill->setOpacity(1.);
		const auto window = controller->parentController();
		const auto open = [=] {
			Wallet::ShowCardView(window, walletUser, pill);
		};
		pill->setClickedCallback(open);
		wallet->setCursor(style::cur_pointer);
		wallet->events() | rpl::filter([=](not_null<QEvent*> e) {
			return e->type() == QEvent::MouseButtonRelease
				&& static_cast<QMouseEvent*>(e.get())->button()
					== Qt::LeftButton;
		}) | rpl::on_next([=] {
			open();
		}, wallet->lifetime());

		wallet->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(wallet);
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = float64(
				AyuUiSettings::dialogRadius(st::boxRadius));
			p.setPen(Qt::NoPen);
			p.setBrush(state->edge
					? (Ui::IsLightBackground(*state->edge)
						? Ui::BlendColors(
							*state->edge,
							Qt::black,
							st::infoProfileTopBarActionButtonBgOpacity)
						: Ui::BlendColors(*state->edge, Qt::white, 0.82))
					: st::shadowFg->c);
			p.drawRoundedRect(QRectF(wallet->rect()), radius, radius);
		}, wallet->lifetime());

		pill->naturalWidthValue() | rpl::on_next([=](int width) {
			state->walletNatural = width;
			row->resizeToWidth(row->width());
		}, wallet->lifetime());
		wallet->sizeValue() | rpl::on_next([=](QSize size) {
			pill->resizeToNaturalWidth(size.width() - 2 * 6);
			if (pill->fittedWidth() < pill->width()) {
				pill->resizeToWidth(pill->fittedWidth());
			}
			pill->move(
				(size.width() - pill->width()) / 2,
				(size.height() - pill->height()) / 2);
		}, wallet->lifetime());

		state->hasWallet = walletUser->session().changes().peerFlagsValue(
			walletUser,
			Data::PeerUpdate::Flag::WalletCard
		) | rpl::map([=] {
			const auto card = walletUser->walletCard();
			return card && (card->balance > 0);
		}) | rpl::distinct_until_changed();
	}

	LOG(("Profile: row created, peer %1, self %2, showMusic %3, wallet %4.").arg(musicPeer->id.value).arg(musicPeer->isSelf() ? 1 : 0).arg(showMusic ? 1 : 0).arg(walletUser ? 1 : 0));
	const auto relayout = [=] {
		const auto width = row->width();
		const auto hasMusic = state->hasMusic.current();
		const auto hasWallet = wallet && state->hasWallet.current();
		static auto relayouts = 0;
		if (relayouts < 40) {
			++relayouts;
			LOG(("Profile: relayout width %1, music %2 (h %3), wallet %4 (natural %5).").arg(width).arg(hasMusic ? 1 : 0).arg(music->height()).arg(hasWallet ? 1 : 0).arg(state->walletNatural));
		}
		const auto walletWidth = hasWallet
			? std::min(
				state->walletNatural + 2 * side,
				std::max(width / 2 - side, 0))
			: 0;
		const auto musicWidth = width
			- (hasWallet && hasMusic ? (walletWidth + side) : 0);
		music->resizeToWidth(musicWidth);
		music->move(0, 0);
		const auto musicHeight = hasMusic ? music->height() : 0;
		const auto walletHeight = (hasMusic
			? std::max(musicHeight - skip - side, 0)
			: (padding.top()
				+ st::infoProfileBalancePillHeight
				+ padding.bottom()));
		if (wallet) {
			wallet->setVisible(hasWallet);
			if (hasWallet) {
				const auto fullWidth = hasMusic
					? walletWidth
					: (width - 2 * side);
				wallet->setGeometry(
					hasMusic ? (width - side - walletWidth) : side,
					skip,
					fullWidth,
					walletHeight);
			}
		}
		const auto height = hasMusic
			? musicHeight
			: hasWallet
			? (skip + walletHeight + side)
			: 0;
		row->resize(width, height);
	};
	row->widthValue() | rpl::on_next([=](int) {
		relayout();
	}, row->lifetime());
	rpl::merge(
		state->hasMusic.value() | rpl::to_empty,
		state->hasWallet.value() | rpl::to_empty,
		music->heightValue() | rpl::to_empty
	) | rpl::on_next([=] {
		relayout();
	}, row->lifetime());

	std::move(topBarColor) | rpl::on_next([=](
			std::optional<QColor> color) {
		state->edge = color;
		if (wallet) {
			wallet->update();
		}
	}, row->lifetime());
}

void AddUnofficialSecurityRiskWarning(
		not_null<Ui::VerticalLayout*> layout,
		not_null<UserData*> user) {
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			object_ptr<Ui::VerticalLayout>(layout)));
	const auto content = wrap->entity();
	user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::on_next([=] {
		while (content->count()) {
			delete content->widgetAt(0);
		}
		if (user->unofficialSecurityRisk()) {
			auto helper = Ui::Text::CustomEmojiHelper();
			auto icon = helper.paletteDependent(Ui::AttentionMarkEmoji());
			auto label = object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_profile_unofficial_warning(
					lt_icon,
					rpl::single(std::move(icon)),
					lt_name,
					rpl::single(TextWithEntities{ user->firstName }),
					tr::marked),
				st::defaultDividerLabel.label,
				st::defaultPopupMenu,
				helper.context([=] { content->update(); }));
			content->add(object_ptr<Ui::DividerLabel>(
				content,
				std::move(label),
				st::defaultBoxDividerLabelPadding,
				st::defaultDividerLabel.bar,
				RectParts()));
		}
		content->resizeToWidth(content->width());
	}, content->lifetime());
	using namespace rpl::mappers;
	wrap->toggleOn(
		content->heightValue() | rpl::map(_1 > 0),
		anim::type::instant);
}

} // namespace

void AddMusicAndWalletRow(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Controller*> controller,
		not_null<PeerData*> musicPeer,
		UserData *walletUser,
		rpl::producer<std::optional<QColor>> topBarColor) {
	AddMusicAndWallet(
		layout,
		controller,
		musicPeer,
		walletUser,
		std::move(topBarColor));
}

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	Origin origin)
: RpWidget(parent)
, _controller(controller)
, _peer(_controller->key().peer())
, _migrated(_controller->migrated())
, _topic(_controller->key().topic())
, _sublist(_controller->key().sublist())
, _savedMessages(_controller->key().savedMessages() != nullptr)
, _content(setupContent(this, origin)) {
	::Settings::AttachTiles(_content.data());
	_content->heightValue(
	) | rpl::on_next([this](int height) {
		if (!_inResize) {
			resizeToWidth(width());
			updateDesiredHeight();
		}
	}, lifetime());
}

rpl::producer<> InnerWidget::backRequest() const {
	return _backClicks.events();
}

object_ptr<Ui::RpWidget> InnerWidget::setupContent(
		not_null<RpWidget*> parent,
		Origin origin) {
	if (const auto user = _peer->asUser()) {
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::on_next([=] {
			const auto &photos = user->session().api().peerPhoto();
			if (const auto original = photos.nonPersonalPhoto(user)) {
				// Preload it for the edit contact box.
				_nonPersonalView = original->createMediaView();
				const auto id = peerToUser(user->id);
				original->load(Data::FileOriginFullUser{ id });
			}
		}, lifetime());
	}

	auto result = object_ptr<Ui::VerticalLayout>(parent);

	if (!_savedMessages
		&& !(_peer->isSelf() && Extera::Streamer::HideProfile())) {
		const auto musicPeer = _sublist
			? _sublist->sublistPeer().get()
			: _peer.get();
		AddMusicAndWallet(
			result.data(),
			_controller,
			musicPeer,
			(_peer->asUser() && !_topic)
				? _peer->asUser()
				: nullptr,
			_topBarColor.value());
		if (const auto user = _peer->asUser()) {
			AddUnofficialSecurityRiskWarning(result.data(), user);
		}
	}

	auto stack = SectionStack(result.data());
	if (_topic && _topic->creating()) {
		stack.finalize();
		return result;
	}

	if (!_savedMessages) {
		BuildProfileDetailsSections(
			stack,
			_controller,
			_peer,
			_topic,
			_sublist,
			origin);
	}

	const auto thirdColumn = (_controller->wrap() == Wrap::Side);
	const auto tabs = UseProfileMediaTabs() && !thirdColumn;
	auto sharedTracker = Ui::MultiSlideTracker();
	if (!tabs) {
		auto sharedMediaWidget = SetupSharedMediaClassic(
			result.data(),
			_controller,
			_peer,
			_topic,
			_sublist,
			_migrated,
			sharedTracker);
		const auto raw = sharedMediaWidget.data();
		_sharedMediaWrap = raw;
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(sharedMediaWidget),
			.shown = raw->toggledValue(),
		});
	}

	const auto addTabsHost = [&] {
		using namespace rpl::mappers;
		const auto tabsPeer = _sublist ? _sublist->sublistPeer() : _peer;
		const auto topicRootId = _topic ? _topic->rootId() : MsgId();
		const auto monoforumPeerId = _sublist
			? _sublist->sublistPeer()->id
			: PeerId();
		auto tabs = std::vector<MediaTabDescriptor>();
		const auto countValue = [&](Storage::SharedMediaType type) {
			return SharedMediaCountValue(
				_peer,
				topicRootId,
				monoforumPeerId,
				_migrated,
				type);
		};
		const auto addTab = [&](Storage::SharedMediaType type) {
			tabs.push_back(MakeMediaTabDescriptor(
				type,
				countValue(type) | rpl::map(_1 > 0)));
		};
		const auto addMediaTabs = [&] {
			using Type = Storage::SharedMediaType;
			tabs.push_back(MakeMediaTabDescriptor(
				Type::PhotoVideo,
				rpl::combine(
					MediaTabsExpandedValue(),
					countValue(Type::Photo),
					countValue(Type::Video)
				) | rpl::map([](bool expanded, int photos, int videos) {
					return !expanded && (photos + videos > 0);
				})));
			const auto addSplit = [&](Type type) {
				tabs.push_back(MakeMediaTabDescriptor(
					type,
					rpl::combine(
						MediaTabsExpandedValue(),
						countValue(type)
					) | rpl::map([](bool expanded, int count) {
						return expanded && (count > 0);
					})));
			};
			addSplit(Type::Photo);
			addSplit(Type::Video);
		};
		if (_savedMessages) {
			tabs.push_back(MakeChatsTabDescriptor());
		}
		if (!_topic && !_savedMessages) {
			tabs.push_back(MakeStoriesTabDescriptor(tabsPeer));
			if (!_sublist) {
				tabs.push_back(MakeGiftsTabDescriptor(_peer));
			}
		}
		if ((_peer->isChat() || _peer->isMegagroup())
			&& !_peer->isMonoforum()
			&& !_topic
			&& !_sublist) {
			tabs.push_back(MakeMembersTabDescriptor(
				_peer,
				MembersInTabValue(_peer)));
		}
		addMediaTabs();
		if (!_topic && !_savedMessages) {
			tabs.push_back(MakeSavedTabDescriptor(tabsPeer));
		}
		addTab(Storage::SharedMediaType::File);
		addTab(Storage::SharedMediaType::Link);
		addTab(Storage::SharedMediaType::MusicFile);
		tabs.push_back(MakePollsTabDescriptor(
			countValue(Storage::SharedMediaType::Poll) | rpl::map(_1 > 0)));
		addTab(Storage::SharedMediaType::RoundVoiceFile);
		addTab(Storage::SharedMediaType::GIF);
		if (!_topic && !_sublist && !_savedMessages) {
			if (const auto user = _peer->asUser()) {
				tabs.push_back(MakeCommonGroupsTabDescriptor(user));
			}
			if ((_peer->asBot() || _peer->asBroadcast())
				&& !AyuSettings::getInstance().hideSimilarChannels()) {
				tabs.push_back(MakeSimilarPeersTabDescriptor(_peer));
			}
		}
		auto tabsHost = object_ptr<TabsHost>(
			result.data(),
			TabsHost::Descriptor{
				.context = MediaTabContext{
					.controller = _controller,
					.peer = _peer,
					.topic = _topic,
					.sublist = _sublist,
					.migrated = _migrated,
					.onlineCountChanged = [this](int count) {
						_onlineCount.fire_copy(count);
					},
				},
				.tabs = std::move(tabs),
			});
		const auto raw = tabsHost.data();
		_tabsHost = raw;
		raw->scrollToRequests(
		) | rpl::on_next([this, raw](Ui::ScrollToRequest request) {
			const auto shift = MapFrom(this, raw, QPoint()).y();
			_scrollToRequests.fire({
				request.ymin + shift,
				(request.ymax < 0) ? -1 : (request.ymax + shift),
			});
		}, raw->lifetime());
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(tabsHost),
			.shown = raw->heightValue() | rpl::map(_1 > 0),
		});
	};
	if (_topic || _sublist || _savedMessages) {
		if (tabs) {
			addTabsHost();
			if (_savedMessages) {
				// The bar is pinned at its minimum height there, so the
				// tabs behave as docked from the very start.
				_tabsDocked = true;
			}
		}
		stack.finalize();
		return result;
	}
	if (auto manage = SetupChannelMembersAndManage(
			_controller,
			result.data(),
			_peer)) {
		const auto raw = static_cast<Ui::SlideWrap<Ui::RpWidget>*>(
			manage.data());
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(manage),
			.shown = raw->toggledValue(),
		});
	}
	if (auto actions = SetupActions(_controller, result.data(), _peer)) {
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(actions),
			.shown = rpl::single(true),
		});
	}
	if ((_peer->isChat() || _peer->isMegagroup())
		&& !_peer->isMonoforum()) {
		auto shown = [&]() -> rpl::producer<bool> {
			if (!tabs) {
				return rpl::single(true);
			}
			return MembersInTabValue(_peer) | rpl::map([](bool inTab) {
				return !inTab;
			});
		}();
		stack.addPlainSeparator();
		stack.add(makeMembersSection(result.data(), std::move(shown)));
	}
	if (tabs) {
		addTabsHost();
	}
	stack.finalize();
	return result;
}

Section InnerWidget::makeMembersSection(
		not_null<QWidget*> parent,
		rpl::producer<bool> shown) {
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	const auto inner = raw->entity();
	const auto toggled = raw->lifetime().make_state<rpl::variable<bool>>();
	raw->toggleOn(toggled->value(), anim::type::instant);

	using namespace rpl::mappers;
	std::move(
		shown
	) | rpl::filter(_1) | rpl::take(1) | rpl::on_next([=] {
		_members = inner->add(object_ptr<Members>(inner, _controller));
		_members->scrollToRequests(
		) | rpl::on_next([this](Ui::ScrollToRequest request) {
			auto min = (request.ymin < 0)
				? request.ymin
				: MapFrom(this, _members, QPoint(0, request.ymin)).y();
			auto max = (request.ymin < 0)
				? MapFrom(this, _members, QPoint()).y()
				: (request.ymax < 0)
				? request.ymax
				: MapFrom(this, _members, QPoint(0, request.ymax)).y();
			_scrollToRequests.fire({ min, max });
		}, _members->lifetime());
		_members->onlineCountValue(
		) | rpl::on_next([=](int count) {
			_onlineCount.fire_copy(count);
		}, _members->lifetime());
		_members->fullCountValue(
		) | rpl::on_next([=](int count) {
			*toggled = (count > 0);
		}, _members->lifetime());
	}, raw->lifetime());
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

int InnerWidget::countDesiredHeight() const {
	return _content->height() + (_members
		? (_members->desiredHeight() - _members->height())
		: 0);
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_content, visibleTop, visibleBottom);
	if (_tabsHost) {
		const auto top = MapFrom(this, _tabsHost, QPoint()).y();
		if (!_clampingTabsScroll && (top > 0) && _tabsHost->searching()) {
			const auto offDock = _tabsHost->searchContentFits()
				? (visibleTop != top)
				: (visibleTop < top);
			if (offDock) {
				_clampingTabsScroll = true;
				_scrollToRequests.fire({ top, -1 });
				_clampingTabsScroll = false;
				return;
			}
		}
		_tabsHost->setVisibleRegion(visibleTop - top, visibleBottom - top);
		_tabsDocked = _savedMessages
			|| ((top > 0) && (visibleTop >= top));
	}
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	if (_members) {
		memento->setMembersState(_members->saveState());
	}
	if (_tabsHost) {
		memento->setTabsState(_tabsHost->saveState());
	}
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	if (_members) {
		_members->restoreState(memento->membersState());
	}
	if (_sharedMediaWrap) {
		_sharedMediaWrap->finishAnimating();
	}
	if (_tabsHost) {
		_tabsHost->restoreState(memento->tabsState());
	}
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<int> InnerWidget::desiredHeightValue() const {
	return _desiredHeight.value();
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0);
	updateDesiredHeight();
	return _content->heightNoMargins();
}

void InnerWidget::enableBackButton() {
	_backToggles.force_assign(true);
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

void InnerWidget::checkBeforeCloseByEscape(Fn<void()> close) {
	if (const auto top = _topBar.get()) {
		top->checkBeforeCloseByEscape(std::move(close));
	} else {
		close();
	}
}

bool InnerWidget::searchAvailable() const {
	const auto top = _topBar.get();
	return top && top->searchAvailable();
}

void InnerWidget::showSearch() {
	if (const auto top = _topBar.get()) {
		top->showSearch();
	}
}

bool InnerWidget::hasFlexibleTopBar() const {
	return true;
}

base::weak_qptr<Ui::RpWidget> InnerWidget::createPinnedToTop(
		not_null<Ui::RpWidget*> parent) {
	const auto shown = _sublist ? _sublist->sublistPeer().get() : _peer.get();
	const auto content = Ui::CreateChild<TopBar>(
		parent,
		TopBar::Descriptor{
			.controller = _controller->parentController(),
			.key = _controller->key(),
			.wrap = _controller->wrapValue(),
			.peer = _sublist ? shown : nullptr,
			.backToggles = _backToggles.value(),
			.showFinished = _showFinished.events(),
			.customStatus = (_savedMessages
				? SavedChatsCountStatus(&_peer->session())
				: rpl::producer<TextWithEntities>()),
			.showSavedMusic = [controller = _controller, shown] {
				controller->showSection(Info::Saved::MakeMusic(shown));
			},
		});
	content->backRequest(
	) | rpl::start_to_stream(_backClicks, content->lifetime());
	content->setOnlineCount(_onlineCount.events());
	if (_tabsHost) {
		content->bindActiveTab(
			_tabsHost->activeTabBindings(),
			_tabsDocked.value());
	} else if (_members
		&& UseProfileMediaTabs()
		&& (_controller->wrap() == Wrap::Side)) {
		const auto members = _members;
		content->setupStandaloneGroupControl(
			members->groupByRoleValue(),
			members->groupByRoleAvailableValue(),
			members->rowsVisibleValue(),
			crl::guard(members, [=](bool grouped) {
				members->setGroupByRole(grouped);
			}));
	}
	_topBarColor = content->edgeColor();
	_topBar = content;
	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

base::weak_qptr<Ui::RpWidget> InnerWidget::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	return nullptr;
}

} // namespace Profile
} // namespace Info
