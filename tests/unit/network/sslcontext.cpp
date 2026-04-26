/**
 * @file      sslcontext.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sslcontext.h>
#include <promeki/buffer.h>
#include <cstring>

using namespace promeki;

namespace {

        // Self-signed RSA-2048 certificate + key, valid until ~year 2126.
        // Generated with `openssl req -x509 -newkey rsa:2048 -days 36500
        // -nodes -subj /CN=localhost`.  RSA was chosen over the smaller
        // EC variant because some mbedTLS PSA configurations strip EC
        // curves from the X.509 path; RSA is in every default build.
        // Inline so the tests do not depend on a test-time openssl
        // invocation.
        constexpr const char *kTestCertPem = "-----BEGIN CERTIFICATE-----\n"
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

        constexpr const char *kTestKeyPem = "-----BEGIN PRIVATE KEY-----\n"
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

        // Wrap a NUL-terminated PEM string in a Buffer suitable for
        // SslContext::setCertificate / setPrivateKey.  The buffer
        // includes the trailing NUL because mbedTLS needs it.
        Buffer pemBuffer(const char *pem) {
                const size_t n = std::strlen(pem) + 1;
                Buffer       b(n);
                std::memcpy(b.data(), pem, n);
                b.setSize(n);
                return b;
        }

} // anonymous namespace

TEST_CASE("SslContext - default state") {
        SslContext ctx;
        CHECK(ctx.protocol() == SslContext::SecureProtocols);
        CHECK(ctx.verifyPeer());
        CHECK_FALSE(ctx.hasCertificate());
        CHECK_FALSE(ctx.hasCaCertificates());
}

TEST_CASE("SslContext - protocol set/get") {
        SslContext ctx;
        ctx.setProtocol(SslContext::TlsV1_2);
        CHECK(ctx.protocol() == SslContext::TlsV1_2);
        ctx.setProtocol(SslContext::TlsV1_3);
        CHECK(ctx.protocol() == SslContext::TlsV1_3);
}

TEST_CASE("SslContext - verifyPeer toggle") {
        SslContext ctx;
        ctx.setVerifyPeer(false);
        CHECK_FALSE(ctx.verifyPeer());
        ctx.setVerifyPeer(true);
        CHECK(ctx.verifyPeer());
}

TEST_CASE("SslContext - load valid cert + key") {
        SslContext ctx;
        Error      e1 = ctx.setCertificate(pemBuffer(kTestCertPem));
        CHECK(e1.isOk());
        CHECK(ctx.hasCertificate());

        Error e2 = ctx.setPrivateKey(pemBuffer(kTestKeyPem));
        CHECK(e2.isOk());
}

TEST_CASE("SslContext - reject malformed cert") {
        SslContext ctx;
        Buffer     junk(32);
        std::memcpy(junk.data(), "this is not a certificate\0", 27);
        junk.setSize(27);
        Error e = ctx.setCertificate(junk);
        CHECK(e == Error::ParseFailed);
        CHECK_FALSE(ctx.hasCertificate());
}

TEST_CASE("SslContext - load CA certificates") {
        SslContext ctx;
        // Use the same self-signed cert as a (single-entry) CA bundle.
        Error e = ctx.setCaCertificates(pemBuffer(kTestCertPem));
        CHECK(e.isOk());
        CHECK(ctx.hasCaCertificates());
}

TEST_CASE("SslContext - nativeConfig is non-null after init") {
        SslContext ctx;
        CHECK(ctx.nativeConfig() != nullptr);
}
