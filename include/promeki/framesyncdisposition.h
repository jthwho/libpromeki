/**
 * @file      framesyncdisposition.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-frame decision a frame-rate-conversion or genlock-drift
 *        stage applies to one input frame.
 * @ingroup proav
 *
 * A frame sync (software or hardware-paced) decides, for every output
 * frame slot, whether to play the next input frame, drop it, or repeat
 * the previous one.  @c FrameSyncDisposition is the value type that
 * encodes that decision; it is consumed by @ref AncFrameSync today and
 * by future video / audio policy stages tomorrow.
 *
 * @c Repeat carries a @c count so a single decision can express
 * "hold this frame for N output slots" — important for codecs whose
 * policy depends on the repeat index, e.g. CEA-708 CDP
 * @c sequence_counter advancement and ATC timecode increment under
 * drop-frame.
 *
 * The type is a @ref simple "Simple data object": plain value, no
 * heap, trivially copyable.
 *
 * @par Example
 * @code
 * FrameSyncDisposition d = FrameSyncDisposition::repeat(3);
 * assert(d.kind() == FrameSyncDisposition::Repeat);
 * assert(d.repeatCount() == 3);
 *
 * FrameSyncDisposition p = FrameSyncDisposition::play();
 * assert(p.kind() == FrameSyncDisposition::Play);
 * @endcode
 */
class FrameSyncDisposition {
        public:
                /**
                 * @brief Decision kind.
                 *
                 * - @c Play   — emit the input frame as-is.
                 * - @c Drop   — discard the input frame; no output frame for this slot.
                 * - @c Repeat — emit the previously-played frame again @c count times.
                 */
                enum Kind {
                        Play,
                        Drop,
                        Repeat
                };

                /** @brief Default repeat count used when @ref repeat is called with no argument. */
                static constexpr uint8_t DefaultRepeatCount = 1;

                /** @brief Returns a @c Play disposition. */
                static constexpr FrameSyncDisposition play() {
                        return FrameSyncDisposition(Play, 0);
                }

                /** @brief Returns a @c Drop disposition. */
                static constexpr FrameSyncDisposition drop() {
                        return FrameSyncDisposition(Drop, 0);
                }

                /**
                 * @brief Returns a @c Repeat disposition with the given count.
                 * @param count Number of times to re-emit the held frame
                 *              (1 = one extra copy after the initial Play,
                 *              for a total of two output frames carrying the
                 *              same picture).  Defaults to
                 *              @ref DefaultRepeatCount.
                 */
                static constexpr FrameSyncDisposition repeat(uint8_t count = DefaultRepeatCount) {
                        return FrameSyncDisposition(Repeat, count);
                }

                /** @brief Default-constructs a @c Play disposition. */
                constexpr FrameSyncDisposition() = default;

                /** @brief Returns the disposition kind. */
                constexpr Kind kind() const { return _kind; }

                /**
                 * @brief Returns the repeat count.
                 * @note Only meaningful when @ref kind is @c Repeat; returns
                 *       @c 0 for @c Play and @c Drop.
                 */
                constexpr uint8_t repeatCount() const { return _repeatCount; }

                /** @brief Equality. */
                constexpr bool operator==(const FrameSyncDisposition &other) const {
                        return _kind == other._kind && _repeatCount == other._repeatCount;
                }

                /** @brief Inequality. */
                constexpr bool operator!=(const FrameSyncDisposition &other) const {
                        return !(*this == other);
                }

        private:
                constexpr FrameSyncDisposition(Kind k, uint8_t c) : _kind(k), _repeatCount(c) {}

                Kind    _kind        = Play;
                uint8_t _repeatCount = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
