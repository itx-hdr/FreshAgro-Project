# Eduroam verification

The active project uses the ESP32-C3 cloud sketch, Supabase ingestion and the Vercel dashboard. Keep eduroam first and Pie as fallback. Do not change the queue partitions or erase NVS/SPIFFS while diagnosing Wi-Fi.

## Findings on 2026-11-23

- The ESP32 sees multiple 2.4 GHz eduroam access points. A prior test reported disconnect reason 204 (handshake timeout); that code alone does not identify a password or certificate failure.
- The user confirmed the full `@nust.edu.pk` identity and unchanged password used by the connected laptop. Credentials remain in ignored `config.local.h`.
- The EAP CA setter stores a pointer and length without parsing the certificate. Its `ESP_OK` response must not be reported as successful certificate verification.
- PEM input to Mbed TLS requires the terminating NUL in its length. The sketch now passes `strlen(EAP_CA) + 1` and checks parsing before enabling enterprise authentication.
- The first corrected boot reported 32 parsed certificates and one parse failure. The local system-root export contained one obsolete MD2-signed VeriSign root; it was removed from the private bundle, with the prior configuration backed up outside the repository. The next boot parsed all 32 remaining certificates with result zero.
- Five pending readings were present at the start of that boot. All three sensors were valid. Pie was unavailable and no upload was acknowledged; queued readings were retained.

The configured EAP domain is a TLS hostname check, not a discovery endpoint. Do not substitute a guessed RADIUS hostname or disable validation. A parsed CA bundle is not proof that it trusts the institution's server.

## Successful primary connection

The diagnostic boot verified the server certificate for `nust.edu.pk` with result zero and verification flags `0x00000000`. The server subject was `CN=nust.edu.pk`, issued by `Sectigo Public Server Authentication CA DV R36`. Wi-Fi obtained an IP approximately 13 seconds after boot, then synchronized its clock and independently validated the Supabase HTTPS certificate. All seven pending readings were acknowledged through queue order 1018. Both final LED checks were green, and Pie was skipped. A read-only Supabase query confirmed all seven sample IDs and 1,083 total device readings.

The final normal build uses 1,296,255 bytes of the 1,310,720-byte application partition. Symbol inspection confirmed that the optional TLS wrappers were absent. Only the application at offset `0x10000` was flashed, and esptool verified its hash. The physical-reset test started with zero pending readings, captured three valid sensors, and connected to eduroam after one handshake timeout/retry (IP obtained around 43 seconds after boot). It uploaded one new reading through order 1019, skipped Pie, showed green for both final LED checks, and reported `Queued=0 dropped=0`. Awake time was 51,772 ms and the scheduled sleep was 548,227 ms. Supabase independently confirmed that sample and 1,084 device readings total. This verifies a reset-triggered cycle; sustained unattended operation and battery current remain separate measurements.

## Optional TLS diagnostics

`Arduino/FreshAgro_Cloud/EapTlsDiagnostics.cpp` is inactive in normal builds. To report certificate verification results and public peer subject/issuer names, add both build properties to the Arduino CLI compile command:

```text
--build-property compiler.cpp.extra_flags=-DFRESHAGRO_EAP_TLS_DIAGNOSTICS=1
--build-property compiler.c.elf.extra_flags=-Wl,--wrap=mbedtls_x509_crt_verify_restartable,--wrap=mbedtls_ssl_handshake_step
```

The wrappers return the original TLS results unchanged and do not alter certificate callbacks, hostname checks, credentials or trust settings. Do not enable packet/key logging. Use a normal build after diagnosis. Builds contain private configuration and must remain ignored.

## Source references

- [Espressif EAP configuration storage](https://github.com/espressif/esp-idf/blob/v5.5.2/components/wpa_supplicant/esp_supplicant/src/esp_eap_client.c)
- [EAP certificate blob length propagation](https://github.com/espressif/esp-idf/blob/v5.5.2/components/wpa_supplicant/src/eap_peer/eap.c)
- [Espressif Mbed TLS certificate and hostname handling](https://github.com/espressif/esp-idf/blob/v5.5.2/components/wpa_supplicant/esp_supplicant/src/crypto/tls_mbedtls.c)

The installed Arduino core is 3.3.12. Its bundled `mbedtls/x509_crt.h` documents that PEM buffer lengths include the terminating NUL.
