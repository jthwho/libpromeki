/**
 * @file      audiotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/audiotestpattern.h>
#include <promeki/audiogen.h>
#include <promeki/ltcencoder.h>
#include <promeki/audio.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

AudioTestPattern::AudioTestPattern(const AudioDesc &desc) : _desc(desc) {
}

AudioTestPattern::~AudioTestPattern() {
        delete _audioGen;
        delete _ltcEncoder;
}

Error AudioTestPattern::configure() {
        delete _audioGen;
        _audioGen = nullptr;
        delete _ltcEncoder;
        _ltcEncoder = nullptr;

        // Drop any AvSync caches so the next create() rebuilds with the
        // current settings (e.g. tone freq / level changes).
        _avSyncToneCache.clear();
        _avSyncSilenceCache.clear();

        if(_mode == LTC || _mode == AvSync) {
                // AvSync embeds LTC alongside the click marker so the
                // inspector / monitor side has both signals to verify
                // — visual sync (the click) and frame-accurate timecode
                // (the LTC) — out of the box from the default TPG
                // configuration.
                _ltcEncoder = new LtcEncoder((int)_desc.sampleRate(), _ltcLevel.toLinearFloat());
        }
        if(_mode == Tone || _mode == Silence) {
                _audioGen = new AudioGen(_desc);
                for(size_t ch = 0; ch < _desc.channels(); ch++) {
                        AudioGen::Config cfg;
                        if(_mode == Tone) {
                                cfg.type = AudioGen::Sine;
                                cfg.freq = (float)_toneFreq;
                                cfg.level = _toneLevel;
                                cfg.phase = 0.0f;
                                cfg.dutyCycle = 0.0f;
                        } else {
                                cfg.type = AudioGen::Silence;
                                cfg.freq = 0.0f;
                                cfg.level = AudioLevel();
                                cfg.phase = 0.0f;
                                cfg.dutyCycle = 0.0f;
                        }
                        _audioGen->setConfig(ch, cfg);
                }
        }
        // AvSync requires no eager setup — buildAvSyncCache() runs on
        // the first create() call once the per-frame sample count is
        // known.
        return Error::Ok;
}

const Audio &AudioTestPattern::avSyncTone(size_t samples) const {
        auto it = _avSyncToneCache.find(samples);
        if(it != _avSyncToneCache.end()) return it->second;

        // Tone buffer: a one-shot AudioGen so its phase doesn't get
        // tangled up with anything else.  Each "marker" frame returns
        // the same identical tone burst.
        AudioGen burst(_desc);
        for(size_t ch = 0; ch < _desc.channels(); ch++) {
                AudioGen::Config cfg;
                cfg.type = AudioGen::Sine;
                cfg.freq = (float)_toneFreq;
                cfg.level = _toneLevel;
                cfg.phase = 0.0f;
                cfg.dutyCycle = 0.0f;
                burst.setConfig(ch, cfg);
        }
        _avSyncToneCache.insert(samples, burst.generate(samples));
        return _avSyncToneCache.find(samples)->second;
}

const Audio &AudioTestPattern::avSyncSilence(size_t samples) const {
        auto it = _avSyncSilenceCache.find(samples);
        if(it != _avSyncSilenceCache.end()) return it->second;

        Audio silence(_desc, samples);
        if(silence.isValid()) silence.zero();
        _avSyncSilenceCache.insert(samples, silence);
        return _avSyncSilenceCache.find(samples)->second;
}

Audio AudioTestPattern::create(size_t samples, const Timecode &tc) const {
        if(_mode == AvSync) {
                // AvSync output is now a mix of two signals:
                //   1. The historical "click marker" tone burst on
                //      every channel that isn't the LTC channel,
                //      fired on the first frame of each second so
                //      a viewer can confirm visual A/V alignment.
                //   2. Continuous LTC on _ltcChannel (default 0),
                //      which the @ref MediaIOTask_Inspector A/V sync
                //      check decodes to validate frame-accurate
                //      audio↔video lock.
                // For the LTC line we need to encode this frame's
                // timecode fresh, so the cached silence/tone buffers
                // can't be returned wholesale — we build a per-call
                // mix instead.  The cost is one buffer allocation
                // and a single linear pass per frame, which is
                // negligible compared to the LTC encode itself.
                Audio audio(_desc, samples);
                if(!audio.isValid()) return Audio();
                audio.zero();
                float *out = audio.data<float>();
                const size_t channels = _desc.channels();

                // Pick the LTC channel.  -1 (the @ref MediaConfig
                // "all channels" sentinel) collapses to channel 0 for
                // AvSync mode — putting LTC on every channel would
                // wipe out the click marker, defeating the point.
                int ltcChannel = (_ltcChannel >= 0 && _ltcChannel < (int)channels)
                                         ? _ltcChannel : 0;

                // Mix LTC into the chosen channel.  An invalid TC
                // gracefully degrades to silence (the encoder returns
                // an invalid Audio when it can't encode), which is
                // exactly what we want when the timecode generator is
                // disabled upstream.
                if(_ltcEncoder != nullptr && tc.isValid()) {
                        Audio ltcAudio = _ltcEncoder->encode(tc);
                        if(ltcAudio.isValid()) {
                                const int8_t *ltcData = ltcAudio.data<int8_t>();
                                const size_t  copyN   = ltcAudio.samples() < samples
                                                                ? ltcAudio.samples()
                                                                : samples;
                                for(size_t s = 0; s < copyN; s++) {
                                        out[s * channels + (size_t)ltcChannel] =
                                                static_cast<float>(ltcData[s]) / 127.0f;
                                }
                        }
                }

                // Overlay the AvSync click marker onto the *other*
                // channels.  Mono streams have nowhere to put the
                // click without trampling LTC, so they get LTC only.
                const bool marker = tc.isValid() && tc.frame() == 0;
                if(marker && channels > 1) {
                        const Audio  &tone        = avSyncTone(samples);
                        const float  *toneData    = tone.data<float>();
                        const size_t  toneStride  = tone.desc().channels();
                        for(size_t s = 0; s < samples; s++) {
                                for(size_t c = 0; c < channels; c++) {
                                        if((int)c == ltcChannel) continue;
                                        out[s * channels + c] = toneData[s * toneStride + c];
                                }
                        }
                }

                return audio;
        }

        if(_mode == LTC && _ltcEncoder != nullptr) {
                Audio ltcAudio = _ltcEncoder->encode(tc);
                if(!ltcAudio.isValid()) return Audio();

                // If multi-channel and specific LTC channel, embed in silent multi-ch audio
                if(_desc.channels() > 1 && _ltcChannel >= 0) {
                        Audio audio(_desc, ltcAudio.samples());
                        audio.zero();
                        int8_t *ltcData = ltcAudio.data<int8_t>();
                        float *outData = audio.data<float>();
                        size_t channels = _desc.channels();
                        for(size_t s = 0; s < ltcAudio.samples(); s++) {
                                float val = (float)ltcData[s] / 127.0f;
                                outData[s * channels + (size_t)_ltcChannel] = val;
                        }
                        return audio;
                }
                return ltcAudio;
        }

        if(_audioGen != nullptr) {
                return _audioGen->generate(samples);
        }

        return Audio();
}

Audio AudioTestPattern::create(size_t samples) const {
        return create(samples, Timecode());
}

void AudioTestPattern::render(Audio &audio, const Timecode &tc) const {
        Audio generated = create(audio.samples(), tc);
        if(!generated.isValid()) return;

        // Copy generated data into the target buffer
        size_t bytes = audio.samples() * audio.desc().channels() * audio.desc().bytesPerSample();
        size_t genBytes = generated.samples() * generated.desc().channels() * generated.desc().bytesPerSample();
        size_t copyBytes = (bytes < genBytes) ? bytes : genBytes;
        memcpy(audio.data<uint8_t>(), generated.data<uint8_t>(), copyBytes);
}

void AudioTestPattern::render(Audio &audio) const {
        render(audio, Timecode());
}

Result<AudioTestPattern::Mode> AudioTestPattern::fromString(const String &name) {
        if(name == "tone")    return makeResult(Tone);
        if(name == "silence") return makeResult(Silence);
        if(name == "ltc")     return makeResult(LTC);
        if(name == "avsync")  return makeResult(AvSync);
        return makeError<Mode>(Error::Invalid);
}

String AudioTestPattern::toString(Mode mode) {
        switch(mode) {
                case Tone:    return "tone";
                case Silence: return "silence";
                case LTC:     return "ltc";
                case AvSync:  return "avsync";
        }
        return "unknown";
}

PROMEKI_NAMESPACE_END
