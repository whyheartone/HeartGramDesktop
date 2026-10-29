#pragma once

#include "base/basic_types.h"
#include "data/data_types.h"
#include "info/profile/info_profile_badge.h"

#include <rpl/producer.h>

#include <QtCore/QString>
#include <QtGui/QColor>
#include <optional>
#include <vector>

class PeerData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// Project badges ("hearts" next to names), served by the project's own
// badge list, the same one the phone plugin uses. Drawn as a second badge,
// next to (never instead of) AyuGram's exteraGram badges.
namespace ProjectBadges {

enum class Kind : int {
	Client = 1,
	Official = 2,
	Developer = 3,
	Supporter = 4,
};

struct Badge {
	Kind kind = Kind::Client;
	bool owner = false;
	// Custom emoji document to draw; never empty (a heart when none is set).
	DocumentId emojiId = 0;
};

struct SupportItem {
	QString name;
	QString url; // "copy:<address>" for wallets
	DocumentId emojiId = 0;
};

struct SupportBlock {
	QString note;
	QString amount;
	double amountRub = 0.;
	double tonRub = 0.; // rubles per 1 TON
	QString contact;
	std::vector<SupportItem> items;
};

void Start();

[[nodiscard]] bool Enabled();
[[nodiscard]] QString Project();
[[nodiscard]] SupportBlock Support();

// Cheap: called from paint code.
[[nodiscard]] std::optional<Badge> For(not_null<PeerData*> peer);

[[nodiscard]] rpl::producer<> Updated();

// For the Info::Profile::Badge next to names in profiles, the drawer and
// the settings header.
[[nodiscard]] rpl::producer<Info::Profile::Badge::Content> BadgeContent(
	not_null<PeerData*> peer);
[[nodiscard]] Fn<void()> ClickHandler(not_null<PeerData*> peer);

// Colour of the little sparks around the badge with this emoji.
[[nodiscard]] QColor SparkColor(DocumentId id);

// The emoji the server set for a whole level ("official resource" etc).
[[nodiscard]] DocumentId LevelEmoji(Kind kind);

// Plugin trust list from the badge server: channels plugins may come from,
// and files confirmed by their SHA-256 (lowercase hex).
[[nodiscard]] bool IsTrustedPluginSource(not_null<PeerData*> peer);
[[nodiscard]] bool IsVerifiedPlugin(const QString &sha256);

// "Support the development" box: how to donate and get a badge.
void ShowDonateInfo(not_null<Window::SessionController*> controller);

void ShowAbout(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);


} // namespace ProjectBadges
