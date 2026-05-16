/**
 * @file      ancframesync.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Implements the per-frame ANC walk + dispatch driver loop on top of
 * AncTranslator's SyncPolicy registry.  The interesting bits — what
 * each codec does on Repeat / Drop — live in the per-codec policy
 * registrations.  This file only owns the iteration, the CoW
 * detach, and the once-per-format fallback warning.
 */

#include <promeki/ancframesync.h>
#include <promeki/ancdesc.h>
#include <promeki/ancpayload.h>
#include <promeki/logger.h>
#include <promeki/mediapayload.h>

PROMEKI_NAMESPACE_BEGIN

Result<List<Frame>> AncFrameSync::apply(Frame in, FrameSyncDisposition disposition) {
        List<Frame> out;
        switch (disposition.kind()) {
                case FrameSyncDisposition::Drop: {
                        // No output frame, but every packet still runs through
                        // its Drop policy — the SCTE-104 stash-and-forward
                        // path returns the cue here so it can ride the next
                        // surviving frame.
                        Error err = applyDropAndStash(in);
                        if (err.isError()) return makeError<List<Frame>>(err);
                        return makeResult<List<Frame>>(std::move(out));
                }

                case FrameSyncDisposition::Play: {
                        Frame f   = in;
                        Error err = applyToFrame(f, disposition, 0);
                        if (err.isError()) return makeError<List<Frame>>(err);
                        // Append stashed packets after the policy walk so they
                        // ride through verbatim — the source codec's Drop
                        // policy already produced exactly the wire-form
                        // packet it wanted to forward, and we don't want to
                        // re-process it.
                        drainStashInto(f);
                        out.pushToBack(std::move(f));
                        return makeResult<List<Frame>>(std::move(out));
                }

                case FrameSyncDisposition::Repeat: {
                        const uint8_t n = disposition.repeatCount();
                        for (uint8_t i = 0; i < n; ++i) {
                                Frame f   = in;
                                Error err = applyToFrame(f, disposition, i);
                                if (err.isError()) return makeError<List<Frame>>(err);
                                // Stash drains onto the first surviving frame
                                // of the run only — cues should fire once,
                                // not once per repeat slot.
                                if (i == 0) drainStashInto(f);
                                out.pushToBack(std::move(f));
                        }
                        return makeResult<List<Frame>>(std::move(out));
                }
        }
        // Unreachable (kind is exhaustive) — defensive return so the build
        // stays warning-free under -Wreturn-type.
        return makeResult<List<Frame>>(std::move(out));
}

Error AncFrameSync::applyDropAndStash(const Frame &frame) {
        // Read-only walk — no mutation of the input frame, no output frame
        // produced.  Each packet runs through applySyncPolicy with Drop;
        // any non-empty return from a per-format policy ends up in the
        // stash.
        const MediaPayload::PtrList &payloads = frame.payloadList();
        for (size_t i = 0; i < payloads.size(); ++i) {
                const MediaPayload::Ptr &p = payloads[i];
                if (!p.isValid()) continue;
                if (p->kind() != MediaPayloadKind::AncillaryData) continue;
                const AncPayload *anc = p->as<AncPayload>();
                if (anc == nullptr) continue;
                for (const AncPacket &pkt : anc->packets()) {
                        if (!AncTranslator::hasSyncPolicy(pkt.format())) {
                                warnFallbackOnce(pkt.format().id(), pkt.format().name());
                        }
                        Result<List<AncPacket>> res =
                                _translator.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
                        if (res.second().isError()) return res.second();
                        if (!res.first().isEmpty()) {
                                stashPackets(pkt.format().id(), res.first());
                        }
                }
        }
        return Error::Ok;
}

void AncFrameSync::stashPackets(AncFormat::ID id, const List<AncPacket> &pkts) {
        if (pkts.isEmpty()) return;
        if (_stash.contains(id)) {
                promekiWarn("AncFrameSync: stash collision for format=%d; "
                            "replacing previously-stashed packet(s) (latest-wins)",
                            static_cast<int>(id));
        }
        // Map::insert is insert_or_assign semantics — overwrites the whole list.
        _stash.insert(id, pkts);
}

size_t AncFrameSync::stashedPacketCount() const {
        size_t total = 0;
        for (auto it = _stash.cbegin(); it != _stash.cend(); ++it) {
                total += it->second.size();
        }
        return total;
}

void AncFrameSync::drainStashInto(Frame &frame) {
        if (_stash.isEmpty()) return;

        // Find the first AncPayload on the frame.  CoW-detach via
        // payloads[i].modify() so we can mutate it in place without
        // affecting other Frame handles that share the same slot.
        AncPayload *target = nullptr;
        {
                MediaPayload::PtrList &payloads = frame.payloadList();
                for (size_t i = 0; i < payloads.size(); ++i) {
                        if (!payloads[i].isValid()) continue;
                        if (payloads[i]->kind() != MediaPayloadKind::AncillaryData) continue;
                        MediaPayload *mp = payloads[i].modify();
                        target = mp->as<AncPayload>();
                        if (target != nullptr) break;
                }
        }
        if (target == nullptr) {
                // No AncPayload on the frame — append a fresh empty one.
                AncDesc         desc;
                AncPayload::Ptr fresh = AncPayload::Ptr::create(desc);
                frame.addPayload(fresh);
                MediaPayload::PtrList &payloadsAfter = frame.payloadList();
                target = payloadsAfter.back().modify()->as<AncPayload>();
        }

        for (auto it = _stash.cbegin(); it != _stash.cend(); ++it) {
                for (const AncPacket &p : it->second) {
                        target->addPacket(p);
                }
        }
        _stash.clear();
}

Error AncFrameSync::applyToFrame(Frame &frame, FrameSyncDisposition disposition, uint8_t repeatIndex) {
        // Mutable accessor triggers Frame::Data CoW detach if shared.
        MediaPayload::PtrList &payloads = frame.payloadList();

        for (size_t i = 0; i < payloads.size(); ++i) {
                MediaPayload::Ptr &p = payloads[i];
                if (!p.isValid()) continue;
                if (p->kind() != MediaPayloadKind::AncillaryData) continue;

                // .modify() on the SharedPtr does the CoW detach for the
                // AncPayload itself: if the underlying AncPayload is shared
                // with another Frame's payload list, allocate a clone and
                // point this slot at it.  After this call we hold the sole
                // writable reference to the AncPayload in question.
                MediaPayload *mp  = p.modify();
                AncPayload   *anc = mp->as<AncPayload>();
                if (anc == nullptr) continue;

                // Build the new packet list by dispatching each existing
                // packet through the SyncPolicy registry.  Per-codec
                // policies decide what each disposition actually does.
                AncPacket::List newPackets;
                for (const AncPacket &pkt : anc->packets()) {
                        if (!AncTranslator::hasSyncPolicy(pkt.format())) {
                                warnFallbackOnce(pkt.format().id(), pkt.format().name());
                        }
                        Result<List<AncPacket>> res =
                                _translator.applySyncPolicy(pkt, disposition, repeatIndex);
                        if (res.second().isError()) return res.second();
                        for (const AncPacket &outPkt : res.first()) {
                                newPackets.pushToBack(outPkt);
                        }
                }
                anc->packets() = std::move(newPackets);
        }
        return Error::Ok;
}

void AncFrameSync::warnFallbackOnce(AncFormat::ID id, const String &name) {
        if (_fallbackWarned.contains(id)) return;
        _fallbackWarned.insert(id);
        promekiWarn("AncFrameSync: no sync policy registered for format=%d (%s); "
                    "using AncTranslator default (Drop=drop, Play/Repeat=copy)",
                    static_cast<int>(id), name.cstr());
}

PROMEKI_NAMESPACE_END
