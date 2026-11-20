#pragma once
// Copy to config.local.h and fill locally. Never commit config.local.h.
// Backup 2.4 GHz WPA2-Personal network. This is tried only if eduroam cannot store data.
constexpr char WIFI_SSID[] = "YOUR_BACKUP_2_4_GHZ_WIFI";
constexpr char WIFI_PASSWORD[] = "YOUR_BACKUP_WIFI_PASSWORD";

// Optional primary WPA2-Enterprise network. Use the institution's verified profile.
#define ENTERPRISE_SSID_DEFINED
constexpr char ENTERPRISE_SSID[] = "eduroam";
#define EAP_SETTINGS_DEFINED
constexpr char EAP_IDENTITY[] = ""; // Empty uses EAP_USERNAME as the outer identity
constexpr char EAP_USERNAME[] = "YOUR_LMS_ID@nust.edu.pk";
constexpr char EAP_PASSWORD[] = "YOUR_EDUROAM_PASSWORD";
constexpr char EAP_CA[] = ""; // Paste institution CA or trusted system roots as PEM
constexpr char EAP_SERVER_DOMAIN[] = "nust.edu.pk";
constexpr char INGEST_URL[] = "https://YOUR_PROJECT.supabase.co/functions/v1/ingest";
constexpr char DEVICE_TOKEN[] = "GENERATE_RANDOM_TOKEN_AT_LEAST_32_CHARACTERS";
constexpr char ROOT_CA[] = R"PEM(
PASTE_THE_VERIFIED_SUPABASE_HTTPS_ROOT_CA_PEM_HERE
)PEM";
