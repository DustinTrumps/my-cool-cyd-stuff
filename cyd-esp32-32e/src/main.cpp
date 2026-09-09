#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS);

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 20);
  tft.println("Cheap Yellow Display");
  tft.println("ESP32-32E");

  ts.begin();
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    Serial.printf("Touch X=%d Y=%d\n", p.x, p.y);

    int x = map(p.x, 200, 3800, 0, tft.width());
    int y = map(p.y, 240, 3800, 0, tft.height());
    y = tft.height() - y;

    tft.fillCircle(x, y, 6, TFT_RED);
  }
  delay(30);
}
