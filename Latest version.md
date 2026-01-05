#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Keypad.h>

// --- PINY (ESP32) ---
#define TFT_CS     13
#define TFT_RST    10
#define TFT_DC      9
#define PIN_BTN_JUMP 17 

const byte ROWS = 2; 
const byte COLS = 2; 
char keys[ROWS][COLS] = {{'1', '2'}, {'3', '4'}};
byte rowPins[ROWS] = {4, 5}; 
byte colPins[COLS] = {6, 7}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

#define TILE_SIZE  20
#define COLS_DISP  12 
#define ROWS_DISP  16
#define PLAYER_SCREEN_X 100 

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// --- HRÁČ A SVET ---
float playerY = 60.0;
float velY = 0, velX = 0;
const float gravity = 0.5, jumpForce = -7.5, walkSpeed = 3.5;
float scrollX = 505.0;
float oldPlayerY = 0;
int lastScrollGrid = -1;

// --- KURZOR ---
int cursorDir = 0; // 0: Vpravo, 1: Vľavo, 2: Hore, 3: Dole
int oldCursorX = -1, oldCursorY = -1;

// --- FARBY ---
#define C_SKY      0x867D 
#define C_GRASS    0x2444 
#define C_DIRT     0x8200 
#define C_WOOD     0x52AA 
#define C_LEAVES   0x0500 

struct BlockMod { int x, y; uint16_t type; };
BlockMod mods[100]; 
int modCount = 0;

// --- FUNKCIE SVETA ---
uint16_t getOriginalBlock(int wx, int wy) {
  int groundY = 10 + (int)(sin(wx * 0.4) * 3.0);
  if (wy > groundY) return C_DIRT;
  if (wy == groundY) return C_GRASS;
  for (int dx = -1; dx <= 1; dx++) {
    int checkX = wx + dx;
    if (checkX % 10 == 0 && checkX != 0) {
      int baseTop = 10 + (int)(sin(checkX * 0.4) * 3.0);
      int trunkTopY = baseTop - 3;
      if (dx == 0 && wy >= trunkTopY && wy < baseTop) return C_WOOD;
      if (wy >= trunkTopY - 2 && wy < trunkTopY) return C_LEAVES;
    }
  }
  return C_SKY;
}

uint16_t getBlock(int wx, int wy) {
  for (int i = 0; i < modCount; i++) {
    if (mods[i].x == wx && mods[i].y == wy) return mods[i].type;
  }
  return getOriginalBlock(wx, wy);
}

bool isSolid(int wx, int wy) {
  uint16_t b = getBlock(wx, wy);
  if (b == C_SKY || b == C_LEAVES) return false;
  return (b == C_DIRT || b == C_GRASS || b == C_WOOD);
}

// --- INVENTÁR ---
void drawInventory() {
  int invX = 10;
  int invY = 10;
  int slotSize = 25;
  int spacing = 5;

  for (int i = 0; i < 5; i++) {
    int x = invX + (i * (slotSize + spacing));
    // Dithering (priesvitný efekt)
    for (int dy = 0; dy < slotSize; dy++) {
      for (int dx = 0; dx < slotSize; dx++) {
        if ((dx + dy) % 2 == 0) {
          tft.drawPixel(x + dx, invY + dy, ST77XX_WHITE);
        }
      }
    }
    tft.drawRect(x, invY, slotSize, slotSize, ST77XX_WHITE);
  }
}

void setup() {
  pinMode(PIN_BTN_JUMP, INPUT_PULLDOWN);
  SPI.begin(12, -1, 11, 13);
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(C_SKY);
  drawInventory();
}

void loop() {
  velX = 0;
  keypad.getKeys();
  
  for (int i=0; i<LIST_MAX; i++) {
    if (keypad.key[i].kstate == PRESSED || keypad.key[i].kstate == HOLD) {
      if (keypad.key[i].kchar == '1') velX = -walkSpeed;
      if (keypad.key[i].kchar == '2') velX = walkSpeed;
      
      if (keypad.key[i].kchar == '4' && keypad.key[i].stateChanged) {
        cursorDir = (cursorDir + 1) % 4;
      }
      
      if (keypad.key[i].kchar == '3' && keypad.key[i].stateChanged) {
        int curGrid = (int)(scrollX / TILE_SIZE);
        if (modCount < 100) {
          mods[modCount++] = {oldCursorX, oldCursorY, C_SKY};
          tft.fillRect((oldCursorX - curGrid) * TILE_SIZE, oldCursorY * TILE_SIZE, TILE_SIZE, TILE_SIZE, C_SKY);
        }
      }
    }
  }

  // Fyzika a kolízie
  float nextScrollX = scrollX + velX;
  int cYT = (int)(playerY + 2) / TILE_SIZE;
  int cYB = (int)(playerY + TILE_SIZE - 2) / TILE_SIZE;
  bool blocked = false;
  if (velX > 0) {
    int nGX = (int)((PLAYER_SCREEN_X + nextScrollX + TILE_SIZE - 1) / TILE_SIZE);
    if (isSolid(nGX, cYT) || isSolid(nGX, cYB)) blocked = true;
  } else if (velX < 0) {
    int nGX = (int)((PLAYER_SCREEN_X + nextScrollX) / TILE_SIZE);
    if (isSolid(nGX, cYT) || isSolid(nGX, cYB)) blocked = true;
  }
  if (!blocked) scrollX = nextScrollX;
  if (scrollX < 0) scrollX = 0;

  velY += gravity;
  float nextY = playerY + velY;
  int gXL = (int)((PLAYER_SCREEN_X + scrollX + 2) / TILE_SIZE);
  int gXR = (int)((PLAYER_SCREEN_X + scrollX + TILE_SIZE - 2) / TILE_SIZE);
  int gYF = (int)((nextY + TILE_SIZE) / TILE_SIZE);
  if (isSolid(gXL, gYF) || isSolid(gXR, gYF)) {
    velY = 0; playerY = (gYF - 1) * TILE_SIZE;
    if (digitalRead(PIN_BTN_JUMP) == HIGH) velY = jumpForce;
  } else { playerY = nextY; }

  // Prekreslenie sveta
  int curGrid = (int)(scrollX / TILE_SIZE);
  if (curGrid != lastScrollGrid) {
    for (int i = 0; i < COLS_DISP; i++) {
      for (int j = 0; j < ROWS_DISP; j++) {
        tft.fillRect(i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE, getBlock(i + curGrid, j));
      }
    }
    lastScrollGrid = curGrid;
    drawInventory(); // Po posune mapy prekresliť inventár
  }

  // Logika kurzora a hráča
  int pGX = (int)((PLAYER_SCREEN_X + scrollX) / TILE_SIZE);
  int pGY = (int)(playerY / TILE_SIZE);
  
  // Mazanie starého hráča
  tft.fillRect(PLAYER_SCREEN_X, (int)oldPlayerY, TILE_SIZE, TILE_SIZE, getBlock(pGX, (int)oldPlayerY/TILE_SIZE));
  
  // Mazanie starého kurzora
  if (oldCursorX != -1) {
     int oldCsDispX = (oldCursorX - curGrid) * TILE_SIZE;
     if (oldCsDispX >= 0 && oldCsDispX < 240) {
        tft.fillRect(oldCsDispX, oldCursorY * TILE_SIZE, TILE_SIZE, TILE_SIZE, getBlock(oldCursorX, oldCursorY));
     }
  }

  // Nový kurzor
  int newCursorX = pGX;
  int newCursorY = pGY;
  if (cursorDir == 0) newCursorX++; 
  else if (cursorDir == 1) newCursorX--;
  else if (cursorDir == 2) newCursorY--;
  else if (cursorDir == 3) newCursorY++;

  int csDispX = (newCursorX - curGrid) * TILE_SIZE;
  if (csDispX >= 0 && csDispX < 240) {
    tft.drawRect(csDispX, newCursorY * TILE_SIZE, TILE_SIZE, TILE_SIZE, ST77XX_WHITE);
  }

  // Kreslenie hráča
  tft.fillRect(PLAYER_SCREEN_X, (int)playerY, TILE_SIZE, TILE_SIZE, ST77XX_RED);

  // Prekreslenie inventára, ak ho niečo prekrylo (hráč alebo pohyb)
  if (playerY < 45 || oldPlayerY < 45) {
    drawInventory();
  }

  oldPlayerY = playerY;
  oldCursorX = newCursorX;
  oldCursorY = newCursorY;

  delay(15);
}
