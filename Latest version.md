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
#define COLS_DISP  13
#define ROWS_DISP  17 
#define PLAYER_SCREEN_X 100 // Hráč fixne na obrazovke
#define PLAYER_SCREEN_Y 140

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// --- HRÁČ A SVET ---
float playerWorldX = 500.0; // Pozícia v svete
float playerWorldY = 100.0;
float velY = 0, velX = 0;
const float gravity = 0.5, jumpForce = -7.5, walkSpeed = 3.5;
float scrollX = 0.0; // Kamera - horizontálne
float scrollY = 0.0; // Kamera - vertikálne (NOVÉ)
float oldPlayerWorldY = 0;
int lastScrollGridX = -1;
int lastScrollGridY = -1;

// --- KURZOR (z prvého kódu) ---
int cursorDir = 0; // 0: Vpravo, 1: Vľavo, 2: Hore, 3: Dole
int oldCursorX = -1, oldCursorY = -1;

// --- FARBY ---
#define C_SKY      0x867D 
#define C_GRASS    0x2444 
#define C_DIRT     0x8200 
#define C_WOOD     0x52AA 
#define C_LEAVES   0x0500 
#define ST77XX_GRAY 0x8410
#define C_BEDROCK  0x4208

struct BlockMod { int x, y; uint16_t type; };
BlockMod mods[100]; 
int modCount = 0;

// --- MAPA SVETA ---
#define WORLD_MIN_X -100
#define WORLD_MAX_X 100
#define WORLD_MIN_Y 0
#define WORLD_MAX_Y 30
uint16_t world[WORLD_MAX_X - WORLD_MIN_X + 1][WORLD_MAX_Y - WORLD_MIN_Y + 1];

void generateWorld() {
  for (int wx = WORLD_MIN_X; wx <= WORLD_MAX_X; wx++) {
    int groundY = 10 + (int)(sin(wx * 0.4) * 3.0);
    int dirtDepth = random(3, 8);
    int stoneStart = groundY + dirtDepth;
    
    // Bedrock úroveň - ÚPLNE SPODOK MAPY
    int bedrockLevel = WORLD_MAX_Y;  // Zmena: bedrock na úplnom spodku (najvyššie Y)
    
    for (int wy = WORLD_MIN_Y; wy <= WORLD_MAX_Y; wy++) {
      uint16_t block = C_SKY;
      
      // 1. BEDROCK - vrstva na úplnom SPODKU MAPY
      if (wy >= bedrockLevel - 1 && wy <= bedrockLevel) {  // Opravené: spodné 2 vrstvy
        // Posledný rad (najspodnejší) je plný bedrock
        if (wy == bedrockLevel) {
          block = C_BEDROCK;
        }
        // Predposledný rad má občasné medzery
        else if (wy == bedrockLevel - 1) {
          if (random(5) > 0) {  // 80% šanca na bedrock
            block = C_BEDROCK;
          } else {
            // Miesto bez bedrocku - kameň
            if (wy >= stoneStart) {
              if (random(25) == 0) block = 0xC5D7; 
              else block = ST77XX_GRAY;
            } else if (wy > groundY && wy < stoneStart) {
              block = C_DIRT;
            }
          }
        }
      }
      // 2. NORMÁLNE GENEROVANIE TERÉNU (nad bedrockom)
      else if (wy >= groundY && wy < bedrockLevel - 1) {  // Opravené: iba nad bedrockom
        if (wy == groundY) block = C_GRASS;
        else if (wy > groundY && wy < stoneStart) block = C_DIRT;
        else if (wy >= stoneStart) {
          if (random(25) == 0) block = 0xC5D7; 
          else block = ST77XX_GRAY;
        }
      }
      
      // 3. STROMY - ZLEPŠENÉ
      if (wx % 10 == 0 && wx != 0) {
        int trunkTopY = groundY - 3;
        int trunkBottomY = groundY;
        
        // Kmeň stromu (3 bloky vysoký)
        if (wy >= trunkTopY && wy < trunkBottomY) {
          block = C_WOOD;
        }
        // Koruna stromu (2x3 bloky nad kmeňom)
        for (int lx = -1; lx <= 1; lx++) {       // 3 bloky na šírku
          for (int ly = 1; ly <= 2; ly++) {      // 2 bloky na výšku
            if (wx + lx == wx && wy == trunkTopY - ly) {
              block = C_LEAVES;
            }
          }
        }
      }
      
      world[wx - WORLD_MIN_X][wy - WORLD_MIN_Y] = block;
    }
  }
}

// --- NAHRADÍ getOriginalBlock ---
uint16_t getOriginalBlock(int wx, int wy) {
  if (wx < WORLD_MIN_X || wx > WORLD_MAX_X || wy < WORLD_MIN_Y || wy > WORLD_MAX_Y)
    return C_SKY;
  return world[wx - WORLD_MIN_X][wy - WORLD_MIN_Y];
}


// --- FUNKCIE SVETA (z prvého kódu) ---
// --- FUNKCIE SVETA ---
// 1️⃣ Generovanie sveta (tráva, dirt, kameň, stromy)


// 2️⃣ Prekrytie modifikácií (zmenené bloky)
uint16_t getBlock(int wx, int wy) {
  for (int i = 0; i < modCount; i++) {
    if (mods[i].x == wx && mods[i].y == wy) return mods[i].type;
  }
  return getOriginalBlock(wx, wy);
}

// 3️⃣ Kontrola kolízie
bool isSolid(int wx, int wy) {
  uint16_t b = getBlock(wx, wy);
  if (b == C_SKY || b == C_LEAVES) return false;
  return (b == C_DIRT || b == C_GRASS || b == C_WOOD || 
          b == ST77XX_GRAY || b == C_BEDROCK);
}


// --- INVENTÁR ---
void drawInventory() {
  int slotSize = 25;
  int spacing = 5;
  int totalWidth = (5 * slotSize) + (4 * spacing);
  int invX = (240 - totalWidth) / 2; 
  int invY = 320 - slotSize - 10;

  for (int i = 0; i < 5; i++) {
    int x = invX + (i * (slotSize + spacing));
    
    // Dithering efekt
    for (int dy = 0; dy < slotSize; dy++) {
      for (int dx = 0; dx < slotSize; dx++) {
        if ((dx + dy) % 2 == 0) {
          tft.drawPixel(x + dx, invY + dy, ST77XX_WHITE);
        }
      }
    }
    // Obrys slotu
    tft.drawRect(x, invY, slotSize, slotSize, ST77XX_WHITE);
  }
}

void setup() {
  pinMode(PIN_BTN_JUMP, INPUT_PULLDOWN);
  SPI.begin(12, -1, 11, 13);
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(C_SKY);
  generateWorld();
  drawInventory();
}

void loop() {
  velX = 0;
  keypad.getKeys();
  
  // --- OVLÁDANIE Z PRVÉHO KÓDU ---
  for (int i=0; i<LIST_MAX; i++) {
    if (keypad.key[i].kstate == PRESSED || keypad.key[i].kstate == HOLD) {
      if (keypad.key[i].kchar == '1') velX = -walkSpeed;
      if (keypad.key[i].kchar == '2') velX = walkSpeed;
      
      if (keypad.key[i].kchar == '4' && keypad.key[i].stateChanged) {
        cursorDir = (cursorDir + 1) % 4;
      }
      
      if (keypad.key[i].kchar == '3' && keypad.key[i].stateChanged && oldCursorX != -1 && oldCursorY != -1) {
        int curGridX = (int)(scrollX / TILE_SIZE);
        int curGridY = (int)(scrollY / TILE_SIZE);
        
        // KONTROLA - ak je to bedrock, NEDÁ SA KOPAŤ
        uint16_t currentBlock = getBlock(oldCursorX, oldCursorY);
        if (currentBlock == C_BEDROCK) {
          // Bedrock sa nedá vyťažiť - skonči bez zmeny
          continue;
        }

        // 1️⃣ Zmeniť blok v mods, aby sa hneď zapamätal ako prázdny
        if (modCount < 100) {
            mods[modCount++] = {oldCursorX, oldCursorY, C_SKY};
        }

        // 2️⃣ Okamžite prekresli blok na displeji
        int dispX = (oldCursorX - curGridX) * TILE_SIZE;
        int dispY = (oldCursorY - curGridY) * TILE_SIZE;
        if (dispX >= 0 && dispX < 240 && dispY >= 0 && dispY < 320) {
            tft.fillRect(dispX, dispY, TILE_SIZE, TILE_SIZE, C_SKY);
        }

        // 3️⃣ Odblokuj hráča, ak je práve nad vykopaným blokom
        int playerGridX = (int)(playerWorldX / TILE_SIZE);
        int playerGridY = (int)(playerWorldY / TILE_SIZE);
        if (playerGridX == oldCursorX && playerGridY == oldCursorY) {
            velY = 0;  // hráč prestane "padat" do hitboxu
        }
      }
    }
  }

  // --- FYZIKA HRÁČA ---
  // Horizontálna kolízia
  float nextWorldX = playerWorldX + velX;
  int pGridYT = (int)(playerWorldY + 2) / TILE_SIZE;
  int pGridYB = (int)(playerWorldY + TILE_SIZE - 2) / TILE_SIZE;
  bool blockedX = false;
  
  if (velX > 0) {
    int rightGrid = (int)((nextWorldX + TILE_SIZE - 1) / TILE_SIZE);
    if (isSolid(rightGrid, pGridYT) || isSolid(rightGrid, pGridYB)) blockedX = true;
  } else if (velX < 0) {
    int leftGrid = (int)((nextWorldX / TILE_SIZE));
    if (isSolid(leftGrid, pGridYT) || isSolid(leftGrid, pGridYB)) blockedX = true;
  }
  
  if (!blockedX) playerWorldX = nextWorldX;
  
  // Vertikálna kolízia s kontrolou blokov nad hlavou
  velY += gravity;
  float nextWorldY = playerWorldY + velY;
  int pGridXL = (int)((playerWorldX + 2) / TILE_SIZE);
  int pGridXR = (int)((playerWorldX + TILE_SIZE - 2) / TILE_SIZE);
  int pGridYF = (int)((nextWorldY + TILE_SIZE) / TILE_SIZE);
  int pGridYT_top = (int)(nextWorldY / TILE_SIZE); // horný blok hráča

  // Kolízia dole
  if (isSolid(pGridXL, pGridYF) || isSolid(pGridXR, pGridYF)) {
      velY = 0; 
      playerWorldY = (pGridYF - 1) * TILE_SIZE;
      if (digitalRead(PIN_BTN_JUMP) == HIGH) velY = jumpForce;
  } 
  // Kolízia hore (blok nad hlavou)
  else if (isSolid(pGridXL, pGridYT_top) || isSolid(pGridXR, pGridYT_top)) {
      if (velY < 0) {
          velY = 0;
          playerWorldY = (pGridYT_top + 1) * TILE_SIZE;
      }
  } 
  else { 
      playerWorldY = nextWorldY; 
  }

  // --- KAMERA (vertikálne + horizontálne) ---
  float targetScrollX = playerWorldX - PLAYER_SCREEN_X;
  float targetScrollY = playerWorldY - PLAYER_SCREEN_Y;
  scrollX = targetScrollX;
  scrollY = targetScrollY;
  
  // --- PRERENDEROVANIE SVETA ---
  int curGridX = (int)(scrollX / TILE_SIZE);
  int curGridY = (int)(scrollY / TILE_SIZE);
  
  if (curGridX != lastScrollGridX || curGridY != lastScrollGridY) {
    for (int i = 0; i < COLS_DISP; i++) {
      for (int j = 0; j < ROWS_DISP; j++) {
        uint16_t blockColor = getBlock(i + curGridX, j + curGridY);
        tft.fillRect(i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE, blockColor);
      }
    }
    lastScrollGridX = curGridX;
    lastScrollGridY = curGridY;
    drawInventory();
  }

  // --- LOGIKA KURZORA ---
  int playerGridX = (int)(playerWorldX / TILE_SIZE);
  int playerGridY = (int)(playerWorldY / TILE_SIZE);
  tft.fillRect(PLAYER_SCREEN_X, PLAYER_SCREEN_Y, TILE_SIZE, TILE_SIZE, 
               getBlock(playerGridX, playerGridY));
  if (oldCursorX != -1) {
    int oldCsDispX = (oldCursorX - curGridX) * TILE_SIZE;
    int oldCsDispY = (oldCursorY - curGridY) * TILE_SIZE;
    if (oldCsDispX >= 0 && oldCsDispX < 240 && oldCsDispY >= 0 && oldCsDispY < 320) {
      tft.fillRect(oldCsDispX, oldCsDispY, TILE_SIZE, TILE_SIZE, 
                   getBlock(oldCursorX, oldCursorY));
    }
  }

  int newCursorX = playerGridX;
  int newCursorY = playerGridY;
  if (cursorDir == 0) newCursorX++; 
  else if (cursorDir == 1) newCursorX--;
  else if (cursorDir == 2) newCursorY--;
  else if (cursorDir == 3) newCursorY++;

  int csDispX = (newCursorX - curGridX) * TILE_SIZE;
  int csDispY = (newCursorY - curGridY) * TILE_SIZE;
  if (csDispX >= 0 && csDispX < 240 && csDispY >= 0 && csDispY < 320) {
    tft.drawRect(csDispX, csDispY, TILE_SIZE, TILE_SIZE, ST77XX_WHITE);
  }

  tft.fillRect(PLAYER_SCREEN_X, PLAYER_SCREEN_Y, TILE_SIZE, TILE_SIZE, ST77XX_RED);

  if (playerWorldY < 45 || oldPlayerWorldY < 45) {
    drawInventory();
  }

  oldPlayerWorldY = playerWorldY;
  oldCursorX = newCursorX;
  oldCursorY = newCursorY;

  delay(15);
}
