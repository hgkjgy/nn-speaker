#include <Arduino.h>
<<<<<<< HEAD
#include "esp_heap_caps.h"
=======
#include <WiFi.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "I2SMicSampler.h"
#include "ADCSampler.h"
#include "I2SOutput.h"
#include "config.h"
#include "Application.h"
#include "SPIFFS.h"
#include "IntentProcessor.h"
#include "Speaker.h"
#include "IndicatorLight.h"
#include "AudioKitHAL.h"
>>>>>>> 8bd8b83 (feat(firmware): add UART WiFi provisioning with NVS storage)

// 指標先宣告成全域，方便配置後還能存取
uint8_t* internal_buf = nullptr;
uint8_t* dma_buf = nullptr;
uint8_t* psram_buf = nullptr;

// 印出目前記憶體狀態
void printMemoryStatus(const char* title) {
  Serial.println();
  Serial.println("========================================");
  Serial.println(title);
  Serial.println("========================================");

  // 一般 Heap 資訊
  Serial.printf("Heap total size            : %u bytes\n", ESP.getHeapSize());
  Serial.printf("Heap free size             : %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Heap min free size         : %u bytes\n", ESP.getMinFreeHeap());
  Serial.printf("Heap max alloc size        : %u bytes\n", ESP.getMaxAllocHeap());

  Serial.println();

  // 各種 capability 的剩餘空間
  Serial.printf("Free MALLOC_CAP_INTERNAL   : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Free MALLOC_CAP_8BIT       : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_8BIT));
  Serial.printf("Free MALLOC_CAP_DMA        : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_DMA));
  Serial.printf("Free MALLOC_CAP_SPIRAM     : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  Serial.println();

<<<<<<< HEAD
  // 最大可分配連續區塊，拿來觀察 fragmentation
  Serial.printf("Largest block INTERNAL     : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Largest block 8BIT         : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  Serial.printf("Largest block DMA          : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  Serial.printf("Largest block SPIRAM       : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

  Serial.println("========================================");
=======
static Preferences g_wifiPrefs;
static String g_uartInputLine;

static String trimCopy(const String &in)
{
  String out = in;
  out.trim();
  return out;
}

static void loadWifiCredentials(String &ssid, String &password)
{
  g_wifiPrefs.begin("wifi", true);
  ssid = g_wifiPrefs.getString("ssid", WIFI_SSID);
  password = g_wifiPrefs.getString("pass", WIFI_PSWD);
  g_wifiPrefs.end();
}

static bool saveWifiCredentials(const String &ssid, const String &password)
{
  if (ssid.length() == 0)
  {
    return false;
  }

  g_wifiPrefs.begin("wifi", false);
  bool okSsid = g_wifiPrefs.putString("ssid", ssid) > 0;
  bool okPass = g_wifiPrefs.putString("pass", password) >= 0;
  g_wifiPrefs.end();
  return okSsid && okPass;
}

static void clearWifiCredentials()
{
  g_wifiPrefs.begin("wifi", false);
  g_wifiPrefs.clear();
  g_wifiPrefs.end();
}

static void processWifiUartCommand(const String &line)
{
  String command = trimCopy(line);
  if (command.length() == 0)
  {
    return;
  }

  if (command.equalsIgnoreCase("WIFI HELP"))
  {
    Serial.println("UART WiFi commands:");
    Serial.println("  WIFI SET <ssid>|<password>");
    Serial.println("  WIFI SHOW");
    Serial.println("  WIFI CLEAR");
    return;
  }

  if (command.equalsIgnoreCase("WIFI SHOW"))
  {
    String ssid;
    String password;
    loadWifiCredentials(ssid, password);
    Serial.printf("Stored SSID: %s\n", ssid.c_str());
    Serial.printf("Stored password length: %d\n", password.length());
    return;
  }

  if (command.equalsIgnoreCase("WIFI CLEAR"))
  {
    clearWifiCredentials();
    Serial.println("Stored WiFi credentials cleared.");
    return;
  }

  if (command.startsWith("WIFI SET "))
  {
    String payload = command.substring(strlen("WIFI SET "));
    int sep = payload.indexOf('|');
    if (sep <= 0)
    {
      Serial.println("Invalid format. Use: WIFI SET <ssid>|<password>");
      return;
    }

    String ssid = trimCopy(payload.substring(0, sep));
    String password = trimCopy(payload.substring(sep + 1));
    if (ssid.length() == 0)
    {
      Serial.println("SSID cannot be empty.");
      return;
    }

    if (saveWifiCredentials(ssid, password))
    {
      Serial.println("WiFi credentials saved.");
      Serial.println("Reboot to apply.");
    }
    else
    {
      Serial.println("Failed to save WiFi credentials.");
    }
    return;
  }

  Serial.println("Unknown command. Type WIFI HELP");
}

static void handleUartWifiProvisioning(const String &line)
{
  processWifiUartCommand(line);
}

void es8388_init(void)
{
    audiokit::AudioKit kit;
    auto cfg = kit.defaultConfig(audiokit::KitInputOutput);
    cfg.sample_rate = AUDIO_HAL_16K_SAMPLES;
    cfg.i2s_active = false;
    Serial.println("set AudioKit");
    kit.begin(cfg);
    kit.setVolume(100);
>>>>>>> 8bd8b83 (feat(firmware): add UART WiFi provisioning with NVS storage)
}

// 配置不同類型記憶體
void allocateMemory() {
  Serial.println();
  Serial.println("Allocating memory...");

  // Internal SRAM
  internal_buf = (uint8_t*)heap_caps_malloc(16 * 1024, MALLOC_CAP_INTERNAL);

  // DMA memory
  dma_buf = (uint8_t*)heap_caps_malloc(8 * 1024, MALLOC_CAP_DMA);

  // PSRAM
  psram_buf = (uint8_t*)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);

  if (internal_buf != nullptr) {
    Serial.println("[OK] Internal SRAM allocated: 16 KB");
    memset(internal_buf, 0x11, 16 * 1024);
  } else {
    Serial.println("[FAIL] Internal SRAM allocation failed");
  }

  if (dma_buf != nullptr) {
    Serial.println("[OK] DMA memory allocated: 8 KB");
    memset(dma_buf, 0x22, 8 * 1024);
  } else {
    Serial.println("[FAIL] DMA memory allocation failed");
  }

  if (psram_buf != nullptr) {
    Serial.println("[OK] PSRAM allocated: 64 KB");
    memset(psram_buf, 0x33, 64 * 1024);
  } else {
    Serial.println("[FAIL] PSRAM allocation failed");
    Serial.println("Note: Your board may not support PSRAM, or PSRAM is not enabled.");
  }
}

// 可選：釋放記憶體，再觀察一次
void freeMemory() {
  Serial.println();
  Serial.println("Freeing memory...");

  if (internal_buf != nullptr) {
    free(internal_buf);
    internal_buf = nullptr;
    Serial.println("[OK] Freed internal SRAM");
  }

  if (dma_buf != nullptr) {
    free(dma_buf);
    dma_buf = nullptr;
    Serial.println("[OK] Freed DMA memory");
  }

  if (psram_buf != nullptr) {
    free(psram_buf);
    psram_buf = nullptr;
    Serial.println("[OK] Freed PSRAM");
  }
}

void setup() {
  Serial.begin(115200);
<<<<<<< HEAD
  delay(2000);
=======
  delay(1000);
  Serial.println("Starting up");
  Serial.println("UART WiFi provisioning enabled. Type WIFI HELP and press Enter.");
>>>>>>> 8bd8b83 (feat(firmware): add UART WiFi provisioning with NVS storage)

  Serial.println("ESP32 Exercise 2 - Memory Observation and Allocation");

<<<<<<< HEAD
  // Part 1：配置前
  printMemoryStatus("=== Before Allocation ===");
=======
  // start up wifi
  // launch WiFi
  String wifiSsid;
  String wifiPassword;
  loadWifiCredentials(wifiSsid, wifiPassword);
  Serial.printf("Connecting to SSID: %s\n", wifiSsid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    //ESP.restart();
  }
  Serial.printf("Total heap: %d\n", ESP.getHeapSize());
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());
  Serial.printf("Internal heap free: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Internal heap largest block: %u\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Chip model: %s\n", ESP.getChipModel());
>>>>>>> 8bd8b83 (feat(firmware): add UART WiFi provisioning with NVS storage)

  // Part 2：配置不同記憶體
  allocateMemory();

  // Part 3：配置後再觀察
  printMemoryStatus("=== After Allocation ===");

  // 這段不是作業硬性要求，但可以加分
  freeMemory();
  printMemoryStatus("=== After Free ===");
}

<<<<<<< HEAD
void loop() {
  // 不需要重複執行
}
=======
void loop()
{
  while (Serial.available() > 0)
  {
    char ch = static_cast<char>(Serial.read());

    if (ch == '\r' || ch == '\n')
    {
      if (g_uartInputLine.length() > 0)
      {
        Serial.println();
        handleUartWifiProvisioning(g_uartInputLine);
        g_uartInputLine = "";
      }
      continue;
    }

    g_uartInputLine += ch;
    Serial.print(ch);
  }

  vTaskDelay(10);
}
>>>>>>> 8bd8b83 (feat(firmware): add UART WiFi provisioning with NVS storage)
