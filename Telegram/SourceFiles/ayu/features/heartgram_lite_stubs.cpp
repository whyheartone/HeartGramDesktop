// HeartGram Lite: AyuGram's message filters, saved deleted/edited messages,
// noforwards-bypassing forward and the offline-after-online worker are not
// compiled into this build. These no-op definitions answer their callers.
#include "ayu/ayu_worker.h"
#include "ayu/data/messages_storage.h"
#include "ayu/features/filters/filters_cache_controller.h"
#include "ayu/features/filters/filters_controller.h"
#include "ayu/features/filters/filters_utils.h"
#include "ayu/features/forward/ayu_forward.h"
#include "ayu/features/forward/ayu_forward_rich.h"

#ifndef HEARTGRAM_LITE
#error "heartgram_lite_stubs.cpp belongs to the HEARTGRAM_LITE build only."
#endif // HEARTGRAM_LITE

namespace AyuWorker {

void markAsOnline(not_null<Main::Session*> session) {
}

void initialize() {
}

} // namespace AyuWorker

namespace AyuMessages {

void addEditedMessage(not_null<HistoryItem*> item) {
}

std::vector<AyuMessageBase> getEditedMessages(
		not_null<HistoryItem*> item,
		ID minId,
		ID maxId,
		int totalLimit) {
	return {};
}

bool hasRevisions(not_null<HistoryItem*> item) {
	return false;
}

void addDeletedMessage(not_null<HistoryItem*> item) {
}

std::vector<AyuMessageBase> getDeletedMessages(
		not_null<PeerData*> peer,
		ID topicId,
		ID minId,
		ID maxId,
		int totalLimit,
		const QString &searchQuery) {
	return {};
}

bool hasDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	return false;
}

void removeDeletedMessage(not_null<HistoryItem*> item) {
}

void clearDeletedMessages(not_null<PeerData*> peer, ID topicId) {
}

} // namespace AyuMessages

namespace FiltersController {

bool isEnabled(not_null<PeerData*> peer) {
	return false;
}

bool isBlocked(not_null<HistoryItem*> item) {
	return false;
}

bool isBlocked(not_null<PeerData*> peer) {
	return false;
}

bool filtered(not_null<HistoryItem*> historyItem) {
	return false;
}

std::optional<bool> filteredMessagesShown(not_null<PeerData*> peer) {
	return std::nullopt;
}

void toggleFilteredMessagesShown(not_null<PeerData*> peer) {
}

void invalidate(not_null<HistoryItem*> item) {
}

} // namespace FiltersController

namespace FiltersCacheController {

void fireUpdate() {
}

rpl::producer<> updates() {
	return rpl::never<>();
}

void rebuildCache() {
}

std::shared_ptr<const Cache> snapshot() {
	static const auto empty = std::make_shared<const Cache>();
	return empty;
}

std::unordered_map<long long, std::unordered_set<HashablePattern, PatternHasher>> buildExclusions(
		const std::vector<RegexFilterGlobalExclusion> &exclusions,
		const std::vector<HashablePattern> &shared) {
	return {};
}

std::optional<bool> isFiltered(not_null<HistoryItem*> item) {
	return false;
}

bool hasFilteredMessages(not_null<PeerData*> peer) {
	return false;
}

void putHiddenBlockedMessage(not_null<HistoryItem*> item) {
}

void putFiltered(
		not_null<HistoryItem*> item,
		const Data::Group *group,
		bool res,
		const std::shared_ptr<const Cache> &matchedCache) {
}

void invalidate(not_null<HistoryItem*> item) {
}

} // namespace FiltersCacheController

void FilterUtils::importFromLink(const QString &link) {
}

void FilterUtils::importFromJson(const QByteArray &json) {
}

void FilterUtils::publishFilters() {
}

QString FilterUtils::exportFilters() {
	return QString();
}

QString FilterUtils::extractAllText(
		not_null<HistoryItem*> item,
		const Data::Group *group) {
	return QString();
}

namespace AyuForward {

bool isForwarding(const PeerId &id) {
	return false;
}

void cancelForward(const PeerId &id, const Main::Session &session) {
}

std::pair<QString, QString> stateName(const PeerId &id) {
	return {};
}

void ForwardState::updateBottomBar(
		const Main::Session &session,
		const PeerId *peer,
		const State &st) {
}

bool isAyuForwardNeeded(const std::vector<not_null<HistoryItem*>> &items) {
	return false;
}

bool isAyuForwardNeeded(not_null<HistoryItem*> item) {
	return false;
}

bool isFullAyuForwardNeeded(not_null<HistoryItem*> item) {
	return false;
}

void intelligentForward(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		const Data::ResolvedForwardDraft &draft) {
}

void forwardMessages(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		bool forwardState,
		const Data::ResolvedForwardDraft &draft) {
}

bool forwardRichMessage(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		const Api::SendAction &action,
		Fn<bool()> cancelled) {
	return false;
}

} // namespace AyuForward
