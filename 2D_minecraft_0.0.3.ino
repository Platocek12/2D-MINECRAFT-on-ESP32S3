#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Keypad.h>
#include <WiFi.h>
#include <esp_system.h>

// --- PINY (ESP32) ---
#define TFT_CS     13
#define TFT_RST    10
#define TFT_DC     9
#define PIN_BTN_JUMP 17 
#define PIN_SYS_INFO 18

const byte ROWS = 2; 
const byte COLS = 2; 
char keys[ROWS][COLS] = {{'1', '2'}, {'3', '4'}};
byte rowPins[ROWS] = {4, 5}; 
byte colPins[COLS] = {6, 7}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

#define TILE_SIZE  20
#define COLS_DISP  13
#define ROWS_DISP  17 
#define PLAYER_SCREEN_X 100
#define PLAYER_SCREEN_Y 140

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// --- HRÁČ A SVET ---
float playerWorldX = 500.0;
float playerWorldY = 100.0;
float velY = 0, velX = 0;
const float gravity = 0.5, jumpForce = -7.5, walkSpeed = 3.5;
float scrollX = 0.0;
float scrollY = 0.0;
float oldPlayerWorldY = 0;
int lastScrollGridX = -1;
int lastScrollGridY = -1;

// --- KURZOR ---
int cursorDir = 0;
int oldCursorX = -1, oldCursorY = -1;

// --- SYSTÉMOVÉ INFORMÁCIE ---
bool showSystemInfo = false;
unsigned long lastInfoUpdate = 0;
const unsigned long infoUpdateInterval = 1000;

// Premenné pre sledovanie CPU
unsigned long lastCpuTime = 0;
unsigned long loopStartTime = 0;
float cpuUsage = 0.0;
float avgCpuUsage = 0.0;
int cpuSamples = 0;

// Premenné pre hráčove súradnice
int playerGridX = 0;
int playerGridY = 0;

// --- FARBY BLOKOV ---
#define C_SKY         0x867D
#define C_GRASS 0x2444 
#define C_DIRT 0x8200 
#define C_WOOD 0xA345 
#define C_LEAVES 0x0500 
#define C_COBBLESTONE 0x8410 
#define C_BEDROCK 0x4208 
#define C_COAL 0x0000 
#define C_GOLD 0xF7C7 
#define C_DIAMOND 0x077E 
#define C_IRON 0xB534 
#define C_REDSTONE 0xC800 
#define C_LAPIS 0x01FA

struct BlockMod { int x, y; uint16_t type; };
BlockMod mods[100]; 
int modCount = 0;

// --- MAPA SVETA ---
#define WORLD_MIN_X -500
#define WORLD_MAX_X 500
#define WORLD_MIN_Y -50
#define WORLD_MAX_Y 50
uint16_t world[WORLD_MAX_X - WORLD_MIN_X + 1][WORLD_MAX_Y - WORLD_MIN_Y + 1];

// Funkcia na presný výpočet CPU využitia
void updateCpuUsage() {
  static unsigned long lastSampleTime = 0;
  static unsigned long totalBusyTime = 0;
  static unsigned long sampleCount = 0;
  
  unsigned long now = micros();
  unsigned long loopTime = now - lastCpuTime;
  
  if (lastCpuTime != 0) {
    // Čas strávený v loop() (bez delay)
    unsigned long busyTime = loopTime - 15000; // odčítame delay(15)
    if (busyTime > loopTime) busyTime = 0;
    
    totalBusyTime += busyTime;
    sampleCount++;
    
    // Priemer za posledných 10 vzoriek
    if (sampleCount >= 10) {
      avgCpuUsage = (totalBusyTime * 100.0) / (loopTime * sampleCount);
      if (avgCpuUsage < 0) avgCpuUsage = 0;
      if (avgCpuUsage > 100) avgCpuUsage = 100;
      totalBusyTime = 0;
      sampleCount = 0;
    }
  }
  
  lastCpuTime = now;
}

// Funkcia na získanie presných RAM informácií
void getMemoryInfo(uint32_t& totalHeap, uint32_t& freeHeap, uint32_t& minFreeHeap) {
  multi_heap_info_t heap_info;
  heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
  
  totalHeap = heap_info.total_free_bytes + heap_info.total_allocated_bytes;
  freeHeap = heap_info.total_free_bytes;
  minFreeHeap = heap_info.minimum_free_bytes;
}

// --- GENEROVANIE SVETA ---
void generateWorld() {
  for (int wx = WORLD_MIN_X; wx <= WORLD_MAX_X; wx++) {
    int groundY = 10 + (int)(sin(wx * 0.4) * 3.0);
    int dirtDepth = random(3, 8);
    int stoneStart = groundY + dirtDepth;
    int bedrockLevel = WORLD_MAX_Y;

    for (int wy = WORLD_MIN_Y; wy <= WORLD_MAX_Y; wy++) {
      uint16_t block = C_SKY;
      int depthFromBottom = bedrockLevel - wy;

      if (depthFromBottom == 0) block = C_BEDROCK;
      else if (depthFromBottom == 1) block = (random(100) < 65) ? C_BEDROCK : C_COBBLESTONE;
      else if (depthFromBottom == 2) block = (random(100) < 20) ? C_BEDROCK : C_COBBLESTONE;
      else {
        if (wy >= stoneStart) {
          int r = random(1000);
          if (depthFromBottom <= 10) {
            if (r < 10) block = C_DIAMOND;
            else if (r < 20) block = C_REDSTONE;
            else if (r < 40) block = C_LAPIS;
            else if (r < 70) block = C_GOLD;
            else block = C_COBBLESTONE;
          }
          else if (wy <= stoneStart + (stoneStart - groundY) * 2 / 3) {
            if (r < 30) block = C_IRON;
            else if (r < 80) block = C_COAL;
            else block = C_COBBLESTONE;
          } else {
            if (r < 50) block = C_COAL;
            else block = C_COBBLESTONE;
          }
        } 
        else if (wy > groundY && wy < stoneStart) block = C_DIRT;
        else if (wy == groundY) block = C_GRASS;
      }

      world[wx - WORLD_MIN_X][wy - WORLD_MIN_Y] = block;
    }
  }

  for (int wx = WORLD_MIN_X; wx <= WORLD_MAX_X; wx++) {
    int groundY = 10 + (int)(sin(wx * 0.4) * 3.0);
    if (wx % 10 == 0 && wx != 0 && groundY > 5) {
      for (int i = 1; i <= 3; i++) {
        int trunkY = groundY - i;
        if (trunkY >= WORLD_MIN_Y) world[wx - WORLD_MIN_X][trunkY - WORLD_MIN_Y] = C_WOOD;
      }
      int topOfTrunk = groundY - 3;
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = 1; dy <= 2; dy++) {
          int leafX = wx + dx;
          int leafY = topOfTrunk - dy;
          if (leafX >= WORLD_MIN_X && leafX <= WORLD_MAX_X &&
              leafY >= WORLD_MIN_Y && leafY <= WORLD_MAX_Y) {
            if (world[leafX - WORLD_MIN_X][leafY - WORLD_MIN_Y] == C_SKY)
              world[leafX - WORLD_MIN_X][leafY - WORLD_MIN_Y] = C_LEAVES;
          }
        }
      }
    }
  }
}

// --- GET ORIGINAL BLOCK ---
uint16_t getOriginalBlock(int wx, int wy) {
  if (wx < WORLD_MIN_X || wx > WORLD_MAX_X || wy < WORLD_MIN_Y || wy > WORLD_MAX_Y) return C_SKY;
  return world[wx - WORLD_MIN_X][wy - WORLD_MIN_Y];
}

// --- GET BLOCK S MODS ---
uint16_t getBlock(int wx, int wy) {
  for (int i = 0; i < modCount; i++) {
    if (mods[i].x == wx && mods[i].y == wy) return mods[i].type;
  }
  return getOriginalBlock(wx, wy);
}

// --- KOLÍZIA ---
bool isSolid(int wx, int wy) {
  uint16_t b = getBlock(wx, wy);
  if (b == C_SKY || b == C_LEAVES) return false;
  return (b == C_DIRT || b == C_GRASS || b == C_WOOD || 
          b == C_COBBLESTONE || b == C_BEDROCK ||
          b == C_COAL || b == C_GOLD || b == C_DIAMOND || 
          b == C_IRON || b == C_REDSTONE || b == C_LAPIS);
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
    for (int dy = 0; dy < slotSize; dy++)
      for (int dx = 0; dx < slotSize; dx++)
        if ((dx + dy) % 2 == 0) tft.drawPixel(x + dx, invY + dy, ST77XX_WHITE);
    tft.drawRect(x, invY, slotSize, slotSize, ST77XX_WHITE);
  }
}

// --- SYSTÉMOVÉ INFORMÁCIE ---
void drawSystemInfo() {
  // Aktualizuj CPU využitie
  updateCpuUsage();
  
  // Pozadie pre tabuľku
  tft.fillRect(10, 10, 220, 200, ST77XX_BLACK);
  tft.drawRect(10, 10, 220, 200, ST77XX_WHITE);
  
  // Nadpis
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(20, 15);
  tft.println("SYS INFO (ESP32-S3)");
  
  // Získanie presných RAM informácií
  uint32_t totalHeap, freeHeap, minFreeHeap;
  getMemoryInfo(totalHeap, freeHeap, minFreeHeap);
  
  float heapUsedPercent = 100.0 - ((float)freeHeap / (float)totalHeap * 100.0);
  
  // PSRAM informácie
  bool hasPsram = psramFound();
  size_t totalPsram = 0;
  size_t freePsram = 0;
  float psramUsedPercent = 0.0;
  
  if (hasPsram) {
    totalPsram = ESP.getPsramSize();
    freePsram = ESP.getFreePsram();
    if (totalPsram > 0) {
      psramUsedPercent = 100.0 - ((float)freePsram / (float)totalPsram * 100.0);
    }
  }
  
  uint8_t cpuFreq = getCpuFrequencyMhz();
  
  String wifiStatus = "OFF";
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)";
  }
  
  // Zobrazenie informácií
  int yPos = 35;
  int lineHeight = 12;
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.print("Chip: ESP32-S3");
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("CPU: %.1f%% @ %dMHz", avgCpuUsage, cpuFreq);
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("RAM: %.1f%% used", heapUsedPercent);
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("Heap: %u/%u KB", freeHeap/1024, totalHeap/1024);
  
  if (hasPsram) {
    tft.setCursor(15, yPos); yPos += lineHeight;
    tft.printf("PSRAM: %.1f%% used", psramUsedPercent);
    
    tft.setCursor(15, yPos); yPos += lineHeight;
    if (totalPsram >= 1024*1024) {
      tft.printf("PSRAM: %u/%u MB", freePsram/(1024*1024), totalPsram/(1024*1024));
    } else {
      tft.printf("PSRAM: %u/%u KB", freePsram/1024, totalPsram/1024);
    }
  } else {
    tft.setCursor(15, yPos); yPos += lineHeight;
    tft.print("PSRAM: Not available");
    yPos += lineHeight;
  }
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("Flash: %u MB", ESP.getFlashChipSize()/(1024*1024));
  
  // Súradnice hráča
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("Pos: X=%d Y=%d", playerGridX, playerGridY);
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("World: %.0f,%.0f", playerWorldX, playerWorldY);
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  if (wifiStatus.length() > 15) {
    tft.printf("WiFi: %s", wifiStatus.substring(0, 15).c_str());
  } else {
    tft.printf("WiFi: %s", wifiStatus.c_str());
  }
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("Mods: %d/100", modCount);
  
  tft.setCursor(15, yPos); yPos += lineHeight;
  tft.printf("Uptime: %lu s", millis()/1000);
  
  // Instrukcia
  tft.setCursor(15, 195);
  tft.print("Press 18 to hide");
}

// --- SETUP ---
void setup() {
  pinMode(PIN_BTN_JUMP, INPUT_PULLDOWN);
  pinMode(PIN_SYS_INFO, INPUT_PULLDOWN);
  
  SPI.begin(12, -1, 11, 13);
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(C_SKY);
  generateWorld();
  drawInventory();
  
  // Inicializácia WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
}

void loop() {
  // Začiatok merania CPU
  loopStartTime = micros();
  
  // Kontrola tlačidla pre systémové informácie
  static bool lastButtonState = false;
  bool currentButtonState = digitalRead(PIN_SYS_INFO);
  
  if (currentButtonState && !lastButtonState) {
    showSystemInfo = !showSystemInfo;
    
    if (showSystemInfo) {
      drawSystemInfo();
    } else {
      int curGridX = (int)(scrollX / TILE_SIZE);
      int curGridY = (int)(scrollY / TILE_SIZE);
      for (int i = 0; i < COLS_DISP; i++) {
        for (int j = 0; j < ROWS_DISP; j++) {
          uint16_t blockColor = getBlock(i + curGridX, j + curGridY);
          tft.fillRect(i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE, blockColor);
        }
      }
      drawInventory();
      lastScrollGridX = -1;
      lastScrollGridY = -1;
    }
  }
  lastButtonState = currentButtonState;
  
  if (showSystemInfo) {
    if (millis() - lastInfoUpdate > infoUpdateInterval) {
      drawSystemInfo();
      lastInfoUpdate = millis();
    }
    delay(50);
    return;
  }
  
  // --- PÔVODNÁ HERNÁ LOGIKA ---
  velX = 0;
  keypad.getKeys();
  
  for (int i=0; i<LIST_MAX; i++) {
    if (keypad.key[i].kstate == PRESSED || keypad.key[i].kstate == HOLD) {
      if (keypad.key[i].kchar == '1') velX = -walkSpeed;
      if (keypad.key[i].kchar == '2') velX = walkSpeed;
      if (keypad.key[i].kchar == '4' && keypad.key[i].stateChanged) cursorDir = (cursorDir + 1) % 4;
      if (keypad.key[i].kchar == '3' && keypad.key[i].stateChanged && oldCursorX != -1 && oldCursorY != -1) {
        int curGridX = (int)(scrollX / TILE_SIZE);
        int curGridY = (int)(scrollY / TILE_SIZE);
        uint16_t currentBlock = getBlock(oldCursorX, oldCursorY);
        if (currentBlock == C_BEDROCK) continue;
        if (modCount < 100) mods[modCount++] = {oldCursorX, oldCursorY, C_SKY};
        int dispX = (oldCursorX - curGridX) * TILE_SIZE;
        int dispY = (oldCursorY - curGridY) * TILE_SIZE;
        if (dispX >= 0 && dispX < 240 && dispY >= 0 && dispY < 320) tft.fillRect(dispX, dispY, TILE_SIZE, TILE_SIZE, C_SKY);
        int playerGridX = (int)(playerWorldX / TILE_SIZE);
        int playerGridY = (int)(playerWorldY / TILE_SIZE);
        if (playerGridX == oldCursorX && playerGridY == oldCursorY) velY = 0;
      }
    }
  }

  // --- FYZIKA HRÁČA ---
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

  velY += gravity;
  float nextWorldY = playerWorldY + velY;
  int pGridXL = (int)((playerWorldX + 2) / TILE_SIZE);
  int pGridXR = (int)((playerWorldX + TILE_SIZE - 2) / TILE_SIZE);
  int pGridYF = (int)((nextWorldY + TILE_SIZE) / TILE_SIZE);
  int pGridYT_top = (int)(nextWorldY / TILE_SIZE);
  if (isSolid(pGridXL, pGridYF) || isSolid(pGridXR, pGridYF)) {
      velY = 0; 
      playerWorldY = (pGridYF - 1) * TILE_SIZE;
      if (digitalRead(PIN_BTN_JUMP) == HIGH) velY = jumpForce;
  } else if (isSolid(pGridXL, pGridYT_top) || isSolid(pGridXR, pGridYT_top)) {
      if (velY < 0) {
          velY = 0;
          playerWorldY = (pGridYT_top + 1) * TILE_SIZE;
      }
  } else { 
      playerWorldY = nextWorldY; 
  }

  float targetScrollX = playerWorldX - PLAYER_SCREEN_X;
  float targetScrollY = playerWorldY - PLAYER_SCREEN_Y;
  scrollX = targetScrollX;
  scrollY = targetScrollY;

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

  playerGridX = (int)(playerWorldX / TILE_SIZE);
  playerGridY = (int)(playerWorldY / TILE_SIZE);
  
  tft.fillRect(PLAYER_SCREEN_X, PLAYER_SCREEN_Y, TILE_SIZE, TILE_SIZE, getBlock(playerGridX, playerGridY));
  if (oldCursorX != -1) {
    int oldCsDispX = (oldCursorX - curGridX) * TILE_SIZE;
    int oldCsDispY = (oldCursorY - curGridY) * TILE_SIZE;
    if (oldCsDispX >= 0 && oldCsDispX < 240 && oldCsDispY >= 0 && oldCsDispY < 320) {
      tft.fillRect(oldCsDispX, oldCsDispY, TILE_SIZE, TILE_SIZE, getBlock(oldCursorX, oldCursorY));
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
  if (csDispX >= 0 && csDispX < 240 && csDispY >= 0 && csDispY < 320) tft.drawRect(csDispX, csDispY, TILE_SIZE, TILE_SIZE, ST77XX_WHITE);

  tft.fillRect(PLAYER_SCREEN_X, PLAYER_SCREEN_Y, TILE_SIZE, TILE_SIZE, ST77XX_RED);

  if (playerWorldY < 45 || oldPlayerWorldY < 45) drawInventory();
  oldPlayerWorldY = playerWorldY;
  oldCursorX = newCursorX;
  oldCursorY = newCursorY;

  // Korekcia delay pre presnejšie CPU meranie
  unsigned long loopEndTime = micros();
  unsigned long elapsedTime = loopEndTime - loopStartTime;
  if (elapsedTime < 15000) {
    delayMicroseconds(15000 - elapsedTime);
  }
}