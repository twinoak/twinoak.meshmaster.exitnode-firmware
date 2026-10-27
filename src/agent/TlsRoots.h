// The roots this firmware verifies its TLS connections against. Two, both Let's Encrypt's, both
// written here with the evidence a reader can check beside the PEM, and nothing else is ever
// trusted except the ESP32 core's own certificate bundle (src/agent/AgentClient.cpp,
// src/net/MqttClient.cpp), which is verification against the Mozilla roots and not a bypass.
//
// ISRG Root X1 - the root meshmaster.dk's chain ends at (task 01).
//   Verified on the way in (2026-09-28): fetched from https://letsencrypt.org/certs/isrgrootx1.pem,
//   SHA-256 fingerprint 96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6,
//   subject C=US, O=Internet Security Research Group, CN=ISRG Root X1, valid until 2035-06-04.
//   The live chain that day was meshmaster.dk <- YR1 <- Root YR <- ISRG Root X1 (a cross-signed
//   chain), which verifies against this root alone. It is the first root the uplink tries; the
//   core's bundle is the second, for the day Let's Encrypt's chain stops ending here.
//
// ISRG Root X2 - the root the two observer brokers' chains belong to (task p1.7-27).
//   Read off the live chains on 2026-10-27 with `openssl s_client -showcerts` from this machine:
//     mqtt.meshview.dk:443     <- C=US, O=Let's Encrypt, CN=YE1 <- C=US, O=ISRG, CN=Root YE
//                                 <- ISRG Root X2 (served cross-signed by ISRG Root X1)
//     mqtt.meshmapper.net:443  <- C=US, O=Let's Encrypt, CN=YE2 <- C=US, O=ISRG, CN=Root YE
//                                 <- ISRG Root X2 (served cross-signed by ISRG Root X1)
//   Both are Let's Encrypt's ECDSA hierarchy: the self-signed root of that hierarchy is ISRG
//   Root X2, and the fourth certificate each server sends is X2 cross-signed by X1 (fingerprint
//   EE:5F:7A:BD:69:81:BB:02:55:63:2C:D8:F4:92:83:45:1B:4B:18:84:4D:12:04:0B:44:EE:00:F0:7B:8F:E2:C6,
//   valid until 2032-09-02), so each chain as served ends at X1 and verifies against either root
//   alone (`openssl s_client -CAfile` with X1 alone and with X2 alone both answer
//   `Verify return code: 0 (ok)` for both hosts; tasks/phase-1.7/reports/27.md has the whole
//   output). This firmware carries both: X2 because it is the root the chain is anchored at
//   once the cross-sign is dropped, X1 because the chain ends there today and because it is
//   where the RSA hierarchy (R10, R11) ends, should a broker move back to it.
//   The PEM below was fetched from https://letsencrypt.org/certs/isrg-root-x2.pem on 2026-10-27:
//   SHA-256 fingerprint 69:72:9B:8E:15:A8:6E:FC:17:7A:57:AF:B7:17:1D:FC:64:AD:D2:8C:2F:CA:8C:F1:50:7E:34:45:3C:CB:14:70,
//   subject C=US, O=Internet Security Research Group, CN=ISRG Root X2, valid until 2040-09-17.
//
// test/test_tls_policy checks that each root's subject and fingerprint are still written here
// beside its PEM, and that kCarriedRootsPem holds exactly the two.
//
// The two PEMs are macros so that kCarriedRootsPem can be the pair as one buffer: mbedtls reads
// a PEM buffer with several certificates in it as a list, which is what a client's CA store
// takes (esp-mqtt's cert_pem, WiFiClientSecure's setCACert alike).

#pragma once

#define MM_ISRG_ROOT_X1_PEM                                                        \
  "-----BEGIN CERTIFICATE-----\n"                                                  \
  "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"             \
  "TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"             \
  "cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"             \
  "WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"             \
  "ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"             \
  "MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"             \
  "h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"             \
  "0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"             \
  "A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"             \
  "T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"             \
  "B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"             \
  "B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"             \
  "KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"             \
  "OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"             \
  "jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"             \
  "qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"             \
  "rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"             \
  "HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"             \
  "hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"             \
  "ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"             \
  "3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"             \
  "NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"             \
  "ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"             \
  "TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"             \
  "jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"             \
  "oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"             \
  "4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"             \
  "mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"             \
  "emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"             \
  "-----END CERTIFICATE-----\n"

#define MM_ISRG_ROOT_X2_PEM                                                        \
  "-----BEGIN CERTIFICATE-----\n"                                                  \
  "MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"             \
  "CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"             \
  "R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"             \
  "MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"             \
  "ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"             \
  "EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"             \
  "+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"             \
  "ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"             \
  "AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"             \
  "zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"             \
  "tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"             \
  "/q4AaOeMSQ+2b1tbFfLn\n"                                                         \
  "-----END CERTIFICATE-----\n"

namespace mm {

// The uplink's root (src/agent/AgentClient.cpp): meshmaster.dk's chain ends here.
constexpr const char kIsrgRootX1Pem[] = MM_ISRG_ROOT_X1_PEM;

// The brokers' root (src/net/MqttClient.cpp, through src/observer/Connection.h).
constexpr const char kIsrgRootX2Pem[] = MM_ISRG_ROOT_X2_PEM;

// Both, as one CA store: what every observer slot verifies its broker against first. A custom
// broker whose chain ends at neither is tried once against the core's bundle and refused with
// the reason if that fails too (src/observer/Connection.h).
constexpr const char kCarriedRootsPem[] = MM_ISRG_ROOT_X1_PEM MM_ISRG_ROOT_X2_PEM;

}  // namespace mm
