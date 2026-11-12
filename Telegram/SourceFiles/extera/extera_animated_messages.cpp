#include "extera/extera_animated_messages.h"

#include "base/flat_map.h"
#include "logs.h"
#include "base/flat_set.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "history/history_item.h"

#include <QtCore/QtMath>

#include <array>
#include <deque>
#include <cmath>

namespace Extera::AnimatedMessages {
namespace {

constexpr auto kMaxEntries = 200;
constexpr auto kMaxConcurrent = size_t(5);
constexpr auto kMaxSeen = size_t(800);
constexpr auto kOpenWindow = crl::time(700);
constexpr auto kOpenStagger = crl::time(35);

struct Entry {
	crl::time created = 0;
	crl::time start = 0;
	crl::time delay = 0;
	bool started = false;
	int duration = 0;
	int style = 0;
	float64 intensity = 1.;
	float64 sign = 1.;
	bool reduce = false;
	bool darkSafe = true;
	int dateStyle = 0;
	int dateDuration = 0;
	float64 dateScale = 0.65;
	float64 dateTension = 4.;
};

// First-in-first-out, so only the oldest keys are forgotten (as in the plugin).
struct SeenSet {
	std::deque<FullMsgId> order;
	base::flat_set<FullMsgId> keys;

	bool add(const FullMsgId &item) {
		if (!keys.emplace(item).second) {
			return false;
		}
		order.push_back(item);
		while (order.size() > kMaxSeen) {
			keys.erase(order.front());
			order.pop_front();
		}
		return true;
	}
};

SeenSet &Seen() {
	static auto result = SeenSet();
	return result;
}

crl::time OpenedAt = 0;
bool OpenedPending = false;
int OpenedCount = 0;

base::flat_map<FullMsgId, Entry> &Entries() {
	static auto result = base::flat_map<FullMsgId, Entry>();
	return result;
}

[[nodiscard]] float64 Clamp(float64 value, float64 a, float64 b) {
	return std::min(std::max(value, a), b);
}

[[nodiscard]] float64 Decelerate(float64 t) {
	return 1. - (1. - t) * (1. - t);
}

[[nodiscard]] float64 AccelerateDecelerate(float64 t) {
	return (std::cos((t + 1.) * M_PI) / 2.) + 0.5;
}

[[nodiscard]] float64 Overshoot(float64 t, float64 tension) {
	t -= 1.;
	return t * t * ((tension + 1.) * t + tension) + 1.;
}

struct Profile {
	int style;
	int speed;
};

[[nodiscard]] Profile PresetProfile(int preset) {
	switch (preset) {
	case 0: return { 0, 1 };
	case 1: return { 4, 1 };
	case 2: return { 6, 2 };
	}
	return { 9, 3 };
}

[[nodiscard]] uint32 Seed(uint64 dialog, bool outgoing) {
	auto h = uint32(2166136261U);
	const auto mix = [&](uint64 value) {
		for (auto i = 0; i != 8; ++i) {
			h = (h ^ uint32(value & 0xFF)) * 16777619U;
			value >>= 8;
		}
	};
	mix(dialog);
	mix(outgoing ? 1 : 0);
	mix(uint64(Int(u"am_profile"_q)));
	return h;
}

[[nodiscard]] Entry MakeEntry(not_null<HistoryItem*> item) {
	static const auto speeds = std::array<float64, 4>{ 0.5, 1., 1.5, 2. };
	static const auto intensities = std::array<float64, 4>{
		0.5, 1., 1.4, 1.8
	};
	const auto outgoing = item->out();
	const auto follow = Bool(u"am_follow_profile"_q);
	const auto profile = PresetProfile(Int(u"am_profile"_q));
	auto style = follow ? profile.style : Int(u"am_style"_q);
	auto speed = follow ? profile.speed : Int(u"am_speed"_q);
	auto intensity = intensities[std::clamp(Int(u"am_intensity"_q), 0, 3)];
	const auto reduce = Bool(u"am_reduce_motion"_q);
	if (Bool(u"am_random_chat"_q)) {
		const auto seed = Seed(item->history()->peer->id.value, outgoing);
		const auto mode = Int(u"am_random_mode"_q);
		if (mode >= 1) {
			style = int((seed ^ 0xA53) % 12);
		}
		if (mode >= 2) {
			speed = int((seed ^ 0xB19) % 4);
		}
	}
	style = std::clamp(style, 0, 11);
	speed = std::clamp(speed, 0, 3);
	if (reduce && (style == 3 || style == 5 || style == 9 || style == 11)) {
		style = follow ? 0 : 4;
	}
	const auto base = speeds[speed];
	const auto it = Clamp(intensity, 0.5, 2.);
	auto duration = int(620 / base);
	duration = int(duration * (1. / std::max(0.75, std::min(it, 1.6))));
	duration = std::clamp(duration, 170, 920);
	static const auto dateDurations = std::array<int, 4>{ 820, 600, 420, 260 };
	static const auto dateTensions = std::array<float64, 4>{ 2., 4., 6., 8. };
	static const auto dateScales = std::array<float64, 4>{ 0.8, 0.65, 0.5, 0.35 };
	const auto dateSpeed = std::clamp(Int(u"am_date_speed"_q), 0, 3);
	return {
		.created = crl::now(),
		.start = crl::now(),
		.duration = duration,
		.style = style,
		.intensity = it,
		.sign = 1.,
		.reduce = reduce,
		.darkSafe = Bool(u"am_dark_safe"_q),
		.dateStyle = std::clamp(Int(u"am_date_style"_q), 0, 3),
		.dateDuration = dateDurations[dateSpeed],
		.dateScale = dateScales[std::clamp(Int(u"am_date_size"_q), 0, 3)],
		.dateTension = dateTensions[dateSpeed],
	};
}

[[nodiscard]] std::optional<Frame> DateFrame(
		const Entry &e,
		crl::time elapsed,
		int width,
		int band) {
	if (elapsed >= e.dateDuration) {
		return std::nullopt;
	}
	const auto linear = elapsed < 0
		? 0.
		: Clamp(float64(elapsed) / e.dateDuration, 0., 1.);
	const auto cx = width / 2.;
	const auto cy = band / 2.;
	auto transform = QTransform();
	transform.translate(cx, cy);
	auto opacity = 1.;
	switch (e.dateStyle) {
	case 0: {
		const auto t = Overshoot(linear, e.dateTension);
		const auto scale = e.dateScale + (1. - e.dateScale) * t;
		transform.translate(0., 14. * (1. - t));
		transform.scale(scale, scale);
		opacity = Clamp(linear * 2., 0., 1.);
	} break;
	case 1: {
		const auto t = Decelerate(linear);
		transform.translate(-48. * (1. - t), 0.);
		opacity = t;
	} break;
	case 2: {
		const auto t = AccelerateDecelerate(linear);
		transform.rotate(90. * (1. - t), Qt::XAxis);
		opacity = Clamp(linear * 2., 0., 1.);
	} break;
	default: {
		const auto pulse = 1. + 0.18 * std::sin(M_PI * linear);
		transform.scale(pulse, pulse);
		opacity = Clamp(linear * 3., 0.35, 1.);
	} break;
	}
	transform.translate(-cx, -cy);
	return Frame{ .opacity = opacity, .transform = transform };
}

} // namespace

void NoteOpened() {
	OpenedAt = 0;
	OpenedPending = true;
	OpenedCount = 0;
}

void Mark(not_null<HistoryItem*> item) {
	if (!Bool(u"am_enabled"_q)) {
		LOG(("AnimatedMessages: Mark skipped, am_enabled is off."));
		return;
	}
	LOG(("AnimatedMessages: Mark %1.").arg(item->id.bare));
	auto &entries = Entries();
	if (entries.size() > kMaxEntries) {
		entries.clear();
	}
	Seen().add(item->fullId());
	auto entry = MakeEntry(item);
	entry.start = 0;
	entries[item->fullId()] = std::move(entry);
}

bool Running() {
	auto &entries = Entries();
	const auto now = crl::now();
	for (auto i = entries.begin(); i != entries.end();) {
		const auto &e = i->second;
		const auto total = std::max(e.duration, e.dateDuration) + e.delay;
		const auto expired = e.started
			? (now - e.start > total)
			: (now - e.created > 3000);
		if (expired) {
			i = entries.erase(i);
		} else {
			++i;
		}
	}
	return !entries.empty();
}

Frames FramesFor(
		not_null<HistoryItem*> item,
		QSize size,
		int dateBand) {
	if (!Bool(u"am_enabled"_q)) {
		return {};
	}
	auto &entries = Entries();
	auto i = entries.find(item->fullId());
	if (i == end(entries) && Seen().add(item->fullId())) {
		if (OpenedPending) {
			OpenedPending = false;
			OpenedAt = crl::now();
		}
		const auto fresh = (base::unixtime::now() - item->date()) < 3;
		// a message we just sent animated when it was created; its real id
		// arriving later must not play it again
		// a screenful appearing at once is not animated: too many rows
		// repainted per frame made it stutter
		const auto crowded = entries.size() >= kMaxConcurrent;
		const auto justSent = item->out()
			&& (base::unixtime::now() - item->date()) < 30;
		if (justSent || (crowded && !fresh)) {
		} else if (fresh || Bool(u"am_on_open"_q)) {
			auto entry = MakeEntry(item);
			const auto opening = OpenedAt
				&& (crl::now() - OpenedAt) < kOpenWindow;
			if (opening && !fresh) {
				// the bottom message comes first, older ones follow
				entry.delay = std::min(OpenedCount++, 8) * kOpenStagger;
			}
			i = entries.emplace(item->fullId(), entry).first;
			LOG(("AnimatedMessages: entry for %1 (fresh %2, opening %3).").arg(item->id.bare).arg(fresh ? 1 : 0).arg(opening ? 1 : 0));
		} else {
			LOG(("AnimatedMessages: %1 not animated (not fresh, am_on_open off).").arg(item->id.bare));
		}
	}
	if (i == end(entries)) {
		return {};
	}
	auto &e = i->second;
	if (!e.started) {
		e.started = true;
		e.start = crl::now() + e.delay;
	}
	const auto elapsed = crl::now() - e.start;
	auto result = Frames();
	if (dateBand > 0 && Bool(u"am_dates"_q)) {
		result.date = DateFrame(e, elapsed, size.width(), dateBand);
	}
	if (elapsed >= std::max(e.duration, dateBand > 0 ? e.dateDuration : 0)) {
		entries.erase(i);
		return {};
	}
	if (elapsed < 0) {
		result.message = Frame{
			.opacity = e.darkSafe ? 0.05 : 0.,
			.transform = QTransform(),
		};
		return result;
	}
	if (elapsed >= e.duration) {
		return result;
	}
	const auto linear = Clamp(float64(elapsed) / e.duration, 0., 1.);
	// the plugin measures a phone-wide cell; here a bubble is much narrower
	const auto w = float64(std::min(size.width(), 420));
	const auto h = float64(size.height());
	const auto it = e.intensity;
	const auto sign = e.sign;
	const auto rm = e.reduce;
	const auto dsafe = e.darkSafe;
	const auto calm = rm ? 0.55 : 1.;

	auto t = Decelerate(linear);
	auto dx = 0., dy = 0., rotation = 0., rotationX = 0., rotationY = 0.;
	auto scale0 = 1.;
	auto wobble = false;
	const auto from = [&](float64 start) {
		return start * (1. - t);
	};

	switch (e.style) {
	case 0: {
		scale0 = rm || dsafe ? 0.90 : (0.86 - 0.08 * (it - 1.));
		scale0 = Clamp(scale0, dsafe ? 0.78 : 0.65, 0.94);
	} break;
	case 1: dx = w * 0.55 * it * sign * calm; break;
	case 2: dy = h * 1.05 * it * calm; break;
	case 3: {
		rotationY = (dsafe ? 55. : 90.) * it * sign * calm;
		scale0 = dsafe ? 0.96 : 0.90;
		t = AccelerateDecelerate(linear);
	} break;
	case 4: {
		dy = h * 0.32 * it * calm;
		auto tension = 1.8 * (0.85 + 0.22 * it);
		if (rm || dsafe) {
			tension = 1.15;
		}
		t = Overshoot(linear, tension);
	} break;
	case 5: {
		if (rm || dsafe) {
			scale0 = 0.90;
		} else {
			rotation = 200. * it * sign;
			scale0 = 0.55;
			t = AccelerateDecelerate(linear);
		}
	} break;
	case 6: {
		rotation = (10. + 8. * it) * sign * calm;
		scale0 = dsafe ? 0.92 : 0.86;
		t = Overshoot(linear, (rm || dsafe) ? 1.05 : 1.6);
	} break;
	case 7: {
		scale0 = dsafe ? 0.90 : 0.78;
		t = Overshoot(linear, (rm || dsafe) ? 1.1 : 2.2);
	} break;
	case 8: dx = -w * 0.55 * it * sign * calm; break;
	case 9: {
		rotationX = (dsafe ? 35. : 60.) * sign * calm;
		scale0 = dsafe ? 0.92 : 0.72;
		t = Overshoot(linear, (rm || dsafe) ? 1.1 : 2.1);
	} break;
	case 10: {
		scale0 = dsafe ? 0.92 : 0.70;
		dy = h * 0.16 * it * calm;
	} break;
	default: {
		dx = w * 0.20 * it * sign * calm;
		wobble = true;
		t = AccelerateDecelerate(linear);
	} break;
	}

	auto angle = from(rotation);
	if (wobble) {
		const auto a1 = (dsafe ? 5. : 8.) * sign * calm;
		// 0 -> a1 -> -a1 -> a1/2 -> 0 over the animation, as in the plugin
		const auto keys = std::array<float64, 5>{ a1, a1, -a1, a1 * 0.5, 0. };
		const auto position = t * 4.;
		const auto index = std::min(int(position), 3);
		const auto local = position - index;
		angle = keys[index] + (keys[index + 1] - keys[index]) * local;
	}
	const auto scale = scale0 + (1. - scale0) * t;
	// pivot near the side the bubble sits on, not the middle of the row
	const auto fullWidth = float64(size.width());
	const auto pivotX = item->out()
		? std::max(fullWidth - 160., fullWidth / 2.)
		: std::min(160., fullWidth / 2.);
	auto transform = QTransform();
	transform.translate(pivotX + from(dx), h / 2. + from(dy));
	if (angle != 0.) {
		transform.rotate(angle);
	}
	if (rotationY != 0.) {
		transform.rotate(from(rotationY), Qt::YAxis);
	}
	if (rotationX != 0.) {
		transform.rotate(from(rotationX), Qt::XAxis);
	}
	transform.scale(scale, scale);
	transform.translate(-pivotX, -h / 2.);
	const auto alpha0 = dsafe ? 0.05 : 0.;
	result.message = Frame{
		.opacity = alpha0 + (1. - alpha0) * Clamp(Decelerate(linear), 0., 1.),
		.transform = transform,
	};
	return result;
}

} // namespace Extera::AnimatedMessages
