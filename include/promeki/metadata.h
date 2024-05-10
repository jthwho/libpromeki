/*****************************************************************************
 * metadata.h
 * April 30, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <map>
#include <promeki/namespace.h>
#include <promeki/variant.h>
#include <promeki/util.h>
#include <promeki/json.h>

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

                template <typename T> void set(ID id, const T &value) {
                        d[id] = Variant(value);
                        return;
                }
                const Variant &get(ID id) const { return d.at(id); }
                bool contains(ID id) const { return d.find(id) != d.end(); }
                void remove(ID id) { d.erase(id); return; }
                void clear() { d.clear(); return; }
                size_t size() const { return d.size(); }
                bool isEmpty() const { return d.size() == 0; }

                template <typename Func> void forEach(Func &&func) const {
                        for(const auto &[id, value] : d) {
                                func(id, value);
                        }
                        return;
                }

                StringList dump() const;

                JsonObject toJson() const {
                    JsonObject ret;
                    for(const auto &[id, value] : d) {
                        ret.setFromVariant(idToString(id), value);
                    }
                    return ret;
                }

        private:
                std::map<ID, Variant> d;
};

PROMEKI_NAMESPACE_END

