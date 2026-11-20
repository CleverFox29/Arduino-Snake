
#include <LiquidCrystal.h>
#include <cstdint>




// ============================================================
// CONFIGURATION
// ============================================================

LiquidCrystal lcd(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);

#define backlight_pin 13

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 16

#define LCD_COLS 16
#define LCD_ROWS 2

#define CELL_WIDTH 5
#define CELL_HEIGHT 8

#define CELL_COUNT 32

#define BATCH_SIZE 8
#define BATCH_COUNT 4

#define BATCH_INTERVAL 0

// ============================================================
// FRAMEBUFFER
// ============================================================


uint8_t frame_buffer[LCD_COLS][LCD_ROWS][CELL_HEIGHT];

// ============================================================
// RENDERER STATE
// ============================================================

unsigned long last_render = 0;
uint8_t currentBatch = 0;
bool backlight_state = true;
// ============================================================
// PIXEL OPERATIONS
// ============================================================

void clearPixels() {
  memset(frame_buffer, 0, sizeof(frame_buffer));
}

void setPixel(int x, int y, bool state = true) {

  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return;
  }

  uint8_t charX = x / CELL_WIDTH;
  uint8_t charY = y / CELL_HEIGHT;
  uint8_t x_offset = x % CELL_WIDTH;
  uint8_t y_offset = y % CELL_HEIGHT;
  uint8_t mask = 1 << (CELL_WIDTH - 1 - x_offset);
  if (state)
    frame_buffer[charX][charY][y_offset] |= mask;
  else
    frame_buffer[charX][charY][y_offset] &= ~mask;
}





// ============================================================
// PATTERN EXTRACTION
// ============================================================

uint8_t* getCellBitmap(int cell) {
  int cellX = cell % LCD_COLS;
  int cellY = cell / LCD_COLS;
  return frame_buffer[cellX][cellY];
}

// ============================================================
// EXACT BATCH RENDERER
// ============================================================

void renderBatch(int batch) {

  // Clear DDRAM before changing CGRAM so earlier custom glyph references
  // cannot turn into visible stripes when their shared slots are redefined.
  //lcd.clear();
  int prev_batch = (batch + BATCH_COUNT - 1) % BATCH_COUNT;
  int firstcell_of_prev = prev_batch * BATCH_SIZE;
  int firstCell = batch * BATCH_SIZE;

  for (int slot = 0; slot < BATCH_SIZE; slot++) {
    lcd.setCursor((firstcell_of_prev + slot) % LCD_COLS, (firstcell_of_prev + slot) / LCD_COLS);
    lcd.write(' ');
    lcd.createChar(slot, getCellBitmap(batch * BATCH_SIZE + slot));
    lcd.setCursor((firstCell + slot) % LCD_COLS, (firstCell + slot) / LCD_COLS);
    lcd.write(slot);
  }

  // Every 8-cell batch is one contiguous half-row on the 16x2 LCD.
}

// ============================================================
// RENDER COMPLETE FRAME
// ============================================================

// ============================================================
// LCD INITIALIZATION
// ============================================================


void backlight(){
  digitalWrite(backlight_pin, HIGH);
}
void noBacklight(){
  digitalWrite(backlight_pin, LOW);
}

void initLCD() {

  lcd.begin(LCD_COLS, LCD_ROWS);

  

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
bool wait_on_gameover;
unsigned long last_pause;
unsigned long last_read;
unsigned long last_update;
unsigned long last_input_found;
unsigned long pause_duration;

const unsigned long dt = 300;
const unsigned long button_debounce = dt/ 10;

coord food;

uint8_t dir;
uint8_t proposed_dir;
coord directions[] = { { 1, 0 }, { 1, 2 }, { 0, 1 }, { 2, 1 } };

// UP DOWN LEFT RIGHT
const int game_controls[] = { A0,A1, A2, A3};
const int control_reverse[] = { 1, 0, 3, 2 };



Queue<coord> snake;
int16_t indices[SCREEN_WIDTH][SCREEN_HEIGHT];
PixelVector<coord> food_space;

bool input_dirty = false;
uint16_t score = 0;
int raw_input;
bool long_pressed;
uint8_t long_press_count;
unsigned long last_backlight_change;

void readInput() {

  bool input = false;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(game_controls[i]) == LOW) {
      if (i != raw_input) {
        last_input_found = millis();
        raw_input = i;
      }
      if (millis() - last_input_found > button_debounce && raw_input != -1) {
        if (proposed_dir == raw_input)
          if (long_press_count < 3)
            long_press_count++;
          else
            long_pressed = true;
        else {
          long_press_count = 0;
          long_pressed = false;
          proposed_dir = raw_input;
        }
        raw_input = -1;
        input_dirty = true;
      }
      input = true;
    }
  }
  if (!input) {
    if (-1 != raw_input) {
      last_input_found = millis();
      raw_input = -1;
    }
    long_pressed = false;
    long_press_count = 0;
  }
}
bool pause_flicker = false;
void acknowledgeInput() {
  input_dirty = false;
}
void pause(int t) {
  last_pause = millis();
  pause_duration = t;
}
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
  pause_flicker = false;

  addtoSnake(53, 8);
  addtoSnake(52, 8);
  addtoSnake(51, 8);
  addtoSnake(50, 8);
  spawnFood();
  dir = 2;
  proposed_dir = dir;
  raw_input = -1;
  long_pressed = false;
  long_press_count = 0;
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
      readInput();
      if (input_dirty) {
        acknowledgeInput();
        lcd.clear();
        return;
      }
    }
    lcd.clear();
    lastdelay = millis();
    while (millis() - lastdelay <= 300) {
      readInput();
      if (input_dirty) {
        acknowledgeInput();
        lcd.clear();
        return;
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
  if (millis() - last_pause <= pause_duration) {
    if (millis() - last_backlight_change < 50 || !pause_flicker) return;
    last_backlight_change = millis();
    if (backlight_state) {
      noBacklight();
      backlight_state = false;
    } else {

      backlight_state = true;
      backlight();
    }
    return;
  }
  pause_flicker = false;
  if (!backlight_state) {
    backlight_state = true;
    backlight();
  }


  readInput();

  if (millis() - last_update > ((long_pressed)?(dt/6): dt)) {
    last_update = millis();

    if (control_reverse[proposed_dir] == dir) {
      proposed_dir = dir;
    } else {
      dir = proposed_dir;
    }

    acknowledgeInput();
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
        pause_flicker = true;
        pause(1500);
        wait_on_gameover = false;
      } else {
        gameOver();
        gameSetup();
      }
      return;
    } else if (next == food) {
      addtoSnake(next.first, next.second);
      if (!spawnFood()) {
        pause_flicker = true;
        pause(3000);
      }
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
  pinMode(backlight_pin, OUTPUT);

  Serial.begin(9600);

  initLCD();
  clearPixels();
  backlight();
  backlight_state = true;
  last_backlight_change = millis();
  pause_flicker = false;
  gameSplash();
  gameSetup();
}
// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  updateGame();
  if (millis() - last_render >= BATCH_INTERVAL) {
    last_render = millis();
    renderBatch(currentBatch);
    currentBatch = (currentBatch + 1) % BATCH_COUNT;
  }
}