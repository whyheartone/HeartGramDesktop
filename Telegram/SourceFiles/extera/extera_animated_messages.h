#pragma once

#include "base/basic_types.h"

#include <QtGui/QTransform>
#include <optional>

class HistoryItem;

// Entrance animation of new messages, after the "Animated Messages" plugin
// by Natu: the same twelve styles, speeds, presets and intensity.
namespace Extera::AnimatedMessages {

struct Frame {
	float64 opacity = 1.;
	QTransform transform;
};

void Mark(not_null<HistoryItem*> item);
void NoteOpened();

struct Frames {
	std::optional<Frame> message;
	// For the date separator band (dateBand px at the top of the element).
	std::optional<Frame> date;
};

// Called while painting; empty when the item has no running animation.
[[nodiscard]] Frames FramesFor(
	not_null<HistoryItem*> item,
	QSize size,
	int dateBand);

[[nodiscard]] bool Running();

} // namespace Extera::AnimatedMessages
