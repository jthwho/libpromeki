/**
 * @file      audiodatadecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <promeki/audiodatadecoder.h>
#include <promeki/audioformat.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/crc.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Pull @p sampleCount samples of @p channel out of @p payload
        // into @p out as normalized floats.  Handles both interleaved
        // and planar layouts (one underlying plane buffer with
        // per-channel offset / stride from AudioDesc).  Returns false
        // when the payload is empty / inaccessible.
        bool extractChannel(const PcmAudioPayload &payload, uint32_t channel, uint64_t firstSample, uint64_t sampleCount,
                            std::vector<float> &out) {
                const AudioDesc &desc = payload.desc();
                if (channel >= desc.channels()) return false;
                if (payload.planeCount() < 1) return false;
                if (sampleCount == 0) {
                        out.clear();
                        return true;
                }
                const size_t bufferSamples = payload.sampleCount();
                if (firstSample + sampleCount > bufferSamples) return false;

                const AudioFormat &fmt = desc.format();
                const size_t       bps = fmt.bytesPerSample();
                if (bps == 0) return false;
                const size_t   stride = desc.bytesPerSampleStride();
                const uint8_t *base = payload.data()[0].data();
                if (base == nullptr) return false;
                base += desc.channelBufferOffset(channel, bufferSamples);
                base += static_cast<size_t>(firstSample) * stride;

                out.assign(static_cast<size_t>(sampleCount), 0.0f);

                if (stride == bps) {
                        // Planar — single contiguous run, single conversion call.
                        fmt.samplesToFloat(out.data(), base, static_cast<size_t>(sampleCount));
                        return true;
                }

                // Interleaved — gather strided bytes into a contiguous
                // scratch buffer first, then convert in bulk.  Per-
                // sample conversion through samplesToFloat is correct
                // but most format implementations are bulk-friendly so
                // a single pass amortises better.
                std::vector<uint8_t> scratch(static_cast<size_t>(sampleCount) * bps);
                for (size_t i = 0; i < sampleCount; i++) {
                        std::memcpy(scratch.data() + i * bps, base + i * stride, bps);
                }
                fmt.samplesToFloat(out.data(), scratch.data(), static_cast<size_t>(sampleCount));
                return true;
        }

        // Returns the sub-sample sign-change position between two
        // samples of opposite sign.  Linear interpolation is the right
        // model: the encoder writes hard step edges and the SRC
        // smooths them to a (roughly) linear ramp through zero, so a
        // straight-line fit through the two adjacent samples lands on
        // the actual transition with sub-sample accuracy.
        double subSampleZero(double sLow, double sHigh, float vLow, float vHigh) {
                const double diff = static_cast<double>(vHigh) - static_cast<double>(vLow);
                if (diff == 0.0) return sLow + 0.5 * (sHigh - sLow);
                const double t = -static_cast<double>(vLow) / diff;
                return sLow + t * (sHigh - sLow);
        }

        // ---------------------------------------------------------------------------
        // Sync-nibble localisation and bit-pitch measurement.
        // ---------------------------------------------------------------------------
        //
        // The encoder lays the sync nibble down as Manchester half-bits
        // @c (+, -, -, +, +, -, -, +) — eight half-bits with sign
        // changes at half-bit boundaries 0→1, 2→3, 4→5, 6→7 (no
        // changes at 1→2, 3→4, 5→6 because adjacent half-bits already
        // share the same polarity).  Those four transitions bracket
        // three @c 2H-sample runs that are bounded on both sides by
        // encoder transitions and don't depend on knowing the leading
        // edge accurately — perfect for a sub-sample-accurate
        // bit-width estimate.  Mirrors the image decoder's three-run
        // measurement.
        struct SyncMeasurement {
                        bool     ok = false;
                        Error    error = Error::Ok;
                        double   samplesPerBit = 0.0;
                        double   syncStart = 0.0;
                        uint64_t syncStartSampleInt = 0;
        };

        SyncMeasurement findSync(const std::vector<float> &samples, uint32_t expectedSamplesPerBit) {
                SyncMeasurement r;

                if (samples.size() < 8) {
                        r.error = Error::CorruptData;
                        return r;
                }

                // 1. Locate the first sustained-positive run.  Bare
                // zero-crossing detection is fooled by the small ripple
                // a sample-rate converter leaves in the silence pad
                // between codewords — pre-/post-ringing scatters
                // short-period oscillations there with magnitudes well
                // under the codeword peak.  A real codeword's leading
                // half-bit is @c samplesPerBit/2 same-sign samples, so
                // requiring a sustained run of ~half-bit width filters
                // the ripple while still locking on every legitimate
                // sync edge — including edges whose first sample is
                // smoothed below the codeword peak.  Sized off the
                // constructor hint @c _expectedSamplesPerBit; the
                // hard floor of 2 keeps the smallest legal cell width
                // (samplesPerBit=4 → half-bit=2) workable.
                const size_t minRun =
                        std::max<size_t>(2, static_cast<size_t>(expectedSamplesPerBit) / 2);
                size_t firstPos = 0;
                while (firstPos + minRun <= samples.size()) {
                        bool sustained = true;
                        for (size_t k = 0; k < minRun; ++k) {
                                if (samples[firstPos + k] <= 0.0f) {
                                        sustained = false;
                                        break;
                                }
                        }
                        if (sustained) break;
                        ++firstPos;
                }
                if (firstPos + minRun > samples.size()) {
                        r.error = Error::CorruptData;
                        return r;
                }
                r.syncStartSampleInt = firstPos;

                // 2. Walk forward looking for four sign-flip transitions
                // with alternating direction (+→-, -→+, +→-, -→+).
                // These are the transitions at sync half-bit boundaries
                // 0→1, 2→3, 4→5 and 6→7.  The fourth transition lands
                // safely inside the sync nibble — the next possible
                // transition (at the start of payload bit 63's second
                // half, or further) is at least @c 2H samples beyond.
                constexpr int kWanted = 4;
                double        transitions[kWanted] = {0.0, 0.0, 0.0, 0.0};
                int           tFound = 0;
                bool          wantNeg = true;
                size_t        i = firstPos;
                while (i + 1 < samples.size() && tFound < kWanted) {
                        const float vCur = samples[i];
                        const float vNext = samples[i + 1];
                        const bool  flip = wantNeg ? (vCur >= 0.0f && vNext < 0.0f) : (vCur < 0.0f && vNext >= 0.0f);
                        if (flip) {
                                transitions[tFound++] = subSampleZero(static_cast<double>(i),
                                                                      static_cast<double>(i + 1), vCur, vNext);
                                wantNeg = !wantNeg;
                        }
                        ++i;
                }
                if (tFound < kWanted) {
                        r.error = Error::CorruptData;
                        return r;
                }

                // 3. The three inter-transition distances are each
                // @c 2H = samplesPerBit.  Three independent
                // measurements of the same quantity.
                const double d01 = transitions[1] - transitions[0];
                const double d12 = transitions[2] - transitions[1];
                const double d23 = transitions[3] - transitions[2];
                if (d01 <= 0.0 || d12 <= 0.0 || d23 <= 0.0) {
                        r.error = Error::CorruptData;
                        return r;
                }

                const double sPB = (d01 + d12 + d23) / 3.0;

                // ±25 % per-run tolerance — wide enough to absorb
                // ordinary SRC drift, narrow enough to reject a sync
                // hit on noise.  Same envelope as the image decoder.
                const double samplesArr[3] = {d01, d12, d23};
                for (double d : samplesArr) {
                        const double dev = std::abs(d - sPB) / sPB;
                        if (dev > 0.25) {
                                r.error = Error::CorruptData;
                                return r;
                        }
                }

                r.samplesPerBit = sPB;
                // The first transition at @c transitions[0] sits at the
                // boundary between sync half-bit 0 and half-bit 1, i.e.
                // half a bit-cell after the codeword's leading edge.
                r.syncStart = transitions[0] - sPB * 0.5;
                r.ok = true;
                return r;
        }

} // namespace

AudioDataDecoder::AudioDataDecoder(const AudioDesc &desc, uint32_t expectedSamplesPerBit)
    : _desc(desc), _expectedSamplesPerBit(expectedSamplesPerBit) {
        if (!_desc.isValid()) return;
        if (_desc.isCompressed()) return;
        if (expectedSamplesPerBit < AudioDataEncoder::MinSamplesPerBit ||
            expectedSamplesPerBit > AudioDataEncoder::MaxSamplesPerBit) {
                promekiErr("AudioDataDecoder: expectedSamplesPerBit %u out of range", expectedSamplesPerBit);
                return;
        }
        // ±50 % acceptance band — same generosity the image decoder uses.
        _samplesPerBitMin = std::max<uint32_t>(1, expectedSamplesPerBit / 2);
        _samplesPerBitMax = expectedSamplesPerBit + expectedSamplesPerBit / 2;
        _valid = true;
}

AudioDataDecoder::DecodedItem AudioDataDecoder::decodeOne(const PcmAudioPayload &payload, const Band &band) const {
        DecodedItem item;

        if (band.sampleCount == 0) {
                item.error = Error::InvalidArgument;
                return item;
        }

        // Even at the minimum samples-per-bit the codeword takes
        // BitsPerPacket * MinSamplesPerBit samples.  Reject Bands that
        // can't fit one outright.
        const uint64_t minSpan = static_cast<uint64_t>(BitsPerPacket) * AudioDataEncoder::MinSamplesPerBit;
        if (band.sampleCount < minSpan) {
                item.error = Error::OutOfRange;
                return item;
        }

        std::vector<float> samples;
        if (!extractChannel(payload, band.channel, band.firstSample, band.sampleCount, samples)) {
                item.error = Error::ConversionFailed;
                return item;
        }
        return decodeSamples(samples.data(), samples.size());
}

AudioDataDecoder::DecodedItem AudioDataDecoder::decodeSamples(const float *samplesPtr, size_t count) const {
        DecodedItem item;

        const size_t minSpan = static_cast<size_t>(BitsPerPacket) * AudioDataEncoder::MinSamplesPerBit;
        if (count < minSpan) {
                item.error = Error::OutOfRange;
                return item;
        }

        // findSync needs a vector handle; copy the pointer span into a
        // small std::vector once so the helper signatures stay
        // unchanged.  Decoder is called O(1) times per frame so the
        // copy doesn't show up in profiles.
        std::vector<float> samples(samplesPtr, samplesPtr + count);

        SyncMeasurement sync = findSync(samples, _expectedSamplesPerBit);
        if (!sync.ok) {
                item.error = sync.error;
                return item;
        }

        return demodulate(samples.data(), samples.size(), sync.syncStart, sync.samplesPerBit, sync.syncStartSampleInt);
}

AudioDataDecoder::DecodedItem AudioDataDecoder::demodulate(const float *samples, size_t count, double syncStart,
                                                            double  samplesPerBitVal,
                                                            uint64_t syncStartSampleInt) const {
        DecodedItem item;
        item.samplesPerBit = samplesPerBitVal;
        item.syncStartSample = syncStartSampleInt;
        item.packetSampleCount = static_cast<int64_t>(samplesPerBitVal * static_cast<double>(BitsPerPacket) + 0.5);

        // Bandwidth check against the constructor hint.  A sync that
        // landed on noise — common when a non-PcmMarker channel
        // (LTC, continuous tone, ...) lets findSync hit four
        // arbitrary transitions — typically produces a samplesPerBit
        // far outside the expected band.  Distinguish that "false
        // sync lock" from a "real codeword that failed validation"
        // by returning @c OutOfRange for bandwidth failures and
        // reserving @c CorruptData for the post-demod sync /
        // CRC mismatches below.  The inspector relies on this
        // distinction to suppress anomaly reports on channels that
        // simply don't carry codewords.
        const uint32_t spbRound = static_cast<uint32_t>(samplesPerBitVal + 0.5);
        if (spbRound < _samplesPerBitMin || spbRound > _samplesPerBitMax) {
                item.error = Error::OutOfRange;
                return item;
        }

        // Demodulate by integrate-and-compare.  For each of the 76
        // bits, sum the samples in the first half-bit and the second
        // half-bit and compare averages.  bit '1' = first half higher
        // (encoder wrote +A then -A); bit '0' = second half higher
        // (encoder wrote -A then +A).  The integration is the part
        // that survives SRC's smoothing — both halves get smoothed
        // symmetrically and the comparison stays valid.
        const double sStart = syncStart;
        const double sPB = samplesPerBitVal;
        const double H = sPB * 0.5;

        uint8_t  syncOut = 0;
        uint64_t payloadOut = 0;
        uint8_t  crcOut = 0;

        for (uint32_t bitIdx = 0; bitIdx < BitsPerPacket; ++bitIdx) {
                const double bitStart = sStart + static_cast<double>(bitIdx) * sPB;
                const double bitMid = bitStart + H;
                const double bitEnd = bitStart + sPB;

                // Half-bit windows are half-open [firstStart, firstEnd)
                // and [secondStart, secondEnd) so the bit-mid sample is
                // never counted in both halves.  ceil() on every edge
                // keeps the boundary on the same side regardless of
                // whether @c sStart is integer or sub-sample.
                const long firstStart = static_cast<long>(std::ceil(bitStart));
                const long firstEnd = static_cast<long>(std::ceil(bitMid));
                const long secondStart = firstEnd;
                const long secondEnd = static_cast<long>(std::ceil(bitEnd));

                if (firstStart < 0 || secondEnd > static_cast<long>(count)) {
                        // Buffer ran out before we could integrate
                        // this bit — partial codeword.  Same class
                        // as a bandwidth fail: not a "real" codeword
                        // that failed validation, so report
                        // OutOfRange rather than CorruptData.
                        item.error = Error::OutOfRange;
                        return item;
                }

                double sumFirst = 0.0;
                long   nFirst = 0;
                for (long s = firstStart; s < firstEnd; ++s) {
                        sumFirst += samples[static_cast<size_t>(s)];
                        ++nFirst;
                }
                double sumSecond = 0.0;
                long   nSecond = 0;
                for (long s = secondStart; s < secondEnd; ++s) {
                        sumSecond += samples[static_cast<size_t>(s)];
                        ++nSecond;
                }
                if (nFirst == 0 || nSecond == 0) {
                        item.error = Error::CorruptData;
                        return item;
                }
                const double   avgFirst = sumFirst / static_cast<double>(nFirst);
                const double   avgSecond = sumSecond / static_cast<double>(nSecond);
                const uint64_t bit = (avgFirst > avgSecond) ? 1u : 0u;

                if (bitIdx < SyncBits) {
                        syncOut = static_cast<uint8_t>((syncOut << 1) | bit);
                } else if (bitIdx < SyncBits + PayloadBits) {
                        payloadOut = (payloadOut << 1) | bit;
                } else {
                        crcOut = static_cast<uint8_t>((crcOut << 1) | bit);
                }
        }

        item.decodedSync = syncOut;
        item.payload = payloadOut;
        item.decodedCrc = crcOut;
        item.expectedCrc = AudioDataEncoder::computeCrc(payloadOut);

        if (syncOut != SyncNibble) {
                item.error = Error::CorruptData;
                return item;
        }
        if (item.decodedCrc != item.expectedCrc) {
                item.error = Error::CorruptData;
                return item;
        }
        item.error = Error::Ok;
        return item;
}

Error AudioDataDecoder::decode(const PcmAudioPayload &payload, const List<Band> &bands, DecodedList &out) const {
        out.clear();
        if (!_valid) return Error::Invalid;
        if (!payload.isValid()) return Error::Invalid;
        if (payload.desc().format() != _desc.format() || payload.desc().sampleRate() != _desc.sampleRate() ||
            payload.desc().channels() != _desc.channels()) {
                return Error::InvalidArgument;
        }
        for (const Band &b : bands) {
                out.pushToBack(decodeOne(payload, b));
        }
        return Error::Ok;
}

AudioDataDecoder::DecodedItem AudioDataDecoder::decode(const PcmAudioPayload &payload, const Band &band) const {
        DecodedItem item;
        if (!_valid) {
                item.error = Error::Invalid;
                return item;
        }
        if (!payload.isValid() || payload.desc().format() != _desc.format() ||
            payload.desc().sampleRate() != _desc.sampleRate() ||
            payload.desc().channels() != _desc.channels()) {
                item.error = Error::InvalidArgument;
                return item;
        }
        return decodeOne(payload, band);
}

AudioDataDecoder::DecodedItem AudioDataDecoder::decode(const float *samples, size_t count) const {
        DecodedItem item;
        if (!_valid) {
                item.error = Error::Invalid;
                return item;
        }
        if (samples == nullptr || count == 0) {
                item.error = Error::InvalidArgument;
                return item;
        }
        return decodeSamples(samples, count);
}

void AudioDataDecoder::decodeAll(StreamState &state, const float *newSamples, size_t count, DecodedList &out) const {
        out.clear();
        if (!_valid) return;

        // Append.  Reserve is a no-op if newSamples is empty.
        if (newSamples != nullptr && count > 0) {
                state.buffer.reserve(state.buffer.size() + count);
                for (size_t i = 0; i < count; ++i) state.buffer.pushToBack(newSamples[i]);
        }

        const size_t minPacketSamples = static_cast<size_t>(BitsPerPacket) * AudioDataEncoder::MinSamplesPerBit;

        // Decode-and-consume loop.  Each iteration that finds a
        // syncable, complete codeword pushes a DecodedItem (success
        // or failure — the caller distinguishes via @c
        // DecodedItem::error) and advances the buffer past it.  A
        // findSync failure means either "not enough samples yet" or
        // "buffer is silence" — in both cases we just return and
        // wait for more data; the buffer cap below keeps memory
        // bounded if the failure persists.
        while (state.buffer.size() >= minPacketSamples) {
                std::vector<float> view(state.buffer.data(), state.buffer.data() + state.buffer.size());
                SyncMeasurement    sync = findSync(view, _expectedSamplesPerBit);
                if (!sync.ok) break;

                // Need @c samplesPerBit*BitsPerPacket samples after
                // @c syncStart to demodulate the full codeword.  If
                // the buffer doesn't reach that far yet, the codeword
                // is incomplete — leave it intact for the next call.
                const double endPos = sync.syncStart + sync.samplesPerBit * static_cast<double>(BitsPerPacket);
                if (endPos > static_cast<double>(view.size())) break;

                DecodedItem item = demodulate(view.data(), view.size(), sync.syncStart, sync.samplesPerBit,
                                              sync.syncStartSampleInt);
                item.streamSampleStart = state.sampleAnchor + static_cast<int64_t>(sync.syncStartSampleInt);
                out.pushToBack(item);

                // Consume past this codeword regardless of whether the
                // demod call reported success — a CRC mismatch still
                // means we identified a packet boundary, and stalling
                // on the same bytes would just emit the same failure
                // forever.  ceil() so a fractional samplesPerBit
                // doesn't strand the trailing half-sample inside the
                // buffer for the next call.
                const size_t consume =
                        std::min(static_cast<size_t>(std::ceil(endPos)), state.buffer.size());
                if (consume == 0) break; // safety; shouldn't happen
                state.buffer.erase(state.buffer.cbegin(), state.buffer.cbegin() + static_cast<long>(consume));
                state.sampleAnchor += static_cast<int64_t>(consume);
        }

        // Cap buffer.  When the cap fires we drop the oldest
        // (kStreamBufferMaxSamples / 2) samples and re-anchor — keeps
        // memory bounded while preserving the most-recent samples
        // that are most likely to contain a forthcoming codeword.
        if (state.buffer.size() > kStreamBufferMaxSamples) {
                const size_t drop = state.buffer.size() - kStreamBufferMaxSamples / 2;
                state.buffer.erase(state.buffer.cbegin(), state.buffer.cbegin() + static_cast<long>(drop));
                state.sampleAnchor += static_cast<int64_t>(drop);
        }
}

PROMEKI_NAMESPACE_END
