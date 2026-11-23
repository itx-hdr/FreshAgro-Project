// Optional diagnostic build only. These wrappers retain the TLS result and all
// verification settings; they report public certificate metadata, never secrets.
// Enable FRESHAGRO_EAP_TLS_DIAGNOSTICS and link with
// --wrap=mbedtls_x509_crt_verify_restartable,--wrap=mbedtls_ssl_handshake_step.
#if defined(FRESHAGRO_EAP_TLS_DIAGNOSTICS)
#include <Arduino.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

extern "C" int __real_mbedtls_x509_crt_verify_restartable(
    mbedtls_x509_crt *, mbedtls_x509_crt *, mbedtls_x509_crl *,
    const mbedtls_x509_crt_profile *, const char *, uint32_t *,
    int (*)(void *, mbedtls_x509_crt *, int, uint32_t *), void *,
    mbedtls_x509_crt_restart_ctx *);

extern "C" int __wrap_mbedtls_x509_crt_verify_restartable(
    mbedtls_x509_crt *crt, mbedtls_x509_crt *trust, mbedtls_x509_crl *crl,
    const mbedtls_x509_crt_profile *profile, const char *hostname, uint32_t *flags,
    int (*verify)(void *, mbedtls_x509_crt *, int, uint32_t *), void *context,
    mbedtls_x509_crt_restart_ctx *restart) {
  const int result = __real_mbedtls_x509_crt_verify_restartable(
      crt, trust, crl, profile, hostname, flags, verify, context, restart);
  Serial.printf("TLS certificate verification: result=%d flags=0x%08lx hostname=%.253s\n",
                result, flags ? static_cast<unsigned long>(*flags) : 0UL,
                hostname ? hostname : "(unset)");
  if (crt) {
    char name[256] = {};
    mbedtls_x509_dn_gets(name, sizeof(name), &crt->subject);
    Serial.printf("TLS peer subject: %s\n", name);
    mbedtls_x509_dn_gets(name, sizeof(name), &crt->issuer);
    Serial.printf("TLS peer issuer: %s\n", name);
  }
  return result;
}

extern "C" int __real_mbedtls_ssl_handshake_step(mbedtls_ssl_context *);
extern "C" int __wrap_mbedtls_ssl_handshake_step(mbedtls_ssl_context *ssl) {
  const int result = __real_mbedtls_ssl_handshake_step(ssl);
  if (result < 0 && result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
    Serial.printf("TLS handshake error: -0x%04x\n", static_cast<unsigned>(-result));
  }
  return result;
}
#endif
