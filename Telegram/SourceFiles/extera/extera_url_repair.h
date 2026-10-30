#pragma once

namespace Extera {

// Another app (the original Telegram or AyuGram) can leave a registration
// for "tg://" behind that points at an exe which no longer exists, and
// Windows keeps using it as the default. Rewrites such stale entries to
// the given executable. Returns the number of entries it fixed.
int RepairStaleUrlHandlers(const QString &executable, const QString &arguments);

} // namespace Extera
