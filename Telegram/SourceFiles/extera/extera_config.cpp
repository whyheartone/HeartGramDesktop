#include "extera/extera_config.h"

#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"

#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace Extera {
namespace {

constexpr auto kSaveDelay = crl::time(1000);

[[nodiscard]] const QJsonObject &Defaults() {
	static const auto result = QJsonObject{
		{ u"translate_button"_q, true },
		{ u"translate_chats"_q, false },
		{ u"translate_provider"_q, 0 },
		{ u"disable_number_rounding"_q, false },
		{ u"time_with_seconds"_q, false },
		{ u"filter_zalgo"_q, true },
		{ u"download_boost"_q, 0 },
		{ u"upload_boost"_q, false },
		{ u"relative_last_seen"_q, false },
		{ u"approx_last_seen"_q, true },
		{ u"hide_phone"_q, false },
		{ u"show_id_dc"_q, 0 },

		{ u"avatar_corners"_q, 28 },
		{ u"single_corner_radius"_q, true },
		{ u"hide_status"_q, false },
		{ u"hide_stories"_q, false },
		{ u"sender_mini_avatars"_q, false },
		{ u"folder_title_style"_q, 0 },
		{ u"folder_counter"_q, 0 },
		{ u"hide_all_chats"_q, false },
		{ u"streamer_mode"_q, false },
		{ u"streamer_full"_q, false },
		{ u"streamer_capture"_q, 0 },
		{ u"menu_order"_q, QString() },
		{ u"menu_hidden"_q, QString() },
		{ u"streamer_name"_q, true },
		{ u"streamer_avatar"_q, true },
		{ u"streamer_profile"_q, true },
		{ u"streamer_private"_q, true },
		{ u"streamer_channels"_q, true },
		{ u"streamer_phone"_q, 0 },
		{ u"streamer_username"_q, 0 },
		{ u"streamer_id"_q, 0 },
		{ u"streamer_private_avatar"_q, true },
		{ u"streamer_private_name"_q, true },
		{ u"streamer_private_text"_q, true },
		{ u"streamer_bots_avatar"_q, true },
		{ u"streamer_bots_name"_q, true },
		{ u"streamer_bots_text"_q, true },
		{ u"streamer_groups_avatar"_q, true },
		{ u"streamer_groups_name"_q, true },
		{ u"streamer_groups_text"_q, true },
		{ u"streamer_channels_avatar"_q, false },
		{ u"streamer_channels_name"_q, false },
		{ u"streamer_channels_text"_q, true },
		{ u"streamer_shown"_q, QString() },
		{ u"streamer_hidden"_q, QString() },
		{ u"feed_in_menu"_q, true },
		{ u"feed_wide_posts"_q, true },
		{ u"wide_posts_group"_q, true },
		{ u"feed_unread_counter"_q, true },
		{ u"feed_include_archived"_q, true },
		{ u"feed_hidden"_q, QString() },

		{ u"sticker_size"_q, 14 },
		{ u"sticker_time"_q, 0 },
		{ u"unlimited_recent_stickers"_q, false },
		{ u"hide_reactions_private"_q, false },
		{ u"hide_reactions_groups"_q, false },
		{ u"hide_reactions_channels"_q, false },
		{ u"double_click_incoming"_q, 0 },
		{ u"double_click_outgoing"_q, 0 },
		{ u"disable_notification_delay"_q, false },
		{ u"disable_telegram_ai"_q, false },
		{ u"disable_greeting_sticker"_q, false },
		{ u"comma_after_mention"_q, false },
		{ u"inline_math"_q, false },
		{ u"hide_send_as"_q, false },
		{ u"remove_message_tail"_q, false },
		{ u"edited_icon"_q, false },
		{ u"online_indicator"_q, false },
		{ u"forward_count"_q, false },
		{ u"hide_share_button"_q, false },
		{ u"menu_repeat"_q, false },
		{ u"menu_quick_row"_q, false },
		{ u"menu_plugins"_q, 1 },
		{ u"menu_quick_items"_q, u"reply,delete,copy,edit"_q },
		{ u"menu_details"_q, true },
		{ u"always_send_hd"_q, false },
		{ u"video_seek_step"_q, 5 },
		{ u"prefer_original_quality"_q, false },
		{ u"pause_on_minimize_video"_q, false },
		{ u"pause_on_minimize_voice"_q, false },
		{ u"pause_on_minimize_round"_q, false },

		{ u"md3_switches"_q, true },
		{ u"md3_sliders"_q, true },
		{ u"md3_chat_list"_q, false },
		{ u"md3_menus"_q, false },
		{ u"md3_settings"_q, false },
		{ u"md3_dialogs"_q, false },
		{ u"material_design"_q, true },
		{ u"custom_chat_themes"_q, true },
		{ u"app_icon"_q, QString() },
		{ u"welcome_shown"_q, false },
		{ u"local_premium_emoji"_q, true },
		{ u"poll_results_before_voting"_q, false },
		{ u"bottom_button"_q, 0 },
		{ u"wide_posts"_q, false },
		{ u"ai_chat_key"_q, QString() },
		{ u"ai_chat_model"_q, 0 },
		{ u"ai_chat_role"_q, 0 },
		{ u"ai_chat_history"_q, 3 },
		{ u"ai_chat_streaming"_q, true },
		{ u"ai_chat_temperature"_q, 10 },
		{ u"plugins_enabled"_q, true },
		{ u"plugins_pinned"_q, QString() },
		{ u"plugins_compact_view"_q, false },
		{ u"project_badges"_q, true },
		{ u"topbar_avatar"_q, true },
		{ u"player_md3"_q, true },
		{ u"player_mini_ring"_q, false },
		{ u"player_mode"_q, 0 },
		{ u"player_rhythm"_q, true },
		{ u"player_live_gradient"_q, false },
		{ u"player_gpu"_q, true },
		{ u"player_font"_q, QString() },
		{ u"separate_volumes"_q, true },
		{ u"voice_volume"_q, 90 },
		{ u"player_change_pitch"_q, false },
		{ u"player_lyrics"_q, true },
		{ u"player_cover_color"_q, true },
		{ u"player_line_progress"_q, true },
		{ u"am_enabled"_q, true },
		{ u"am_on_open"_q, true },
		{ u"am_dates"_q, true },
		{ u"am_date_style"_q, 0 },
		{ u"am_date_size"_q, 1 },
		{ u"am_date_speed"_q, 1 },
		{ u"am_dark_safe"_q, true },
		{ u"am_reduce_motion"_q, false },
		{ u"am_profile"_q, 1 },
		{ u"am_follow_profile"_q, true },
		{ u"am_intensity"_q, 1 },
		{ u"am_style"_q, 4 },
		{ u"am_speed"_q, 1 },
		{ u"am_random_chat"_q, false },
		{ u"am_random_mode"_q, 1 },
		{ u"plugins_safe_mode"_q, false },
	};
	return result;
}

void RepaintAll() {
	for (const auto top : QApplication::topLevelWidgets()) {
		top->update();
		for (const auto child : top->findChildren<QWidget*>()) {
			child->update();
		}
	}
}

} // namespace

Config &Config::Instance() {
	static const auto result = new Config();
	return *result;
}

Config::Config()
: _saveTimer([=] { saveNow(); }) {
}

void Config::setWorkingDir(const QString &path) {
	_path = path + u"tdata/extera.json"_q;
	_loaded = false;
	_values = QJsonObject();
	load();
	AyuUiSettings::setMaterialMenus(boolValue(u"md3_menus"_q));
	AyuUiSettings::setMaterialDialogs(boolValue(u"md3_dialogs"_q));
	AyuUiSettings::setMaterialSettings(boolValue(u"md3_settings"_q));
}

QString Config::path() const {
	return _path;
}

void Config::load() {
	if (_loaded) {
		return;
	}
	if (_path.isEmpty()) {
		return;
	}
	_loaded = true;
	auto file = QFile(path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (document.isObject()) {
		_values = document.object();
	}
}

QJsonValue Config::value(const QString &key) const {
	const_cast<Config*>(this)->load();
	const auto i = _values.constFind(key);
	if (i != _values.constEnd()) {
		return *i;
	}
	return Defaults().value(key);
}

void Config::set(const QString &key, QJsonValue value) {
	load();
	if (value == Defaults().value(key)) {
		if (!_values.contains(key)) {
			return;
		}
		_values.remove(key);
	} else if (_values.value(key) == value) {
		return;
	} else {
		_values.insert(key, value);
	}
	_saveTimer.callOnce(kSaveDelay);
	if (key == u"md3_menus"_q) {
		AyuUiSettings::setMaterialMenus(boolValue(key));
	} else if (key == u"md3_dialogs"_q) {
		AyuUiSettings::setMaterialDialogs(boolValue(key));
	} else if (key == u"md3_settings"_q) {
		AyuUiSettings::setMaterialSettings(boolValue(key));
	}
	if (key.startsWith(u"md3_"_q)) {
		RepaintAll();
	}
	_changes.fire_copy(key);
}

bool Config::boolValue(const QString &key) const {
	return value(key).toBool();
}

int Config::intValue(const QString &key) const {
	return value(key).toInt();
}

QString Config::stringValue(const QString &key) const {
	return value(key).toString();
}

void Config::setBool(const QString &key, bool value) {
	set(key, value);
}

void Config::setInt(const QString &key, int value) {
	set(key, value);
}

void Config::setString(const QString &key, const QString &value) {
	set(key, value);
}

rpl::producer<bool> Config::boolChanges(const QString &key) const {
	return rpl::single(
		boolValue(key)
	) | rpl::then(_changes.events(
	) | rpl::filter(
		rpl::mappers::_1 == key
	) | rpl::map([=] {
		return boolValue(key);
	})) | rpl::distinct_until_changed();
}

rpl::producer<QString> Config::stringChanges(const QString &key) const {
	return rpl::single(
		stringValue(key)
	) | rpl::then(_changes.events(
	) | rpl::filter(
		rpl::mappers::_1 == key
	) | rpl::map([=] {
		return stringValue(key);
	})) | rpl::distinct_until_changed();
}

rpl::producer<int> Config::intChanges(const QString &key) const {
	return rpl::single(
		intValue(key)
	) | rpl::then(_changes.events(
	) | rpl::filter(
		rpl::mappers::_1 == key
	) | rpl::map([=] {
		return intValue(key);
	})) | rpl::distinct_until_changed();
}

rpl::producer<QString> Config::changes() const {
	return _changes.events();
}

QByteArray Config::exportJson() const {
	const_cast<Config*>(this)->load();
	return QJsonDocument(_values).toJson(QJsonDocument::Indented);
}

bool Config::importJson(const QByteArray &data) {
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) {
		return false;
	}
	load();
	const auto imported = document.object();
	for (auto i = imported.begin(); i != imported.end(); ++i) {
		if (Defaults().contains(i.key())) {
			set(i.key(), i.value());
		}
	}
	return true;
}

void Config::reset() {
	load();
	const auto keys = _values.keys();
	for (const auto &key : keys) {
		set(key, Defaults().value(key));
	}
}

void Config::saveNow() {
	_saveTimer.cancel();
	if (_path.isEmpty()) {
		return;
	}
	auto file = QSaveFile(path());
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(_values).toJson(QJsonDocument::Indented));
	file.commit();
}

namespace {

// Features both clients have: HeartGram keeps one setting, AyuGram's, and the
// exteraGram code reads it under the old key. Settings rows for these keys are
// not shown in the exteraGram pages (see settings_extera.cpp).
using AyuBoolGetter = bool(*)(const AyuSettings &);
using AyuIntGetter = int(*)(const AyuSettings &);

[[nodiscard]] const QHash<QString, AyuBoolGetter> &AyuBools() {
	static const auto result = QHash<QString, AyuBoolGetter>{
		{ u"remove_message_tail"_q, [](const AyuSettings &a) { return a.removeMessageTail(); } },
		{ u"disable_notification_delay"_q, [](const AyuSettings &a) { return a.disableNotificationsDelay(); } },
		{ u"local_premium_emoji"_q, [](const AyuSettings &a) { return a.localPremium(); } },
		{ u"hide_all_chats"_q, [](const AyuSettings &a) { return a.hideAllChatsFolder(); } },
		{ u"hide_stories"_q, [](const AyuSettings &a) { return a.disableStories(); } },
		{ u"hide_share_button"_q, [](const AyuSettings &a) { return a.hideFastShare(); } },
		{ u"md3_switches"_q, [](const AyuSettings &a) { return a.materialSwitches(); } },
		{ u"filter_zalgo"_q, [](const AyuSettings &a) { return a.filterZalgo(); } },
		{ u"disable_greeting_sticker"_q, [](const AyuSettings &a) { return a.disableGreetingSticker(); } },
		{ u"unlimited_recent_stickers"_q, [](const AyuSettings &a) { return a.unlimitedRecentStickers(); } },
		{ u"disable_telegram_ai"_q, [](const AyuSettings &a) { return !a.showAiEditorButtonInMessageField(); } },
		{ u"custom_chat_themes"_q, [](const AyuSettings &a) { return !a.disableCustomBackgrounds(); } },
		{ u"time_with_seconds"_q, [](const AyuSettings &a) { return a.showMessageSeconds(); } },
		{ u"edited_icon"_q, [](const AyuSettings &a) { return a.replaceBottomInfoWithIcons(); } },
		// AyuGram has its own streamer mode; exteraGram's module stays off.
		{ u"streamer_mode"_q, [](const AyuSettings &) { return false; } },
		{ u"hide_reactions_channels"_q, [](const AyuSettings &a) { return !a.showChannelReactions(); } },
		{ u"hide_reactions_groups"_q, [](const AyuSettings &a) { return !a.showGroupReactions(); } },
		{ u"hide_reactions_private"_q, [](const AyuSettings &a) { return !a.showPrivateChatReactions(); } },
		{ u"hide_phone"_q, [](const AyuSettings &) { return false; } },
	};
	return result;
}

[[nodiscard]] const QHash<QString, AyuIntGetter> &AyuInts() {
	static const auto result = QHash<QString, AyuIntGetter>{
		{ u"show_id_dc"_q, [](const AyuSettings &a) { return int(a.showPeerId()); } },
		// AyuGram implements these itself; the exteraGram variants stay off.
		{ u"bottom_button"_q, [](const AyuSettings &) { return 0; } },
		{ u"translate_provider"_q, [](const AyuSettings &) { return 0; } },
	};
	return result;
}

[[nodiscard]] std::optional<bool> AyuBool(const QString &key) {
	const auto &all = AyuBools();
	const auto i = all.constFind(key);
	if (i == all.constEnd()) {
		return std::nullopt;
	}
	return (*i)(AyuSettings::getInstance());
}

[[nodiscard]] std::optional<int> AyuInt(const QString &key) {
	const auto &all = AyuInts();
	const auto i = all.constFind(key);
	if (i == all.constEnd()) {
		return std::nullopt;
	}
	return (*i)(AyuSettings::getInstance());
}

} // namespace

bool Bool(const QString &key) {
	if (const auto value = AyuBool(key)) {
		return *value;
	}
	return Config::Instance().boolValue(key);
}

int Int(const QString &key) {
	if (const auto value = AyuInt(key)) {
		return *value;
	}
	return Config::Instance().intValue(key);
}

QString String(const QString &key) {
	if (key == u"app_icon"_q) {
		return QString(); // The app icon is AyuGram's setting.
	}
	return Config::Instance().stringValue(key);
}

int RecentStickersLimit(int serverLimit) {
	return Bool(u"unlimited_recent_stickers"_q)
		? std::max(serverLimit, 200)
		: serverLimit;
}

} // namespace Extera
