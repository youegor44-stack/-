// CYD Arcade — ESP32-2432S028 (2.8" ILI9341 320x240, XPT2046 touch)
// Только библиотека TFT_eSPI. Управление полностью сенсорное.
// Чтобы добавить игру: namespace с init()/frame() + строка в таблице games[].
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>

#define W 320
#define H 240
#define T_CS 33
#define T_CLK 25
#define T_MOSI 32
#define T_MISO 39
#define T_IRQ 36

TFT_eSPI tft;
TFT_eSprite spr(&tft);
Preferences prefs;

// ---------------- Touch (bit-bang XPT2046) ----------------
int tx = 0, ty = 0;
bool down = false, pressed = false, exitReq = false, inGame = false;
float cal[4];
bool calSwap = false;

static uint16_t rawRead(uint8_t cmd) {
  digitalWrite(T_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(T_MOSI, (cmd >> i) & 1);
    digitalWrite(T_CLK, HIGH); delayMicroseconds(1);
    digitalWrite(T_CLK, LOW);  delayMicroseconds(1);
  }
  uint16_t r = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(T_CLK, HIGH); delayMicroseconds(1);
    r <<= 1;
    if (digitalRead(T_MISO)) r |= 1;
    digitalWrite(T_CLK, LOW);  delayMicroseconds(1);
  }
  digitalWrite(T_CS, HIGH);
  return (r >> 3) & 0xFFF;
}

bool readRaw(int &a, int &b) {
  if (digitalRead(T_IRQ)) return false;
  rawRead(0x90); rawRead(0xD0);  // прогрев АЦП
  long sa = 0, sb = 0;
  for (int i = 0; i < 4; i++) { sa += rawRead(0x90); sb += rawRead(0xD0); }
  a = sa / 4; b = sb / 4;
  return !(a < 100 || a > 4000 || b < 100 || b > 4000);
}

void mapTouch(int a, int b) {
  float rx = calSwap ? b : a, ry = calSwap ? a : b;
  tx = constrain((int)(20 + (rx - cal[0]) * 280.0f / (cal[1] - cal[0])), 0, W - 1);
  ty = constrain((int)(20 + (ry - cal[2]) * 200.0f / (cal[3] - cal[2])), 0, H - 1);
}

void waitTap(int px, int py, int &a, int &b) {
  tft.fillScreen(TFT_BLACK);
  tft.drawLine(px - 12, py, px + 12, py, TFT_WHITE);
  tft.drawLine(px, py - 12, px, py + 12, TFT_WHITE);
  tft.setTextColor(TFT_YELLOW); tft.setTextDatum(MC_DATUM);
  tft.drawString("Calibration: tap the cross", 160, 120, 2);
  int ta, tb;
  while (readRaw(ta, tb)) delay(10);
  while (!readRaw(a, b)) delay(10);
  delay(80); readRaw(a, b);
  while (readRaw(ta, tb)) delay(10);
  delay(150);
}

void calibrate() {
  int a1, b1, a2, b2, a3, b3;
  waitTap(20, 20, a1, b1);
  waitTap(300, 20, a2, b2);
  waitTap(300, 220, a3, b3);
  calSwap = abs(b2 - b1) > abs(a2 - a1);
  cal[0] = calSwap ? b1 : a1;
  cal[1] = calSwap ? b2 : a2;
  cal[2] = calSwap ? a2 : b2;
  cal[3] = calSwap ? a3 : b3;
  prefs.begin("cyd", false);
  prefs.putBytes("cal", cal, sizeof(cal));
  prefs.putBool("swap", calSwap);
  prefs.end();
}

bool loadCal() {
  bool ok = false;
  prefs.begin("cyd", false);
  if (prefs.getBytesLength("cal") == sizeof(cal)) {
    prefs.getBytes("cal", cal, sizeof(cal));
    calSwap = prefs.getBool("swap", false);
    ok = true;
  }
  prefs.end();
  return ok;
}

void updateTouch() {
  static bool prev = false;
  static uint32_t lastT = 0;
  int a, b;
  bool t = readRaw(a, b);
  if (t) { mapTouch(a, b); lastT = millis(); }
  bool d = t || (prev && millis() - lastT < 50);
  pressed = d && !prev;
  down = d;
  prev = d;
  exitReq = false;
  if (inGame && d && tx >= 292 && ty < 28) {  // кнопка выхода (правый верхний угол)
    if (pressed) exitReq = true;
    down = false; pressed = false;
  }
}

// ---------------- RGB LED (active LOW, PWM) ----------------
#define LED_R 4
#define LED_G 16
#define LED_B 17
const char *ledNames[] = {"Off", "Red", "Green", "Blue", "Yellow", "Cyan", "Pink", "White", "Rainbow"};
const uint8_t ledRGB[8][3] = {{0,0,0},{255,0,0},{0,255,0},{0,0,255},{255,160,0},{0,255,255},{255,0,255},{255,255,255}};
uint8_t ledMode = 0;

void ledWrite(uint8_t r, uint8_t g, uint8_t b) {  // яркость ~40%, светодиод очень яркий
  ledcWrite(1, 255 - r * 2 / 5); ledcWrite(2, 255 - g * 2 / 5); ledcWrite(3, 255 - b * 2 / 5);
}
void ledSetup() {
  const int pins[3] = {LED_R, LED_G, LED_B};
  for (int i = 0; i < 3; i++) { ledcSetup(i + 1, 5000, 8); ledcAttachPin(pins[i], i + 1); }
  prefs.begin("cyd", false); ledMode = prefs.getUChar("led", 0); prefs.end();
  if (ledMode > 8) ledMode = 0;
}
void ledNext() {
  ledMode = (ledMode + 1) % 9;
  prefs.begin("cyd", false); prefs.putUChar("led", ledMode); prefs.end();
}
void ledUpdate() {
  if (ledMode < 8) { ledWrite(ledRGB[ledMode][0], ledRGB[ledMode][1], ledRGB[ledMode][2]); return; }
  float h = (millis() % 4000) / 4000.0f * 6.0f;
  int i = (int)h; float f = h - i;
  uint8_t up = 255 * f, dn = 255 - up;
  switch (i % 6) {
    case 0: ledWrite(255, up, 0); break;
    case 1: ledWrite(dn, 255, 0); break;
    case 2: ledWrite(0, 255, up); break;
    case 3: ledWrite(0, dn, 255); break;
    case 4: ledWrite(up, 0, 255); break;
    default: ledWrite(255, 0, dn); break;
  }
}

// ---------------- Helpers ----------------
bool over = false;
int score = 0;
uint32_t deadAt = 0;
const char *overMsg = "GAME OVER";

void txt(const String &s, int x, int y, uint16_t c, uint8_t f = 2, uint8_t d = TL_DATUM) {
  spr.setTextDatum(d); spr.setTextColor(c); spr.drawString(s, x, y, f);
}
uint16_t hue(int i) {
  return tft.color565(128 + 127 * sin(i * 0.35f), 128 + 127 * sin(i * 0.35f + 2.1f), 128 + 127 * sin(i * 0.35f + 4.2f));
}
float rf(float a, float b) { return a + (b - a) * random(1000) / 1000.0f; }
void hud() { txt("Score " + String(score), 4, 2, TFT_WHITE); }
void die() { if (!over) { over = true; deadAt = millis(); } }
void overlay(void (*initf)()) {
  if (!over) return;
  spr.fillRect(60, 80, 200, 80, TFT_BLACK);
  spr.drawRect(60, 80, 200, 80, TFT_WHITE);
  txt(overMsg, 160, 100, TFT_RED, 4, MC_DATUM);
  txt("Score: " + String(score), 160, 128, TFT_WHITE, 2, MC_DATUM);
  txt("tap to restart", 160, 146, TFT_LIGHTGREY, 2, MC_DATUM);
  if (pressed && millis() - deadAt > 600) initf();
}

// =============== 2D GAMES ===============

namespace Snake {
int sx[300], sy[300], len, dx, dy, fx, fy;
uint32_t t;
void init() {
  len = 3; for (int i = 0; i < 3; i++) { sx[i] = 10 - i; sy[i] = 7; }
  dx = 1; dy = 0; fx = random(20); fy = random(15);
  score = 0; over = false; t = millis();
}
void frame() {
  if (!over) {
    if (down) {
      int ax = tx - (sx[0] * 16 + 8), ay = ty - (sy[0] * 16 + 8), ndx = 0, ndy = 0;
      if (abs(ax) > abs(ay)) ndx = ax > 0 ? 1 : -1; else ndy = ay > 0 ? 1 : -1;
      if (!(ndx == -dx && ndy == -dy)) { dx = ndx; dy = ndy; }
    }
    if (millis() - t > (uint32_t)max(70, 140 - score * 3)) {
      t = millis();
      int nx = sx[0] + dx, ny = sy[0] + dy;
      if (nx < 0 || ny < 0 || nx >= 20 || ny >= 15) die();
      else {
        for (int i = 0; i < len - 1; i++) if (sx[i] == nx && sy[i] == ny) die();
        if (!over) {
          bool eat = (nx == fx && ny == fy);
          if (eat && len < 299) len++;
          for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
          sx[0] = nx; sy[0] = ny;
          if (eat) { score++; fx = random(20); fy = random(15); }
        }
      }
    }
  }
  spr.fillScreen(TFT_BLACK);
  spr.fillCircle(fx * 16 + 8, fy * 16 + 8, 6, TFT_RED);
  for (int i = len - 1; i >= 0; i--) spr.fillRoundRect(sx[i] * 16 + 1, sy[i] * 16 + 1, 14, 14, 4, i == 0 ? TFT_GREENYELLOW : TFT_GREEN);
  hud(); overlay(init);
}
}  // namespace Snake

namespace Pong {
float bx, by, vx, vy, py, ay;
void serve() { bx = 160; by = 120; vx = -4; vy = random(2) ? 2.5f : -2.5f; }
void init() { py = ay = 120; serve(); score = 0; over = false; }
void frame() {
  if (!over) {
    if (down) py += (ty - py) * 0.5f;
    py = constrain(py, 25.0f, 215.0f);
    ay += constrain(by - ay, -3.4f, 3.4f); ay = constrain(ay, 25.0f, 215.0f);
    bx += vx; by += vy;
    if (by < 4) { by = 4; vy = -vy; }
    if (by > H - 4) { by = H - 4; vy = -vy; }
    if (vx < 0 && bx < 24 && bx > 6 && fabsf(by - py) < 30) { vx = -vx * 1.04f; vy += (by - py) * 0.12f; bx = 24; score++; }
    if (vx > 0 && bx > 296 && bx < 314 && fabsf(by - ay) < 30) { vx = -vx; vy += (by - ay) * 0.1f; bx = 296; }
    vx = constrain(vx, -9.0f, 9.0f); vy = constrain(vy, -6.0f, 6.0f);
    if (bx < 0) die();
    if (bx > W) { score += 5; serve(); }
  }
  spr.fillScreen(TFT_BLACK);
  for (int y = 0; y < H; y += 16) spr.drawFastVLine(160, y, 8, TFT_DARKGREY);
  spr.fillRect(10, (int)py - 30, 8, 60, TFT_WHITE);
  spr.fillRect(302, (int)ay - 30, 8, 60, TFT_LIGHTGREY);
  spr.fillCircle((int)bx, (int)by, 5, TFT_YELLOW);
  hud(); overlay(init);
}
}  // namespace Pong

namespace Breakout {
float bx, by, vx, vy, px; bool br[5][10]; int left;
void fill() { for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) br[r][c] = true; left = 50; }
void ball() { bx = 160; by = 200; vx = 2.5f; vy = -3.2f; }
void init() { fill(); ball(); px = 160; score = 0; over = false; }
void frame() {
  if (!over) {
    if (down) px += (tx - px) * 0.4f;
    px = constrain(px, 25.0f, 295.0f);
    bx += vx; by += vy;
    if (bx < 3) { bx = 3; vx = -vx; }
    if (bx > W - 3) { bx = W - 3; vx = -vx; }
    if (by < 3) { by = 3; vy = -vy; }
    if (vy > 0 && by >= 218 && by <= 230 && fabsf(bx - px) < 30) {
      vy = -fabsf(vy); vx = (bx - px) * 0.12f;
      if (fabsf(vx) < 0.8f) vx = vx < 0 ? -0.8f : 0.8f;
    }
    if (by >= 30 && by < 90 && bx >= 10 && bx < 310) {
      int c = (int)(bx - 10) / 30, r = (int)(by - 30) / 12;
      if (br[r][c]) {
        br[r][c] = false; vy = -vy; score++; left--;
        if (!left) { fill(); ball(); vy *= 1.1f; }
      }
    }
    if (by > H) die();
  }
  spr.fillScreen(TFT_BLACK);
  for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++)
    if (br[r][c]) spr.fillRect(11 + c * 30, 31 + r * 12, 28, 10, hue(r * 3));
  spr.fillRoundRect((int)px - 30, 225, 60, 8, 3, TFT_WHITE);
  spr.fillCircle((int)bx, (int)by, 4, TFT_YELLOW);
  hud(); overlay(init);
}
}  // namespace Breakout

namespace Flappy {
float by, vy; int pxs[3], gap[3]; bool started;
void init() {
  by = 120; vy = 0; started = false; score = 0; over = false;
  for (int i = 0; i < 3; i++) { pxs[i] = 320 + i * 130; gap[i] = 60 + random(120); }
}
void frame() {
  if (!over) {
    if (!started) { by = 120 + sinf(millis() / 200.0f) * 5; if (pressed) { started = true; vy = -5.2f; } }
    else {
      if (pressed) vy = -5.2f;
      vy += 0.38f; by += vy;
      for (int i = 0; i < 3; i++) {
        pxs[i] -= 3;
        if (pxs[i] <= 19 && pxs[i] > 16) score++;
        if (pxs[i] < -40) { pxs[i] += 390; gap[i] = 60 + random(120); }
        if (68 > pxs[i] && 52 < pxs[i] + 40 && (by - 8 < gap[i] - 40 || by + 8 > gap[i] + 40)) die();
      }
      if (by < 8 || by > H - 8) die();
    }
  }
  spr.fillScreen(tft.color565(110, 200, 255));
  for (int i = 0; i < 3; i++) {
    spr.fillRect(pxs[i], 0, 40, gap[i] - 40, TFT_DARKGREEN);
    spr.fillRect(pxs[i], gap[i] + 40, 40, H, TFT_DARKGREEN);
    spr.drawRect(pxs[i], -1, 40, gap[i] - 39, TFT_BLACK);
    spr.drawRect(pxs[i], gap[i] + 40, 40, H, TFT_BLACK);
  }
  spr.fillCircle(60, (int)by, 8, TFT_YELLOW);
  spr.fillCircle(63, (int)by - 2, 2, TFT_BLACK);
  hud();
  if (!started) txt("tap to flap", 160, 190, TFT_BLACK, 2, MC_DATUM);
  overlay(init);
}
}  // namespace Flappy

namespace Shooter {
struct E { float x, y, v; bool a; } e[8];
struct B { float x, y; bool a; } b[12];
float sx; uint32_t lastShot, lastSpawn;
void init() {
  sx = 160; for (auto &k : e) k.a = false; for (auto &k : b) k.a = false;
  lastShot = lastSpawn = millis(); score = 0; over = false;
}
void frame() {
  if (!over) {
    if (down) sx += (tx - sx) * 0.35f;
    sx = constrain(sx, 12.0f, 308.0f);
    if (millis() - lastShot > 220) {
      lastShot = millis();
      for (auto &k : b) if (!k.a) { k.a = true; k.x = sx; k.y = 200; break; }
    }
    if (millis() - lastSpawn > (uint32_t)max(350, 900 - score * 10)) {
      lastSpawn = millis();
      for (auto &k : e) if (!k.a) { k.a = true; k.x = 10 + random(300); k.y = -10; k.v = rf(1.2f, 2.7f) + score * 0.02f; break; }
    }
    for (auto &k : b) if (k.a) { k.y -= 7; if (k.y < 0) k.a = false; }
    for (auto &k : e) if (k.a) {
      k.y += k.v;
      if (k.y > H) die();
      if (fabsf(k.x - sx) < 14 && k.y > 195) die();
      for (auto &q : b) if (q.a && fabsf(q.x - k.x) < 12 && fabsf(q.y - k.y) < 10) { q.a = false; k.a = false; score++; break; }
    }
  }
  spr.fillScreen(TFT_BLACK);
  for (auto &k : e) if (k.a) { spr.fillRect((int)k.x - 8, (int)k.y - 8, 16, 16, TFT_RED); spr.fillRect((int)k.x - 4, (int)k.y - 2, 8, 4, TFT_BLACK); }
  for (auto &k : b) if (k.a) spr.fillRect((int)k.x - 1, (int)k.y - 4, 3, 8, TFT_YELLOW);
  spr.fillTriangle((int)sx, 200, (int)sx - 12, 222, (int)sx + 12, 222, TFT_CYAN);
  hud(); overlay(init);
}
}  // namespace Shooter

namespace Dots {
struct D { float x, y; uint32_t born, life; bool a; } d[5];
int lives; uint32_t ts; const int R = 24;
void init() { for (auto &k : d) k.a = false; lives = 3; score = 0; over = false; ts = millis(); }
void frame() {
  if (!over) {
    if (millis() - ts > (uint32_t)max(350, 800 - score * 12)) {
      ts = millis();
      for (auto &k : d) if (!k.a) { k.a = true; k.x = 30 + random(260); k.y = 40 + random(180); k.born = millis(); k.life = max(800, 2000 - score * 25); break; }
    }
    if (pressed) for (auto &k : d) if (k.a && hypotf(tx - k.x, ty - k.y) < R + 8) { k.a = false; score++; break; }
    for (auto &k : d) if (k.a && millis() - k.born > k.life) { k.a = false; if (--lives <= 0) die(); }
  }
  spr.fillScreen(TFT_BLACK);
  for (auto &k : d) if (k.a) {
    float f = 1.0f - (float)(millis() - k.born) / k.life; if (f < 0) f = 0;
    spr.drawCircle((int)k.x, (int)k.y, R, TFT_WHITE);
    spr.fillCircle((int)k.x, (int)k.y, (int)(R * f) + 1, hue((int)(k.born / 100)));
  }
  hud(); txt("Lives " + String(lives), 120, 2, TFT_PINK);
  overlay(init);
}
}  // namespace Dots

namespace Stack {
int lx[400], lw[400], n, bw; float bx, v;
void init() { n = 1; lx[0] = 110; lw[0] = 100; bx = 0; bw = 100; v = 3; score = 0; over = false; }
void frame() {
  if (!over) {
    bx += v;
    if (bx < 0) { bx = 0; v = -v; }
    if (bx + bw > W) { bx = W - bw; v = -v; }
    if (pressed) {
      int l = max((int)bx, lx[n - 1]), r = min((int)bx + bw, lx[n - 1] + lw[n - 1]);
      if (r - l <= 2 || n >= 399) die();
      else { lx[n] = l; lw[n] = r - l; bw = r - l; n++; score++; bx = 0; v = fabsf(v) + 0.12f; }
    }
  }
  spr.fillScreen(TFT_BLACK);
  for (int i = n - 1; i >= 0; i--) { int y = 100 + (n - i) * 12; if (y >= H) break; spr.fillRect(lx[i], y, lw[i], 11, hue(i)); }
  if (!over) spr.fillRect((int)bx, 100, bw, 11, hue(n));
  hud(); overlay(init);
}
}  // namespace Stack

namespace Runner {
struct O { float x; int w, h; } o[3];
float py, vy, spd;
void respawn(O &k) {
  float mx = 320; for (auto &q : o) if (q.x > mx) mx = q.x;
  k.x = mx + 300 + random(200); k.w = 14 + random(16); k.h = 18 + random(24);
}
void init() {
  py = 200; vy = 0; spd = 5; score = 0; over = false;
  for (int i = 0; i < 3; i++) { o[i].x = 0; }
  for (int i = 0; i < 3; i++) respawn(o[i]);
}
void frame() {
  if (!over) {
    if (pressed && py >= 200) vy = -12;
    vy += 0.7f; py += vy; if (py > 200) { py = 200; vy = 0; }
    for (auto &k : o) {
      k.x -= spd;
      if (k.x + k.w < 0) { respawn(k); score++; spd = fminf(5 + score * 0.1f, 8.5f); }
      if (k.x < 70 && k.x + k.w > 50 && py > 200 - k.h) die();
    }
  }
  spr.fillScreen(tft.color565(240, 240, 240));
  spr.drawFastHLine(0, 201, W, TFT_BLACK);
  for (auto &k : o) spr.fillRect((int)k.x, 200 - k.h, k.w, k.h, TFT_DARKGREEN);
  spr.fillRect(50, (int)py - 26, 20, 26, TFT_BLACK);
  spr.fillRect(62, (int)py - 22, 4, 4, TFT_WHITE);
  spr.setTextColor(TFT_BLACK); txt("Score " + String(score), 4, 2, TFT_BLACK);
  overlay(init);
}
}  // namespace Runner

// =============== 3D GAMES ===============

namespace Tunnel {  // полёт по 3D-туннелю, уворачиваемся от кубов
struct Ob { float x, y, z; } ob[5];
float cx, cy, dist, spd; const float F = 140;
void spawn(Ob &k, float z) { k.x = rf(-0.8f, 0.8f); k.y = rf(-0.8f, 0.8f); k.z = z; }
void init() {
  cx = cy = 0; dist = 0; spd = 0.12f; score = 0; over = false;
  for (int i = 0; i < 5; i++) spawn(ob[i], 5 + i * 2.5f);
}
void frame() {
  if (!over) {
    if (down) {
      cx += ((tx - 160) / 160.0f * 0.85f - cx) * 0.25f;
      cy += ((ty - 120) / 120.0f * 0.85f - cy) * 0.25f;
    }
    dist += spd; spd = fminf(spd + 0.00005f, 0.35f);
    for (auto &k : ob) {
      k.z -= spd;
      if (k.z < 0.4f) {
        if (fabsf(k.x - cx) < 0.55f && fabsf(k.y - cy) < 0.55f) die();
        else { score++; spawn(k, 12 + random(4)); }
      }
    }
  }
  spr.fillScreen(TFT_BLACK);
  float off = fmodf(dist, 0.9f);
  for (int k = 14; k >= 1; k--) {
    float z = k * 0.9f - off; if (z < 0.3f) continue;
    float s = F / z;
    int l = 160 + (int)((-1 - cx) * s), r = 160 + (int)((1 - cx) * s);
    int t = 120 + (int)((-1 - cy) * s), b = 120 + (int)((1 - cy) * s);
    int br = constrain((int)(255 - z * 17), 30, 255);
    spr.drawRect(l, t, r - l, b - t, tft.color565(0, br / 2, br));
  }
  // грубая сортировка: дальние рисуем первыми
  int idx[5] = {0, 1, 2, 3, 4};
  for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) if (ob[idx[j]].z > ob[idx[i]].z) { int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp; }
  for (int i = 0; i < 5; i++) {
    Ob &k = ob[idx[i]]; float s = F / k.z;
    int x0 = 160 + (int)((k.x - 0.3f - cx) * s), y0 = 120 + (int)((k.y - 0.3f - cy) * s), sz = (int)(0.6f * s);
    int br = constrain((int)(255 - k.z * 14), 60, 255);
    spr.fillRect(x0, y0, sz, sz, tft.color565(br, br / 4, br / 5));
    spr.drawRect(x0, y0, sz, sz, TFT_WHITE);
  }
  spr.drawCircle(160, 120, 6, TFT_GREEN); spr.drawFastHLine(150, 120, 20, TFT_GREEN); spr.drawFastVLine(160, 110, 20, TFT_GREEN);
  hud(); overlay(init);
}
}  // namespace Tunnel

namespace Shapes {  // 3D-фигуры: тяните пальцем, внизу выбор фигуры
const float CV[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
const uint8_t CE[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
const float PV[5][3] = {{-1,1,-1},{1,1,-1},{1,1,1},{-1,1,1},{0,-1.2f,0}};
const uint8_t PE[8][2] = {{0,1},{1,2},{2,3},{3,0},{0,4},{1,4},{2,4},{3,4}};
const float OV[6][3] = {{1.4f,0,0},{-1.4f,0,0},{0,1.4f,0},{0,-1.4f,0},{0,0,1.4f},{0,0,-1.4f}};
const uint8_t OE[12][2] = {{0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{2,5},{3,4},{3,5}};
float ax, ay; int obj, lx, ly; bool drag;
void init() { ax = 0.4f; ay = 0.6f; obj = 0; drag = false; over = false; }
void drawObj(const float v[][3], int nv, const uint8_t e[][2], int ne) {
  float px[8], py[8], cX = cosf(ax), sX = sinf(ax), cY = cosf(ay), sY = sinf(ay);
  for (int i = 0; i < nv; i++) {
    float x = v[i][0], y = v[i][1], z = v[i][2];
    float x1 = x * cY + z * sY, z1 = -x * sY + z * cY;
    float y1 = y * cX - z1 * sX, z2 = y * sX + z1 * cX;
    float s = 240.0f / (z2 + 5.0f);
    px[i] = 160 + x1 * s; py[i] = 105 + y1 * s;
  }
  for (int i = 0; i < ne; i++) {
    spr.drawLine((int)px[e[i][0]], (int)py[e[i][0]], (int)px[e[i][1]], (int)py[e[i][1]], hue(i));
    spr.drawLine((int)px[e[i][0]] + 1, (int)py[e[i][0]], (int)px[e[i][1]] + 1, (int)py[e[i][1]], hue(i));
  }
}
void frame() {
  if (pressed) { if (ty > 205) { obj = tx * 3 / W; drag = false; } else { drag = true; lx = tx; ly = ty; } }
  else if (down && drag) { ay += (tx - lx) * 0.02f; ax += (ty - ly) * 0.02f; lx = tx; ly = ty; }
  else if (!down) { ay += 0.02f; ax += 0.013f; }
  spr.fillScreen(TFT_BLACK);
  if (obj == 0) drawObj(CV, 8, CE, 12);
  else if (obj == 1) drawObj(PV, 5, PE, 8);
  else drawObj(OV, 6, OE, 12);
  const char *nm[3] = {"Cube", "Pyramid", "Octa"};
  for (int i = 0; i < 3; i++) {
    spr.drawRoundRect(i * 107 + 3, 210, 101, 28, 6, i == obj ? TFT_GREEN : TFT_DARKGREY);
    txt(nm[i], i * 107 + 53, 224, i == obj ? TFT_GREEN : TFT_LIGHTGREY, 2, MC_DATUM);
  }
  txt("drag to rotate", 4, 2, TFT_LIGHTGREY);
}
}  // namespace Shapes

namespace Maze {  // 3D-лабиринт (raycasting), каждый раз новый
uint8_t mz[15][15]; float px, py, pa; int level; uint32_t t0;
void carve(int x, int y) {
  mz[y][x] = 0;
  int d[4] = {0, 1, 2, 3};
  for (int i = 3; i > 0; i--) { int j = random(i + 1), s = d[i]; d[i] = d[j]; d[j] = s; }
  static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  for (int k = 0; k < 4; k++) {
    int nx = x + DX[d[k]] * 2, ny = y + DY[d[k]] * 2;
    if (nx > 0 && nx < 14 && ny > 0 && ny < 14 && mz[ny][nx]) { mz[y + DY[d[k]]][x + DX[d[k]]] = 0; carve(nx, ny); }
  }
}
void gen() { memset(mz, 1, sizeof(mz)); carve(1, 1); px = 1.5f; py = 1.5f; pa = 0.5f; t0 = millis(); }
void init() { level = 1; score = 0; over = false; gen(); }
bool blk(float x, float y) {
  for (int i = 0; i < 4; i++) {
    float cx = x + ((i & 1) ? 0.15f : -0.15f), cy = y + ((i & 2) ? 0.15f : -0.15f);
    if (mz[(int)cy][(int)cx]) return true;
  }
  return false;
}
void frame() {
  if (down) {
    if (tx < 100) pa -= 0.09f;
    else if (tx > 220) pa += 0.09f;
    else {
      float m = (ty < 120) ? 0.08f : -0.08f;
      float nx = px + cosf(pa) * m, ny = py + sinf(pa) * m;
      if (!blk(nx, py)) px = nx;
      if (!blk(px, ny)) py = ny;
    }
  }
  if ((int)px == 13 && (int)py == 13) { level++; score = level - 1; gen(); }
  spr.fillRect(0, 0, W, 120, tft.color565(30, 30, 60));
  spr.fillRect(0, 120, W, 120, tft.color565(50, 50, 50));
  for (int col = 0; col < 160; col++) {
    float ra = pa + (col - 80) / 160.0f * 1.1f;
    float rdx = cosf(ra), rdy = sinf(ra);
    if (fabsf(rdx) < 1e-5f) rdx = 1e-5f;
    if (fabsf(rdy) < 1e-5f) rdy = 1e-5f;
    int mx = (int)px, my = (int)py, sx, sy, side = 0;
    float ddx = fabsf(1 / rdx), ddy = fabsf(1 / rdy), sdx, sdy;
    if (rdx < 0) { sx = -1; sdx = (px - mx) * ddx; } else { sx = 1; sdx = (mx + 1 - px) * ddx; }
    if (rdy < 0) { sy = -1; sdy = (py - my) * ddy; } else { sy = 1; sdy = (my + 1 - py) * ddy; }
    for (int i = 0; i < 30; i++) {
      if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; } else { sdy += ddy; my += sy; side = 1; }
      if (mx < 0 || my < 0 || mx > 14 || my > 14 || mz[my][mx]) break;
    }
    float dd = (side == 0 ? sdx - ddx : sdy - ddy) * cosf(ra - pa);
    if (dd < 0.1f) dd = 0.1f;
    int h = (int)(H / dd); if (h > H) h = H;
    int sh = (int)(255 / (1 + dd * 0.6f)); if (side) sh = sh * 2 / 3;
    spr.fillRect(col * 2, (H - h) / 2, 2, h, tft.color565(sh, sh * 7 / 10, sh * 4 / 10));
  }
  // мини-карта
  for (int y = 0; y < 15; y++) for (int x = 0; x < 15; x++) spr.fillRect(x * 3, y * 3, 3, 3, mz[y][x] ? TFT_DARKGREY : TFT_BLACK);
  spr.fillRect(39, 39, 3, 3, TFT_GREEN);
  spr.fillRect((int)(px * 3) - 1, (int)(py * 3) - 1, 3, 3, TFT_RED);
  txt("Level " + String(level) + "  " + String((millis() - t0) / 1000) + "s", 52, 2, TFT_WHITE);
  txt("<", 10, 225, TFT_LIGHTGREY, 4); txt(">", 300, 225, TFT_LIGHTGREY, 4, TR_DATUM);
  txt("go ^", 160, 100, TFT_LIGHTGREY, 2, MC_DATUM);
}
}  // namespace Maze

// =============== MENU ===============
struct Game { const char *name; uint16_t col; void (*init)(); void (*frame)(); };
Game games[] = {
  {"Snake", TFT_DARKGREEN, Snake::init, Snake::frame},
  {"Pong", TFT_NAVY, Pong::init, Pong::frame},
  {"Breakout", TFT_MAROON, Breakout::init, Breakout::frame},
  {"Flappy", TFT_DARKCYAN, Flappy::init, Flappy::frame},
  {"Shooter", TFT_PURPLE, Shooter::init, Shooter::frame},
  {"Tap Dots", TFT_OLIVE, Dots::init, Dots::frame},
  {"Stack", TFT_BROWN, Stack::init, Stack::frame},
  {"Runner", 0x4208, Runner::init, Runner::frame},
  {"3D Tunnel", TFT_BLUE, Tunnel::init, Tunnel::frame},
  {"3D Shapes", 0x780F, Shapes::init, Shapes::frame},
  {"3D Maze", 0x8200, Maze::init, Maze::frame},
};
const int NG = sizeof(games) / sizeof(games[0]);
const int PER = 9;
int page = 0, cur = 0;

void menuFrame() {
  spr.fillScreen(TFT_BLACK);
  txt("CYD Arcade", 160, 2, TFT_WHITE, 4, TC_DATUM);
  for (int i = 0; i < PER; i++) {
    int idx = page * PER + i; if (idx >= NG) break;
    int x = 4 + (i % 3) * 106, y = 30 + (i / 3) * 60;
    spr.fillRoundRect(x, y, 102, 56, 8, games[idx].col);
    txt(games[idx].name, x + 51, y + 28, TFT_WHITE, 2, MC_DATUM);
  }
  if (page > 0) txt("<", 30, 214, TFT_WHITE, 4, TC_DATUM);
  if ((page + 1) * PER < NG) txt(">", 290, 214, TFT_WHITE, 4, TC_DATUM);
  {
    uint16_t lc = ledMode == 0 ? TFT_DARKGREY : (ledMode == 8 ? hue(millis() / 100) : tft.color565(ledRGB[ledMode][0], ledRGB[ledMode][1], ledRGB[ledMode][2]));
    spr.fillCircle(78, 226, 5, lc);
    txt(String("LED:") + ledNames[ledMode], 88, 226, TFT_LIGHTGREY, 2, ML_DATUM);
  }
  txt("calibrate", 205, 226, TFT_DARKGREY, 2, MC_DATUM);
  spr.pushSprite(0, 0);
  if (!pressed) return;
  if (ty >= 30 && ty < 210) {
    int idx = page * PER + ((ty - 30) / 60) * 3 + constrain((tx - 4) / 106, 0, 2);
    if (idx < NG) { cur = idx; inGame = true; games[cur].init(); }
  } else if (ty >= 212) {
    if (tx < 70) { if (page > 0) page--; }
    else if (tx > 250) { if ((page + 1) * PER < NG) page++; }
    else if (tx < 168) ledNext();
    else calibrate();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(T_CS, OUTPUT); digitalWrite(T_CS, HIGH);
  pinMode(T_CLK, OUTPUT); pinMode(T_MOSI, OUTPUT);
  pinMode(T_MISO, INPUT); pinMode(T_IRQ, INPUT);
  ledSetup();
  tft.init();
  tft.setRotation(1);  // если картинка перевёрнута — поставьте 3 и пройдите калибровку заново
  tft.fillScreen(TFT_BLACK);
  if (!loadCal()) calibrate();
  spr.setColorDepth(16);
  if (!spr.createSprite(W, H)) { tft.setTextColor(TFT_RED); tft.drawString("No memory for sprite", 10, 10, 2); while (1) delay(1000); }
  randomSeed(esp_random());
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < 30) return;
  last = millis();
  ledUpdate();
  updateTouch();
  if (inGame) {
    if (exitReq) { inGame = false; return; }
    games[cur].frame();
    spr.drawRect(296, 2, 22, 22, TFT_DARKGREY);
    spr.drawLine(302, 8, 312, 18, TFT_WHITE); spr.drawLine(312, 8, 302, 18, TFT_WHITE);
    spr.pushSprite(0, 0);
  } else menuFrame();
}
