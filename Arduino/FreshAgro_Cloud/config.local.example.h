#pragma once
// Copy to config.local.h and fill locally. Never commit config.local.h.
#define WIFI_ENTERPRISE 0
constexpr char WIFI_SSID[] = "YOUR_2_4_GHZ_WIFI";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
constexpr char INGEST_URL[] = "https://YOUR_PROJECT.supabase.co/functions/v1/ingest";
constexpr char DEVICE_TOKEN[] = "GENERATE_RANDOM_TOKEN_AT_LEAST_32_CHARACTERS";
constexpr char ROOT_CA[] = R"PEM(
PASTE_THE_VERIFIED_SUPABASE_HTTPS_ROOT_CA_PEM_HERE
)PEM";
