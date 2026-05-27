/**
 * @file      stringlist.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/stringlist.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// DataStream wire format (v1: tagged uint32 count + N tagged Strings).
// ============================================================================

Error StringList::writeToStream(DataStream &s) const {
        s << static_cast<uint32_t>(size());
        for (const String &str : *this) s << str;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<StringList> StringList::readFromStream<1>(DataStream &s) {
        uint32_t count = 0;
        s >> count;
        if (s.status() != DataStream::Ok) return makeError<StringList>(s.toError());
        // Use the same per-stream container sanity bound as the
        // templated container readers.
        if (count > (256u * 1024u * 1024u)) {
                return makeError<StringList>(Error::CorruptData);
        }
        StringList out;
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
                String str;
                s >> str;
                if (s.status() != DataStream::Ok) return makeError<StringList>(s.toError());
                out.pushToBack(std::move(str));
        }
        return makeResult(std::move(out));
}

PROMEKI_NAMESPACE_END
