/**
 * @file      metadata.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <map>
#include <promeki/namespace.h>
#include <promeki/variant.h>
#include <promeki/util.h>
#include <promeki/json.h>
#include <promeki/sharedptr.h>

#define PROMEKI_ENUM_METADATA_ID \
        X(Invalid, std::monostate) \
        X(Timecode, class Timecode) \
        X(Gamma, double) \
        X(Title, String) \
        X(Copyright, String) \
        X(Software, String) \
        X(Artist, String) \
        X(Comment, String) \
        X(Date, String) \
        X(Album, String) \
        X(License, String) \
        X(TrackNumber, int) \
        X(Genre, String) \
        X(EnableBWF, bool) \
        X(Description, String) \
        X(Originator, String) \
        X(OriginatorReference, String) \
        X(OriginationDateTime, String) \
        X(FrameRate, Rational<int>) \
        X(UMID, String) \
        X(CodingHistory, String) \
        X(CompressionLevel, double) \
        X(EnableVBR, bool) \
        X(VBRQuality, double) \
        X(CompressedSize, int)

PROMEKI_NAMESPACE_BEGIN

class StringList;

class Metadata {
        public:
                #define X(name, type) name,
                enum ID { PROMEKI_ENUM_METADATA_ID };
                #undef X

                static const String &idToString(ID id);
                static ID stringToID(const String &val);
                static Metadata fromJson(const JsonObject &json, bool *ok = nullptr);

                Metadata() : d(SharedPtr<Data>::create()) {}

                template <typename T> void set(ID id, const T &value) {
                        d.modify()->map[id] = Variant(value);
                        return;
                }
                const Variant &get(ID id) const { return d->map.at(id); }
                bool contains(ID id) const { return d->map.find(id) != d->map.end(); }
                void remove(ID id) { d.modify()->map.erase(id); return; }
                void clear() { d.modify()->map.clear(); return; }
                size_t size() const { return d->map.size(); }
                bool isEmpty() const { return d->map.size() == 0; }

                template <typename Func> void forEach(Func &&func) const {
                        for(const auto &[id, value] : d->map) {
                                func(id, value);
                        }
                        return;
                }

                StringList dump() const;

                JsonObject toJson() const {
                    JsonObject ret;
                    for(const auto &[id, value] : d->map) {
                        ret.setFromVariant(idToString(id), value);
                    }
                    return ret;
                }

                int referenceCount() const { return d.referenceCount(); }

        private:
                class Data {
                        PROMEKI_SHARED_FINAL(Data)
                        public:
                                std::map<ID, Variant> map;
                                Data() = default;
                                Data(const Data &o) = default;
                };

                SharedPtr<Data> d;
};

PROMEKI_NAMESPACE_END
