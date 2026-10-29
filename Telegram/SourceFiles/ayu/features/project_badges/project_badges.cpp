#include "ayu/features/project_badges/project_badges.h"

#if __has_include("ayu/features/project_badges/badge_secrets.h")
#include "ayu/features/project_badges/badge_secrets.h"
#else
#include "ayu/features/project_badges/badge_secrets.example.h"
#endif
#include "ayu/ui/boxes/donate_info_box.h"
#include "ayu/ui/toasts.h"
#include "ui/toast/toast.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_chat.h"

#include "apiwrap.h"
#include "base/flat_map.h"
#include "base/timer.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "extera/extera_config.h"
#include "extera/extera_text.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"
#include "settings/settings_common.h"
#include "ui/basic_click_handlers.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <openssl/evp.h>

#include <map>

object_ptr<Ui::RpWidget> HeartGramBadgeToastIcon(
	not_null<PeerData*> peer,
	Info::Profile::Badge::Content content);

namespace ProjectBadges {
namespace {

// The heart used when a level has no emoji of its own.
constexpr auto kHeartEmoji = DocumentId(5278611606756942667ULL);
constexpr auto kRefreshInterval = 5 * 60 * 1000;

struct Entry {
	Kind kind = Kind::Client;
	bool owner = false;
};

struct BadgeData {
	base::flat_map<qint64, Entry> entries;
	base::flat_map<qint64, QString> customText;
	base::flat_map<qint64, QString> customEmoji;
	std::map<QString, QString> texts;
	std::map<QString, QString> projects;
	std::map<QString, QString> emoji;
	std::map<QString, std::pair<QString, QString>> buttons;
	QString project = u"whyheartone"_q;
	QString bot;
	SupportBlock support;
	std::vector<QString> supportEmoji;
	base::flat_map<qint64, QString> extraProject;
	base::flat_map<qint64, std::pair<QString, QString>> extraButton;
	base::flat_set<qint64> trustedSources;
	base::flat_set<QString> verifiedPlugins;
};

struct State {
	BadgeData data;
	QByteArray etag;
	QNetworkAccessManager *manager = nullptr;
	QTimer *timer = nullptr;
	base::flat_map<QString, DocumentId> resolved;
	base::flat_set<QString> pending;
	Main::Session *session = nullptr;
	bool started = false;
	bool requested = false;
};

State &Instance() {
	static auto result = State();
	return result;
}

rpl::lifetime &Lifetime() {
	static auto result = rpl::lifetime();
	return result;
}

rpl::event_stream<> &UpdatesStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QString CachePath() {
	return cWorkingDir() + u"tdata/project_badges.json"_q;
}

[[nodiscard]] QString EtagPath() {
	return cWorkingDir() + u"tdata/project_badges.etag"_q;
}

[[nodiscard]] QString LevelName(Kind kind, bool owner) {
	if (owner) {
		return u"owner"_q;
	}
	switch (kind) {
	case Kind::Developer: return u"developer"_q;
	case Kind::Official: return u"official"_q;
	case Kind::Supporter: return u"supporter"_q;
	case Kind::Client: break;
	}
	return u"client"_q;
}

[[nodiscard]] QString DefaultText(const QString &level) {
	if (level == u"owner"_q) {
		return u"{name} создатель и владелец {project}"_q;
	} else if (level == u"developer"_q) {
		return u"{name} разработчик {project}"_q;
	} else if (level == u"official"_q) {
		return u"{name} является официальным ресурсом {project}"_q;
	} else if (level == u"supporter"_q) {
		return u"{name} поддержал {project}"_q;
	}
	return u"{name} с нами в {project}"_q;
}

[[nodiscard]] qint64 KeyFor(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return qint64(peerToUser(user->id).bare);
	} else if (const auto channel = peer->asChannel()) {
		return -qint64(peerToChannel(channel->id).bare);
	} else if (const auto chat = peer->asChat()) {
		return -qint64(peerToChat(chat->id).bare);
	}
	return 0;
}

[[nodiscard]] qint64 IdFromJson(const QJsonValue &value) {
	return value.isString()
		? value.toString().trimmed().toLongLong()
		: qint64(value.toDouble());
}

// AES-256-GCM: iv(12) | cipher | tag(16), base64. The key is two XOR halves
// (the same ones the phone plugin keeps), assembled only for the call.
[[nodiscard]] QByteArray Unseal(const QString &sealed) {
	const auto raw = QByteArray::fromBase64(sealed.toLatin1());
	if (raw.size() < 12 + 16) {
		return {};
	}
	auto key = std::array<unsigned char, 32>();
	for (auto i = 0; i != int(key.size()); ++i) {
		key[i] = Secrets::kSealA[i] ^ Secrets::kSealB[i];
	}
	const auto cipherSize = raw.size() - 12 - 16;
	const auto data = reinterpret_cast<const unsigned char*>(raw.constData());
	auto result = QByteArray(cipherSize, Qt::Uninitialized);
	auto ok = false;
	if (const auto ctx = EVP_CIPHER_CTX_new()) {
		auto length = 0;
		ok = EVP_DecryptInit_ex(
				ctx,
				EVP_aes_256_gcm(),
				nullptr,
				nullptr,
				nullptr) == 1
			&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1
			&& EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), data) == 1
			&& EVP_DecryptUpdate(
				ctx,
				reinterpret_cast<unsigned char*>(result.data()),
				&length,
				data + 12,
				cipherSize) == 1
			&& EVP_CIPHER_CTX_ctrl(
				ctx,
				EVP_CTRL_GCM_SET_TAG,
				16,
				const_cast<unsigned char*>(data + 12 + cipherSize)) == 1;
		if (ok) {
			auto tail = 0;
			ok = EVP_DecryptFinal_ex(
				ctx,
				reinterpret_cast<unsigned char*>(result.data()) + length,
				&tail) == 1;
		}
		EVP_CIPHER_CTX_free(ctx);
	}
	key.fill(0);
	return ok ? result : QByteArray();
}

[[nodiscard]] QString ListUrl() {
	auto raw = QByteArray(int(Secrets::kSrc.size()), Qt::Uninitialized);
	for (auto i = 0; i != int(Secrets::kSrc.size()); ++i) {
		raw[i] = char(
			Secrets::kSrc[i] ^ Secrets::kSrcKey[i % Secrets::kSrcKey.size()]);
	}
	return QString::fromUtf8(raw);
}

void NotifyPeers() {
	auto &state = Instance();
	if (!state.session) {
		return;
	}
	auto &owner = state.session->data();
	for (const auto &[key, entry] : state.data.entries) {
		auto peer = (PeerData*)nullptr;
		if (key > 0) {
			peer = owner.peerLoaded(peerFromUser(UserId(uint64(key))));
		} else {
			const auto bare = uint64(-key);
			peer = owner.peerLoaded(peerFromChannel(ChannelId(bare)));
			if (!peer) {
				peer = owner.peerLoaded(peerFromChat(ChatId(bare)));
			}
		}
		if (peer) {
			state.session->changes().peerUpdated(
				peer,
				Data::PeerUpdate::Flag::EmojiStatus);
		}
	}
}

void Changed() {
	UpdatesStream().fire({});
	NotifyPeers();
}

[[nodiscard]] bool IsNumber(const QString &ref) {
	if (ref.isEmpty()) {
		return false;
	}
	for (const auto ch : ref) {
		if (!ch.isDigit()) {
			return false;
		}
	}
	return true;
}

void Resolve(const QString &ref) {
	auto &state = Instance();
	if (ref.isEmpty()
		|| IsNumber(ref)
		|| state.resolved.contains(ref)
		|| state.pending.contains(ref)
		|| !state.session) {
		return;
	}
	const auto slash = ref.lastIndexOf('/');
	if (slash <= 0) {
		return;
	}
	const auto name = ref.left(slash);
	const auto index = ref.mid(slash + 1).toInt();
	const auto session = state.session;
	state.pending.emplace(ref);
	session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetShortName(MTP_string(name)),
		MTP_int(0)
	)).done(crl::guard(session, [=](const MTPmessages_StickerSet &result) {
		auto &state = Instance();
		state.pending.remove(ref);
		result.match([&](const MTPDmessages_stickerSet &data) {
			const auto set = session->data().stickers().feedSetFull(data);
			if (index >= 0 && index < set->stickers.size()) {
				state.resolved[ref] = set->stickers[index]->id;
				Changed();
			}
		}, [](const auto &) {});
	})).fail(crl::guard(session, [=] {
		Instance().pending.remove(ref);
	})).send();
}

void ResolveAll() {
	const auto &data = Instance().data;
	for (const auto &[level, ref] : data.emoji) {
		Resolve(ref);
	}
	for (const auto &[key, ref] : data.customEmoji) {
		Resolve(ref);
	}
	for (const auto &ref : data.supportEmoji) {
		Resolve(ref);
	}
}

[[nodiscard]] DocumentId EmojiIdFor(const QString &ref) {
	if (ref.isEmpty()) {
		return kHeartEmoji;
	} else if (IsNumber(ref)) {
		return DocumentId(ref.toULongLong());
	}
	const auto &state = Instance();
	const auto i = state.resolved.find(ref);
	return (i != end(state.resolved)) ? i->second : kHeartEmoji;
}

[[nodiscard]] QString RefFor(qint64 key, const Entry &entry) {
	const auto &data = Instance().data;
	if (const auto i = data.customEmoji.find(key); i != end(data.customEmoji)) {
		return i->second;
	}
	const auto level = LevelName(entry.kind, entry.owner);
	auto find = [&](const QString &name) {
		const auto i = data.emoji.find(name);
		return (i != data.emoji.end()) ? i->second : QString();
	};
	auto result = find(level);
	if (result.isEmpty() && entry.owner) {
		result = find(u"developer"_q);
	}
	return result;
}

bool Apply(const QByteArray &body) {
	const auto document = QJsonDocument::fromJson(body);
	if (!document.isObject()) {
		return false;
	}
	const auto root = document.object();
	auto data = BadgeData();
	auto secretTrust = QJsonObject();
	const auto assign = [&](const QString &name, Kind kind) {
		for (const auto &value : root.value(name).toArray()) {
			const auto id = IdFromJson(value);
			if (!id || (kind == Kind::Client && id < 0)) {
				continue;
			}
			data.entries[id] = Entry{ .kind = kind };
		}
	};
	// Lowest priority first, so the higher level wins.
	assign(u"clients"_q, Kind::Client);
	assign(u"supporters"_q, Kind::Supporter);
	assign(u"official"_q, Kind::Official);
	assign(u"developers"_q, Kind::Developer);
	for (const auto &value : root.value(u"owners"_q).toArray()) {
		const auto i = data.entries.find(IdFromJson(value));
		if (i != data.entries.end()) {
			i->second.owner = true;
		}
	}
	for (const auto name : { u"owner"_q, u"developer"_q, u"official"_q, u"supporter"_q, u"client"_q }) {
		data.texts[name] = DefaultText(name);
	}
	const auto texts = root.value(u"texts"_q).toObject();
	for (auto i = texts.begin(); i != texts.end(); ++i) {
		if (!i.value().toString().isEmpty()) {
			data.texts[i.key()] = i.value().toString();
		}
	}
	const auto emoji = root.value(u"emoji"_q).toObject();
	for (auto i = emoji.begin(); i != emoji.end(); ++i) {
		data.emoji[i.key()] = i.value().toString().trimmed();
	}
	const auto custom = root.value(u"customEmoji"_q).toObject();
	for (auto i = custom.begin(); i != custom.end(); ++i) {
		if (!i.value().toString().trimmed().isEmpty()) {
			data.customEmoji[i.key().toLongLong()] = i.value().toString().trimmed();
		}
	}
	const auto perLevel = root.value(u"projects"_q).toObject();
	for (auto i = perLevel.begin(); i != perLevel.end(); ++i) {
		if (!i.value().toString().trimmed().isEmpty()) {
			data.projects[i.key()] = i.value().toString().trimmed();
		}
	}
	if (const auto project = root.value(u"project"_q).toString().trimmed(); !project.isEmpty()) {
		data.project = project;
	}
	const auto sealed = root.value(u"sealed"_q).toString();
	if (!sealed.isEmpty()) {
		const auto plain = Unseal(sealed);
		const auto secret = QJsonDocument::fromJson(plain).object();
		secretTrust = secret.value(u"trust"_q).toObject();
		data.bot = secret.value(u"bot"_q).toString().trimmed();
		const auto texts = secret.value(u"custom"_q).toObject();
		for (auto i = texts.begin(); i != texts.end(); ++i) {
			data.customText[i.key().toLongLong()] = i.value().toString();
		}
		const auto buttons = secret.value(u"buttons"_q).toObject();
		for (auto i = buttons.begin(); i != buttons.end(); ++i) {
			const auto button = i.value().toObject();
			data.buttons[i.key()] = {
				button.value(u"text"_q).toString().trimmed(),
				button.value(u"url"_q).toString().trimmed(),
			};
		}
		const auto extras = secret.value(u"extras"_q).toObject();
		for (auto i = extras.begin(); i != extras.end(); ++i) {
			const auto key = i.key().toLongLong();
			const auto extra = i.value().toObject();
			const auto project = extra.value(u"project"_q).toString().trimmed();
			if (!project.isEmpty()) {
				data.extraProject[key] = project;
			}
			const auto url = extra.value(u"buttonUrl"_q).toString().trimmed();
			if (!url.isEmpty()) {
				data.extraButton[key] = {
					extra.value(u"buttonText"_q).toString().trimmed(),
					url,
				};
			}
		}
		const auto support = secret.value(u"support"_q).toObject();
		data.support.note = support.value(u"note"_q).toString().trimmed();
		data.support.amount = support.value(u"amount"_q).toString().trimmed();
		data.support.amountRub = support.value(u"amountRub"_q).toDouble();
		data.support.tonRub = support.value(u"tonRub"_q).toDouble();
		data.support.contact = support.value(u"contact"_q).toString().trimmed();
		for (const auto &item : support.value(u"items"_q).toArray()) {
			const auto object = item.toObject();
			const auto name = object.value(u"name"_q).toString().trimmed();
			const auto url = object.value(u"url"_q).toString().trimmed();
			if (!name.isEmpty() && !url.isEmpty()) {
				data.support.items.push_back({ name, url });
				data.supportEmoji.push_back(
					object.value(u"emoji"_q).toString().trimmed());
			}
		}
	}
	const auto trust = secretTrust;
	for (const auto &value : trust.value(u"sources"_q).toArray()) {
		data.trustedSources.emplace(IdFromJson(value.toObject().value(u"id"_q)));
	}
	for (const auto &value : trust.value(u"plugins"_q).toArray()) {
		data.verifiedPlugins.emplace(
			value.toObject().value(u"sha256"_q).toString().toLower());
	}
	Instance().data = std::move(data);
	ResolveAll();
	Changed();
	return true;
}

void Refresh() {
	auto &state = Instance();
	if (!state.manager || state.requested || !Enabled()) {
		return;
	}
	const auto url = ListUrl();
	if (url.isEmpty()) {
		return;
	}
	state.requested = true;
	auto request = QNetworkRequest(QUrl(url));
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("User-Agent", "HeartGram");
	if (!state.etag.isEmpty()) {
		request.setRawHeader("If-None-Match", state.etag);
	}
	const auto reply = state.manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &state = Instance();
		state.requested = false;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || status != 200) {
			return; // 304 (unchanged) and failures keep what we have.
		}
		const auto body = reply->readAll();
		if (!Apply(body)) {
			return;
		}
		state.etag = reply->rawHeader("ETag");
		auto file = QFile(CachePath());
		if (file.open(QIODevice::WriteOnly)) {
			file.write(body);
		}
		auto tag = QFile(EtagPath());
		if (tag.open(QIODevice::WriteOnly)) {
			tag.write(state.etag);
		}
	});
}

[[nodiscard]] QString Fill(
		QString text,
		const QString &name,
		const QString &project) {
	return text.replace(u"{name}"_q, name).replace(u"{project}"_q, project);
}

} // namespace

bool Enabled() {
	return true;
}

QString Project() {
	return Instance().data.project;
}

SupportBlock Support() {
	const auto &data = Instance().data;
	auto result = data.support;
	for (auto i = 0; i != int(result.items.size()); ++i) {
		const auto &ref = data.supportEmoji[i];
		const auto j = Instance().resolved.find(ref);
		result.items[i].emojiId = IsNumber(ref)
			? DocumentId(ref.toULongLong())
			: (j != end(Instance().resolved)) ? j->second : DocumentId();
	}
	return result;
}

bool IsTrustedPluginSource(not_null<PeerData*> peer) {
	const auto key = KeyFor(peer);
	return key && Instance().data.trustedSources.contains(key);
}

bool IsVerifiedPlugin(const QString &sha256) {
	return Instance().data.verifiedPlugins.contains(sha256.toLower());
}

rpl::producer<> Updated() {
	return UpdatesStream().events();
}

void Start() {
	auto &state = Instance();
	if (state.started) {
		return;
	}
	state.started = true;
	auto cached = QFile(CachePath());
	if (cached.open(QIODevice::ReadOnly)) {
		Apply(cached.readAll());
	}
	auto tag = QFile(EtagPath());
	if (tag.open(QIODevice::ReadOnly)) {
		state.etag = tag.readAll().trimmed();
	}
	state.manager = new QNetworkAccessManager(qApp);
	state.timer = new QTimer(state.manager);
	QObject::connect(state.timer, &QTimer::timeout, [] { Refresh(); });
	state.timer->start(kRefreshInterval);
	Refresh();
	Core::App().domain().activeSessionValue(
	) | rpl::filter([](Main::Session *session) {
		return session != nullptr;
	}) | rpl::on_next([](Main::Session *session) {
		Instance().session = session;
		ResolveAll();
		NotifyPeers();
	}, Lifetime());
}

base::flat_map<DocumentId, QColor> &SparkColors() {
	static auto result = base::flat_map<DocumentId, QColor>();
	return result;
}

QColor SparkColor(DocumentId id) {
	const auto &colors = SparkColors();
	const auto i = colors.find(id);
	return (i != colors.end()) ? i->second : QColor(0xFF, 0x5C, 0x7A);
}

std::optional<Badge> For(not_null<PeerData*> peer) {
	if (!Enabled()) {
		return std::nullopt;
	}
	const auto key = KeyFor(peer);
	if (!key) {
		return std::nullopt;
	}
	const auto &data = Instance().data;
	const auto i = data.entries.find(key);
	if (i == end(data.entries)) {
		return std::nullopt;
	}
	const auto emojiId = EmojiIdFor(RefFor(key, i->second));
	SparkColors()[emojiId] = i->second.owner
		? QColor(0xFF, 0x6F, 0xA3)
		: (i->second.kind == Kind::Developer)
		? QColor(0xFF, 0x4D, 0x8D)
		: (i->second.kind == Kind::Official)
		? QColor(0x6A, 0xA8, 0xFF)
		: (i->second.kind == Kind::Supporter)
		? QColor(0xFF, 0xC2, 0x47)
		: QColor(0xFF, 0x5C, 0x5C);
	return Badge{
		.kind = i->second.kind,
		.owner = i->second.owner,
		.emojiId = emojiId,
	};
}

rpl::producer<Info::Profile::Badge::Content> BadgeContent(
		not_null<PeerData*> peer) {
	const auto compute = [=] {
		const auto badge = For(peer);
		return badge
			? Info::Profile::Badge::Content{
				.badge = Info::Profile::BadgeType::ExteraCustom,
				.emojiStatusId = EmojiStatusId{ .documentId = badge->emojiId },
			}
			: Info::Profile::Badge::Content();
	};
	return rpl::single(rpl::empty) | rpl::then(
		Updated()
	) | rpl::map(compute) | rpl::distinct_until_changed();
}

DocumentId LevelEmoji(Kind kind) {
	const auto &data = Instance().data;
	const auto i = data.emoji.find(LevelName(kind, false));
	return EmojiIdFor((i != data.emoji.end()) ? i->second : QString());
}

Fn<void()> ClickHandler(not_null<PeerData*> peer) {
	return [=] {
		const auto window = Core::App().activeWindow();
		if (const auto controller = window
				? window->sessionController()
				: nullptr) {
			ShowAbout(controller, peer);
		}
	};
}

void ShowDonateInfo(not_null<Window::SessionController*> controller) {
	const auto support = Support();
	auto rubles = support.amount;
	auto ton = QString();
	if (support.amountRub > 0.) {
		const auto value = (support.amountRub == std::floor(support.amountRub))
			? QString::number(qint64(support.amountRub))
			: QString::number(support.amountRub, 'f', 2);
		rubles = value + QChar(0x20BD);
		if (support.tonRub > 0.) {
			// round up, so a payment never falls a little short
			ton = QString::number(
				std::ceil(support.amountRub / support.tonRub * 100.) / 100.,
				'f',
				2);
		}
	}
	controller->show(Box(
		Ui::FillProjectDonateInfoBox,
		controller,
		rubles,
		ton,
		support.contact));
}

void ShowAbout(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto key = KeyFor(peer);
	const auto &data = Instance().data;
	const auto i = data.entries.find(key);
	if (i == end(data.entries)) {
		return;
	}
	const auto level = LevelName(i->second.kind, i->second.owner);
	const auto custom = data.customText.find(key);
	const auto hasCustom = (custom != end(data.customText))
		&& !custom->second.isEmpty();
	const auto template_ = hasCustom
		? custom->second
		: [&] {
			const auto j = data.texts.find(level);
			return (j != data.texts.end()) ? j->second : DefaultText(level);
		}();
	const auto levelProject = [&] {
		if (const auto e = data.extraProject.find(key);
			e != data.extraProject.end()) {
			return e->second;
		}
		const auto j = data.projects.find(level);
		return (j != data.projects.end()) ? j->second : data.project;
	}();
	const auto text = Fill(template_, peer->name(), levelProject);
	auto buttonText = QString();
	auto buttonUrl = QString();
	const auto ownButton = data.extraButton.find(key);
	const auto supporter = (i->second.kind == Kind::Supporter)
		&& !i->second.owner
		&& ownButton == data.extraButton.end();
	if (ownButton != data.extraButton.end()) {
		buttonText = ownButton->second.first;
		buttonUrl = ownButton->second.second;
	} else if (!hasCustom && !supporter) {
		const auto j = data.buttons.find(level);
		if (j != data.buttons.end() && !j->second.second.isEmpty()) {
			buttonText = j->second.first;
			buttonUrl = j->second.second;
		} else if (!data.bot.isEmpty() && level == u"client"_q) {
			buttonText = Extera::Text(
				u"Открыть бота"_q,
				u"Open the bot"_q);
			buttonUrl = u"https://t.me/"_q + data.bot;
		}
	}
	auto marked = QString(template_);
	marked.replace(u"{name}"_q, u"**"_q + peer->name() + u"**"_q);
	marked.replace(u"{project}"_q, u"**"_q + levelProject + u"**"_q);
	auto config = Ui::Toast::Config{
		.text = tr::rich(marked),
		.iconContent = HeartGramBadgeToastIcon(
			peer,
			Info::Profile::Badge::Content{
				.badge = Info::Profile::BadgeType::ExteraCustom,
				.emojiStatusId = EmojiStatusId{
					.documentId = For(peer) ? For(peer)->emojiId : 0,
				},
			}),
		.st = &st::exteraBadgeToast,
		.adaptive = true,
		.duration = 4 * crl::time(1000),
	};
	if (supporter) {
		Ayu::Ui::ShowToastWithAction(
			std::move(config),
			tr::lng_collectible_learn_more(tr::now),
			[=] { ShowDonateInfo(controller); });
	} else if (!buttonUrl.isEmpty()) {
		Ayu::Ui::ShowToastWithAction(
			std::move(config),
			buttonText.isEmpty()
				? Extera::Text(u"Открыть"_q, u"Open"_q)
				: buttonText,
			[=] { UrlClickHandler::Open(buttonUrl); });
	} else {
		Ui::Toast::Show(std::move(config));
	}
}

} // namespace ProjectBadges
