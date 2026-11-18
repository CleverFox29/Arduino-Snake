#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <queue>
#include <vector>
#include <utility>
#include <cstdint>

// ============================================================
// CONFIGURATION
// ============================================================

hd44780_I2Cexp lcd;

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 16

#define LCD_COLS 16
#define LCD_ROWS 2

#define CELL_WIDTH 5
#define CELL_HEIGHT 8

#define CELL_COUNT 32

#define BATCH_SIZE 8
#define BATCH_COUNT 4

#define BATCH_INTERVAL 120

// ============================================================
// FRAMEBUFFER
// ============================================================

uint8_t framebuffer[SCREEN_HEIGHT][SCREEN_WIDTH];

// ============================================================
// RENDERER STATE
// ============================================================

unsigned long previousBatchTime = 0;
uint8_t currentBatch = 0;

// ============================================================
// PIXEL OPERATIONS
// ============================================================

void clearPixels() {
  memset(framebuffer, 0, sizeof(framebuffer));
}

void setPixel(int x, int y, bool state = true) {

  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return;
  }

  framebuffer[y][x] = state ? 1 : 0;
}

bool getPixel(int x, int y) {

  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return false;
  }

  return framebuffer[y][x] != 0;
}

// ============================================================
// DRAWING PRIMITIVES
// ============================================================

void drawLine(int x0, int y0, int x1, int y1) {

  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);

  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;

  int err = dx - dy;

  while (true) {

    setPixel(x0, y0);

    if (x0 == x1 && y0 == y1)
      break;

    int e2 = 2 * err;

    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }

    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void drawRect(int x, int y, int w, int h) {

  if (w <= 0 || h <= 0) return;

  drawLine(x, y, x + w - 1, y);
  drawLine(x, y + h - 1, x + w - 1, y + h - 1);

  drawLine(x, y, x, y + h - 1);
  drawLine(x + w - 1, y, x + w - 1, y + h - 1);
}

void fillRect(int x, int y, int w, int h) {

  if (w <= 0 || h <= 0) return;

  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      setPixel(px, py);
    }
  }
}

void drawCircle(int cx, int cy, int radius) {

  int x = radius;
  int y = 0;
  int err = 1 - radius;

  while (x >= y) {

    setPixel(cx + x, cy + y);
    setPixel(cx + y, cy + x);
    setPixel(cx - y, cy + x);
    setPixel(cx - x, cy + y);

    setPixel(cx - x, cy - y);
    setPixel(cx - y, cy - x);
    setPixel(cx + y, cy - x);
    setPixel(cx + x, cy - y);

    y++;

    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

// ============================================================
// BITMAP DRAWING
// ============================================================

void drawBitmap5x8(int x, int y, const uint8_t* bitmap) {

  for (int row = 0; row < 8; row++) {

    for (int col = 0; col < 5; col++) {

      bool pixel = bitmap[row] & (1 << (4 - col));

      setPixel(x + col, y + row, pixel);
    }
  }
}

// ============================================================
// PATTERN EXTRACTION
// ============================================================

void extractPattern(int cell, uint8_t bitmap[CELL_HEIGHT]) {
  int cellX = cell % LCD_COLS;
  int cellY = cell / LCD_COLS;
  for (int row = 0; row < CELL_HEIGHT; ++row) {
    uint8_t rowBits = 0;
    for (int col = 0; col < CELL_WIDTH; ++col) {
      if (getPixel(cellX * CELL_WIDTH + col,
                   cellY * CELL_HEIGHT + row)) {
        rowBits |= (uint8_t)(1U << (CELL_WIDTH - 1 - col));
      }
    }
    bitmap[row] = rowBits;
  }
}

// ============================================================
// EXACT BATCH RENDERER
// ============================================================

void renderBatch(int batch) {

  // Clear DDRAM before changing CGRAM so earlier custom glyph references
  // cannot turn into visible stripes when their shared slots are redefined.
  lcd.clear();
  for (int slot = 0; slot < BATCH_SIZE; slot++) {
    uint8_t bitmap[CELL_HEIGHT];
    extractPattern(batch * BATCH_SIZE + slot, bitmap);
    lcd.createChar(slot, bitmap);
  }

  // Every 8-cell batch is one contiguous half-row on the 16x2 LCD.
  int firstCell = batch * BATCH_SIZE;
  lcd.setCursor(firstCell % LCD_COLS, firstCell / LCD_COLS);
  for (uint8_t slot = 0; slot < BATCH_SIZE; ++slot)
    lcd.write(slot);
}

// ============================================================
// RENDER COMPLETE FRAME
// ============================================================

// ============================================================
// LCD INITIALIZATION
// ============================================================




void initLCD() {

  int status = lcd.begin(LCD_COLS, LCD_ROWS);

  if (status != 0) {

    Serial.println("LCD initialization failed!");

    while (true) {
      delay(100);
    }
  }

  lcd.backlight();
  lcd.clear();
}


//**************************************************************
//Game code
using coord = std::pair<uint8_t, uint8_t>;

unsigned long last_update = 0;
int dt = 240;

coord food = { 16, 8 };
int dir = 2;
coord directions[] = { { 1, 0 }, { 1, 2 }, { 0, 1 }, { 2, 1 } };
// UP DOWN LEFT RIGHT
int game_controls[] = { 2, 3, 4, 5 };
int control_reverse[] = { 1, 0, 3, 2 };

std::queue<coord> snake;

int16_t indices[SCREEN_WIDTH][SCREEN_HEIGHT];
std::vector<coord> food_space;
int proposed_dir = 2;

int score = 0;

void gameSetup() {
  score = 0;
  food_space.reserve(SCREEN_WIDTH * SCREEN_HEIGHT);

  food_space.clear();
  while (!snake.empty()) {
    snake.pop();
  }
  memset(indices, -1, sizeof(indices));
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
      indices[x][y] = food_space.size();
      food_space.push_back({ x, y });
    }
  }
  food = { 16, 8 };

  addtoSnake(53, 8);
  addtoSnake(52, 8);
  addtoSnake(51, 8);
  addtoSnake(50, 8);
  dir = 2;
  proposed_dir = dir;
  last_update = millis();
}

void gameOver() {
  String x = "GAME OVER";
  String scr = "Score: " + String(score);
  int space_x = (LCD_COLS - x.length() + 1) / 2;
  int space_scr = (LCD_COLS - scr.length() + 1) / 2;

  unsigned long lastdelay = 0;

  bool out = false;
  while (true) {
    lcd.clear();
    lcd.setCursor(space_x, 0);
    lcd.print(x);
    lcd.setCursor(space_scr, 1);
    lcd.print(scr);
    lastdelay = millis();
    while (millis() - lastdelay <= 1000) {
      for (int i : game_controls) {
        if (digitalRead(i) == LOW) {
          out = true;
        }
        if (out) {
          break;
        }
      }
      if (out) {
        break;
      }
    }
    if (out)
      break;
    lcd.clear();
    lastdelay = millis();
    while (millis() - lastdelay <= 300) {
      for (int i : game_controls) {
        if (digitalRead(i) == LOW) {
          out = true;
        }
        if (out) {
          break;
        }
      }
      if (out) {
        break;
      }
    }
    if (out)
      break;
  }
}

void gameSplash() {
  String x = "SNAKE";
  String scr = "GAME!";
  int space_x = (LCD_COLS - x.length() + 1) / 2;
  int space_scr = (LCD_COLS - scr.length() + 1) / 2;

  unsigned long lastdelay = 0;

  bool out = false;
  while (true) {
    lcd.clear();
    lcd.setCursor(space_x, 0);
    lcd.print(x);
    lcd.setCursor(space_scr, 1);
    lcd.print(scr);
    lastdelay = millis();
    while (millis() - lastdelay <= 1000) {
      for (int i : game_controls) {
        if (digitalRead(i) == LOW) {
          out = true;
        }
        if (out) {
          break;
        }
      }
      if (out) {
        break;
      }
    }
    if (out)
      break;
    lcd.clear();
    lastdelay = millis();
    while (millis() - lastdelay <= 300) {
      for (int i : game_controls) {
        if (digitalRead(i) == LOW) {
          out = true;
        }
        if (out) {
          break;
        }
      }
      if (out) {
        break;
      }
    }
    if (out)
      break;
  }

  delay(200);
}

void addtoSnake(uint8_t x, uint8_t y) {
  int idx = indices[x][y];
  int last_idx = food_space.size() - 1;
  coord last = food_space[last_idx];
  food_space[idx] = last;
  indices[last.first][last.second] = idx;
  indices[x][y] = -1;
  food_space.pop_back();


  snake.push({ x, y });
}

void removefromSnake() {
  coord t = snake.front();
  indices[t.first][t.second] = food_space.size();
  food_space.push_back(t);
  snake.pop();
}

int now = 0;
bool wait = 1;

void spawnFood() {
  food = food_space[random(food_space.size())];
}

void updateGame() {
  for (int i = 0; i < 4; i++)
    if (digitalRead(game_controls[i]) == LOW)
      proposed_dir = i;

  if (millis() - last_update > dt) {
    last_update = millis();

    if (control_reverse[proposed_dir] == dir) {
      proposed_dir = dir;
    } else {
      dir = proposed_dir;
    }

    coord next = { directions[dir].first + snake.back().first - 1, directions[dir].second + snake.back().second - 1 };

    if (next.first >= SCREEN_WIDTH || next.second >= SCREEN_HEIGHT) {
      next.first++;
      next.second++;
      next.first = (SCREEN_WIDTH + next.first - 1) % SCREEN_WIDTH;
      next.second = (SCREEN_HEIGHT + next.second - 1) % SCREEN_HEIGHT;
    }
    if (indices[next.first][next.second] == -1) {
      if(wait){
        now = millis();
        wait = 0;
      }
      if (millis() - now >= 1500) {
        gameOver();
        gameSetup();
        wait = 1;
      }
    } else if (next == food) {
      addtoSnake(next.first, next.second);
      spawnFood();
      score++;
    } else {
      addtoSnake(next.first, next.second);
      removefromSnake();
    }
  }
}


void DrawGame() {
  clearPixels();
  setPixel(food.first, food.second);
  int t = snake.size();
  while (t-- > 0) {
    coord t = snake.front();
    setPixel(t.first, t.second);
    snake.push(t);
    snake.pop();
  }
}


//***************************************************************

// ============================================================
// SETUP
// ============================================================

void setup() {
  for (int i : game_controls)
    pinMode(i, INPUT_PULLUP);

  Serial.begin(9600);

  initLCD();
  clearPixels();
  gameSplash();
  gameSetup();
}
// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  updateGame();
  unsigned long now = millis();
  if (now - previousBatchTime >= BATCH_INTERVAL) {
    previousBatchTime = now;
    DrawGame();
    renderBatch(currentBatch);
    currentBatch = (currentBatch + 1) % BATCH_COUNT;
  }
}