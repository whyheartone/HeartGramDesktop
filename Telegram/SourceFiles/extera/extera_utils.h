#pragma once

namespace Extera {

[[nodiscard]] QString FormatTime(const QTime &time);
[[nodiscard]] bool HasZalgo(const QString &text);
[[nodiscard]] QString FilterZalgo(QString text);
// Result for a text ending with "=", shown greyed until Space inserts it.
[[nodiscard]] std::optional<QString> InlineMathPreview(
	const QString &text);

namespace Volume {

// Voice messages and round videos get their own level, apart from songs.
[[nodiscard]] bool Separate();
[[nodiscard]] float64 Voice();
void SetVoice(float64 value);
[[nodiscard]] float64 RememberedVoice();
void SetRememberedVoice(float64 value);
[[nodiscard]] rpl::producer<> Changes();

} // namespace Volume

} // namespace Extera
