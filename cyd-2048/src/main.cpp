#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

TFT_eSPI tft = TFT_eSPI();

const int SCREEN_W = 320;
const int SCREEN_H = 240;

const int GX = 2, GY = 2;
const int CELL = 56, GAP = 5;
const int PANEL_X = GX + 4 * CELL + 3 * GAP + 10;

const int TFT_BL_PIN = 21;

const int TOUCH_MIN_X = 200;
const int TOUCH_MAX_X = 3700;
const int TOUCH_MIN_Y = 240;
const int TOUCH_MAX_Y = 3800;

#define HIST_MAX 32

SPIClass ts_spi = SPIClass(VSPI);
XPT2046_Touchscreen ts(33);
Preferences prefs;

const uint16_t C_BG     = tft.color565(187, 173, 160);
const uint16_t C_EMPTY  = tft.color565(205, 193, 180);
const uint16_t C_DARK   = tft.color565(119, 110, 101);
const uint16_t C_LIGHT  = tft.color565(249, 246, 242);

int board[4][4];
int oldBoard[4][4];
long score;
long best;
bool gameOver;
long lastScore = -1;
long lastBest = -1;
bool lastOver = false;

struct State {
  int tiles[4][4];
  long score;
};
State history[HIST_MAX];
int histCount = 0;

const int BTN_W = 320 - PANEL_X;
const int BTN_UNDO_Y = 118;
const int BTN_RES_Y = 150;
const int BTN_H = 28;

// generous hit zones that overlap slightly at the midpoint
#define BTN_HIT_UNDO_Y (BTN_UNDO_Y - 6)
#define BTN_HIT_RES_Y  (BTN_RES_Y - 6)
#define BTN_HIT_H      (BTN_H + 12)

int cellX(int c) { return GX + c * (CELL + GAP); }
int cellY(int r) { return GY + r * (CELL + GAP); }

uint16_t tileColor(int v) {
  switch (v) {
    case 2:     return tft.color565(238, 228, 218);
    case 4:     return tft.color565(237, 224, 200);
    case 8:     return tft.color565(242, 177, 121);
    case 16:    return tft.color565(245, 149, 99);
    case 32:    return tft.color565(246, 124, 95);
    case 64:    return tft.color565(246, 94, 59);
    case 128:   return tft.color565(237, 207, 114);
    case 256:   return tft.color565(237, 204, 97);
    case 512:   return tft.color565(237, 200, 80);
    case 1024:  return tft.color565(237, 197, 63);
    case 2048:  return tft.color565(237, 194, 46);
    default:    return tft.color565(60, 58, 50);
  }
}

void drawTile(int r, int c) {
  int x = cellX(c);
  int y = cellY(r);
  int v = board[r][c];
  if (v == 0) {
    tft.fillRect(x, y, CELL, CELL, C_EMPTY);
    return;
  }
  tft.fillRect(x, y, CELL, CELL, tileColor(v));

  char buf[8];
  itoa(v, buf, 10);
  int len = strlen(buf);
  int textSize = (len <= 4) ? 2 : 1;
  tft.setTextFont(1);
  tft.setTextSize(textSize);
  uint16_t tc = (v <= 4) ? C_DARK : C_LIGHT;
  tft.setTextColor(tc);
  int w = tft.textWidth(buf);
  int h = tft.fontHeight(textSize);
  tft.setCursor(x + (CELL - w) / 2, y + (CELL - h) / 2);
  tft.print(buf);
}

void drawButton(const char *label, int y) {
  tft.fillRoundRect(PANEL_X, y, BTN_W, BTN_H, 5, C_EMPTY);
  tft.drawRoundRect(PANEL_X, y, BTN_W, BTN_H, 5, C_DARK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(C_DARK);
  int w = tft.textWidth(label);
  int h = tft.fontHeight(1);
  tft.setCursor(PANEL_X + (BTN_W - w) / 2, y + (BTN_H - h) / 2);
  tft.print(label);
}

void flashButton(const char *label, int y) {
  tft.fillRoundRect(PANEL_X, y, BTN_W, BTN_H, 5, C_DARK);
  tft.drawRoundRect(PANEL_X, y, BTN_W, BTN_H, 5, C_LIGHT);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(C_LIGHT);
  int w = tft.textWidth(label);
  int h = tft.fontHeight(1);
  tft.setCursor(PANEL_X + (BTN_W - w) / 2, y + (BTN_H - h) / 2);
  tft.print(label);
  delay(90);
  drawButton(label, y);
}

void drawOverlay();

void drawPanel() {
  int x = PANEL_X;

  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextColor(tft.color565(237, 194, 46));
  tft.setCursor(x, 6);
  tft.print("2048");

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(200, 190, 175));
  tft.setCursor(x, 34);
  tft.print("SCORE");
  char buf[10];
  itoa(score, buf, 10);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.fillRect(x, 46, BTN_W, 18, C_BG);
  tft.setCursor(x, 48);
  tft.print(buf);

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(200, 190, 175));
  tft.setCursor(x, 72);
  tft.print("BEST");
  itoa(best, buf, 10);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.fillRect(x, 84, BTN_W, 18, C_BG);
  tft.setCursor(x, 86);
  tft.print(buf);

  drawButton("UNDO", BTN_UNDO_Y);
  drawButton("RESTART", BTN_RES_Y);

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(200, 190, 175));
  tft.setCursor(x, 218);
  tft.print("Swipe to move");

  drawOverlay();
}

void drawOverlay() {
  int x = PANEL_X;
  tft.fillRect(x, 188, BTN_W, 24, C_BG);
  if (gameOver) {
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(246, 94, 59));
    tft.setCursor(x, 192);
    tft.print("GAME OVER");
    tft.setCursor(x + 2, 202);
    tft.print("Tap to retry");
  }
}

void showScore() {
  int x = PANEL_X;
  char buf[10];
  itoa(score, buf, 10);
  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.fillRect(x, 46, BTN_W, 18, C_BG);
  tft.setCursor(x, 48);
  tft.print(buf);
  lastScore = score;
}

void showBest() {
  int x = PANEL_X;
  char buf[10];
  itoa(best, buf, 10);
  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.fillRect(x, 84, BTN_W, 18, C_BG);
  tft.setCursor(x, 86);
  tft.print(buf);
  lastBest = best;
}

void drawGame() {
  tft.startWrite();
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (board[r][c] != oldBoard[r][c])
        drawTile(r, c);
  tft.endWrite();
  memcpy(oldBoard, board, sizeof(board));

  if (score != lastScore) showScore();
  if (best != lastBest) showBest();
  if (gameOver != lastOver) { drawOverlay(); lastOver = gameOver; }
}

void drawStatic() {
  tft.fillScreen(C_BG);
  tft.fillRect(PANEL_X, 0, BTN_W, SCREEN_H, C_BG);
  tft.startWrite();
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      drawTile(r, c);
  tft.endWrite();
  memcpy(oldBoard, board, sizeof(board));
  lastScore = lastBest = -1;
  lastOver = false;
  drawPanel();
  lastScore = score;
  lastBest = best;
  lastOver = gameOver;
}

bool slide(int a[4], int &pts) {
  int b[4] = {0, 0, 0, 0};
  int k = 0;
  for (int i = 0; i < 4; i++)
    if (a[i]) b[k++] = a[i];

  int m[4] = {0, 0, 0, 0};
  int n = 0;
  for (int i = 0; i < k; i++) {
    if (i + 1 < k && b[i] == b[i + 1]) {
      m[n++] = b[i] * 2;
      pts += b[i] * 2;
      i++;
    } else
      m[n++] = b[i];
  }

  bool changed = false;
  for (int i = 0; i < 4; i++) {
    if (a[i] != m[i]) changed = true;
    a[i] = m[i];
  }
  return changed;
}

void revArr(int a[4]) {
  int t = a[0]; a[0] = a[3]; a[3] = t;
  t = a[1]; a[1] = a[2]; a[2] = t;
}

void getCol(int c, int a[4]) {
  for (int r = 0; r < 4; r++) a[r] = board[r][c];
}
void setCol(int c, int a[4]) {
  for (int r = 0; r < 4; r++) board[r][c] = a[r];
}

void saveBest();

bool moveLeft() {
  bool changed = false;
  int pts = 0;
  for (int r = 0; r < 4; r++) changed |= slide(board[r], pts);
  if (pts) { score += pts; if (score > best) saveBest(); }
  return changed;
}
bool moveRight() {
  bool changed = false;
  int pts = 0;
  for (int r = 0; r < 4; r++) {
    revArr(board[r]);
    changed |= slide(board[r], pts);
    revArr(board[r]);
  }
  if (pts) { score += pts; if (score > best) saveBest(); }
  return changed;
}
bool moveUp() {
  bool changed = false;
  int pts = 0;
  int a[4];
  for (int c = 0; c < 4; c++) {
    getCol(c, a);
    if (slide(a, pts)) { setCol(c, a); changed = true; }
  }
  if (pts) { score += pts; if (score > best) saveBest(); }
  return changed;
}
bool moveDown() {
  bool changed = false;
  int pts = 0;
  int a[4];
  for (int c = 0; c < 4; c++) {
    getCol(c, a);
    revArr(a);
    if (slide(a, pts)) { revArr(a); setCol(c, a); changed = true; }
  }
  if (pts) { score += pts; if (score > best) saveBest(); }
  return changed;
}

void saveBest() {
  best = score;
  prefs.putInt("best", best);
}

bool doUndo() {
  Serial.printf("UNDO called hist=%d\n", histCount);
  if (histCount == 0) return false;
  histCount--;
  memcpy(board, history[histCount].tiles, sizeof(board));
  score = history[histCount].score;
  gameOver = false;
  Serial.println("UNDO applied");
  return true;
}

void spawnTile() {
  int empty[16];
  int e = 0;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (board[r][c] == 0) empty[e++] = r * 4 + c;
  if (e == 0) return;
  int idx = empty[random(e)];
  board[idx / 4][idx % 4] = (random(10) == 0) ? 4 : 2;
}

bool boardFull() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (board[r][c] == 0) return false;
  return true;
}

bool neighborsEquals() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      int v = board[r][c];
      if (c + 1 < 4 && board[r][c + 1] == v) return true;
      if (r + 1 < 4 && board[r + 1][c] == v) return true;
    }
  return false;
}

void newGame() {
  Serial.println("RESTART");
  memset(board, 0, sizeof(board));
  score = 0;
  gameOver = false;
  histCount = 0;
  spawnTile();
  spawnTile();
  drawStatic();
}

bool doMove(int dir) {
  if (gameOver) return false;
  State snap;
  memcpy(snap.tiles, board, sizeof(board));
  snap.score = score;

  bool moved = false;
  switch (dir) {
    case 0: moved = moveLeft(); break;
    case 1: moved = moveRight(); break;
    case 2: moved = moveUp(); break;
    case 3: moved = moveDown(); break;
  }
  if (!moved) return false;

  if (histCount < HIST_MAX) {
    memcpy(history[histCount].tiles, snap.tiles, sizeof(snap.tiles));
    history[histCount].score = snap.score;
    histCount++;
  } else {
    memmove(history, history + 1, sizeof(State) * (HIST_MAX - 1));
    memcpy(history[HIST_MAX - 1].tiles, snap.tiles, sizeof(snap.tiles));
    history[HIST_MAX - 1].score = snap.score;
  }

  spawnTile();
  if (boardFull() && !neighborsEquals()) gameOver = true;
  drawGame();
  return true;
}

void mapTouch(TS_Point p, int &sx, int &sy) {
  sx = SCREEN_W - map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_W);
  sy = SCREEN_H - map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_H);
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.initDMA();

  ts_spi.begin(25, 39, 32, 33);
  ts.begin(ts_spi);
  ts.setRotation(1);

  prefs.begin("g2048", false);
  best = prefs.getInt("best", 0);

  randomSeed(esp_random());
  newGame();
}

void loop() {
  static bool wasTouched = false;
  static int16_t startX = 0, startY = 0;
  static int16_t maxDx = 0, maxDy = 0;
  static int16_t lastX = 0, lastY = 0;

  while (Serial.available()) {
    char ch = Serial.read();
    switch (ch) {
      case 'd': case 'D': doMove(1); break;
      case 'a': case 'A': doMove(0); break;
      case 's': case 'S': doMove(3); break;
      case 'w': case 'W': doMove(2); break;
      case 'r': case 'R': newGame(); break;
      case 'p': case 'P': if (doUndo()) drawGame(); break;
    }
  }

  bool nowTouched = ts.touched();

  if (nowTouched) {
    TS_Point p = ts.getPoint();
    lastX = p.x;
    lastY = p.y;
    if (!wasTouched) {
      startX = lastX;
      startY = lastY;
      maxDx = 0;
      maxDy = 0;
      Serial.printf("PRESS raw=%d,%d\n", lastX, lastY);
    } else {
      int dx = lastX - startX;
      int dy = lastY - startY;
      if (abs(dx) > abs(maxDx)) maxDx = dx;
      if (abs(dy) > abs(maxDy)) maxDy = dy;
    }
    wasTouched = true;
    delay(3);
  } else if (wasTouched) {
    wasTouched = false;

    int fx = lastX - startX;
    int fy = lastY - startY;
    Serial.printf("RELEASE final=%d,%d max=%d,%d\n", fx, fy, maxDx, maxDy);

    if (abs(fx) >= 150 || abs(fy) >= 150) {
      int dx = maxDx;
      int dy = maxDy;
      if (abs(dx) >= abs(dy))
        (dx > 0) ? doMove(0) : doMove(1);
      else
        (dy > 0) ? doMove(2) : doMove(3);
    } else {
      int sx, sy;
      mapTouch(TS_Point(lastX, lastY, 0), sx, sy);
      Serial.printf("TAP pos=%d,%d\n", sx, sy);
      bool handled = false;
      if (inRect(sx, sy, PANEL_X, BTN_HIT_UNDO_Y, BTN_W, BTN_HIT_H)) {
        Serial.println("HIT UNDO");
        flashButton("UNDO", BTN_UNDO_Y);
        if (doUndo()) drawGame();
        handled = true;
      } else if (inRect(sx, sy, PANEL_X, BTN_HIT_RES_Y, BTN_W, BTN_HIT_H)) {
        Serial.println("HIT RESTART");
        flashButton("RESTART", BTN_RES_Y);
        newGame();
        handled = true;
      }
      if (!handled && gameOver) newGame();
    }
  }
  delay(15);
}