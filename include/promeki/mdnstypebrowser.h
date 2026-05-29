/**
 * @file      mdnstypebrowser.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mdnsservicetype.h>
#include <promeki/mutex.h>
#include <promeki/networkinterface.h>
#include <promeki/objectbase.h>
#include <promeki/socketaddress.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class MdnsManager;

/**
 * @brief Discovers the set of DNS-SD service @b types currently advertised on the link.
 * @ingroup network
 *
 * Where @ref MdnsBrowser surfaces the @b instances of one service
 * type, @c MdnsTypeBrowser surfaces the @b types themselves — the
 * answer to "what kinds of services are on this network?"  It does
 * this by issuing the RFC 6763 §9 meta-query
 * @c _services._dns-sd._udp.local. and watching the PTR responses
 * that name every type currently advertised.
 *
 * Once a type appears, @ref typeFoundSignal fires; once it Goodbyes
 * (or expires), @ref typeLostSignal fires.  No instance-level
 * information is reported here — chain a @ref MdnsBrowser per
 * discovered type if instances are wanted.
 *
 * @par Threading model
 *
 * Same as @ref MdnsBrowser — @ref handlePacket / @ref onManagerTick
 * run on the manager's worker thread; signals dispatch via the
 * connecting object's @ref EventLoop.
 *
 * @par Example
 * @code
 * MdnsTypeBrowser tb;
 * tb.typeFoundSignal.connect([](MdnsServiceType t) {
 *     promekiInfo("type: %s", t.toString().cstr());
 * }, ctx);
 * tb.setManager(&mgr);
 * tb.start();
 * @endcode
 */
class MdnsTypeBrowser : public ObjectBase {
                PROMEKI_OBJECT(MdnsTypeBrowser, ObjectBase)
        public:
                /** @brief Initial continuous-query interval in ms. */
                static constexpr int64_t InitialQueryIntervalMs = 1000;

                /** @brief Maximum continuous-query interval in ms (RFC 6762 §5.2). */
                static constexpr int64_t MaxQueryIntervalMs = 3600 * 1000;

                /** @brief Constructs an idle type browser. */
                explicit MdnsTypeBrowser(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Detaches from the manager if attached. */
                ~MdnsTypeBrowser() override;

                /** @brief Attaches to (or detaches from) a manager.  See @ref MdnsBrowser::setManager. */
                void setManager(MdnsManager *manager);

                /** @brief Returns the explicitly attached manager, or @c nullptr. */
                MdnsManager *manager() const { return _manager; }

                /** @brief Returns the manager actually used (explicit or app fallback). */
                MdnsManager *effectiveManager() const;

                /** @brief Returns @c true between successful @ref start and @ref stop. */
                bool isActive() const { return _active.value(); }

                /** @brief Begins active discovery — sends the initial meta-query. */
                Error start();

                /** @brief Marks the browser inactive.  Does not clear the cache. */
                void stop();

                /** @brief Sends one meta-query without flipping the active flag. */
                Error sendQuery();

                /** @brief Per-packet entry point called by the manager. */
                void handlePacket(const Buffer &data, const SocketAddress &sender,
                                  const NetworkInterface &iface);

                /** @brief Tick driver — backoff + cache eviction. */
                void onManagerTick(const TimeStamp &now);

                /** @brief Snapshot of the types currently known to the browser. */
                List<MdnsServiceType> types() const;

                /** @brief Removes every cached type without emitting signals. */
                void clearCache();

                /** @brief Emitted when a new service type is observed. @signal */
                PROMEKI_SIGNAL(typeFound, MdnsServiceType);
                /** @brief Emitted when a known type Goodbyes or expires. @signal */
                PROMEKI_SIGNAL(typeLost,  MdnsServiceType);

        private:
                struct Entry {
                        MdnsServiceType type;
                        TimeStamp       lastSeen;
                        Duration        ttl;
                };

                MdnsManager        *_manager = nullptr;
                mutable Mutex       _entriesMtx;
                Map<String, Entry>  _entries;   // keyed by case-folded type fqdn
                Atomic<bool>        _active;

                TimeStamp           _nextQueryAt;
                Duration            _currentInterval;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
