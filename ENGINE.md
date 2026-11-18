# ArduinoArcade Rendering Engine

This sketch provides an 80×16 monochrome pixel framebuffer for a 16×2 HD44780 LCD. The game code draws into RAM; the renderer converts the framebuffer into 5×8 character-cell patterns and displays it in four batches.

## Drawing API

- `clearPixels()` clears the whole framebuffer.
- `setPixel(x, y, state = true)` sets or clears one pixel. Coordinates outside the 80×16 screen are ignored.
- `getPixel(x, y)` returns whether a pixel is set; out-of-range coordinates return `false`.
- `drawLine(x0, y0, x1, y1)` draws a line.
- `drawRect(x, y, w, h)` draws an outline rectangle.
- `fillRect(x, y, w, h)` draws a filled rectangle.
- `drawCircle(cx, cy, radius)` draws a circle outline.
- `drawBitmap5x8(x, y, bitmap)` draws an 8-row bitmap, with each row's low five bits representing pixels from left to right.

All drawing functions modify the framebuffer only. They do not communicate with the LCD.

## Adding a game

Add game state and functions in the sketch, then call them from `setup()` and `loop()`:

```cpp
void drawGame() {
    clearPixels();
    fillRect(10, 6, 4, 4);
    setPixel(20, 8);
}
```

Call `drawGame()` whenever the game state changes. The existing `loop()` advances the renderer automatically. Do not call `renderBatch()` from game code; it is renderer internals. To animate gameplay, use `millis()` timers in `loop()` rather than long `delay()` calls.

## How display refresh works

Each LCD character cell represents a 5×8 section of the framebuffer. The renderer divides the 32 cells into four groups of eight. Every `BATCH_INTERVAL` milliseconds it:

1. Clears the LCD's displayed cells.
2. Loads the next eight cell patterns into CGRAM slots 0–7.
3. Writes those eight glyphs to their corresponding LCD positions.

At the configured 30 ms interval, a complete four-batch pass takes about 120 ms (roughly 8 full passes per second). The HD44780 has only eight shared custom glyph slots, so it cannot show all 32 independently drawn cell patterns simultaneously. The batches trade a complete image for flicker and transient blanking. Lowering `BATCH_INTERVAL` may increase flicker or exceed what the LCD/I²C backpack can update reliably; raising it makes the full image refresh more slowly.

## Configuration

- `SCREEN_WIDTH`, `SCREEN_HEIGHT`: framebuffer dimensions in pixels; currently 80×16.
- `LCD_COLS`, `LCD_ROWS`: LCD character dimensions; currently 16×2.
- `CELL_WIDTH`, `CELL_HEIGHT`: pixels per character cell; currently 5×8.
- `BATCH_SIZE`, `BATCH_COUNT`: cells per batch and number of batches; currently 8 and 4.
- `BATCH_INTERVAL`: milliseconds between batch writes.

These dimensions are interdependent. Keep `SCREEN_WIDTH == LCD_COLS * CELL_WIDTH`, `SCREEN_HEIGHT == LCD_ROWS * CELL_HEIGHT`, and `BATCH_SIZE * BATCH_COUNT == LCD_COLS * LCD_ROWS` when changing them.

## Hardware and libraries

The sketch uses Bill Perry's `hd44780` library with `hd44780_I2Cexp` to initialize and control the I²C LCD. The LCD must be a compatible 16×2 HD44780 display with an I²C backpack.
