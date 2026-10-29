#pragma once

#include <QtCore/QJsonValue>
#include <QtCore/QJsonObject>

class History;
class PeerData;

namespace Extera::Plugins {

enum class SendStrategy {
	Default,
	Cancel,
	Modify,
};

struct SendResult {
	SendStrategy strategy = SendStrategy::Default;
	QString text;
};

struct MenuItem {
	QString id;
	QString text;
};

void Start();
void Shutdown();

[[nodiscard]] bool Available();
[[nodiscard]] bool Started();
[[nodiscard]] QString RuntimeError();

[[nodiscard]] QJsonValue Call(
	const QString &method,
	const QJsonObject &args = QJsonObject());

[[nodiscard]] bool Bypassing();
[[nodiscard]] SendResult OnSendMessage(
	not_null<History*> history,
	const QString &text,
	MsgId replyTo);

[[nodiscard]] std::vector<MenuItem> MenuItems(const QString &type);
void ClickMenuItem(const QString &id, QJsonObject context);

[[nodiscard]] int64 DialogId(not_null<PeerData*> peer);
[[nodiscard]] QJsonObject MenuContext(not_null<HistoryItem*> item);
[[nodiscard]] bool IsTrustedSource(not_null<PeerData*> peer);
// Files a plugin sent as a GIF are not added to the saved GIFs.
[[nodiscard]] bool SentByPlugin(const QString &filename);

[[nodiscard]] rpl::producer<> listChanges();
[[nodiscard]] rpl::producer<QString> settingsChanges();
void notifyListChanged();

} // namespace Extera::Plugins
