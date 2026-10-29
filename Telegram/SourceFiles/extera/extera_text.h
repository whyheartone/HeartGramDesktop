#pragma once

#include "lang/lang_instance.h"

#include <QtCore/QString>

namespace Extera {

// HeartGram's own short texts (credits, about card): Russian for Russian
// language packs (including "ru-raw"), English otherwise. Evaluated when the
// page is built, so a language change applies after reopening the page.
[[nodiscard]] inline QString Text(const QString &ru, const QString &en) {
	const auto &lang = Lang::GetInstance();
	const auto russian = lang.id().startsWith(QLatin1String("ru"))
		|| lang.baseId().startsWith(QLatin1String("ru"));
	return russian ? ru : en;
}

} // namespace Extera
