#include <Arduino.h>
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

// ===== Memory buffers =====
uint8_t* internal_buf = nullptr;
uint8_t* dma_buf = nullptr;
uint8_t* psram_buf = nullptr;

// ===== WiFi UART =====
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

// ===== Audio init =====
void es8388_init(void)
{
  audiokit::AudioKit kit;
  auto cfg = kit.defaultConfig(audiokit::KitInputOutput);
  cfg.sample_rate = AUDIO_HAL_16K_SAMPLES;
  cfg.i2s_active = false;
  Serial.println("set AudioKit");
  kit.begin(cfg);
  kit.setVolume(100);
}

// ===== Memory observation =====
void printMemoryStatus(const char* title)
{
  Serial.println();
  Serial.println("========================================");
  Serial.println(title);
  Serial.println("========================================");

  Serial.printf("Heap total size            : %u bytes\n", ESP.getHeapSize());
  Serial.printf("Heap free size             : %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Heap min free size         : %u bytes\n", ESP.getMinFreeHeap());
  Serial.printf("Heap max alloc size        : %u bytes\n", ESP.getMaxAllocHeap());

  Serial.println();

  Serial.printf("Free MALLOC_CAP_INTERNAL   : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Free MALLOC_CAP_8BIT       : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_8BIT));
  Serial.printf("Free MALLOC_CAP_DMA        : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_DMA));
  Serial.printf("Free MALLOC_CAP_SPIRAM     : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  Serial.println();

  Serial.printf("Largest block INTERNAL     : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Largest block 8BIT         : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  Serial.printf("Largest block DMA          : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  Serial.printf("Largest block SPIRAM       : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

  Serial.println("========================================");
}

void allocateMemory()
{
  Serial.println();
  Serial.println("Allocating memory...");

  internal_buf = (uint8_t*)heap_caps_malloc(16 * 1024, MALLOC_CAP_INTERNAL);
  dma_buf = (uint8_t*)heap_caps_malloc(8 * 1024, MALLOC_CAP_DMA);
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

void freeMemory()
{
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

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("ESP32 Exercise 2 - Memory Observation and Allocation");

  // WiFi startup
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
    // ESP.restart();
  }

  Serial.printf("Total heap: %d\n", ESP.getHeapSize());
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());
  Serial.printf("Internal heap free: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Internal heap largest block: %u\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Chip model: %s\n", ESP.getChipModel());

  printMemoryStatus("=== Before Allocation ===");
  allocateMemory();
  printMemoryStatus("=== After Allocation ===");
  freeMemory();
  printMemoryStatus("=== After Free ===");
}

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