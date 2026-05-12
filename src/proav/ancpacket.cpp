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

void AncPacket::setFormat(const AncFormat &fmt) { _d.modify()->format = fmt; }

void AncPacket::setTransport(const AncTransport &transport) { _d.modify()->transport = transport; }

void AncPacket::setData(Buffer data) { _d.modify()->data = std::move(data); }

void AncPacket::setMeta(Metadata meta) { _d.modify()->meta = std::move(meta); }

Buffer &AncPacket::dataMut() { return _d.modify()->data; }

Metadata &AncPacket::metaMut() { return _d.modify()->meta; }

bool AncPacket::isValid() const {
        return _d->format.isValid() && _d->transport != AncTransport::Invalid;
}

bool AncPacket::operator==(const AncPacket &o) const {
        if (_d.ptr() == o._d.ptr()) return true;
        return _d->format == o._d->format && _d->transport == o._d->transport && _d->data == o._d->data &&
               _d->meta == o._d->meta;
}

String AncPacket::toString(bool verbose) const {
        String s = "AncPacket(";
        s += _d->format.isValid() ? _d->format.name() : String("Invalid");
        s += " on ";
        s += _d->transport.valueName();
        s += ", ";
        s += String::number(static_cast<uint64_t>(_d->data.size()));
        s += " bytes";
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
}

AncPacket readAncPacketData(DataStream &stream) {
        AncFormat    fmt;
        AncTransport transport;
        Buffer       data;
        Metadata     meta;
        stream >> fmt;
        stream >> transport;
        stream >> data;
        stream >> meta;
        return AncPacket(fmt, transport, std::move(data), std::move(meta));
}

DataStream &operator<<(DataStream &stream, const AncPacket &pkt) {
        stream.writeTag(DataStream::TypeAncPacket);
        writeAncPacketData(stream, pkt);
        return stream;
}

DataStream &operator>>(DataStream &stream, AncPacket &pkt) {
        if (!stream.readTag(DataStream::TypeAncPacket)) {
                pkt = AncPacket();
                return stream;
        }
        pkt = readAncPacketData(stream);
        return stream;
}

PROMEKI_NAMESPACE_END
