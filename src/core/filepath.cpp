/**
 * @file      filepath.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Out-of-line implementations for @ref FilePath operations whose
 * dependencies (file I/O, @c set<>) are too heavy for the header.
 * The header-only inline path covers the trivial accessors and the
 * @c std::filesystem wrappers; this file carries the pseudo-symlink
 * reader and the symlink-chain resolver.
 */

#include <promeki/filepath.h>

#include <cctype>
#include <cstdio>
#include <set>
#include <string>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Reads up to @p maxBytes + 1 bytes of @p path into @p outBytes.
        // Returns @c false when the file is unreadable or strictly
        // larger than @p maxBytes; in the latter case @p outBytes is
        // left untouched.  Uses C stdio rather than std::ifstream to
        // keep the pulled-in template instantiations small.
        bool readSmallFile(const std::filesystem::path &path, size_t maxBytes,
                           std::string &outBytes) {
                std::FILE *fp = std::fopen(path.string().c_str(), "rb");
                if (fp == nullptr) return false;
                std::string buf;
                buf.reserve(maxBytes);
                char   chunk[1024];
                size_t total = 0;
                while (total <= maxBytes) {
                        const size_t want = (maxBytes + 1 - total) < sizeof(chunk)
                                                    ? (maxBytes + 1 - total)
                                                    : sizeof(chunk);
                        const size_t got = std::fread(chunk, 1, want, fp);
                        if (got == 0) break;
                        buf.append(chunk, got);
                        total += got;
                        if (total > maxBytes) {
                                std::fclose(fp);
                                return false;
                        }
                }
                std::fclose(fp);
                outBytes.swap(buf);
                return true;
        }

        // Parses @p body — already-loaded content of a candidate
        // pseudo-symlink — and returns the trimmed target on success.
        // Returns an empty string when the magic header is missing,
        // the payload is empty, or the payload contains a NUL / ASCII
        // control character.  Bytes >= 0x80 are accepted so UTF-8
        // paths flow through unmolested.
        std::string parsePseudoSymlinkBody(const std::string &body) {
                const std::string magic(FilePath::kPseudoSymlinkMagic);
                if (body.size() < magic.size()) return {};
                if (body.compare(0, magic.size(), magic) != 0) return {};
                // The magic must be terminated by EOF, '\n', or '\r'
                // (covers Unix LF and Windows CRLF / classic CR).  No
                // trailing characters on the magic line are allowed
                // so we can't be fooled by a longer header.
                size_t cursor = magic.size();
                if (cursor < body.size()) {
                        char c = body[cursor];
                        if (c == '\n') {
                                ++cursor;
                        } else if (c == '\r') {
                                ++cursor;
                                if (cursor < body.size() && body[cursor] == '\n') ++cursor;
                        } else {
                                return {}; // garbage after magic on first line
                        }
                }
                // Trim ASCII whitespace at both ends of the payload.
                size_t start = cursor;
                size_t end = body.size();
                while (start < end &&
                       std::isspace(static_cast<unsigned char>(body[start])))
                        ++start;
                while (end > start &&
                       std::isspace(static_cast<unsigned char>(body[end - 1])))
                        --end;
                if (start >= end) return {};
                std::string payload(body, start, end - start);
                // Validate: no NUL, no ASCII control char in the body
                // of the path.  Embedded newlines are caught here too.
                for (char c : payload) {
                        unsigned char u = static_cast<unsigned char>(c);
                        if (u == 0) return {};
                        if (u < 0x20) return {};
                }
                return payload;
        }

} // namespace

bool FilePath::isPseudoSymlink() const {
        std::error_code ec;
        // Reject OS symlinks outright — those are handled by
        // isSymlink / readSymlink; we only fire when the path is a
        // genuine regular file.  symlink_status avoids following the
        // link so a symlink-to-regular-file doesn't get misclassified.
        auto st = std::filesystem::symlink_status(_path, ec);
        if (ec) return false;
        if (std::filesystem::is_symlink(st)) return false;
        if (!std::filesystem::is_regular_file(st)) return false;

        // Size pre-check via stat — much cheaper than reading the
        // file body for things we already know are too big.
        std::error_code szec;
        auto            sz = std::filesystem::file_size(_path, szec);
        if (szec) return false;
        if (sz == 0 || sz > kPseudoSymlinkMaxBytes) return false;

        std::string body;
        if (!readSmallFile(_path, kPseudoSymlinkMaxBytes, body)) return false;
        return !parsePseudoSymlinkBody(body).empty();
}

Result<FilePath> FilePath::readPseudoSymlink() const {
        std::string body;
        if (!readSmallFile(_path, kPseudoSymlinkMaxBytes, body)) {
                return Result<FilePath>(*this, Error::Invalid);
        }
        std::string target = parsePseudoSymlinkBody(body);
        if (target.empty()) return Result<FilePath>(*this, Error::Invalid);
        return Result<FilePath>(FilePath(std::filesystem::path(target)), Error::Ok);
}

Error FilePath::writePseudoSymlink(const FilePath &target) const {
        const std::string magic(kPseudoSymlinkMagic);
        const std::string body = target.toString().str();
        if (body.empty()) return Error::Invalid;
        // Header + LF + body + trailing LF.  +2 for the two
        // newlines.  Reject targets too long to fit alongside the
        // header so the file we write would itself satisfy
        // isPseudoSymlink.
        if (magic.size() + body.size() + 2 > kPseudoSymlinkMaxBytes) return Error::Invalid;
        // Validate the target body has no embedded NUL / control
        // characters — same rule readPseudoSymlink enforces on read.
        for (char c : body) {
                unsigned char u = static_cast<unsigned char>(c);
                if (u == 0 || u < 0x20) return Error::Invalid;
        }
        std::FILE *fp = std::fopen(_path.string().c_str(), "wb");
        if (fp == nullptr) return Error::Invalid;
        std::fwrite(magic.data(), 1, magic.size(), fp);
        std::fputc('\n', fp);
        std::fwrite(body.data(), 1, body.size(), fp);
        std::fputc('\n', fp);
        std::fclose(fp);
        return Error::Ok;
}

Result<FilePath> FilePath::resolveLink(int maxHops) const {
        FilePath cur = *this;
        // Track visited absolute string paths so a self-referencing
        // chain (a → b → a) trips the loop guard instead of spinning
        // up to maxHops.  We compare on the absolute form because the
        // raw stored target on each hop is often relative.
        std::set<std::string> visited;
        visited.insert(cur.absolutePath().toString().str());

        for (int hop = 0; hop < maxHops; ++hop) {
                const bool osLink = cur.isSymlink();
                const bool pseudo = !osLink && cur.isPseudoSymlink();
                if (!osLink && !pseudo) {
                        return Result<FilePath>(cur, Error::Ok);
                }

                Result<FilePath> r = osLink ? cur.readSymlink() : cur.readPseudoSymlink();
                if (r.second().isError()) {
                        return Result<FilePath>(cur, Error::Invalid);
                }
                FilePath target = r.first();
                // OS symlink semantics: relative targets resolve
                // against the link file's parent directory.  We apply
                // the same rule to pseudo-symlinks so the two kinds
                // are indistinguishable to callers.
                if (target.isRelative()) {
                        target = cur.parent() / target;
                }
                // Lexically-normalise so duplicate slashes / dots
                // don't trick the loop detector.
                std::error_code ec;
                std::filesystem::path absNorm = std::filesystem::absolute(target.toStdPath(), ec);
                if (!ec) {
                        absNorm = absNorm.lexically_normal();
                        target = FilePath(absNorm);
                }

                const std::string key = target.absolutePath().toString().str();
                if (visited.count(key) > 0) {
                        return Result<FilePath>(target, Error::Invalid);
                }
                visited.insert(key);
                cur = target;
        }
        // Ran out of hops without reaching a terminal node.
        return Result<FilePath>(cur, Error::Invalid);
}

PROMEKI_NAMESPACE_END
