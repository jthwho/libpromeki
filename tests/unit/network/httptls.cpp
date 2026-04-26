/**
 * @file      httptls.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end TLS loopback: drive HttpServer + HttpClient through
 * an SslSocket-terminated handshake using a self-signed RSA cert.
 * Verifies that the SSL plumbing inside HttpConnection /
 * HttpClient::Pending agrees on framing across a real socket pair.
 */

#include <doctest/doctest.h>
#include <promeki/httpserver.h>
#include <promeki/httpclient.h>
#include <promeki/sslcontext.h>
#include <promeki/thread.h>
#include <promeki/eventloop.h>
#include <promeki/socketaddress.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>

using namespace promeki;

namespace {

        // Same RSA-2048 self-signed CN=localhost cert + key as
        // tests/unit/network/sslcontext.cpp.  Inline-duplicated so this
        // test stays self-contained; both files are regenerated together
        // when the cert is rotated.
        constexpr const char *kCertPem = "-----BEGIN CERTIFICATE-----\n"
                                         "MIIDCzCCAfOgAwIBAgIUQdRsb8r4aSLN98QDN4Qc9EeMZ1MwDQYJKoZIhvcNAQEL\n"
                                         "BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MDQyNTAwNDMxMloYDzIxMjYw\n"
                                         "NDAxMDA0MzEyWjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwggEiMA0GCSqGSIb3DQEB\n"
                                         "AQUAA4IBDwAwggEKAoIBAQC3iLONFRxerAxhG/Yrd3i79wMcU6UxzE/EBfjbUxAC\n"
                                         "Sm9rLSzkTj4zE6nHXb/xw2aWk+fBdP9XF4gEr21o76jAyjaihqFgR483zSR3TYBO\n"
                                         "k1pgqv0ECaoYDCw3QoNMkBPFJGd3L3mAqIXUpCJvTDpCMJRBUjHCLGDkoo10eSX2\n"
                                         "JerXSIt+BxTfxvZCaH0HjyRA0dS3c61v9jj/1LTVVmGB8o3xVDYotbQO8uS2lQkZ\n"
                                         "14Sqy57u8kz2o631AEkPH/YK6P0rE/uRpMLwPrY40PkBbHjllhHXVNUpgxfK5UnB\n"
                                         "5U4aodMqbpfQltSRjbiFNU4Q/EzZFBwB1rVGP1aLbN47AgMBAAGjUzBRMB0GA1Ud\n"
                                         "DgQWBBSQ8/JBFiht8y7tPCqsdgiHj3UfTjAfBgNVHSMEGDAWgBSQ8/JBFiht8y7t\n"
                                         "PCqsdgiHj3UfTjAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQC3\n"
                                         "QUTwrCXZuoHmvhjMEeB5tUswLH9vrTANtVlD3oh3i8ovq9SS82w6AQX9lInteesa\n"
                                         "f100OkpGuDUERbRfDlWwMH8Is7FSGv3rBM5VgHjPTFAUiav2/+l5uj88xWyagxNJ\n"
                                         "kWnviXMgBBFqTruWhzxO2mDmmhNK/0iYj14Qm4UDf4tzUv2BwbN3XodyhfFS12LK\n"
                                         "tUCW3KqbiIFXR1d4+P2e/DARPKHE0lgPDizpf3Pr41t8kSIh8CgqObqdBaXQ6VaS\n"
                                         "cYvQB7KRcyd1BDClztMhdIvUV+pMOxVJg1iNdDSL8hyfjy5BDtNWg8XLbyvK4nSE\n"
                                         "0rF8akQCSFiYyp+Yc1cn\n"
                                         "-----END CERTIFICATE-----\n";

        constexpr const char *kKeyPem = "-----BEGIN PRIVATE KEY-----\n"
                                        "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQC3iLONFRxerAxh\n"
                                        "G/Yrd3i79wMcU6UxzE/EBfjbUxACSm9rLSzkTj4zE6nHXb/xw2aWk+fBdP9XF4gE\n"
                                        "r21o76jAyjaihqFgR483zSR3TYBOk1pgqv0ECaoYDCw3QoNMkBPFJGd3L3mAqIXU\n"
                                        "pCJvTDpCMJRBUjHCLGDkoo10eSX2JerXSIt+BxTfxvZCaH0HjyRA0dS3c61v9jj/\n"
                                        "1LTVVmGB8o3xVDYotbQO8uS2lQkZ14Sqy57u8kz2o631AEkPH/YK6P0rE/uRpMLw\n"
                                        "PrY40PkBbHjllhHXVNUpgxfK5UnB5U4aodMqbpfQltSRjbiFNU4Q/EzZFBwB1rVG\n"
                                        "P1aLbN47AgMBAAECggEACfn4wJ06cbOPDLVwilZUoH1UJvVSK6AXqegAoOEltg68\n"
                                        "6AU/V5kgo5FGfS2yIhDlONZ2zU0m8mFuEzjWjPeSES516Pd4zMRJUkTES/h1zSsQ\n"
                                        "gC2P0ntduFSiNGB9oKHxc3U2tEzZWBd1mS2aTZUPCabmE5wOnTnhML64JpaEbCxj\n"
                                        "e+HjohWdoLeVSp6PrdxMKAxVjptdNntP77HXwZx5DM6dAg1CInD1/aJ4ypt1DFGV\n"
                                        "TB2doUAs56mHTVPggD1V68RdszxQZbs+JrZ7IhOE5yIkgg3JwJFwkK6eQ+Vh3Cs+\n"
                                        "elIWmZL/F/ENDYmIQAV8Lu55YEWdYKOF7h8kzlQZQQKBgQDxp7QtgUZNBhFGSpkF\n"
                                        "rhGhj9X74yyY+k9nznHXXPnIT8rCI0VQVrCdKpm0rZ0Txsv//4/K0/QlnuCj1sv9\n"
                                        "VYZiIe9zKbvTWouGYDzEpwHEXWw/WNorltLeJ2sLLhuTFXiAzNn29zylcPOoA75x\n"
                                        "o6MqksH2AW4J/YutnpYCZc2+4QKBgQDCbcOQOFlcyMbCRy0ZrIr3xO+osmAJ9/rt\n"
                                        "GvX70xMzhe25oKn16HboKPaIZeCbkMJUY7z4jxKc1kxzZQFhDzK+R7qI37O8B7y3\n"
                                        "/gXGCxWmypEFFljjhsYmxtthuxucluPW7wbwepJogeT9Klz7ULpq314dPQEaPfav\n"
                                        "iaYkJp/MmwKBgFlto4sXhSmp7iiiIKDuew3cCedueamfMFWNG6oEeVd8198iaFtD\n"
                                        "yZZQFpO7kB6qegIh1FfOOlLVyfI34kO9K78TKebncd/UaT/wS2zHFStTG2UR/6MT\n"
                                        "7LNTyRRZGtFCp9aaeKshcasT8sehow+w7AgsSWU9wDgoQVGeF4uJmythAoGAJV/g\n"
                                        "Nfr/Apz9yB7ShprqY9KRl0YivAfVTnreSjg6+q6GEibWRRUYtmwZaALdeEoNcRdz\n"
                                        "HfyywT9Ylt4Vs8iuInG7Y9BMxppeJqhIB9fdo6BQ3D99es9Pi+iyB0lmd2VyCsEL\n"
                                        "/nIxbrF5iUj5cr4D98NUXh559cdvgjLdoxlhon0CgYB1dxb2EzLTygHmilcAUJWD\n"
                                        "Kvku9/vmtZEulfbEiATQTjr0sQhirDHubXmXSDfrR7fII9wmVvFzbfOiayzKlTDs\n"
                                        "x2G58mAdDCCexD/vsfYzgnffKWLaL523243Rn1TqMora4gZcPkwMm3IKdLNReS15\n"
                                        "rdcfEq7H1AsDBRXFxCxmUg==\n"
                                        "-----END PRIVATE KEY-----\n";

        Buffer pemBuffer(const char *pem) {
                const size_t n = std::strlen(pem) + 1;
                Buffer       b(n);
                std::memcpy(b.data(), pem, n);
                b.setSize(n);
                return b;
        }

        // Spin up an HttpServer (TLS-terminated) and an HttpClient on
        // independent worker threads.  Mirrors ClientFixture in
        // tests/unit/network/httpclient.cpp but pre-installs a server-side
        // SslContext and a client-side SslContext that disables peer
        // verification (the cert is self-signed).
        struct TlsFixture {
                        Thread      serverThread;
                        Thread      clientThread;
                        HttpServer *server = nullptr;
                        HttpClient *client = nullptr;
                        uint16_t    port = 0;

                        TlsFixture() {
                                serverThread.start();
                                clientThread.start();

                                serverThread.threadEventLoop()->postCallable([this]() {
                                        server = new HttpServer();
                                        SslContext::Ptr ctx = SslContext::Ptr::takeOwnership(new SslContext());
                                        REQUIRE(ctx.modify()->setCertificate(pemBuffer(kCertPem)).isOk());
                                        REQUIRE(ctx.modify()->setPrivateKey(pemBuffer(kKeyPem)).isOk());
                                        server->setSslContext(ctx);
                                });
                                clientThread.threadEventLoop()->postCallable([this]() {
                                        client = new HttpClient();
                                        SslContext::Ptr ctx = SslContext::Ptr::takeOwnership(new SslContext());
                                        // Self-signed: disable peer verification so
                                        // the client doesn't reject the handshake.
                                        ctx.modify()->setVerifyPeer(false);
                                        client->setSslContext(ctx);
                                });
                                for (int i = 0; i < 200 && (server == nullptr || client == nullptr); ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                }
                                REQUIRE(server != nullptr);
                                REQUIRE(client != nullptr);
                        }

                        void configure(std::function<void(HttpServer &)> cfg) {
                                std::atomic<bool> done{false};
                                serverThread.threadEventLoop()->postCallable([&]() {
                                        cfg(*server);
                                        done = true;
                                });
                                for (int i = 0; i < 500 && !done; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                }
                                REQUIRE(done);
                        }

                        void listenOnAnyPort() {
                                std::atomic<bool> done{false};
                                serverThread.threadEventLoop()->postCallable([&]() {
                                        Error err = server->listen(SocketAddress::localhost(0));
                                        REQUIRE(err.isOk());
                                        port = server->serverAddress().port();
                                        done = true;
                                });
                                for (int i = 0; i < 500 && !done; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                }
                                REQUIRE(port != 0);
                        }

                        Result<HttpResponse> request(std::function<Future<HttpResponse>(HttpClient &)> issue,
                                                     unsigned int timeoutMs = 5000) {
                                std::promise<Future<HttpResponse>> futPromise;
                                auto                               futShared = futPromise.get_future();
                                clientThread.threadEventLoop()->postCallable(
                                        [&]() { futPromise.set_value(issue(*client)); });
                                Future<HttpResponse> fut = futShared.get();
                                return fut.result(timeoutMs);
                        }

                        ~TlsFixture() {
                                clientThread.threadEventLoop()->postCallable([this]() {
                                        delete client;
                                        client = nullptr;
                                });
                                serverThread.threadEventLoop()->postCallable([this]() {
                                        delete server;
                                        server = nullptr;
                                });
                                clientThread.quit();
                                serverThread.quit();
                                clientThread.wait(2000);
                                serverThread.wait(2000);
                        }
        };

        String urlFor(uint16_t port, const String &path) {
                return String::sprintf("https://127.0.0.1:%u%s", port, path.cstr());
        }

} // anonymous namespace

TEST_CASE("HttpServer + HttpClient - TLS loopback GET") {
        TlsFixture f;
        f.configure([](HttpServer &s) {
                s.route("/secure", HttpMethod::Get,
                        [](const HttpRequest &, HttpResponse &res) { res.setText("encrypted hello"); });
        });
        f.listenOnAnyPort();

        auto [res, err] = f.request([&](HttpClient &c) { return c.get(urlFor(f.port, "/secure")); });
        CHECK(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        REQUIRE(res.body().size() == 15);
        CHECK(std::memcmp(res.body().data(), "encrypted hello", 15) == 0);
}

TEST_CASE("HttpServer + HttpClient - TLS loopback POST") {
        TlsFixture f;
        f.configure([](HttpServer &s) {
                s.route("/echo", HttpMethod::Post,
                        [](const HttpRequest &req, HttpResponse &res) { res.setBody(req.body()); });
        });
        f.listenOnAnyPort();

        Buffer body(7);
        std::memcpy(body.data(), "tlsbody", 7);
        body.setSize(7);

        auto [res, err] = f.request(
                [&](HttpClient &c) { return c.post(urlFor(f.port, "/echo"), body, "application/octet-stream"); });
        CHECK(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        REQUIRE(res.body().size() == 7);
        CHECK(std::memcmp(res.body().data(), "tlsbody", 7) == 0);
}
