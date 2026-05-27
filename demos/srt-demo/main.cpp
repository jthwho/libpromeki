/**
 * @file      srt-demo/main.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Self-contained loopback demonstration of the libpromeki SRT API:
 *
 *   1. An @ref SrtServer is brought up on 127.0.0.1:<port>, with a
 *      shared AES passphrase set on it.
 *   2. An @ref SrtEpoll multiplexes the listener; its push-dispatch
 *      callback runs on a dedicated worker thread (@ref SrtEpoll::run)
 *      and accepts the connection that lands.
 *   3. A second thread drives an @ref SrtSocket caller — it connects
 *      with the matching passphrase, then sends a handful of payloads
 *      with a small delay between each.
 *   4. The accepted server socket reads the payloads and prints them.
 *   5. After all payloads have been exchanged the demo prints the
 *      caller's @ref SrtSocket::stats snapshot and exits.
 *
 * Usage:
 *   srt-demo [--port N] [--passphrase STR] [--messages N]
 *
 * The demo intentionally uses a single process and loopback so it is
 * runnable with no setup.  The same code shape works between two
 * machines once the addresses are swapped for real ones.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>

#include <promeki/application.h>
#include <promeki/error.h>
#include <promeki/socketaddress.h>
#include <promeki/srtsocket.h>
#include <promeki/srtserver.h>
#include <promeki/srtepoll.h>
#include <promeki/string.h>
#include <promeki/thread.h>

using namespace promeki;

namespace {

        struct Config {
                        uint16_t port = 5042;
                        String   passphrase = String("demo-srt-passphrase");
                        int      messages = 5;
        };

        static void usageAndExit(const char *argv0, int rc) {
                std::fprintf(stderr,
                             "Usage: %s [--port N] [--passphrase STR] [--messages N]\n",
                             argv0);
                std::exit(rc);
        }

        static Config parseArgs(int argc, char **argv) {
                Config cfg;
                for (int i = 1; i < argc; ++i) {
                        const String a = argv[i];
                        const auto   need = [&](int idx) {
                                if (idx >= argc) usageAndExit(argv[0], 2);
                        };
                        if (a == String("--port")) {
                                need(i + 1);
                                cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
                        } else if (a == String("--passphrase")) {
                                need(i + 1);
                                cfg.passphrase = String(argv[++i]);
                        } else if (a == String("--messages")) {
                                need(i + 1);
                                cfg.messages = std::atoi(argv[++i]);
                        } else if (a == String("--help") || a == String("-h")) {
                                usageAndExit(argv[0], 0);
                        } else {
                                std::fprintf(stderr, "Unknown arg: %s\n", argv[i]);
                                usageAndExit(argv[0], 2);
                        }
                }
                return cfg;
        }

} // anonymous namespace

int main(int argc, char **argv) {
        Application app(argc, argv);
        const Config cfg = parseArgs(argc, argv);

        std::printf("=== libpromeki SRT demo ===\n");
        std::printf("Listening on 127.0.0.1:%u with %zu-byte passphrase\n",
                    cfg.port, cfg.passphrase.byteCount());
        std::printf("Will exchange %d encrypted messages over loopback.\n\n",
                    cfg.messages);

        // -------- listener side --------
        SrtServer server;
        server.setPassphrase(cfg.passphrase);
        server.setEncryptionKeyLength(16);
        server.setLatency(120);
        Error e = server.listen(SocketAddress::localhost(cfg.port));
        if (e.isError()) {
                std::fprintf(stderr, "listen failed: %s\n", e.name().cstr());
                return 1;
        }

        SrtEpoll        mux;
        SrtSocket::UPtr accepted;
        std::atomic<bool> stop{false};
        e = mux.add(server, SrtEpoll::ReadReady);
        if (e.isError()) {
                std::fprintf(stderr, "mux.add failed: %s\n", e.name().cstr());
                return 1;
        }
        // Push-dispatch: fire when the listener has an accept ready.
        // We adopt the new socket and then drop the listener from the
        // multiplexer (single-peer demo).
        e = mux.setCallback(server, [&](int events) {
                if (!(events & SrtEpoll::ReadReady)) return;
                accepted = server.accept(0);
                if (accepted) {
                        std::printf("[listener] accepted from %s\n",
                                    accepted->peerAddress().toString().cstr());
                        stop.store(true, std::memory_order_release);
                }
        });
        if (e.isError()) return 1;

        std::thread mxThread([&] { mux.run(); });

        // -------- caller side --------
        std::thread caller([&] {
                SrtSocket sock;
                sock.setPassphrase(cfg.passphrase);
                sock.setEncryptionKeyLength(16);
                sock.setLatency(120);
                if (sock.open(IODevice::ReadWrite).isError()) {
                        std::fprintf(stderr, "[caller] open failed: %s\n",
                                     sock.lastSrtError().cstr());
                        return;
                }
                sock.setConnectTimeout(3000);
                Error err = sock.connectToHost(SocketAddress::localhost(cfg.port));
                if (err.isError()) {
                        std::fprintf(stderr, "[caller] connect failed: %s (%s)\n",
                                     err.name().cstr(), sock.lastSrtError().cstr());
                        return;
                }
                std::printf("[caller] handshake complete.\n");
                sock.setSendTimeout(2000);
                for (int i = 0; i < cfg.messages; ++i) {
                        char buf[256];
                        const int n = std::snprintf(buf, sizeof(buf),
                                                    "msg %d / %d", i + 1, cfg.messages);
                        sock.write(buf, n);
                        std::printf("[caller] sent: \"%.*s\"\n", n, buf);
                        promeki::BasicThread::sleepMs(150);
                }
                // Print stats before tearing down.
                const auto s = sock.stats();
                std::printf("[caller] stats: pktSent=%lld byteSent=%llu RTT=%.2fms BW=%.2fMbps\n",
                            static_cast<long long>(s.pktSent),
                            static_cast<unsigned long long>(s.byteSent),
                            s.rttMs, s.linkBandwidthMbps);
                promeki::BasicThread::sleepMs(200);
                sock.close();
        });

        // Wait for the listener-side accept (driven by mux.run on its
        // own thread) before draining the data path.
        while (!stop.load(std::memory_order_acquire)) {
                promeki::BasicThread::sleepMs(20);
        }
        mux.stop();
        mxThread.join();

        if (accepted) {
                accepted->setReceiveTimeout(2000);
                char buf[2048];
                int  received = 0;
                while (received < cfg.messages) {
                        const int64_t got = accepted->read(buf, sizeof(buf));
                        if (got <= 0) break;
                        std::printf("[listener] got: \"%.*s\" (%lld bytes)\n",
                                    static_cast<int>(got), buf,
                                    static_cast<long long>(got));
                        ++received;
                }
        }

        caller.join();
        std::printf("\nDone.\n");
        return 0;
}
