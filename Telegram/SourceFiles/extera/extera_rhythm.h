#pragma once

namespace Extera::Rhythm {

// What the player background reads every frame (the State of exteraGram's
// RhythmTap): smoothed levels plus events that happened since the last read.
struct State {
	float bass = 0.f;
	float mid = 0.f;
	float high = 0.f;
	float energy = 0.f;
	float bpm = 0.f;
	float intensity = 0.5f;
	float beat = 0.f;
	float hat = 0.f;
	bool downbeat = false;
	bool drop = false;
	int phrases = 0;

	void clearEvents() {
		hat = 0.f;
		beat = 0.f;
		drop = false;
		downbeat = false;
		phrases = 0;
	}
};

// Thread: audio loader. PCM that is about to be queued to OpenAL for songs.
void Feed(
	crl::time startMs,
	const char *data,
	int bytes,
	int alFormat,
	int frequency);

// Thread: main. Applies the frames of the sound up to the given position
// to the state, returns true if there was anything new.
bool Read(crl::time positionMs, State &state);

void Reset();

} // namespace Extera::Rhythm
