/**
 * @file      mediaplay/sidecar.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Helpers for recognising sequence masks and writing `.imgseq`
 * sidecar files alongside the frames mediaplay produces.
 */

#pragma once

#include <promeki/error.h>
#include <promeki/filepath.h>
#include <promeki/mediadesc.h>
#include <promeki/string.h>

namespace mediaplay {

/**
 * @brief Returns true when a path looks like an image-sequence mask.
 *
 * A sequence mask contains either a hash-style placeholder
 * (`foo_####.dpx`) or a printf-style one (`foo_%04d.dpx`).  This is
 * the same rule used by @c NumName::fromMask so single-file paths,
 * `.imgseq` sidecars, and plain filenames return @c false.
 */
bool outputIsSequenceMask(const promeki::String &path);

/**
 * @brief Builds a default sidecar path next to a sequence mask.
 *
 * Strips trailing separator characters (`_`, `.`, `-`, space) from
 * the mask's numeric prefix and places `<stem>.imgseq` in the same
 * directory.  Used when the user asks for `--imgseq` without
 * specifying an explicit `--imgseq-file`.
 */
promeki::FilePath deriveSidecarPath(const promeki::String &maskPath);

/**
 * @brief Writes a JSON `.imgseq` sidecar describing what was written.
 *
 * Populates an @c ImgSeq from the given sequence mask, frame count,
 * and effective descriptor (the one that actually reached the sink,
 * so any Converter stage is already folded in).  Returns
 * @c Error::Ok if the sidecar was written (or was skipped because
 * no frames were produced), otherwise the error from @c ImgSeq::save.
 */
promeki::Error writeImgSeqSidecar(const promeki::FilePath &path,
                                  const promeki::String &maskPath,
                                  const promeki::MediaDesc &mediaDesc,
                                  int seqHead,
                                  int64_t frameCount);

} // namespace mediaplay
