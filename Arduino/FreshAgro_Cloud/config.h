#pragma once
// Copy values into config.local.h (ignored by version control). Never publish that file.
#if __has_include("config.local.h")
#include "config.local.h"
#else
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr char INGEST_URL[] = ""; // https://PROJECT.supabase.co/functions/v1/ingest
constexpr char DEVICE_TOKEN[] = ""; // 32+ random characters; server stores its SHA-256 hash
constexpr char ROOT_CA[] = ""; // Trusted CA PEM for INGEST_URL; certificate checks are mandatory
#endif

// PEAP is provisional: verify the institution's official EAP profile before use.
#ifndef WIFI_ENTERPRISE
#define WIFI_ENTERPRISE 1
#endif
#ifndef EAP_METHOD
#define EAP_METHOD WPA2_AUTH_PEAP
#endif
#ifndef EAP_SETTINGS_DEFINED
constexpr char EAP_IDENTITY[] = "";
constexpr char EAP_USERNAME[] = "";
constexpr char EAP_PASSWORD[] = "";
constexpr char EAP_CA[] = ""; // Institution's trusted authentication CA PEM
constexpr char EAP_SERVER_DOMAIN[] = ""; // Expected authentication server certificate name
#endif
