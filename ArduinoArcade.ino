#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
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

  //lcd.backlight();
  lcd.clear();
}


//**************************************************************
//Data structure libs



template<typename T>
class Queue {
private:
  T a[SCREEN_WIDTH * SCREEN_HEIGHT];
  size_t n = SCREEN_WIDTH * SCREEN_HEIGHT;
  int f = -1;
  int b = -1;
public:
  size_t size() {
    return (b - f + n) % n + (f != -1 && f == b) * n;
  }
  void push(T x) {
    if (f == -1) {
      f = 0;
      b = 0;
    } else if (f == b) {
      Serial.println("overflow");
      return;
    }
    a[b] = x;
    b++;
    b %= n;
  }
  void pop() {
    if (f == -1) {
      Serial.println("underflow");
    }
    f++;
    f %= n;
    if (f == b) {
      f = -1;
      b = -1;
    }
  }
  T front() {
    if (f == -1)
      Serial.println("underflow");

    return a[f];
  }
  T back() {
    if (f == -1)
      Serial.println("underflow");
    return a[(b + n - 1) % n];
  }
  void clear() {
    f = -1;
    b = -1;
  }
};

template<typename T>
class PixelVector {
private:
  T a[SCREEN_WIDTH * SCREEN_HEIGHT];
  size_t cap = SCREEN_WIDTH * SCREEN_HEIGHT;
  size_t n = 0;
public:
  size_t size() {
    return n;
  }
  void push_back(T x) {
    if (n == cap) {
      Serial.println("overflow");
      return;
    }
    a[n] = x;
    n++;
  }
  void pop_back() {
    if (n == 0) {
      Serial.println("underflow");
      return;
    }
    n--;
  }
  T get(size_t i) {
    return a[i];
  }
  void set(size_t i, T x) {
    a[i] = x;
  }
  void clear() {
    n = 0;
  }
};

struct pair {
  uint8_t first;
  uint8_t second;
  bool operator==(const pair& other) const {
    return (other.first == first) && (second == other.second);
  }
};


//**************************************************************


//**************************************************************
//Game code
using coord = pair;
int last_pause = 0;
bool wait_on_gameover = 1;

int pause_duration = 0;

unsigned long last_update = 0;
int dt = 240;

coord food = { 16, 8 };
int dir = 2;
coord directions[] = { { 1, 0 }, { 1, 2 }, { 0, 1 }, { 2, 1 } };
// UP DOWN LEFT RIGHT
const int game_controls[] = { 2, 3, 4, 5 };
const int control_reverse[] = { 1, 0, 3, 2 };

Queue<coord> snake;

void pause(int t) {
  last_pause = millis();
  pause_duration = t;
}

int16_t indices[SCREEN_WIDTH][SCREEN_HEIGHT];
PixelVector<coord> food_space;
int proposed_dir = 2;

int score = 0;

void gameSetup() {
  score = 0;
  lcd.clear();
  clearPixels();

  snake.clear();
  food_space.clear();
  memset(indices, -1, sizeof(indices));
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
      indices[x][y] = food_space.size();
      food_space.push_back({ x, y });
    }
  }
  wait_on_gameover = true;

  addtoSnake(53, 8);
  addtoSnake(52, 8);
  addtoSnake(51, 8);
  addtoSnake(50, 8);
  spawnFood();
  dir = 2;
  proposed_dir = dir;
  last_update = millis();
  pause(1000);
}

void gameOver() {
  char x[] = "GAME OVER";
  char scr[20] = "Score: ";
  itoa(score, scr + strlen(scr), 10);
  int space_x = (LCD_COLS - strlen(x) + 1) / 2;
  int space_scr = (LCD_COLS - strlen(scr) + 1) / 2;

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
          lcd.clear();
          return;
        }
      }
    }
    lcd.clear();
    lastdelay = millis();
    while (millis() - lastdelay <= 300) {
      for (int i : game_controls) {
        if (digitalRead(i) == LOW) {
          lcd.clear();
          return;
        }
      }
    }
  }
}

void gameSplash() {
  char x[] = "SNAKE";
  char scr[] = "GAME!";
  int space_x = (LCD_COLS - strlen(x) + 1) / 2;
  int space_scr = (LCD_COLS - strlen(scr) + 1) / 2;

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
          lcd.clear();
          return;
        }
      }
    }
    lcd.clear();
    lastdelay = millis();
    while (millis() - lastdelay <= 300) {
      for (int i : game_controls) {
        if (digitalRead(i) == LOW) {
          lcd.clear();
          return;
        }
      }
    }
  }
}


void win() {
  char x[] = "YOU";
  char scr[] = "WIN!";
  int space_x = (LCD_COLS - strlen(x) + 1) / 2;
  int space_scr = (LCD_COLS - strlen(scr) + 1) / 2;

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
          lcd.clear();
          return;
        }
      }
    }
    lcd.clear();
    lastdelay = millis();
    while (millis() - lastdelay <= 300) {
      for (int i : game_controls) {
        if (digitalRead(i) == LOW) {
          lcd.clear();
          return;
        }
      }
    }
  }
}

void addtoSnake(uint8_t x, uint8_t y) {
  int idx = indices[x][y];
  int last_idx = food_space.size() - 1;
  coord last = food_space.get(last_idx);
  food_space.set(idx, last);
  indices[last.first][last.second] = idx;
  indices[x][y] = -1;
  food_space.pop_back();

  setPixel(x, y);


  snake.push({ x, y });
}

void removefromSnake() {
  coord t = snake.front();
  indices[t.first][t.second] = food_space.size();
  food_space.push_back(t);
  setPixel(t.first, t.second, false);
  snake.pop();
}


bool spawnFood() {
  if (food_space.size() == 0) return false;
  food = food_space.get(random(food_space.size()));
  setPixel(food.first, food.second);
  return true;
}

void updateGame() {
  if (millis() - last_pause <= pause_duration) return;
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
    if (food_space.size() == 0) {
      win();
      gameSetup();
      return;
    }
    if (indices[next.first][next.second] == -1 && !(next == snake.front())) {
      if (wait_on_gameover) {
        pause(1500);
        wait_on_gameover = false;
      } else {
        gameOver();
        gameSetup();
      }
    } else if (next == food) {
      addtoSnake(next.first, next.second);
      if (!spawnFood())
        pause(3000);
      score++;
    } else {
      removefromSnake();
      addtoSnake(next.first, next.second);
    }
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
  //Serial.println("Game Running");
  updateGame();
  unsigned long now = millis();
  if (now - previousBatchTime >= BATCH_INTERVAL) {
    //Serial.println(freeMemory());
    previousBatchTime = now;
    renderBatch(currentBatch);
    currentBatch = (currentBatch + 1) % BATCH_COUNT;
  }
}