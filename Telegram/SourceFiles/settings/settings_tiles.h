#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Settings {

// Marks every settings row with its place in a group (first, middle, last):
// the rows draw their own tile from that. Follows the "md3_settings" switch.
void AttachTiles(not_null<Ui::RpWidget*> container);

} // namespace Settings
