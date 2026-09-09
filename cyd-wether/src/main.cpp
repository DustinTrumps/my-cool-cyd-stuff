#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "main.h"
#include "config.h"

/* ------------------------------------------------------------------ */
/* Cheap Yellow Display (ESP32-2432S028R) pin map                      */
/* ------------------------------------------------------------------ */
#define SCREEN_W 320
#define SCREEN_H 240
#define TFT_BL_PIN 21

/* Raw touch value range seen on this panel */
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 240
#define TOUCH_MAX_Y 3800

TFT_eSPI tft = TFT_eSPI();
SPIClass ts_spi = SPIClass(VSPI);
XPT2046_Touchscreen ts(33);

/* ------------------------------------------------------------------ */
/* LVGL display + input device wiring                                  */
/* ------------------------------------------------------------------ */
static lv_display_t *disp;
static lv_color_t *drawBuf1;
static lv_color_t *drawBuf2;

void lv_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  tft.pushImage(area->x1, area->y1, w, h, (uint16_t *)px_map);
  lv_display_flush_ready(display);
}

void lv_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int sx = SCREEN_W - map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_W);
    int sy = SCREEN_H - map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_H);
    data->point.x = constrain(sx, 0, SCREEN_W - 1);
    data->point.y = constrain(sy, 0, SCREEN_H - 1);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void setupDisplay() {
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  /* ILI9341 expects big-endian RGB565; LVGL renders little-endian.
     The byte-swapped push is what makes text and colors render correctly. */
  tft.setSwapBytes(true);
}

void setupTouch() {
  ts_spi.begin(25, 39, 32, 33);
  ts.begin(ts_spi);
  ts.setRotation(1);
  delay(10);
}

void setupLvgl() {
  lv_init();

  size_t bufSize = SCREEN_W * 20 * sizeof(lv_color_t);
  drawBuf1 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT);
  drawBuf2 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT);
  if (drawBuf1 == nullptr || drawBuf2 == nullptr) {
    Serial.println("FATAL: could not allocate LVGL draw buffers");
    while (1) delay(1000);
  }

  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, lv_flush_cb);
  lv_display_set_buffers(disp, drawBuf1, drawBuf2, bufSize,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lv_touch_cb);
}

/* ------------------------------------------------------------------ */
/* Weather UI                                                          */
/* ------------------------------------------------------------------ */
enum Status { STATUS_SYNC, STATUS_LIVE, STATUS_OFFLINE };

static lv_obj_t *iconImg;
static lv_obj_t *tempLabel;
static lv_obj_t *condLabel;
static lv_obj_t *humValue;
static lv_obj_t *feelValue;
static lv_obj_t *subLabel;
static lv_obj_t *statusChip;
static lv_obj_t *statusTxt;

static bool gHasData = false;
static bool gRefreshRequested = false;
static int gLastHour = -1;
static int gLastMin = -1;

/* Dark, balanced palette that lets the weather art carry the color. */
#define C_BG     lv_color_hex(0x0B1118) /* near-black navy background  */
#define C_PANEL  lv_color_hex(0x141D29) /* raised surface for chips    */
#define C_INK    lv_color_hex(0xEDF2F8) /* primary text                */
#define C_MUTED  lv_color_hex(0x7E8B9A) /* secondary text              */
#define C_FAINT  lv_color_hex(0x5E6B7B) /* hints, meta                 */
#define C_ACCENT lv_color_hex(0x5BC8FF) /* sky accent                  */
#define C_ALERT  lv_color_hex(0xFF7A6E) /* offline / error             */

lv_obj_t *makeText(lv_obj_t *parent, const char *text, const lv_font_t *font,
                   lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

lv_obj_t *makeChip(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *chip = lv_obj_create(parent);
  lv_obj_set_size(chip, w, h);
  lv_obj_set_pos(chip, x, y);
  lv_obj_set_style_bg_color(chip, C_PANEL, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(chip, 10, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  return chip;
}

void setStatus(Status s, const char *info) {
  lv_label_set_text(subLabel, info);
  switch (s) {
    case STATUS_LIVE:
      lv_obj_set_style_text_color(statusTxt, C_ACCENT, 0);
      lv_label_set_text(statusTxt, "LIVE");
      break;
    case STATUS_SYNC:
      lv_obj_set_style_text_color(statusTxt, C_MUTED, 0);
      lv_label_set_text(statusTxt, "SYNC");
      break;
    case STATUS_OFFLINE:
      lv_obj_set_style_text_color(statusTxt, C_ALERT, 0);
      lv_label_set_text(statusTxt, "OFF");
      break;
  }
}

void setData(float tempF, float feelF, int humPct, int weatherCode, int isDay) {
  char buf[8];

  snprintf(buf, sizeof(buf), "%.0f", tempF);
  lv_label_set_text(tempLabel, buf);

  snprintf(buf, sizeof(buf), "%.0f", feelF);
  lv_label_set_text(feelValue, buf);

  snprintf(buf, sizeof(buf), "%d%%", humPct);
  lv_label_set_text(humValue, buf);

  const lv_image_dsc_t *icon = nullptr;
  const char *label = nullptr;
  if (weatherCode == 0) {
    icon = isDay ? &image_weather_sun : &image_weather_night;
    label = isDay ? "SUNNY" : "CLEAR NIGHT";
  } else if (weatherCode == 1 || weatherCode == 2) {
    icon = &image_weather_cloud;
    label = "PARTLY CLOUDY";
  } else if (weatherCode == 3 || weatherCode == 45 || weatherCode == 48) {
    icon = &image_weather_cloud;
    label = "CLOUDY";
  } else if (weatherCode >= 71 && weatherCode <= 86) {
    icon = &image_weather_snow;
    label = "SNOW";
  } else if (weatherCode == 80 || weatherCode == 81 || weatherCode == 82) {
    icon = &image_weather_rain;
    label = "SHOWERS";
  } else if (weatherCode >= 51 && weatherCode <= 67) {
    icon = &image_weather_rain;
    label = "RAIN";
  } else {
    icon = &image_weather_thunder;
    label = "STORM";
  }

  lv_image_set_src(iconImg, icon);
  lv_label_set_text(condLabel, label);
}

/* ------------------------------------------------------------------ */
/* Weather fetch (Open-Meteo, free, no key needed)                     */
/* ------------------------------------------------------------------ */
static float extractNumber(const String &body, const String &key) {
  String k = "\"" + key + "\":";
  int i = body.indexOf(k);
  if (i < 0) return NAN;
  i += k.length();
  String num;
  while (i < (int)body.length()) {
    char c = body[i];
    if (c == ',' || c == '}' || c == ']') break;
    num += c;
    i++;
  }
  num.trim();
  return num.toFloat();
}

static bool extractTime(const String &body, int &hour, int &minute) {
  const char *k = "\"time\":\"";
  int i = body.indexOf(k);
  if (i < 0) return false;
  i += strlen(k);
  String t = body.substring(i, i + 16); /* 2026-09-08T21:00 */
  if (t.length() != 16) return false;
  hour = t.substring(11, 13).toInt();
  minute = t.substring(14, 16).toInt();
  return true;
}

void printWifiStatus() {
  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL: Serial.println("  wifi: SSID not found"); break;
    case WL_CONNECT_FAILED: Serial.println("  wifi: connect failed"); break;
    case WL_DISCONNECTED: Serial.println("  wifi: disconnected"); break;
    case WL_CONNECTION_LOST: Serial.println("  wifi: connection lost"); break;
    case WL_IDLE_STATUS: Serial.println("  wifi: idle"); break;
    default: {
      Serial.print("  wifi: status=");
      Serial.println((int)WiFi.status());
    } break;
  }
}

bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.print("wifi: connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    if (millis() - start < 12000 && (millis() - start) % 3000 < 300) {
      Serial.print("  ...waiting (");
      Serial.print((millis() - start) / 1000);
      Serial.println("s)");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("wifi: connected, IP=");
    Serial.println(WiFi.localIP());
    return true;
  }

  printWifiStatus();
  return false;
}

bool fetchWeather() {
  setStatus(STATUS_SYNC, "CONNECTING...");
  if (!connectWifi()) {
    setStatus(STATUS_OFFLINE, "NO CONNECTION");
    return false;
  }

  HTTPClient http;
  String url = String("http://api.open-meteo.com/v1/forecast?latitude=") +
               LATITUDE +
               "&longitude=" + LONGITUDE +
               "&current=temperature_2m,relative_humidity_2m,"
               "apparent_temperature,is_day,weather_code" +
               "&temperature_unit=fahrenheit&timezone=auto&forecast_days=1";

  Serial.print("http: GET ");
  Serial.println(url.substring(0, 45));
  http.begin(url);
  http.setTimeout(10000);
  int code = http.GET();
  Serial.print("http: code=");
  Serial.println(code);
  if (code <= 0) Serial.println(http.errorToString(code));

  bool ok = false;
  if (code == 200) {
    String body = http.getString();
    Serial.print("http: body[");
    Serial.print(body.length());
    Serial.println("]");
    Serial.println(body.substring(0, 200));
    float temp  = extractNumber(body, "temperature_2m");
    float feel  = extractNumber(body, "apparent_temperature");
    float hum   = extractNumber(body, "relative_humidity_2m");
    int   isDay = (int)extractNumber(body, "is_day");
    int   wc    = (int)extractNumber(body, "weather_code");

    if (!isnan(temp) && !isnan(feel) && !isnan(hum)) {
      Serial.printf("data: temp=%.1fF feel=%.1fF hum=%.0f code=%d day=%d\r\n",
                    temp, feel, hum, wc, isDay);
      setData(temp, feel, (int)hum, wc, isDay);
      gHasData = true;
      if (extractTime(body, gLastHour, gLastMin)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "UPDATED %02d:%02d", gLastHour, gLastMin);
        setStatus(STATUS_LIVE, buf);
      } else {
        setStatus(STATUS_LIVE, "UPDATED");
      }
      ok = true;
    } else {
      Serial.println("data: parse failed (unexpected payload)");
    }
  }
  http.end();

  if (!ok) setStatus(STATUS_OFFLINE, "FETCH FAILED");
  return ok;
}

/* ------------------------------------------------------------------ */
/* UI construction                                                     */
/* ------------------------------------------------------------------ */
void onScreenClicked(lv_event_t *e) {
  gRefreshRequested = true;
}

void buildUi() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  /* Header */
  lv_obj_t *title = makeText(scr, LOCATION_NAME, &lv_font_montserrat_24, C_INK);
  lv_obj_set_pos(title, 16, 8);

  subLabel = makeText(scr, "CONNECTING...", &lv_font_montserrat_14, C_FAINT);
  lv_obj_set_pos(subLabel, 17, 40);

  statusChip = lv_obj_create(scr);
  lv_obj_set_size(statusChip, 54, 22);
  lv_obj_align(statusChip, LV_ALIGN_TOP_RIGHT, -14, 11);
  lv_obj_set_style_bg_color(statusChip, C_PANEL, 0);
  lv_obj_set_style_bg_opa(statusChip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(statusChip, 11, 0);
  lv_obj_set_style_border_width(statusChip, 0, 0);
  lv_obj_remove_flag(statusChip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(statusChip, LV_OBJ_FLAG_CLICKABLE);
  statusTxt = makeText(statusChip, "SYNC", &lv_font_montserrat_14, C_MUTED);
  lv_obj_center(statusTxt);

  /* Big condition icon, left */
  iconImg = lv_image_create(scr);
  lv_obj_set_size(iconImg, 128, 128);
  lv_obj_set_pos(iconImg, 16, 58);
  lv_image_set_src(iconImg, &image_weather_sun);

  /* Temperature, right */
  tempLabel = makeText(scr, "--", &lv_font_montserrat_32, C_INK);
  lv_obj_set_pos(tempLabel, 168, 40);

  lv_obj_t *unit = makeText(scr, "F", &lv_font_montserrat_14, C_ACCENT);
  lv_obj_align_to(unit, tempLabel, LV_ALIGN_OUT_TOP_RIGHT, 4, 12);

  condLabel = makeText(scr, "--", &lv_font_montserrat_20, C_ACCENT);
  lv_obj_set_pos(condLabel, 168, 96);

  /* Metric chips */
  lv_obj_t *chip1 = makeChip(scr, 164, 136, 140, 40);
  lv_obj_t *humIcon = lv_image_create(chip1);
  lv_image_set_src(humIcon, &image_weather_humidity);
  lv_obj_set_size(humIcon, 24, 24);
  lv_obj_set_pos(humIcon, 10, 8);
  lv_obj_t *humTag = makeText(chip1, "HUMIDITY", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(humTag, 42, 2);
  humValue = makeText(chip1, "--", &lv_font_montserrat_20, C_INK);
  lv_obj_set_pos(humValue, 42, 18);

  lv_obj_t *chip2 = makeChip(scr, 164, 180, 140, 40);
  lv_obj_t *feelIcon = lv_image_create(chip2);
  lv_image_set_src(feelIcon, &image_weather_temperature);
  lv_obj_set_size(feelIcon, 24, 24);
  lv_obj_set_pos(feelIcon, 10, 8);
  lv_obj_t *feelTag = makeText(chip2, "FEELS LIKE", &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_pos(feelTag, 42, 2);
  feelValue = makeText(chip2, "--", &lv_font_montserrat_20, C_INK);
  lv_obj_set_pos(feelValue, 42, 18);

  lv_obj_t *hint = makeText(scr, "TAP TO REFRESH  -  AUTO 10 MIN",
                            &lv_font_montserrat_14, C_FAINT);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, onScreenClicked, LV_EVENT_CLICKED, NULL);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  setupDisplay();
  setupTouch();
  setupLvgl();

  buildUi();
  fetchWeather();
}

void loop() {
  static uint32_t lastTick = 0;
  static uint32_t lastFetch = 0;
  uint32_t now = millis();

  if (gRefreshRequested ||
      (gHasData && (now - lastFetch) >= FETCH_INTERVAL_MS)) {
    gRefreshRequested = false;
    if (fetchWeather()) {
      lastFetch = now;
    } else if ((now - lastFetch) >= FETCH_INTERVAL_MS) {
      lastFetch = now - FETCH_INTERVAL_MS + 30000UL; /* retry in 30s */
    }
  }

  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();
  delay(5);
}