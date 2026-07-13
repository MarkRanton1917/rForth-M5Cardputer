// Copyright (c) 2026 Vladimir Egorov
// This project is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.
#include "utils.h"

#include <atomic>
#include "rForth.h"
#include "M5Cardputer.h"
#include "M5GFX.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "SPI.h"
#include "SD.h"
#include "Preferences.h"
#include "nvs_flash.h"

#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

#define OHIST_DISP_LENGTH 5
#define OHIST_LENGTH 128
#define IHIST_LENGTH 32
#define MAX_STRING_LENGTH 18
#define X_OFFSET 4
#define Y_OFFSET 8
#define FORTH_BUFFER_SIZE 128

enum class InputMode {
  EDITOR,
  VM
};

static std::atomic<InputMode> mode = InputMode::EDITOR;
static std::atomic<int> pendingSymbol = INPUT_NONE;
static RingList<String, OHIST_LENGTH> ohist;
static RingList<String, IHIST_LENGTH> ihist;
static size_t ihistPos = 0;
static size_t ihistCount = 0;
static KeyQueue forthInput;

static SemaphoreHandle_t forthInitSem = xSemaphoreCreateBinary();
static SemaphoreHandle_t forthDoneSem = xSemaphoreCreateBinary();
static SemaphoreHandle_t displayMux = xSemaphoreCreateMutex();

static void forth_output(int size, const char* msg);
static int forth_input();
static void forth_task(void* ctx);
static void add_ohistory_lines(String input);
static void add_ihistory_lines(String input);
static void print_hist(int offset = 0);
static bool is_graph(const String& str);
static void hw_init();
static void ihist_init();

void mem_stat()
{
  size_t t = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  int64_t p = 1000L * f / t;
  float percent = static_cast<float>(p) * 0.1;
  int core = xPortGetCoreID();
  int freq = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  String out = "rForth on core[" + String(core) + "]\nat " + String(freq) + "MHz,\nRAM " + String(percent) + "% free\n("
    + String(f >> 10) + "/" + String(t >> 10) + " KB)\n";
  forth_output(out.length(), out.c_str());
}

bool forth_include(const char* fname)
{
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    Serial.println("forth_include: SD card failed or not present");
    return false;
  }

  File file = SD.open(fname);
  if (!file) {
    Serial.println("forth_include: failed to open file for reading");
    SD.end();
    return false;
  }

  String cmd;
  while (file.available()) {
    int r = file.read();
    if (r == -1) {
      Serial.println("forth_include: failed to read file");
      file.close();
      SD.end();
      return false;
    }
    cmd += (char)r;
    if (r == '\n') {
      forth_interpret(cmd.c_str(), NULL);
      cmd = "";
    }
  }

  file.close();
  SD.end();
  return true;
}

extern "C" void app_main()
{
  hw_init();
  ihist_init();

  String input;
  String inputIhist;

  M5GFX& display = M5Cardputer.Display;
  Keyboard_Class& keyboard = M5Cardputer.Keyboard;

  display.setTextSize(1);
  display.setFont(&fonts::FreeMono9pt7b);
  display.setTextColor(ORANGE);
  display.setTextScroll(false);

  int step = display.height() / (OHIST_DISP_LENGTH + 1);

  int cursor = 0;
  bool cursorVisiblePrev = false;
  int cursorTime = millis();

  auto print_input = [&](int offset = 0, bool cursorVisible = true) {
    assert(offset >= 0);
    xSemaphoreTake(displayMux, portMAX_DELAY);
    display.fillRect(0, display.height() - step, M5Cardputer.Display.width(), step, BLACK);
    display.setCursor(X_OFFSET, display.height() - step / 2 - Y_OFFSET);
    String p;
    if (offset > 0)
      p = "> " + input.substring(2 + offset);
    else
      p = input;

    if (cursorVisible) {
      if (cursor < input.length() - 2)
        p[cursor + 2 - offset] = '_';
      else
        p += '_';
    }

    display.print(p);
    xSemaphoreGive(displayMux);
  };

  xTaskCreate(forth_task, "ForthTask", 8192, nullptr, 1, nullptr);
  xSemaphoreTake(forthInitSem, portMAX_DELAY);

  mem_stat();
  input = "> ";
  print_input();

  int ohistOffset = 0;
  int ihistOffset = 0;

  for (;;) {
    bool inputMode = (mode.load(std::memory_order_relaxed) == InputMode::EDITOR);
    bool keyboardEventOccured = false;
    M5Cardputer.update();
    if (keyboard.isChange() && keyboard.isPressed()) {
      keyboardEventOccured = true;
      Keyboard_Class::KeysState status = keyboard.keysState();
      if (status.word.size()) {
        char c = status.word.back();
        if (inputMode) {
          if (cursor < input.length() - 2)
            input[cursor + 2] = c;
          else
            input += c;
          cursor++;
          cursorTime = millis();
        }
        else {
          pendingSymbol = (int)c;
        }
      }

      if (status.opt) {
        if (inputMode) {
          int len = input.length();
          int pos = cursor + 2;
          if (len > 2 && pos < len) input = input.substring(0, pos) + " " + input.substring(pos);
          cursorTime = millis();
        }
        else {
          pendingSymbol = INPUT_BREAK;
        }
      }

      if (status.del && inputMode) {
        int len = input.length();
        int pos = cursor + 2;
        if (len > 2 && pos < len) input.remove(pos, 1);
        cursorTime = millis();
      }

      if (status.backspace) {
        if (inputMode) {
          int len = input.length();
          int pos = cursor + 2;
          if (len > 2 && pos <= len) {
            input.remove(pos - 1, 1);
            cursor--;
            cursorTime = millis();
          }
        }
        else {
          pendingSymbol = '\b';
        }
      }

      if (status.up && inputMode) {
        int maxOffset = std::max(0, (int)ohist.size() - OHIST_DISP_LENGTH);
        if (ohistOffset < maxOffset) {
          ++ohistOffset;
          print_hist(ohistOffset);
        }
      }

      if (status.down && inputMode) {
        if (ohistOffset > 0) {
          ohistOffset--;
          print_hist(ohistOffset);
        }
      }

      if (status.left && inputMode) {
        if (cursor > 0) {
          cursor--;
          cursorTime = millis();
        }
      }

      if (status.right && inputMode) {
        if (cursor < input.length() - 2) {
          cursor++;
          cursorTime = millis();
        }
      }

      if (status.f11 && inputMode) {
        if (ihistOffset == 0) inputIhist = input.substring(2);

        if (ihistOffset < ihist.size()) {
          ihistOffset++;
          String cmd = ihist[ihist.size() - ihistOffset];
          input = "> " + cmd;
          cursor = cmd.length();
          cursorTime = millis();
        }
      }

      if (status.f12 && inputMode) {
        String cmd;
        if (ihistOffset > 0) {
          ihistOffset--;
          cmd = ihistOffset ? ihist[ihist.size() - ihistOffset] : inputIhist;
          input = "> " + cmd;
          cursor = cmd.length();
          cursorTime = millis();
        }
      }

      if (status.enter) {
        if (inputMode) {
          ohistOffset = 0;
          ihistOffset = 0;
          String cmd = input.substring(2);
          if (is_graph(cmd)) {
            add_ihistory_lines(cmd);
          }
          add_ohistory_lines(cmd + " ");
          print_hist();
          input = "> ";
          cursor = 0;
          cursorTime = millis();
          print_input();
          cmd += '\n';
          forthInput.load(cmd);
        }
        else {
          pendingSymbol = '\n';
        }
      }
    }

    bool cursorVisible = (millis() - cursorTime) / 1000 % 2 == 0;
    if (inputMode && (keyboardEventOccured || cursorVisiblePrev != cursorVisible)) {
      cursorVisiblePrev = cursorVisible;
      print_input(std::max(0, cursor - MAX_STRING_LENGTH), cursorVisible);
    }
    vTaskDelay(20);
  }
}

static void forth_output(int size, const char* msg)
{
  (void)size;
  add_ohistory_lines(msg);
  print_hist();
}

static int forth_input()
{
  if (forth_waiting_input()) {
    return pendingSymbol.exchange(INPUT_NONE);
  }
  int sym = pendingSymbol.load();
  if (sym == INPUT_BREAK) {
    pendingSymbol.store(INPUT_NONE);
    return INPUT_BREAK;
  }
  return forthInput.pop();
}

static void forth_task(void* ctx)
{
  (void)ctx;
  forth_init();
  xSemaphoreGive(forthInitSem);

  for (;;) {
    if (forthInput.size() > 0) {
      mode.store(InputMode::VM);
      while (forthInput.size() > 0)
        forth_vm(forth_input, forth_output);
      mode.store(InputMode::EDITOR);
    }
    vTaskDelay(10);
  }
}

static void add_ohistory_lines(String input)
{
  input.replace("\r", "");
  if (input.isEmpty()) return;

  bool haveOpenLine = false;
  if (ohist.size() > 0) {
    String& last = ohist.newest();
    haveOpenLine = !last.isEmpty() && last[last.length() - 1] != '\n' && last.length() < MAX_STRING_LENGTH;
  }

  for (int pos = 0; pos < input.length();) {
    char c = input[pos++];

    if (c == '\n') {
      if (haveOpenLine) {
        String& line = ohist.newest();
        if (!line.isEmpty() && line[line.length() - 1] != '\n') line += '\n';
      }
      haveOpenLine = false;
      continue;
    }

    if (c == '\b') {
      while (ohist.size() > 0) {
        String& line = ohist.newest();
        if (!line.isEmpty() && line[line.length() - 1] != '\n') {
          line.remove(line.length() - 1);
          haveOpenLine = true;
          break;
        }
        if (line.isEmpty() && ohist.size() > 1) {
          ohist.pull();
          continue;
        }
        break;
      }
      continue;
    }

    if (!haveOpenLine) {
      ohist.push("");
      haveOpenLine = true;
    }

    String& line = ohist.newest();
    line += c;

    if (line.length() >= MAX_STRING_LENGTH) {
      haveOpenLine = false;
    }
  }
}

static void add_ihistory_lines(String input)
{
  ihist.push(input);
  Preferences pref;
  pref.begin("ihist", false);
  String key = "h" + String(ihistPos);
  pref.putString(key.c_str(), input);

  ihistPos++;
  if (ihistPos >= IHIST_LENGTH) ihistPos = 0;
  if (ihistCount < IHIST_LENGTH) ihistCount++;

  pref.putUInt("ihistPos", ihistPos);
  pref.putUInt("ihistCount", ihistCount);
  pref.end();
}

static void print_hist(int offset)
{
  assert(offset >= 0);
  assert(ohist.size());
  assert(ohist.size() >= offset);
  xSemaphoreTake(displayMux, portMAX_DELAY);
  M5GFX& display = M5Cardputer.Display;
  int step = display.height() / (OHIST_DISP_LENGTH + 1);
  int pos = 1;

  display.fillRect(0, 0, display.width(), display.height() - step / 2 - Y_OFFSET, BLACK);

  int end = ohist.size() - offset;
  int begin = end - OHIST_DISP_LENGTH;
  if (begin < 0) begin = 0;

  for (int i = begin; i < end; i++) {
    display.setCursor(X_OFFSET, step * pos++ - step / 2 - Y_OFFSET);
    display.print(ohist[i]);
  }
  xSemaphoreGive(displayMux);
}
static bool is_graph(const String& str)
{
  for (int i = 0; i < str.length(); i++) {
    if (isGraph(str[i])) return true;
  }
  return false;
}

static void hw_init()
{
  Serial.begin(115200);

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
}

static void ihist_init()
{
  Preferences pref;
  pref.begin("ihist", true);
  ihistPos = pref.getUInt("ihistPos", 0);
  ihistCount = pref.getUInt("ihistCount", 0);

  if (ihistCount > IHIST_LENGTH) ihistCount = IHIST_LENGTH;

  size_t start;

  if (ihistCount == IHIST_LENGTH)
    start = ihistPos;
  else
    start = 0;

  for (size_t i = 0; i < ihistCount; i++) {
    size_t index = (start + i) % IHIST_LENGTH;
    String key = "h" + String(index);
    String cmd = pref.getString(key.c_str(), "");
    if (cmd.length()) ihist.push(cmd);
  }
  pref.end();
}
