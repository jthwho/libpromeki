/**
 * @file      videoportref.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/videoportref.h>

#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

VideoPortRef::VideoPortRef() : _d(SharedPtr<Impl>::create()) {}

VideoPortRef::VideoPortRef(VideoConnectorKind kind, int index) : _d(SharedPtr<Impl>::create()) {
        Impl *impl = _d.modify();
        impl->kind  = kind;
        impl->index = index;
}

// ---------------------------------------------------------------------------
// Accessors / mutators
// ---------------------------------------------------------------------------

bool VideoPortRef::isValid() const {
        return _d->kind != VideoConnectorKind::Auto && _d->index >= 1;
}

VideoConnectorKind VideoPortRef::kind() const { return _d->kind; }

int VideoPortRef::index() const { return _d->index; }

void VideoPortRef::setKind(VideoConnectorKind kind) { _d.modify()->kind = kind; }

void VideoPortRef::setIndex(int index) { _d.modify()->index = index; }

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool VideoPortRef::operator==(const VideoPortRef &other) const {
        if (_d.ptr() == other._d.ptr()) return true;
        return _d->kind == other._d->kind && _d->index == other._d->index;
}

bool VideoPortRef::operator<(const VideoPortRef &other) const {
        if (_d.ptr() == other._d.ptr()) return false;
        if (_d->kind != other._d->kind) return _d->kind.value() < other._d->kind.value();
        return _d->index < other._d->index;
}

// ---------------------------------------------------------------------------
// String form
// ---------------------------------------------------------------------------

String VideoPortRef::toString() const {
        if (_d->kind == VideoConnectorKind::Auto) return String("auto");
        return _d->kind.toString().toLower() + String::number(_d->index);
}

Result<VideoPortRef> VideoPortRef::fromString(const String &s) {
        String trimmed = s.trim();
        if (trimmed.isEmpty()) return makeError<VideoPortRef>(Error::InvalidArgument);

        String lower = trimmed.toLower();

        // The "auto" form encodes only the kind; no index is required
        // or accepted, since Auto by definition names no connector.
        if (lower == String("auto")) return makeResult<VideoPortRef>(VideoPortRef());

        // Walk every registered VideoConnectorKind name (lower-cased)
        // and pick the longest matching prefix.  Longest-match is what
        // distinguishes e.g. "displayport" from a hypothetical
        // "display" — the existing kind names happen to be
        // non-overlapping, but a longest-match scan is robust against
        // future additions.
        const Enum::ValueList values = Enum::values(VideoConnectorKind::Type);
        int                   matchedValue   = 0;
        size_t                matchedLength  = 0;
        for (size_t i = 0; i < values.size(); ++i) {
                const String candidate = values.at(i).first().toLower();
                if (candidate == String("auto")) continue;
                if (lower.startsWith(candidate) && candidate.length() > matchedLength) {
                        matchedLength = candidate.length();
                        matchedValue  = values.at(i).second();
                }
        }
        if (matchedLength == 0) return makeError<VideoPortRef>(Error::InvalidArgument);

        String indexPart = lower.substr(matchedLength).trim();
        if (indexPart.isEmpty()) return makeError<VideoPortRef>(Error::InvalidArgument);

        Error parseErr;
        int   index = indexPart.toInt(&parseErr);
        if (parseErr.isError() || index < 1) {
                return makeError<VideoPortRef>(Error::InvalidArgument);
        }

        return makeResult<VideoPortRef>(VideoPortRef(VideoConnectorKind(matchedValue), index));
}

// ---------------------------------------------------------------------------
// DataStream serialization (member-API path for PROMEKI_DATATYPE)
// ---------------------------------------------------------------------------

Error VideoPortRef::writeToStream(DataStream &s) const {
        s << _d->kind;
        s << static_cast<int32_t>(_d->index);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<VideoPortRef> VideoPortRef::readFromStream<1>(DataStream &s) {
        VideoConnectorKind kind;
        int32_t            index = 0;
        s >> kind;
        s >> index;
        if (s.status() != DataStream::Ok) return makeError<VideoPortRef>(s.toError());
        return makeResult<VideoPortRef>(VideoPortRef(kind, static_cast<int>(index)));
}

PROMEKI_NAMESPACE_END
