// A port of exteraGram's RhythmTap / RhythmAnalyzer: band levels, beats,
// tempo, downbeats, phrases and drops of the song being played, taken from
// the PCM handed to OpenAL.
#include "extera/extera_rhythm.h"

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <complex>
#include <deque>
#include <vector>

namespace Extera::Rhythm {
namespace {

constexpr auto kFormatMono8 = 0x1100;
constexpr auto kFormatMono16 = 0x1101;
constexpr auto kFormatStereo8 = 0x1102;
constexpr auto kFormatStereo16 = 0x1103;
constexpr auto kFormatMonoFloat = 0x10010;
constexpr auto kFormatStereoFloat = 0x10011;

constexpr auto kWindow = 2048;
constexpr auto kHop = 512;
constexpr auto kBands = 24;
constexpr auto kLookahead = 4;
constexpr auto kRelatedRatios = std::array<float, 4>{
	2.f, 0.5f, 1.5f, 0.6666667f };
constexpr auto kFrameCap = 512;

using int64 = long long;

[[nodiscard]] int64 Round(double value) {
	return int64(std::floor(value + 0.5));
}

[[nodiscard]] float Clamp01(float value) {
	return std::max(0.f, std::min(1.f, value));
}

[[nodiscard]] float Square(float value) {
	return value * value;
}

[[nodiscard]] float Decibels(float value) {
	return std::log10(std::max(value, 1e-6f)) * 20.f;
}

struct Frame {
	int64 timeUs = 0;
	float bass = 0.f;
	float mid = 0.f;
	float high = 0.f;
	float energy = 0.f;
	float beat = 0.f;
	float bpm = 0.f;
	float hat = 0.f;
	float intensity = 0.f;
	bool downbeat = false;
	bool phrase = false;
	bool drop = false;
};

class Fft final {
public:
	Fft() : _real(kWindow), _imag(kWindow), _spectrum(kWindow / 2 + 1) {
		const auto n = kWindow;
		_reverse.resize(n);
		auto bits = 0;
		while ((1 << bits) < n) {
			++bits;
		}
		for (auto i = 0; i != n; ++i) {
			auto r = 0;
			for (auto b = 0; b != bits; ++b) {
				if (i & (1 << b)) {
					r |= 1 << (bits - 1 - b);
				}
			}
			_reverse[i] = r;
		}
	}

	void forward(const float *samples) {
		for (auto i = 0; i != kWindow; ++i) {
			_real[i] = samples[_reverse[i]];
			_imag[i] = 0.f;
		}
		for (auto size = 1; size < kWindow; size *= 2) {
			const auto angle = -M_PI / size;
			const auto wr = float(std::cos(angle));
			const auto wi = float(std::sin(angle));
			auto cr = 1.f;
			auto ci = 0.f;
			for (auto k = 0; k != size; ++k) {
				for (auto i = k; i < kWindow; i += size * 2) {
					const auto j = i + size;
					const auto tr = cr * _real[j] - ci * _imag[j];
					const auto ti = _imag[j] * cr + _real[j] * ci;
					_real[j] = _real[i] - tr;
					_imag[j] = _imag[i] - ti;
					_real[i] += tr;
					_imag[i] += ti;
				}
				const auto nr = cr * wr - ci * wi;
				ci = ci * wr + cr * wi;
				cr = nr;
			}
		}
		for (auto i = 0; i != int(_spectrum.size()); ++i) {
			_spectrum[i] = std::sqrt(_real[i] * _real[i] + _imag[i] * _imag[i]);
		}
	}

	[[nodiscard]] float band(int index) const {
		return _spectrum[index];
	}

private:
	std::vector<int> _reverse;
	std::vector<float> _real;
	std::vector<float> _imag;
	std::vector<float> _spectrum;

};

class Analyzer final {
public:
	explicit Analyzer(std::vector<Frame> &out) : _out(out) {
		for (auto i = 0; i != kWindow; ++i) {
			_window[i] = float(0.5 - std::cos(i * 2. * M_PI / 2047.) * 0.5);
		}
		reset(0);
	}

	void reset(int rate);
	void process(const float *data, int count, int64 baseUs);

private:
	void analyze(int64 timeUs);
	[[nodiscard]] float getBpm() const {
		return _locked ? _bpm : 0.f;
	}
	[[nodiscard]] double period() const {
		return double(_fps) * 60. / double(_bpm);
	}
	[[nodiscard]] int beatWindow(double period) const {
		return std::max(2, std::min(_beatWindowMax, int(Round(period * 0.12f))));
	}
	[[nodiscard]] float smoothing(float seconds) const {
		return 1.f - float(std::exp(-1. / double(_fps * seconds)));
	}
	[[nodiscard]] float normalize(int index, float value) {
		_levelPeaks[index] = std::max(value, _levelPeaks[index] * 0.999f);
		return value / std::max(_levelPeaks[index], 1e-4f);
	}
	[[nodiscard]] bool pending(int64 frame) const {
		return frame > _frame - _delay && frame <= _frame;
	}

	void addEvent(int64 frame, float weight);
	void emitBeat(int64 frame, float strength);
	[[nodiscard]] float energyAt(int64 frame) const;
	[[nodiscard]] int findOnset(int64 from, int64 to) const;
	[[nodiscard]] float maxNovelty(int64 from, int64 to) const;
	[[nodiscard]] double bestGrid(int64 frame) const;
	void checkPhase(int64 frame);
	void gridStep(int64 frame, float strength);
	void intervalChroma(int64 from, int64 to);
	[[nodiscard]] bool isRelatedTempo(float bpm) const;
	void pickHat(int64 frame);
	void pickOnset(int64 frame);
	void realignGrid(double beat);
	void track(int64 frame);
	void updateSections(float rms, float brightness, int slot);
	void updateTempo();

	std::vector<Frame> &_out;

	std::array<float, kWindow> _window = {};
	std::array<float, kWindow> _samples = {};
	std::array<float, kWindow> _frameSamples = {};
	Fft _fft;
	std::array<int, kBands> _bandFrom = {};
	std::array<int, kBands> _bandTo = {};
	std::array<float, kBands> _bandWeight = {};
	std::array<int, kBands> _bandKind = {};
	std::array<bool, kBands> _bandHat = {};
	std::vector<float> _bandHistory = std::vector<float>(1024 * kBands);
	std::array<float, kBands> _bandLevel = {};
	std::array<std::array<float, kBands>, 2> _pastLevels = {};
	std::array<float, 1024> _novelty = {};
	std::array<float, 1024> _rawNovelty = {};
	std::array<float, 1024> _lowNovelty = {};
	std::array<float, 1024> _hatNovelty = {};
	std::array<float, 1024> _hatRaw = {};
	std::array<float, 1024> _intensityHistory = {};
	std::array<float, 1024> _autocorrelation = {};
	std::array<double, 1024> _tempoScores = {};
	std::array<float, 1024> _centered = {};
	std::array<float, 4> _levelPeaks = {};
	std::array<int64, 16> _onsetFrame = {};
	std::array<float, 16> _onsetStrength = {};
	std::array<int64, 128> _eventFrame = {};
	std::array<float, 128> _eventWeight = {};
	std::array<int64, 64> _pendingTime = {};
	std::array<std::array<float, 64>, 4> _pendingLevels = {};
	std::array<float, 64> _pendingBeat = {};
	std::array<float, 64> _pendingHat = {};
	std::array<float, 64> _pendingIntensity = {};
	std::array<bool, 64> _pendingDownbeat = {};
	std::array<bool, 64> _pendingPhrase = {};
	std::array<bool, 64> _pendingDrop = {};
	std::array<float, 4> _barScores = {};
	std::array<int64, 2> _gridFrames = {};
	std::array<float, kBands> _previousChroma = {};
	std::array<float, kBands> _currentChroma = {};
	std::array<float, 256> _column = {};

	int _sampleRate = 0;
	float _fps = 0.f;
	int _sinceHop = 0;
	int _filled = 0;
	int _position = 0;
	int64 _frame = 0;
	float _harmonicPeak = 0.f;
	float _hatPeak = 0.f;
	float _lowNoveltyPeak = 0.f;
	float _noveltyPeak = 0.f;
	int _eventHead = 0;
	int _eventCount = 0;
	int _onsetHead = 0;
	int _onsetCount = 0;
	int64 _lastDropFrame = 0;
	int64 _lastHatFrame = 0;
	int64 _lastOnsetFrame = 0;
	int _gridFrameCount = 0;
	bool _hasPreviousChroma = false;
	int64 _grid = 0;
	float _gridBpm = 0.f;
	int _downbeatCount = 0;
	int _lastPhraseDownbeat = -16;
	float _lastPhraseIntensity = 0.f;
	float _brightness = 0.f;
	float _referenceEnergy = 0.f;
	float _slowEnergy = 0.f;
	float _fastEnergy = 0.f;
	float _shortEnergy = 0.f;
	float _rangeHigh = -1.f;
	float _rangeLow = -1.f;
	float _candidateBpm = 0.f;
	float _confidence = 0.f;
	float _bpm = 0.f;
	int _candidateCount = 0;
	int64 _lastTempoFrame = 0;
	bool _locked = false;
	int _phaseDisagreements = 0;
	int _misses = 0;
	double _nextBeat = 0.;
	int _beatWindowMax = 2;
	int _delay = 0;

};

void Analyzer::reset(int rate) {
	_sampleRate = rate;
	_fps = (rate > 0) ? (rate / 512.f) : 0.f;
	_sinceHop = 0;
	_filled = 0;
	_position = 0;
	_frame = 0;
	_harmonicPeak = _hatPeak = _lowNoveltyPeak = _noveltyPeak = 1e-4f;
	_levelPeaks.fill(1e-4f);
	for (auto &levels : _pastLevels) {
		levels.fill(0.f);
	}
	_novelty.fill(0.f);
	_rawNovelty.fill(0.f);
	_lowNovelty.fill(0.f);
	_hatNovelty.fill(0.f);
	_hatRaw.fill(0.f);
	_intensityHistory.fill(0.f);
	std::fill(_bandHistory.begin(), _bandHistory.end(), 0.f);
	_barScores.fill(0.f);
	_eventHead = _eventCount = _onsetHead = _onsetCount = 0;
	_lastDropFrame = _lastHatFrame = _lastOnsetFrame
		= -4611686018427387904LL;
	_gridFrameCount = 0;
	_hasPreviousChroma = false;
	_grid = 0;
	_gridBpm = 0.f;
	_downbeatCount = 0;
	_lastPhraseDownbeat = -16;
	_lastPhraseIntensity = 0.f;
	_brightness = _referenceEnergy = 0.f;
	_slowEnergy = _fastEnergy = _shortEnergy = 0.f;
	_rangeHigh = _rangeLow = -1.f;
	_candidateBpm = _confidence = _bpm = 0.f;
	_candidateCount = 0;
	_lastTempoFrame = 0;
	_locked = false;
	_phaseDisagreements = _misses = 0;
	if (rate <= 0) {
		return;
	}
	_beatWindowMax = std::max(2, int(Round(_fps * 0.05f)));
	_delay = std::min(63, kLookahead + _beatWindowMax * 2 + 2);
	const auto binWidth = rate / 2048.f;
	const auto step = std::pow(400., 0.041666666666666664);
	auto previous = 1;
	for (auto i = 0; i != kBands; ++i) {
		const auto from = std::pow(step, i) * 30.;
		const auto to = from * step;
		const auto low = std::max(previous, int(std::floor(from / binWidth)));
		previous = std::min(
			1023,
			std::max(low + 1, int(std::ceil(to / binWidth))));
		_bandFrom[i] = low;
		_bandTo[i] = previous;
		const auto center = std::sqrt(from * to);
		const auto kind = (center < 150.) ? 0 : (center < 2000.) ? 1 : 2;
		_bandKind[i] = kind;
		_bandWeight[i] = (kind == 0) ? 1.f : (kind == 1) ? 0.6f : 0.4f;
		_bandHat[i] = (center > 5000.);
	}
}

void Analyzer::process(const float *data, int count, int64 baseUs) {
	if (_sampleRate <= 0) {
		return;
	}
	for (auto i = 0; i != count; ++i) {
		_samples[_position] = data[i];
		_position = (_position + 1) & (kWindow - 1);
		if (_filled < kWindow) {
			++_filled;
		}
		++_sinceHop;
		if (_sinceHop >= kHop && _filled == kWindow) {
			_sinceHop = 0;
			analyze(((i + 1) * 1000000LL) / _sampleRate
				+ baseUs
				- 1024000000LL / _sampleRate);
		}
	}
}

void Analyzer::addEvent(int64 frame, float weight) {
	const auto slot = (_eventHead + _eventCount) % 128;
	if (_eventCount == 128) {
		_eventHead = (_eventHead + 1) % 128;
	} else {
		++_eventCount;
	}
	_eventFrame[slot] = frame;
	_eventWeight[slot] = weight;
}

void Analyzer::emitBeat(int64 frame, float strength) {
	if (!pending(frame)) {
		return;
	}
	auto &beat = _pendingBeat[frame % 64];
	beat = std::max(beat, std::min(1.f, strength));
}

float Analyzer::energyAt(int64 frame) const {
	return pending(frame) ? _pendingLevels[3][frame % 64] : 0.f;
}

int Analyzer::findOnset(int64 from, int64 to) const {
	auto result = -1;
	for (auto i = 0; i != _onsetCount; ++i) {
		const auto slot = (_onsetHead + i) % 16;
		const auto frame = _onsetFrame[slot];
		if (frame >= from && frame <= to) {
			if (result < 0 || _onsetStrength[slot] > _onsetStrength[result]) {
				result = slot;
			}
		}
	}
	return result;
}

float Analyzer::maxNovelty(int64 from, int64 to) const {
	auto result = 0.f;
	for (auto i = std::max<int64>(0, from); i <= to && i <= _frame; ++i) {
		result = std::max(result, _novelty[i & 1023]);
	}
	return result;
}

double Analyzer::bestGrid(int64 frame) const {
	const auto p = period();
	const auto count = std::max(2, int(double(_fps * 4.f) / p));
	auto best = double(frame);
	auto bestScore = -1.;
	for (auto phase = 0; phase < int(std::ceil(p)); ++phase) {
		auto score = 0.;
		for (auto k = 0; k != count; ++k) {
			const auto at = Round(double(frame - phase) - k * p);
			if (at < 1 || at > _frame) {
				continue;
			}
			auto peak = 0.f;
			for (auto j = at - 1; j <= std::min(at + 1, _frame); ++j) {
				const auto i = j & 1023;
				peak = std::max(peak, _novelty[i] + _lowNovelty[i] * 1.5f);
			}
			score += peak;
		}
		if (score > bestScore) {
			best = double(frame - phase);
			bestScore = score;
		}
	}
	return best;
}

void Analyzer::checkPhase(int64 frame) {
	if (!_locked) {
		return;
	}
	const auto p = period();
	const auto grid = bestGrid(frame);
	auto delta = std::fmod(_nextBeat - grid, p);
	if (delta < 0.) {
		delta += p;
	}
	if (std::min(delta, p - delta) <= beatWindow(p)) {
		_phaseDisagreements = 0;
		return;
	}
	if (++_phaseDisagreements >= 2) {
		_phaseDisagreements = 0;
		_nextBeat = grid + p;
		realignGrid(_nextBeat);
	}
}

void Analyzer::intervalChroma(int64 from, int64 to) {
	const auto count = int(std::min<int64>(256, to - from));
	for (auto i = 0; i != kBands; ++i) {
		if (_bandKind[i] == 2) {
			_currentChroma[i] = 0.f;
			continue;
		}
		for (auto k = 0; k != count; ++k) {
			_column[k] = _bandHistory[int((k + from) & 1023) * kBands + i];
		}
		std::sort(_column.begin(), _column.begin() + count);
		_currentChroma[i] = _column[count / 2];
	}
}

void Analyzer::gridStep(int64 frame, float strength) {
	const auto slot = int(_grid % 4);
	auto peak = 0.f;
	for (auto j = frame - 1; j <= frame + 1; ++j) {
		peak = std::max(peak, _lowNovelty[j & 1023]);
	}
	_barScores[slot] = _barScores[slot] * 0.85f + peak * 0.07499999f;
	_gridBpm = _bpm;
	if (_gridFrameCount == 2) {
		_gridFrames[0] = _gridFrames[1];
		_gridFrames[1] = frame;
	} else {
		_gridFrames[_gridFrameCount++] = frame;
	}
	if (_gridFrameCount == 2) {
		const auto from = _gridFrames[0];
		const auto to = _gridFrames[1];
		if (to - from >= 4) {
			intervalChroma(from, to);
			if (_hasPreviousChroma) {
				auto sum = 0.f;
				auto count = 0;
				for (auto i = 0; i != kBands; ++i) {
					if (_bandKind[i] != 2) {
						sum += std::abs(_currentChroma[i] - _previousChroma[i]);
						++count;
					}
				}
				const auto change = sum / std::max(1, count);
				_harmonicPeak = std::max(change, _harmonicPeak * 0.99f);
				const auto prev = int((_grid + 3) % 4);
				_barScores[prev] = _barScores[prev] * 0.85f
					+ (change * 0.14999998f) / std::max(_harmonicPeak, 1e-4f);
			}
			_previousChroma = _currentChroma;
			_hasPreviousChroma = true;
		}
	}
	++_grid;
	auto best = 0;
	for (auto i = 1; i != 4; ++i) {
		if (_barScores[i] > _barScores[best]) {
			best = i;
		}
	}
	auto second = 0.f;
	for (auto i = 0; i != 4; ++i) {
		if (i != best) {
			second = std::max(second, _barScores[i]);
		}
	}
	if (!pending(frame)) {
		return;
	}
	const auto at = int(frame % 64);
	if (_grid >= 8 && _barScores[best] > second * 1.12f && slot == best) {
		_pendingDownbeat[at] = (strength > 0.f);
		++_downbeatCount;
		const auto intensity = _intensityHistory[frame & 1023];
		const auto since = _downbeatCount - _lastPhraseDownbeat;
		if (strength > 0.f
			&& since >= 4
			&& (since >= 8
				|| std::abs(intensity - _lastPhraseIntensity) > 0.3f)) {
			_pendingPhrase[at] = true;
			_lastPhraseDownbeat = _downbeatCount;
			_lastPhraseIntensity = intensity;
		}
	}
	if (strength > 0.f) {
		emitBeat(frame, strength);
	}
}

bool Analyzer::isRelatedTempo(float bpm) const {
	for (const auto ratio : kRelatedRatios) {
		if (std::abs(bpm - _bpm * ratio) / (_bpm * ratio) < 0.05f) {
			return true;
		}
	}
	return false;
}

void Analyzer::pickHat(int64 frame) {
	const auto index = int(frame & 1023);
	const auto value = _hatNovelty[index];
	if (_hatRaw[index] < 0.02f
		|| frame - _lastHatFrame < Round(_fps * 0.06f)) {
		return;
	}
	for (auto j = frame - 2; j <= frame + 2; ++j) {
		const auto other = _hatNovelty[j & 1023];
		if (j < frame && other >= value) {
			return;
		}
		if (j > frame && other > value) {
			return;
		}
	}
	auto sum = 0.f;
	for (auto j = frame - 8; j <= frame + 2; ++j) {
		sum += _hatNovelty[j & 1023];
	}
	if (value < sum / 11.f + 0.12f) {
		return;
	}
	_lastHatFrame = frame;
	if (frame > _frame - _delay) {
		auto &hat = _pendingHat[frame % 64];
		hat = std::max(hat, std::min(1.f, value));
	}
	addEvent(frame, 0.5f);
}

void Analyzer::pickOnset(int64 frame) {
	const auto index = int(frame & 1023);
	const auto value = _novelty[index];
	if (_rawNovelty[index] < 0.02f
		|| frame - _lastOnsetFrame < Round(_fps * 0.05f)) {
		return;
	}
	for (auto j = frame - 3; j <= frame + 3; ++j) {
		const auto other = _novelty[j & 1023];
		if (j < frame && other >= value) {
			return;
		}
		if (j > frame && other > value) {
			return;
		}
	}
	auto sum = 0.f;
	for (auto j = frame - 10; j <= frame + 4; ++j) {
		sum += _novelty[j & 1023];
	}
	if (value < sum / 15.f + 0.07f) {
		return;
	}
	_lastOnsetFrame = frame;
	const auto slot = (_onsetHead + _onsetCount) % 16;
	if (_onsetCount == 16) {
		_onsetHead = (_onsetHead + 1) % 16;
	} else {
		++_onsetCount;
	}
	_onsetFrame[slot] = frame;
	_onsetStrength[slot] = std::min(1.f, value);
	addEvent(frame, 1.f);
}

void Analyzer::realignGrid(double beat) {
	if (_gridFrameCount == 0) {
		return;
	}
	const auto steps = Round(
		(beat - _gridFrames[_gridFrameCount - 1]) / period()) - 1;
	if (steps < -1 || steps > 16) {
		_grid = 0;
		_barScores.fill(0.f);
	} else if (_gridBpm <= 0.f
		|| std::abs(_bpm - _gridBpm) / _gridBpm >= 0.05f) {
		_grid = 0;
		_barScores.fill(0.f);
	} else {
		_grid = std::max<int64>(0, _grid + steps);
	}
	_gridFrameCount = 0;
	_hasPreviousChroma = false;
}

void Analyzer::track(int64 frame) {
	if (_locked) {
		const auto p = period();
		const auto window = beatWindow(p);
		while (_locked) {
			if (double(frame) < double(window) + _nextBeat) {
				return;
			}
			const auto center = Round(_nextBeat);
			const auto from = center - window;
			const auto to = center + window;
			const auto onset = findOnset(from, to);
			if (onset >= 0) {
				gridStep(_onsetFrame[onset], _onsetStrength[onset] * 0.6f + 0.4f);
				_nextBeat = double(_onsetFrame[onset]) + p;
				_misses = 0;
			} else {
				gridStep(
					center,
					(maxNovelty(from, to) >= 0.12f || energyAt(center) >= 0.35f)
						? 0.3f
						: 0.f);
				_nextBeat += p;
				if (++_misses > 4) {
					_locked = false;
				}
			}
		}
		return;
	}
	const auto onset = findOnset(frame, frame);
	if (onset < 0) {
		return;
	}
	const auto strength = _onsetStrength[onset];
	if (_bpm <= 0.f || _confidence < 0.1f) {
		if (strength >= 0.35f) {
			emitBeat(frame, strength * 0.8f);
		}
		return;
	}
	_locked = true;
	_phaseDisagreements = 0;
	_misses = 0;
	const auto p = period();
	const auto grid = bestGrid(frame);
	auto aligned = std::abs(grid - double(frame)) <= double(beatWindow(p));
	auto target = double(frame);
	if (!aligned) {
		target = grid + p;
	}
	realignGrid(target);
	_nextBeat = grid + p;
	if (aligned) {
		gridStep(frame, strength * 0.6f + 0.4f);
	}
}

void Analyzer::updateSections(float rms, float brightness, int slot) {
	if (_frame == 0) {
		_slowEnergy = _fastEnergy = _shortEnergy = rms;
	}
	_shortEnergy += (rms - _shortEnergy) * smoothing(2.f);
	_fastEnergy += (rms - _fastEnergy) * smoothing(0.25f);
	_slowEnergy += (rms - _slowEnergy) * smoothing(4.f);
	_brightness += (brightness - _brightness) * smoothing(2.f);
	_referenceEnergy = std::max(_shortEnergy, _referenceEnergy * 0.99995f);
	const auto old = _frame - Round(_fps * 2.f);
	while (_eventCount > 0 && _eventFrame[_eventHead] < old) {
		_eventHead = (_eventHead + 1) % 128;
		--_eventCount;
	}
	auto weights = 0.f;
	for (auto i = 0; i != _eventCount; ++i) {
		weights += _eventWeight[(_eventHead + i) % 128];
	}
	const auto level = Clamp01((Decibels(_shortEnergy) + 30.f) / 22.f) * 0.45f
		+ Clamp01((weights / 2.f) / 8.f) * 0.35f
		+ Clamp01(_brightness / 3.f) * 0.2f;
	if (_rangeLow < 0.f) {
		_rangeHigh = _rangeLow = level;
	}
	_rangeHigh = (level > _rangeHigh) ? level : (_rangeHigh - 1.2e-4f);
	_rangeLow = (level < _rangeLow) ? level : (_rangeLow + 1.2e-4f);
	const auto intensity = (_frame < Round(_fps * 3.f))
		? 0.5f
		: Clamp01(level * 0.45f
			+ ((_rangeHigh - _rangeLow > 0.12f)
				? (level - _rangeLow) / (_rangeHigh - _rangeLow)
				: 0.5f) * 0.55f);
	_intensityHistory[_frame & 1023] = intensity;
	_pendingIntensity[slot] = intensity;
	const auto before = _frame - Round(_fps * 2.f);
	if (_frame - _lastDropFrame <= Round(_fps * 6.f)
		|| _frame <= Round(_fps * 3.f)) {
		return;
	}
	if (_fastEnergy <= _slowEnergy * 1.8f
		|| _intensityHistory[before & 1023] >= 0.45f
		|| _fastEnergy <= _referenceEnergy * 0.5f) {
		return;
	}
	_lastDropFrame = _frame;
	_pendingDrop[slot] = true;
}

void Analyzer::updateTempo() {
	const auto count = int(std::min<int64>(
		std::min<int64>(_frame, 1024),
		Round(_fps * 6.f)));
	const auto floorLag = int(std::floor(_fps * 60.f / 200.f));
	const auto ceilLag = int(std::ceil(_fps * 60.f / 60.f));
	const auto top = ceilLag + 1;
	const auto lags = top * 2;
	if (count <= lags + 1) {
		return;
	}
	auto mean = 0.;
	for (auto i = 0; i != count; ++i) {
		mean += _novelty[(_frame - count + i) & 1023];
	}
	mean /= count;
	for (auto i = 0; i != count; ++i) {
		_centered[i] = float(_novelty[(_frame - count + i) & 1023] - mean);
	}
	for (auto lag = 0; lag <= lags; ++lag) {
		auto sum = 0.;
		for (auto i = lag; i < count; ++i) {
			sum += _centered[i] * _centered[i - lag];
		}
		_autocorrelation[lag] = float(sum / (count - lag));
	}
	const auto zero = _autocorrelation[0];
	if (zero <= 0.f) {
		return;
	}
	auto best = -1;
	auto bestScore = 0.;
	for (auto lag = std::max(1, floorLag - 1); lag <= top; ++lag) {
		const auto octave = std::log(
			(double(_fps) * 60. / lag) / 120.) / std::log(2.);
		const auto score = std::exp(octave * -0.5 * octave)
			* (double(std::max(0.f, _autocorrelation[lag]))
				+ double(std::max(0.f, _autocorrelation[lag * 2])) * 0.5);
		_tempoScores[lag] = score;
		if (lag >= floorLag && lag <= ceilLag && score > bestScore) {
			best = lag;
			bestScore = score;
		}
	}
	if (best < 0) {
		return;
	}
	auto lag = double(best);
	const auto left = _tempoScores[best - 1];
	const auto right = _tempoScores[best + 1];
	const auto curve = (left - bestScore * 2.) + right;
	if (curve < 0.) {
		lag += std::max(-0.5, std::min(0.5, ((left - right) * 0.5) / curve));
	}
	auto bpm = float(double(_fps) * 60. / lag);
	_confidence = std::max(0.f, _autocorrelation[best]) / zero;
	if (_bpm <= 0.f || std::abs(bpm - _bpm) / _bpm < 0.05f) {
		if (_bpm > 0.f) {
			bpm = (bpm - _bpm) * 0.2f + _bpm;
		}
		_bpm = bpm;
		_candidateCount = 0;
		return;
	}
	if (_candidateBpm <= 0.f
		|| std::abs(bpm - _candidateBpm) / _candidateBpm >= 0.05f) {
		_candidateBpm = bpm;
		_candidateCount = 1;
		return;
	}
	if (++_candidateCount >= (isRelatedTempo(bpm) ? 4 : 2)) {
		_bpm = bpm;
		_candidateCount = 0;
		_locked = false;
	}
}

void Analyzer::analyze(int64 timeUs) {
	auto sumSquares = 0.;
	for (auto i = 0; i != kWindow; ++i) {
		const auto value = _samples[(_position + i) & (kWindow - 1)];
		if (i >= 1536) {
			sumSquares += double(value) * value;
		}
		_frameSamples[i] = value * _window[i];
	}
	_fft.forward(_frameSamples.data());
	const auto rms = float(std::sqrt(sumSquares / 512.));
	const auto silent = (rms < 1e-4f);
	auto &past = _pastLevels[_frame % 2];
	const auto index = int(_frame & 1023);
	auto weighted = 0.f, weights = 0.f;
	auto lowFlux = 0.f, hatFlux = 0.f;
	auto lowCount = 0, hatCount = 0;
	auto sums = std::array<float, 3>{};
	auto counts = std::array<int, 3>{};
	for (auto i = 0; i != kBands; ++i) {
		auto band = 0.f;
		for (auto k = _bandFrom[i]; k < _bandTo[i]; ++k) {
			band += _fft.band(k);
		}
		const auto level = silent
			? 0.f
			: float(std::log1p(
				((band * 1000.f) * 0.001953125f) / (_bandTo[i] - _bandFrom[i])));
		_bandLevel[i] = level;
		_bandHistory[index * kBands + i] = level;
		auto reference = past[i];
		if (i > 0) {
			reference = std::max(reference, past[i - 1]);
		}
		if (i < kBands - 1) {
			reference = std::max(reference, past[i + 1]);
		}
		const auto flux = std::max(0.f, level - reference);
		weighted += _bandWeight[i] * flux;
		weights += _bandWeight[i];
		const auto kind = _bandKind[i];
		if (kind == 0) {
			lowFlux += flux;
			++lowCount;
		}
		if (_bandHat[i]) {
			hatFlux += flux;
			++hatCount;
		}
		sums[kind] += level;
		++counts[kind];
	}
	past = _bandLevel;

	const auto novelty = weighted / weights;
	_rawNovelty[index] = novelty;
	_noveltyPeak = std::max(novelty, _noveltyPeak * 0.998f);
	_novelty[index] = silent ? 0.f : novelty / std::max(_noveltyPeak, 1e-4f);
	const auto low = lowFlux / std::max(1, lowCount);
	_lowNoveltyPeak = std::max(low, _lowNoveltyPeak * 0.998f);
	_lowNovelty[index] = silent
		? 0.f
		: low / std::max(_lowNoveltyPeak, 1e-4f);
	const auto hat = hatFlux / std::max(1, hatCount);
	_hatRaw[index] = hat;
	_hatPeak = std::max(hat, _hatPeak * 0.998f);
	_hatNovelty[index] = silent ? 0.f : hat / std::max(_hatPeak, 1e-4f);

	const auto slot = int(_frame % 64);
	_pendingTime[slot] = timeUs;
	_pendingBeat[slot] = 0.f;
	_pendingHat[slot] = 0.f;
	_pendingDownbeat[slot] = false;
	_pendingPhrase[slot] = false;
	_pendingDrop[slot] = false;
	_pendingLevels[0][slot] = silent
		? 0.f
		: Square(normalize(0, sums[0] / std::max(1, counts[0])));
	_pendingLevels[1][slot] = silent
		? 0.f
		: Square(normalize(1, sums[1] / std::max(1, counts[1])));
	_pendingLevels[2][slot] = silent
		? 0.f
		: Square(normalize(2, sums[2] / std::max(1, counts[2])));
	_pendingLevels[3][slot] = silent ? 0.f : normalize(3, rms);
	updateSections(
		rms,
		silent ? 0.f : sums[2] / std::max(1, counts[2]),
		slot);

	const auto ready = _frame - kLookahead;
	if (ready >= 10) {
		pickOnset(ready);
		pickHat(ready);
		if (_frame - _lastTempoFrame >= Round(_fps * 0.5f)
			&& _frame >= Round(_fps * 3.f)) {
			_lastTempoFrame = _frame;
			updateTempo();
			checkPhase(ready);
		}
		track(ready);
	}
	const auto output = _frame - _delay;
	if (output >= 0) {
		const auto at = int(output % 64);
		auto frame = Frame();
		frame.timeUs = _pendingTime[at];
		frame.bass = _pendingLevels[0][at];
		frame.mid = _pendingLevels[1][at];
		frame.high = _pendingLevels[2][at];
		frame.energy = _pendingLevels[3][at];
		frame.beat = _pendingBeat[at];
		frame.bpm = getBpm();
		frame.hat = _pendingHat[at];
		frame.intensity = _pendingIntensity[at];
		frame.downbeat = _pendingDownbeat[at];
		frame.phrase = _pendingPhrase[at];
		frame.drop = _pendingDrop[at];
		_out.push_back(frame);
	}
	++_frame;
}

struct Shared {
	QMutex mutex;
	std::vector<Frame> fresh;
	Analyzer analyzer = Analyzer(fresh);
	std::deque<Frame> frames;
	int rate = 0;
	crl::time expected = -1;
	std::vector<float> mono;
};

Shared &Instance() {
	static auto result = Shared();
	return result;
}

[[nodiscard]] float ReadSample(
		const char *data,
		int index,
		int channels,
		int format) {
	auto sum = 0.f;
	for (auto c = 0; c != channels; ++c) {
		const auto i = index * channels + c;
		switch (format) {
		case kFormatMono8:
		case kFormatStereo8:
			sum += (uchar(data[i]) - 128) / 128.f;
			break;
		case kFormatMono16:
		case kFormatStereo16:
			sum += reinterpret_cast<const qint16*>(data)[i] / 32768.f;
			break;
		default:
			sum += reinterpret_cast<const float*>(data)[i];
			break;
		}
	}
	return sum / channels;
}

} // namespace

void Feed(
		crl::time startMs,
		const char *data,
		int bytes,
		int alFormat,
		int frequency) {
	auto channels = 0;
	auto size = 0;
	switch (alFormat) {
	case kFormatMono8: channels = 1; size = 1; break;
	case kFormatStereo8: channels = 2; size = 1; break;
	case kFormatMono16: channels = 1; size = 2; break;
	case kFormatStereo16: channels = 2; size = 2; break;
	case kFormatMonoFloat: channels = 1; size = 4; break;
	case kFormatStereoFloat: channels = 2; size = 4; break;
	default: return;
	}
	if (!data || frequency <= 0) {
		return;
	}
	const auto frames = bytes / (channels * size);
	if (frames <= 0) {
		return;
	}
	auto &state = Instance();
	QMutexLocker lock(&state.mutex);
	if (state.rate != frequency
		|| state.expected < 0
		|| std::abs(startMs - state.expected) > 250) {
		state.rate = frequency;
		state.analyzer.reset(frequency);
		state.frames.clear();
		state.fresh.clear();
	}
	state.expected = startMs + crl::time(frames * 1000LL / frequency);
	state.mono.resize(frames);
	for (auto i = 0; i != frames; ++i) {
		state.mono[i] = ReadSample(data, i, channels, alFormat);
	}
	state.analyzer.process(
		state.mono.data(),
		frames,
		startMs * 1000LL);
	for (const auto &frame : state.fresh) {
		state.frames.push_back(frame);
	}
	state.fresh.clear();
	while (state.frames.size() > kFrameCap) {
		state.frames.pop_front();
	}
}

bool Read(crl::time positionMs, State &state) {
	auto &shared = Instance();
	QMutexLocker lock(&shared.mutex);
	const auto target = positionMs * 1000LL + 40000;
	auto any = false;
	while (!shared.frames.empty() && shared.frames.front().timeUs <= target) {
		const auto &frame = shared.frames.front();
		state.bass = frame.bass;
		state.mid = frame.mid;
		state.high = frame.high;
		state.energy = frame.energy;
		state.bpm = frame.bpm;
		state.intensity = frame.intensity;
		if (target - frame.timeUs <= 300000) {
			state.beat = std::max(state.beat, frame.beat);
			state.hat = std::max(state.hat, frame.hat);
			state.downbeat |= frame.downbeat;
			state.drop |= frame.drop;
			if (frame.phrase) {
				++state.phrases;
			}
		}
		shared.frames.pop_front();
		any = true;
	}
	return any;
}

void Reset() {
	auto &state = Instance();
	QMutexLocker lock(&state.mutex);
	state.frames.clear();
	state.fresh.clear();
	state.analyzer.reset(0);
	state.expected = -1;
	state.rate = 0;
}

} // namespace Extera::Rhythm
