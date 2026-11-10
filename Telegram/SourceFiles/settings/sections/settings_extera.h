#pragma once

#include "settings/settings_type.h"

namespace Settings {

namespace Builder {
class SectionBuilder;
} // namespace Builder

// Adds exteraGram's categories (General, Appearance, Navigation, Chats,
// Plugins, Other) to another page: HeartGram shows everything in one place.
void BuildExteraCategories(Builder::SectionBuilder &builder);

// Content of exteraGram's pages, appended to AyuGram's pages of the same name
// (HeartGram has one General / Appearance / Chats / Other page, not two).
void BuildExteraGeneral(Builder::SectionBuilder &builder);
void BuildExteraAppearance(Builder::SectionBuilder &builder);
void AddExteraMaterialGroup(Builder::SectionBuilder &builder);
void BuildExteraChats(Builder::SectionBuilder &builder);
void BuildExteraOther(Builder::SectionBuilder &builder);

[[nodiscard]] Type ExteraId();
[[nodiscard]] Type ExteraGeneralId();
[[nodiscard]] Type ExteraAppearanceId();
[[nodiscard]] Type ExteraChatsId();
[[nodiscard]] Type ExteraPluginsId();
[[nodiscard]] Type ExteraOtherId();
[[nodiscard]] Type ExteraPluginsEngineId();
[[nodiscard]] Type ExteraNavigationId();
[[nodiscard]] Type ExteraAiChatId();

} // namespace Settings
