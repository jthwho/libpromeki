/**
 * @file      enums_dns.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * DNS / DNS-SD wire-format enums: record types, classes, opcodes,
 * response codes, and packet sections.  These are the Variant-friendly
 * TypedEnum wrappers used in resolver public APIs and config records;
 * the record-internal hot-path types live as raw enum classes on
 * @ref DnsRecord / @ref DnsQuestion to keep per-record memory tight.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief RFC 1035 DNS resource-record type (RRTYPE) code.
 *
 * Numeric values match the on-the-wire 16-bit field (IANA "Resource
 * Record (RR) TYPEs" registry) so callers can compare against raw
 * inputs without an extra mapping.  Only the most common types have
 * named entries; unknown numeric values are still valid @c Enum
 * instances and round-trip cleanly through @c toString /
 * @c Enum::lookup as @c "DnsRecordType::<int>".
 *
 *  - @c A      (1)   — IPv4 host address (RFC 1035).
 *  - @c Ns     (2)   — Authoritative name server (RFC 1035).
 *  - @c Cname  (5)   — Canonical name alias (RFC 1035).
 *  - @c Soa    (6)   — Start of authority (RFC 1035).
 *  - @c Ptr    (12)  — Domain-name pointer; used for reverse lookups
 *                      and DNS-SD service enumeration (RFC 1035 / 6763).
 *  - @c Mx     (15)  — Mail exchange (RFC 1035).
 *  - @c Txt    (16)  — Text record / DNS-SD TXT attributes (RFC 1035 / 6763).
 *  - @c Aaaa   (28)  — IPv6 host address (RFC 3596).
 *  - @c Srv    (33)  — Service locator (RFC 2782 / 6763).
 *  - @c Opt    (41)  — EDNS0 pseudo-record (RFC 6891).
 *  - @c Naptr  (35)  — Naming authority pointer (RFC 3403).
 *  - @c Ds     (43)  — DS (delegation signer, DNSSEC) — not validated;
 *                      surfaced opaquely so callers can inspect it.
 *  - @c Rrsig  (46)  — RRSIG (DNSSEC signature) — opaque.
 *  - @c Nsec   (47)  — NSEC (DNSSEC next-secure) — opaque.
 *  - @c Dnskey (48)  — DNSKEY (DNSSEC public key) — opaque.
 *  - @c Tlsa   (52)  — TLSA (DANE) — opaque (RFC 6698).
 *  - @c Svcb   (64)  — Service binding (RFC 9460) — opaque.
 *  - @c Https  (65)  — HTTPS service binding (RFC 9460) — opaque.
 *  - @c Caa    (257) — Certification Authority Authorization (RFC 8659).
 *  - @c Axfr   (252) — Zone transfer request (RFC 1035) — query only.
 *  - @c Any    (255) — "ANY" wildcard query type (RFC 1035) — query only.
 */
class DnsRecordType : public TypedEnum<DnsRecordType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("DnsRecordType", "DNS Record Type", 1,
                                                   {"Unknown", 0,   "Unknown"},
                                                   {"A",       1,   "IPv4 Host Address"},
                                                   {"Ns",      2,   "Authoritative Name Server"},
                                                   {"Cname",   5,   "Canonical Name Alias"},
                                                   {"Soa",     6,   "Start of Authority"},
                                                   {"Ptr",     12,  "Domain Name Pointer"},
                                                   {"Mx",      15,  "Mail Exchange"},
                                                   {"Txt",     16,  "Text / DNS-SD Attributes"},
                                                   {"Aaaa",    28,  "IPv6 Host Address"},
                                                   {"Srv",     33,  "Service Locator"},
                                                   {"Naptr",   35,  "Naming Authority Pointer"},
                                                   {"Opt",     41,  "EDNS0 Pseudo-Record"},
                                                   {"Ds",      43,  "DNSSEC Delegation Signer"},
                                                   {"Rrsig",   46,  "DNSSEC RRSIG Signature"},
                                                   {"Nsec",    47,  "DNSSEC Next Secure"},
                                                   {"Dnskey",  48,  "DNSSEC Public Key"},
                                                   {"Tlsa",    52,  "DANE TLS Anchor"},
                                                   {"Svcb",    64,  "Service Binding"},
                                                   {"Https",   65,  "HTTPS Service Binding"},
                                                   {"Axfr",    252, "Zone Transfer (Query Only)"},
                                                   {"Any",     255, "Wildcard / ANY (Query Only)"},
                                                   {"Caa",     257, "Cert. Authority Authorization"}); // default: A

                using TypedEnum<DnsRecordType>::TypedEnum;

                static const DnsRecordType Unknown;
                static const DnsRecordType A;
                static const DnsRecordType Ns;
                static const DnsRecordType Cname;
                static const DnsRecordType Soa;
                static const DnsRecordType Ptr;
                static const DnsRecordType Mx;
                static const DnsRecordType Txt;
                static const DnsRecordType Aaaa;
                static const DnsRecordType Srv;
                static const DnsRecordType Naptr;
                static const DnsRecordType Opt;
                static const DnsRecordType Ds;
                static const DnsRecordType Rrsig;
                static const DnsRecordType Nsec;
                static const DnsRecordType Dnskey;
                static const DnsRecordType Tlsa;
                static const DnsRecordType Svcb;
                static const DnsRecordType Https;
                static const DnsRecordType Axfr;
                static const DnsRecordType Any;
                static const DnsRecordType Caa;
};

inline const DnsRecordType DnsRecordType::Unknown{0};
inline const DnsRecordType DnsRecordType::A{1};
inline const DnsRecordType DnsRecordType::Ns{2};
inline const DnsRecordType DnsRecordType::Cname{5};
inline const DnsRecordType DnsRecordType::Soa{6};
inline const DnsRecordType DnsRecordType::Ptr{12};
inline const DnsRecordType DnsRecordType::Mx{15};
inline const DnsRecordType DnsRecordType::Txt{16};
inline const DnsRecordType DnsRecordType::Aaaa{28};
inline const DnsRecordType DnsRecordType::Srv{33};
inline const DnsRecordType DnsRecordType::Naptr{35};
inline const DnsRecordType DnsRecordType::Opt{41};
inline const DnsRecordType DnsRecordType::Ds{43};
inline const DnsRecordType DnsRecordType::Rrsig{46};
inline const DnsRecordType DnsRecordType::Nsec{47};
inline const DnsRecordType DnsRecordType::Dnskey{48};
inline const DnsRecordType DnsRecordType::Tlsa{52};
inline const DnsRecordType DnsRecordType::Svcb{64};
inline const DnsRecordType DnsRecordType::Https{65};
inline const DnsRecordType DnsRecordType::Axfr{252};
inline const DnsRecordType DnsRecordType::Any{255};
inline const DnsRecordType DnsRecordType::Caa{257};

/**
 * @brief RFC 1035 DNS resource-record class (CLASS) code.
 *
 * Wire 16-bit class field.  The top bit is overloaded by mDNS
 * (RFC 6762 §10.2 cache-flush, §5.4 QU bit) and stripped before the
 * remaining 15 bits land here.
 *
 *  - @c In  (1)   — Internet (the only one that matters in practice).
 *  - @c Cs  (2)   — CSNET (obsolete).
 *  - @c Ch  (3)   — CHAOS — used by version queries (@c version.bind. CH TXT).
 *  - @c Hs  (4)   — Hesiod (rarely used).
 *  - @c None (254)— "None" (RFC 2136 dynamic update).
 *  - @c Any (255) — "ANY" wildcard query class.
 */
class DnsRecordClass : public TypedEnum<DnsRecordClass> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("DnsRecordClass", "DNS Record Class", 1,
                                                   {"In",   1,   "Internet"},
                                                   {"Cs",   2,   "CSNET (obsolete)"},
                                                   {"Ch",   3,   "CHAOS"},
                                                   {"Hs",   4,   "Hesiod"},
                                                   {"None", 254, "None (RFC 2136 update)"},
                                                   {"Any",  255, "Wildcard / ANY"}); // default: In

                using TypedEnum<DnsRecordClass>::TypedEnum;

                static const DnsRecordClass In;
                static const DnsRecordClass Cs;
                static const DnsRecordClass Ch;
                static const DnsRecordClass Hs;
                static const DnsRecordClass None;
                static const DnsRecordClass Any;
};

inline const DnsRecordClass DnsRecordClass::In{1};
inline const DnsRecordClass DnsRecordClass::Cs{2};
inline const DnsRecordClass DnsRecordClass::Ch{3};
inline const DnsRecordClass DnsRecordClass::Hs{4};
inline const DnsRecordClass DnsRecordClass::None{254};
inline const DnsRecordClass DnsRecordClass::Any{255};

/**
 * @brief Section of a DNS packet a record belongs to.
 *
 * Mirrors @ref DnsRecord::Section so the wire-form section count
 * (Answer / Authority / Additional, plus Question for symmetry)
 * is reachable from configs and Variant payloads.
 */
class DnsRecordSection : public TypedEnum<DnsRecordSection> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("DnsRecordSection", "DNS Record Section", 1,
                                                   {"Question",   0, "Question"},
                                                   {"Answer",     1, "Answer"},
                                                   {"Authority",  2, "Authority"},
                                                   {"Additional", 3, "Additional"}); // default: Answer

                using TypedEnum<DnsRecordSection>::TypedEnum;

                static const DnsRecordSection Question;
                static const DnsRecordSection Answer;
                static const DnsRecordSection Authority;
                static const DnsRecordSection Additional;
};

inline const DnsRecordSection DnsRecordSection::Question{0};
inline const DnsRecordSection DnsRecordSection::Answer{1};
inline const DnsRecordSection DnsRecordSection::Authority{2};
inline const DnsRecordSection DnsRecordSection::Additional{3};

/**
 * @brief DNS opcode (RFC 1035 §4.1.1, 4-bit OPCODE field of header).
 *
 *  - @c Query  (0) — Standard query (the only one this library issues).
 *  - @c IQuery (1) — Inverse query (obsolete, RFC 3425).
 *  - @c Status (2) — Server status request.
 *  - @c Notify (4) — Zone change notification (RFC 1996).
 *  - @c Update (5) — Dynamic update (RFC 2136).
 */
class DnsOpcode : public TypedEnum<DnsOpcode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("DnsOpcode", "DNS Opcode", 0,
                                                   {"Query",  0, "Standard Query"},
                                                   {"IQuery", 1, "Inverse Query (Obsolete)"},
                                                   {"Status", 2, "Server Status"},
                                                   {"Notify", 4, "Zone Notify"},
                                                   {"Update", 5, "Dynamic Update"}); // default: Query

                using TypedEnum<DnsOpcode>::TypedEnum;

                static const DnsOpcode Query;
                static const DnsOpcode IQuery;
                static const DnsOpcode Status;
                static const DnsOpcode Notify;
                static const DnsOpcode Update;
};

inline const DnsOpcode DnsOpcode::Query{0};
inline const DnsOpcode DnsOpcode::IQuery{1};
inline const DnsOpcode DnsOpcode::Status{2};
inline const DnsOpcode DnsOpcode::Notify{4};
inline const DnsOpcode DnsOpcode::Update{5};

/**
 * @brief DNS response code (RFC 1035 §4.1.1, 4-bit RCODE field of header).
 *
 * Extended via EDNS0 OPT to a 12-bit space; this enum covers the
 * RFC 1035 / 2136 / 2671 / 6891 set in common use.
 *
 *  - @c NoError   (0)  — No error.
 *  - @c FormErr   (1)  — Format error (server couldn't parse the query).
 *  - @c ServFail  (2)  — Server failure (couldn't process for an
 *                        internal reason).
 *  - @c NxDomain  (3)  — Authoritative "name does not exist".
 *  - @c NotImp    (4)  — Server does not implement the requested kind.
 *  - @c Refused   (5)  — Server refused (policy).
 *  - @c YxDomain  (6)  — Update prerequisite failed (RFC 2136).
 *  - @c YxRrSet   (7)  — Update prerequisite failed (RFC 2136).
 *  - @c NxRrSet   (8)  — Update prerequisite failed (RFC 2136).
 *  - @c NotAuth   (9)  — Server not authoritative (or bad TSIG).
 *  - @c NotZone   (10) — Name not in zone (RFC 2136).
 *  - @c BadVers   (16) — EDNS0 version not supported (RFC 6891).
 *  - @c BadSig    (16) — TSIG signature failure (alias of BadVers
 *                        per RFC 2845; promoted here as a distinct
 *                        codepoint is impossible — caller must look
 *                        at the OPT pseudo-record context to
 *                        disambiguate).  Not separately named below.
 *  - @c BadKey    (17) — TSIG key not recognised.
 *  - @c BadTime   (18) — TSIG signature out of time window.
 *  - @c BadMode   (19) — TKEY bad mode.
 *  - @c BadName   (20) — TKEY duplicate name.
 *  - @c BadAlg    (21) — TKEY algorithm not supported.
 *  - @c BadTrunc  (22) — TSIG truncation.
 *  - @c BadCookie (23) — DNS cookie bad (RFC 7873).
 */
class DnsRcode : public TypedEnum<DnsRcode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("DnsRcode", "DNS Response Code", 0,
                                                   {"NoError",   0,  "No Error"},
                                                   {"FormErr",   1,  "Format Error"},
                                                   {"ServFail",  2,  "Server Failure"},
                                                   {"NxDomain",  3,  "Name Does Not Exist"},
                                                   {"NotImp",    4,  "Not Implemented"},
                                                   {"Refused",   5,  "Refused"},
                                                   {"YxDomain",  6,  "YXDOMAIN (Update Prereq)"},
                                                   {"YxRrSet",   7,  "YXRRSET (Update Prereq)"},
                                                   {"NxRrSet",   8,  "NXRRSET (Update Prereq)"},
                                                   {"NotAuth",   9,  "Not Authoritative"},
                                                   {"NotZone",   10, "Name Not In Zone"},
                                                   {"BadVers",   16, "Bad EDNS Version"},
                                                   {"BadKey",    17, "Bad TSIG Key"},
                                                   {"BadTime",   18, "Bad TSIG Time"},
                                                   {"BadMode",   19, "Bad TKEY Mode"},
                                                   {"BadName",   20, "Duplicate TKEY Name"},
                                                   {"BadAlg",    21, "Bad TKEY Algorithm"},
                                                   {"BadTrunc",  22, "TSIG Truncation"},
                                                   {"BadCookie", 23, "Bad DNS Cookie"}); // default: NoError

                using TypedEnum<DnsRcode>::TypedEnum;

                static const DnsRcode NoError;
                static const DnsRcode FormErr;
                static const DnsRcode ServFail;
                static const DnsRcode NxDomain;
                static const DnsRcode NotImp;
                static const DnsRcode Refused;
                static const DnsRcode YxDomain;
                static const DnsRcode YxRrSet;
                static const DnsRcode NxRrSet;
                static const DnsRcode NotAuth;
                static const DnsRcode NotZone;
                static const DnsRcode BadVers;
                static const DnsRcode BadKey;
                static const DnsRcode BadTime;
                static const DnsRcode BadMode;
                static const DnsRcode BadName;
                static const DnsRcode BadAlg;
                static const DnsRcode BadTrunc;
                static const DnsRcode BadCookie;
};

inline const DnsRcode DnsRcode::NoError{0};
inline const DnsRcode DnsRcode::FormErr{1};
inline const DnsRcode DnsRcode::ServFail{2};
inline const DnsRcode DnsRcode::NxDomain{3};
inline const DnsRcode DnsRcode::NotImp{4};
inline const DnsRcode DnsRcode::Refused{5};
inline const DnsRcode DnsRcode::YxDomain{6};
inline const DnsRcode DnsRcode::YxRrSet{7};
inline const DnsRcode DnsRcode::NxRrSet{8};
inline const DnsRcode DnsRcode::NotAuth{9};
inline const DnsRcode DnsRcode::NotZone{10};
inline const DnsRcode DnsRcode::BadVers{16};
inline const DnsRcode DnsRcode::BadKey{17};
inline const DnsRcode DnsRcode::BadTime{18};
inline const DnsRcode DnsRcode::BadMode{19};
inline const DnsRcode DnsRcode::BadName{20};
inline const DnsRcode DnsRcode::BadAlg{21};
inline const DnsRcode DnsRcode::BadTrunc{22};
inline const DnsRcode DnsRcode::BadCookie{23};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
