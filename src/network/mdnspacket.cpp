/**
 * @file      mdnspacket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * No translation-unit content — the mDNS packet types are now
 * aliases of @ref DnsPacket / @ref DnsRecord / @ref DnsQuestion
 * (defined in @c dnspacket.cpp / @c dnsrecord.cpp).  This file
 * stays in the build so the CMake source list does not change
 * shape; the linker drops it as an empty object.
 */

#include <promeki/mdnspacket.h>
