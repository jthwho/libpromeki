/**
 * @file      ancpacket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancpacket.h>
#include <promeki/datastream.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

AncPacket::AncPacket() : _d(SharedPtr<Impl>::create()) {}

AncPacket::AncPacket(const AncFormat &fmt, const AncTransport &transport, Buffer data, Metadata meta)
    : _d(SharedPtr<Impl>::create()) {
        Impl *impl = _d.modify();
        impl->format = fmt;
        impl->transport = transport;
        impl->data = std::move(data);
        impl->meta = std::move(meta);
}

const AncFormat    &AncPacket::format() const { return _d->format; }
const AncTransport &AncPacket::transport() const { return _d->transport; }
const Buffer       &AncPacket::data() const { return _d->data; }
const Metadata     &AncPacket::meta() const { return _d->meta; }

uint16_t AncPacket::st291Line() const { return _d->st291Line; }
uint16_t AncPacket::st291HOffset() const { return _d->st291HOffset; }
bool     AncPacket::st291FieldB() const { return _d->st291FieldB; }
bool     AncPacket::st291CBit() const { return _d->st291CBit; }
uint8_t  AncPacket::st291StreamNum() const { return _d->st291StreamNum; }

void AncPacket::setFormat(const AncFormat &fmt) { _d.modify()->format = fmt; }

void AncPacket::setTransport(const AncTransport &transport) { _d.modify()->transport = transport; }

void AncPacket::setData(Buffer data) { _d.modify()->data = std::move(data); }

void AncPacket::setMeta(Metadata meta) { _d.modify()->meta = std::move(meta); }

void AncPacket::setSt291Line(uint16_t line) { _d.modify()->st291Line = line; }
void AncPacket::setSt291HOffset(uint16_t hOffset) { _d.modify()->st291HOffset = hOffset; }
void AncPacket::setSt291FieldB(bool fieldB) { _d.modify()->st291FieldB = fieldB; }
void AncPacket::setSt291CBit(bool cBit) { _d.modify()->st291CBit = cBit; }
void AncPacket::setSt291StreamNum(uint8_t streamNum) { _d.modify()->st291StreamNum = streamNum; }

void AncPacket::setSt291Framing(uint16_t line, uint16_t hOffset, bool fieldB, bool cBit, uint8_t streamNum) {
        Impl *impl = _d.modify();
        impl->st291Line = line;
        impl->st291HOffset = hOffset;
        impl->st291FieldB = fieldB;
        impl->st291CBit = cBit;
        impl->st291StreamNum = streamNum;
}

Buffer &AncPacket::dataMut() { return _d.modify()->data; }

Metadata &AncPacket::metaMut() { return _d.modify()->meta; }

bool AncPacket::isValid() const {
        return _d->format.isValid() && _d->transport != AncTransport::Invalid;
}

bool AncPacket::operator==(const AncPacket &o) const {
        if (_d.ptr() == o._d.ptr()) return true;
        return _d->format == o._d->format && _d->transport == o._d->transport && _d->data == o._d->data &&
               _d->meta == o._d->meta && _d->st291Line == o._d->st291Line && _d->st291HOffset == o._d->st291HOffset &&
               _d->st291FieldB == o._d->st291FieldB && _d->st291CBit == o._d->st291CBit &&
               _d->st291StreamNum == o._d->st291StreamNum;
}

String AncPacket::toString(bool verbose) const {
        String s = "AncPacket(";
        s += _d->format.isValid() ? _d->format.name() : String("Invalid");
        s += " on ";
        s += _d->transport.valueName();
        s += ", ";
        s += String::number(static_cast<uint64_t>(_d->data.size()));
        s += " bytes";
        if (_d->transport == AncTransport::St291) {
                s += ", line=";
                s += String::number(static_cast<uint64_t>(_d->st291Line));
                if (verbose) {
                        s += ", hOff=";
                        s += String::number(static_cast<uint64_t>(_d->st291HOffset));
                        s += ", fieldB=";
                        s += _d->st291FieldB ? String("1") : String("0");
                        s += ", cBit=";
                        s += _d->st291CBit ? String("1") : String("0");
                        s += ", streamNum=";
                        s += String::number(static_cast<uint64_t>(_d->st291StreamNum));
                }
        }
        if (verbose) {
                s += ", meta={";
                StringList kv = _d->meta.dump();
                for (size_t i = 0; i < kv.size(); ++i) {
                        if (i != 0) s += ", ";
                        s += kv.at(i);
                }
                s += "}";
        }
        s += ")";
        return s;
}

void writeAncPacketData(DataStream &stream, const AncPacket &pkt) {
        stream << pkt.format();
        stream << pkt.transport();
        stream << pkt.data();
        stream << pkt.meta();
        // ST 291 framing trailer.  Held directly on the Impl as of
        // F9.1 (was previously folded into the meta sidecar via the
        // AncMeta::St291 keys).  Always written so the wire layout
        // matches the in-memory shape regardless of which transport
        // the packet rides on; non-ST 291 packets simply carry the
        // default sentinels.
        stream << pkt.st291Line();
        stream << pkt.st291HOffset();
        stream << pkt.st291FieldB();
        stream << pkt.st291CBit();
        stream << pkt.st291StreamNum();
}

AncPacket readAncPacketData(DataStream &stream) {
        AncFormat    fmt;
        AncTransport transport;
        Buffer       data;
        Metadata     meta;
        uint16_t     st291Line = 0x7FE;
        uint16_t     st291HOffset = 0xFFF;
        bool         st291FieldB = false;
        bool         st291CBit = false;
        uint8_t      st291StreamNum = 0;
        stream >> fmt;
        stream >> transport;
        stream >> data;
        stream >> meta;
        stream >> st291Line;
        stream >> st291HOffset;
        stream >> st291FieldB;
        stream >> st291CBit;
        stream >> st291StreamNum;
        AncPacket pkt(fmt, transport, std::move(data), std::move(meta));
        pkt.setSt291Line(st291Line);
        pkt.setSt291HOffset(st291HOffset);
        pkt.setSt291FieldB(st291FieldB);
        pkt.setSt291CBit(st291CBit);
        pkt.setSt291StreamNum(st291StreamNum);
        return pkt;
}

DataStream &operator<<(DataStream &stream, const AncPacket &pkt) {
        stream.beginFrame(DataTypeAncPacket, 1);
        writeAncPacketData(stream, pkt);
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, AncPacket &pkt) {
        if (!stream.readFrame(DataTypeAncPacket)) {
                pkt = AncPacket();
                return stream;
        }
        pkt = readAncPacketData(stream);
        return stream;
}

PROMEKI_NAMESPACE_END
