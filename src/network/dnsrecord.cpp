/**
 * @file      dnsrecord.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dnsrecord.h>

PROMEKI_NAMESPACE_BEGIN

DnsRecord DnsRecord::makeA(const String &owner, const Ipv4Address &addr, const Duration &ttl) {
        DnsRecord r;
        r.type = Type::A;
        r.name = owner;
        r.ttl  = ttl;
        r.a    = addr;
        return r;
}

DnsRecord DnsRecord::makeAaaa(const String &owner, const Ipv6Address &addr, const Duration &ttl) {
        DnsRecord r;
        r.type = Type::Aaaa;
        r.name = owner;
        r.ttl  = ttl;
        r.aaaa = addr;
        return r;
}

DnsRecord DnsRecord::makePtr(const String &owner, const String &target, const Duration &ttl) {
        DnsRecord r;
        r.type       = Type::Ptr;
        r.name       = owner;
        r.ttl        = ttl;
        r.ptrTarget  = target;
        // PTR rdata in unicast DNS or DNS-SD never sets cache-flush
        // (RFC 6762 §10.2 disallows it).  Leave the default false.
        return r;
}

DnsRecord DnsRecord::makeSrv(const String &owner, const String &target, uint16_t port,
                             uint16_t priority, uint16_t weight, const Duration &ttl) {
        DnsRecord r;
        r.type        = Type::Srv;
        r.name        = owner;
        r.ttl         = ttl;
        r.srvPriority = priority;
        r.srvWeight   = weight;
        r.srvPort     = port;
        r.srvTarget   = target;
        return r;
}

DnsRecord DnsRecord::makeCname(const String &owner, const String &target, const Duration &ttl) {
        DnsRecord r;
        r.type        = Type::Cname;
        r.name        = owner;
        r.ttl         = ttl;
        r.cnameTarget = target;
        return r;
}

DnsRecord DnsRecord::makeNs(const String &owner, const String &target, const Duration &ttl) {
        DnsRecord r;
        r.type     = Type::Ns;
        r.name     = owner;
        r.ttl      = ttl;
        r.nsTarget = target;
        return r;
}

DnsRecord DnsRecord::makeMx(const String &owner, const String &exchange, uint16_t preference,
                            const Duration &ttl) {
        DnsRecord r;
        r.type         = Type::Mx;
        r.name         = owner;
        r.ttl          = ttl;
        r.mxPreference = preference;
        r.mxExchange   = exchange;
        return r;
}

PROMEKI_NAMESPACE_END
