/**
 * @file      mediaioport.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/audiodesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIO;
class MediaIOPortGroup;

/**
 * @brief Common base for a single I/O port hanging off a @ref MediaIO.
 * @ingroup mediaio_user
 *
 * A @ref MediaIO is a multi-port container; the actual read/write
 * surfaces are its @ref MediaIOSource and @ref MediaIOSink children
 * (both deriving from @ref MediaIOPort).  A port carries:
 *
 *  - an identity (a per-type @ref index and a human-readable @ref name),
 *  - a back-pointer to its owning @ref MediaIO (reachable through the
 *    port group),
 *  - mandatory membership in a @ref MediaIOPortGroup, which holds the
 *    group's clock / step / position state and references every port
 *    that shares that timing.
 *
 * Per-direction surface area (writeFrame / readFrame, format
 * negotiation, signals) lives on the derived classes — see
 * @ref MediaIOSink and @ref MediaIOSource.  Per-port descriptor
 * caching is added to this base incrementally as later refactor
 * phases move it off @ref MediaIO.  Per-port accounting is
 * intentionally absent: rate tracking, drop / repeat / late counts,
 * and pending command counts all live on the @ref MediaIOPortGroup,
 * because a single backend tick advances the whole group at once.
 *
 * @par Port groups
 * Every port belongs to exactly one @ref MediaIOPortGroup.  An
 * "independent" port is simply a port that is the sole member of its
 * group.  A "synchronized" pair (e.g. paired audio + video on a
 * single backend) shares one group, and the group enforces lockstep
 * advancement.  Ports do not hold their own clock or position state —
 * those live on the group.
 *
 * @par Lifetime
 * Ports are created by @ref MediaIO during @c executeCmd(Open) and
 * parented to their @ref MediaIOPortGroup via @ref ObjectBase
 * parenting; the port group is in turn parented to the owning
 * @ref MediaIO.  Destruction therefore cascades MediaIO → PortGroup
 * → Port, so a port is automatically destroyed when its group is
 * destroyed (or when the @ref MediaIO is).  Ports must not outlive
 * their port group.
 *
 * @par Thread Safety
 * Per-port accessors are read by user-thread callers and updated by
 * the strand worker thread via signals.  Direct reads of cached
 * descriptor state are valid only when the port is open and the user
 * thread is sequentially consistent with prior strand updates — see
 * the per-direction documentation on @ref MediaIOSource /
 * @ref MediaIOSink for details.
 */
class MediaIOPort : public ObjectBase {
                PROMEKI_OBJECT(MediaIOPort, ObjectBase)
        public:
                /**
                 * @brief Role of a port within its @ref MediaIO.
                 *
                 * The role is a structural property — sources read out
                 * of a backend, sinks write into one.  The role is
                 * determined by which derived class is instantiated;
                 * @ref MediaIOSource always has role @c Source,
                 * @ref MediaIOSink always has role @c Sink.
                 */
                enum Role {
                        Source = 0, ///< @brief Port produces frames (reads from backend).
                        Sink        ///< @brief Port consumes frames (writes to backend).
                };

                /**
                 * @brief Constructs a port and binds it to a port group.
                 *
                 * The port is parented to @p group via @ref ObjectBase
                 * parenting and is therefore auto-destroyed when the
                 * group (or its owning @ref MediaIO) is destroyed.
                 * Callers (typically @ref MediaIO helpers) are
                 * responsible for registering the port with the right
                 * per-type list on the owning @ref MediaIO and for
                 * assigning the per-type @ref index.
                 *
                 * @param group The port group this port belongs to;
                 *              must be non-null.  The owning
                 *              @ref MediaIO is reached via
                 *              @c group->mediaIO().
                 * @param name  Human-readable port name (e.g.
                 *              @c "video", @c "audio.left").  May be
                 *              empty; derived classes that want a
                 *              type-and-index default name pass one
                 *              they synthesize from @p index.
                 * @param index Per-type port index (sources and sinks
                 *              have independent index spaces; src0
                 *              and sink0 may coexist).
                 */
                MediaIOPort(MediaIOPortGroup *group, const String &name, int index);

                /** @brief Destructor. */
                ~MediaIOPort() override;

                /**
                 * @brief Returns the role of this port.
                 *
                 * Determined by the concrete subclass.
                 */
                virtual Role role() const = 0;

                /** @brief Returns the @ref MediaIO that owns this port. */
                MediaIO *mediaIO() const { return _mediaIO; }

                /** @brief Returns the per-type port index. */
                int index() const { return _index; }

                /** @brief Returns the human-readable port name. */
                const String &name() const { return _name; }

                /**
                 * @brief Returns the @ref MediaIOPortGroup this port belongs to.
                 *
                 * Always non-null for a constructed port.  The group
                 * holds the timing reference (@ref Clock), step,
                 * current frame, and seek state shared by every port
                 * in the group.  An "independent" port is just the
                 * sole member of a single-port group.
                 */
                MediaIOPortGroup *group() const { return _group; }

                /**
                 * @brief Returns the @ref MediaDesc for this port.
                 *
                 * For sources this is the shape the port produces;
                 * for sinks it is the shape the port accepts.
                 * Backends populate it via @ref CommandMediaIO::addSource
                 * / @c addSink (the @c desc argument).
                 */
                const MediaDesc &mediaDesc() const { return _mediaDesc; }

                /** @brief Sets this port's @ref MediaDesc. */
                void setMediaDesc(const MediaDesc &val) { _mediaDesc = val; }

                /**
                 * @brief Returns the primary @ref AudioDesc for this port.
                 *
                 * Convenience accessor for the first entry of
                 * @c mediaDesc().audioList(); returns an invalid
                 * @ref AudioDesc when the port carries no audio.
                 */
                AudioDesc audioDesc() const {
                        return _mediaDesc.audioList().isEmpty() ? AudioDesc() : _mediaDesc.audioList()[0];
                }

                /**
                 * @brief Returns this port's container @ref Metadata.
                 *
                 * Convenience accessor for @c mediaDesc().metadata().
                 */
                const Metadata &metadata() const { return _mediaDesc.metadata(); }

        private:
                MediaIO          *_mediaIO = nullptr;
                MediaIOPortGroup *_group = nullptr;
                String            _name;
                int               _index = -1;
                MediaDesc         _mediaDesc;
};

PROMEKI_NAMESPACE_END
