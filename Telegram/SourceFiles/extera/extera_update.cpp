#include "extera/extera_update.h"

#include "ayu/ui/toasts.h"
#include "base/timer.h"
#include "core/application.h"
#include "extera/extera_config.h"
#include "extera/extera_text.h"
#include "extera/extera_version.h"
#include "logs.h"
#include "ui/basic_click_handlers.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Extera::Update {
namespace {

constexpr auto kCheckInterval = 6 * 60 * 60 * crl::time(1000);
constexpr auto kFirstCheckDelay = 20 * crl::time(1000);
constexpr auto kMinimalSize = qint64(20 * 1024 * 1024);

struct Release {
	int build = 0;
	QString tag;
	QString notes;
	QString page;
	QString url;
	qint64 size = 0;
	QString sha256;
};

struct State {
	QNetworkAccessManager *manager = nullptr;
	rpl::variable<bool> available = false;
	rpl::variable<QString> status;
	Release release;
	bool busy = false;
	std::unique_ptr<base::Timer> timer;
};

// WHY: never destroyed, the network objects must not outlive the application.
[[nodiscard]] State &Instance() {
	static auto result = new State();
	return *result;
}

[[nodiscard]] QNetworkAccessManager *Manager() {
	auto &state = Instance();
	if (!state.manager) {
		state.manager = new QNetworkAccessManager(qApp);
	}
	return state.manager;
}

[[nodiscard]] QNetworkRequest MakeRequest(const QString &url) {
	auto request = QNetworkRequest(QUrl(url));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setRawHeader("User-Agent", "HeartGram-Desktop");
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	return request;
}

[[nodiscard]] Release Pick(const QJsonArray &list, bool prereleases) {
	static const auto tagPattern = QRegularExpression(
		u"^desktop-v?(\\d+)$"_q);
	static const auto hashPattern = QRegularExpression(
		u"\\b([0-9a-fA-F]{64})\\b"_q);
	auto best = Release();
	for (const auto &entry : list) {
		const auto object = entry.toObject();
		if (object.value(u"draft"_q).toBool()) {
			continue;
		}
		if (object.value(u"prerelease"_q).toBool() && !prereleases) {
			continue;
		}
		const auto tag = object.value(u"tag_name"_q).toString();
		const auto match = tagPattern.match(tag);
		if (!match.hasMatch()) {
			continue;
		}
		const auto build = match.captured(1).toInt();
		if (build <= best.build) {
			continue;
		}
		auto release = Release{
			.build = build,
			.tag = tag,
			.notes = object.value(u"body"_q).toString().trimmed(),
			.page = object.value(u"html_url"_q).toString(),
		};
		for (const auto &assetValue : object.value(u"assets"_q).toArray()) {
			const auto asset = assetValue.toObject();
			if (asset.value(u"name"_q).toString().compare(
					u"HeartGram.exe"_q,
					Qt::CaseInsensitive) != 0) {
				continue;
			}
			release.url = asset.value(u"browser_download_url"_q).toString();
			release.size = qint64(asset.value(u"size"_q).toDouble());
			const auto digest = asset.value(u"digest"_q).toString();
			if (digest.startsWith(u"sha256:"_q)) {
				release.sha256 = digest.mid(7).toLower();
			}
		}
		if (release.url.isEmpty()) {
			continue;
		}
		if (release.sha256.isEmpty()) {
			const auto found = hashPattern.match(release.notes);
			if (found.hasMatch()) {
				release.sha256 = found.captured(1).toLower();
			}
		}
		best = std::move(release);
	}
	return best;
}

void SetStatus(const QString &text) {
	Instance().status = text;
}

void Fail(const QString &text) {
	auto &state = Instance();
	state.busy = false;
	SetStatus(text);
	LOG(("Update: %1").arg(text));
}

void NotifyAvailable() {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return;
	}
	auto config = Ui::Toast::Config{
		.text = TextWithEntities{ Extera::Text(
			u"Доступно обновление HeartGram: сборка %1"_q,
			u"A HeartGram update is available: build %1"_q).arg(
				Instance().release.build) },
		.duration = 12 * crl::time(1000),
	};
	Ayu::Ui::ShowToastWithAction(
		std::move(config),
		Extera::Text(u"Обновить"_q, u"Update"_q),
		[] { Install(); });
}

void SwapAndRestart(const QString &temp) {
	const auto exe = QCoreApplication::applicationFilePath();
	const auto old = QFileInfo(exe).absolutePath() + u"/HeartGram.old.exe"_q;
	// The leftover of the previous update is our own file.
	QFile::remove(old);
	if (!QFile::rename(exe, old)) {
		Fail(Extera::Text(
			u"Не удалось заменить файл программы."_q,
			u"Could not replace the program file."_q));
		return;
	}
	if (!QFile::rename(temp, exe)) {
		QFile::rename(old, exe);
		Fail(Extera::Text(
			u"Не удалось поставить новый файл."_q,
			u"Could not put the new file in place."_q));
		return;
	}
	SetStatus(Extera::Text(u"Перезапуск…"_q, u"Restarting…"_q));
	Core::Restart();
}

} // namespace

void Start() {
	auto &state = Instance();
	if (state.timer) {
		return;
	}
	state.timer = std::make_unique<base::Timer>([] {
		if (Extera::Bool(u"update_auto"_q)) {
			CheckNow([](CheckResult result) {
				if (result == CheckResult::Available) {
					NotifyAvailable();
				}
			});
		}
		Instance().timer->callOnce(kCheckInterval);
	});
	state.timer->callOnce(kFirstCheckDelay);
}

void CheckNow(Fn<void(CheckResult)> done) {
	auto &state = Instance();
	if (state.busy) {
		return;
	}
	SetStatus(Extera::Text(u"Проверка…"_q, u"Checking…"_q));
	const auto prereleases = Extera::Bool(u"update_prerelease"_q);
	const auto reply = Manager()->get(MakeRequest(
		u"https://api.github.com/repos/whyheartone/HeartGram/releases?per_page=40"_q));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			SetStatus(Extera::Text(
				u"Не удалось проверить обновления."_q,
				u"Could not check for updates."_q));
			if (done) {
				done(CheckResult::Failed);
			}
			return;
		}
		const auto list = QJsonDocument::fromJson(
			reply->readAll()).array();
		const auto best = Pick(list, prereleases);
		auto &state = Instance();
		if (best.build > Extera::kBuild) {
			state.release = best;
			state.available = true;
			SetStatus(Extera::Text(
				u"Доступна сборка %1"_q,
				u"Build %1 is available"_q).arg(best.build));
			if (done) {
				done(CheckResult::Available);
			}
		} else {
			state.available = false;
			SetStatus(Extera::Text(
				u"Установлена последняя версия."_q,
				u"You have the latest version."_q));
			if (done) {
				done(CheckResult::Latest);
			}
		}
	});
}

rpl::producer<bool> AvailableValue() {
	return Instance().available.value();
}

QString AvailableVersion() {
	const auto &release = Instance().release;
	return release.build ? QString::number(release.build) : QString();
}

QString AvailableNotes() {
	return Instance().release.notes;
}

rpl::producer<QString> StatusValue() {
	return Instance().status.value();
}

void Install() {
	auto &state = Instance();
	if (state.busy || !state.available.current()) {
		return;
	}
	const auto release = state.release;
	const auto exe = QCoreApplication::applicationFilePath();
	const auto temp = QFileInfo(exe).absolutePath()
		+ u"/HeartGram.update.exe"_q;
	const auto file = std::make_shared<QFile>(temp);
	if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		Fail(Extera::Text(
			u"Не удалось создать файл обновления."_q,
			u"Could not create the update file."_q));
		return;
	}
	state.busy = true;
	SetStatus(Extera::Text(u"Загрузка…"_q, u"Downloading…"_q));
	const auto reply = Manager()->get(MakeRequest(release.url));
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		file->write(reply->readAll());
	});
	QObject::connect(
		reply,
		&QNetworkReply::downloadProgress,
		[=](qint64 received, qint64 total) {
			const auto whole = (total > 0) ? total : release.size;
			if (whole > 0) {
				SetStatus(Extera::Text(
					u"Загрузка: %1%"_q,
					u"Downloading: %1%"_q).arg(
						int(received * 100 / whole)));
			}
		});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		file->write(reply->readAll());
		file->close();
		if (reply->error() != QNetworkReply::NoError) {
			QFile::remove(temp);
			Fail(Extera::Text(
				u"Загрузка не удалась."_q,
				u"The download failed."_q));
			return;
		}
		if (QFileInfo(temp).size() < kMinimalSize) {
			QFile::remove(temp);
			Fail(Extera::Text(
				u"Файл обновления неполный."_q,
				u"The update file is incomplete."_q));
			return;
		}
		if (!release.sha256.isEmpty()) {
			auto check = QFile(temp);
			auto hash = QCryptographicHash(QCryptographicHash::Sha256);
			if (!check.open(QIODevice::ReadOnly) || !hash.addData(&check)) {
				QFile::remove(temp);
				Fail(Extera::Text(
					u"Не удалось проверить файл."_q,
					u"Could not check the file."_q));
				return;
			}
			check.close();
			if (QString::fromLatin1(hash.result().toHex())
					!= release.sha256) {
				QFile::remove(temp);
				Fail(Extera::Text(
					u"Контрольная сумма не совпала, обновление отменено."_q,
					u"The checksum does not match, the update is cancelled."_q));
				return;
			}
		}
		SwapAndRestart(temp);
	});
}

void OpenRelease() {
	const auto &release = Instance().release;
	UrlClickHandler::Open(release.page.isEmpty()
		? u"https://github.com/whyheartone/HeartGram/releases"_q
		: release.page);
}

} // namespace Extera::Update
