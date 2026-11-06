// Real-time pitch corrector for slowed/sped-up music.
// The game's AudioSource plays at pitch = speed, which changes tempo AND pitch. This undoes the pitch part:
// granular overlap-add with two Hann-windowed grains reading the recent input at rate 1/speed.
// Runs on Unity's audio thread (OnAudioFilterRead); no allocation, no locks.
#pragma once

namespace pitch {
void setSpeed(float speed);  // main thread; 1.0 = bypass
void reset();                // main thread; call after seeking so grains don't smear old audio in
void process(float* data, int frames, int channels);  // audio thread, in place
int channels();              // the output channel count last seen by process (0 = not called yet)
}
