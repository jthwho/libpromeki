/**
 * @file      mdnspacket.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Legacy mDNS-only packet header.  The types this header used to
 * define (@c MdnsPacket, @c MdnsParsedRecord, @c MdnsParsedQuestion)
 * are now aliases of the generalised @ref DnsPacket / @ref DnsRecord
 * / @ref DnsQuestion in @c dnspacket.h / @c dnsrecord.h.  Callers
 * that want mDNS wire-format semantics (cache-flush bit on records,
 * QU bit on questions, DNS-SD TXT decode through @ref MdnsTxtRecord)
 * should use @ref DnsPacket::parseMdns rather than the unicast-mode
 * @ref DnsPacket::parse.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <promeki/dnspacket.h>
#include <promeki/dnsrecord.h>
#endif
