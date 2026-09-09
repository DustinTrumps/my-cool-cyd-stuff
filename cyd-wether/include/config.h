#pragma once

/* ------------------------------------------------------------------ */
/* Location & network settings                                          */
/*   Edit WIFI_SSID / WIFI_PASS to your own network.                   */
/*   Location defaults to Aloha, Oregon (Washington County, OR).       */
/* ------------------------------------------------------------------ */

#define WIFI_SSID "dt-network-2.4"
#define WIFI_PASS "Du57!nR00M"

#define LOCATION_NAME "ALOHA, OR"

/* Aloha, Oregon */
#define LATITUDE  "45.51731755299128"
#define LONGITUDE "-122.88899425101259"

/* How often to refresh from the weather API */
#define FETCH_INTERVAL_MS (10UL * 60UL * 1000UL)