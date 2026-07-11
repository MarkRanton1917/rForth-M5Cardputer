// Copyright (c) 2026 Vladimir Egorov
// This project is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.

#include "rForth.h"
#include "M5Cardputer.h"
#include "M5GFX.h"
#include "SPI.h"
#include "SD.h"

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

String ohistory[OHIST_LENGTH];
String ihistory[IHIST_LENGTH];

static void add_ohistory_lines(String input);
static void add_ihistory_lines(String input);
static void print_hist(int offset = 0);
static void hw_init();

void forth_output(int size, const char* msg)
{
  (void)size;
  add_ohistory_lines(msg);
  print_hist();
}

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

  auto dumb = [](int, const char*) {};

  String cmd;
  while (file.available()) {
    int r = file.read();
    if (r == -1) {
      Serial.println("forth_include: failed to read file");
      file.close();
      SD.end();
      return false;
    }
    if (r == '\n') {
      forth_vm(cmd.c_str(), dumb);
      cmd = "";
    }
    else
      cmd += (char)r;
  }

  file.close();
  SD.end();
  return true;
}

extern "C" void app_main()
{
  hw_init();
  forth_init();

  String input;
  String inputIhist;

  M5GFX& display = M5Cardputer.Display;
  Keyboard_Class& keyboard = M5Cardputer.Keyboard;

  display.setTextSize(1);
  display.setFont(&fonts::FreeMono9pt7b);
  display.setTextColor(ORANGE);
  display.setTextScroll(false);

  int step = display.height() / OHIST_DISP_LENGTH;

  int cursor = 0;
  bool cursorVisiblePrev = false;
  int cursorTime = millis();

  auto print_input = [&](int offset = 0, bool cursorVisible = true) {
    assert(offset >= 0);
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
  };

  mem_stat();
  input = "> ";
  print_input();

  int ohistOffset = 0;
  int ihistOffset = 0;
  int ihistMax = 0;
  for (;;) {
    bool keyboardEventOccured = false;
    M5Cardputer.update();
    if (keyboard.isChange() && keyboard.isPressed()) {
      keyboardEventOccured = true;
      Keyboard_Class::KeysState status = keyboard.keysState();
      for (auto i : status.word) {
        if (cursor < input.length() - 2)
          input[cursor + 2] = i;
        else
          input += i;
        cursor++;
        cursorTime = millis();
      }

      if (status.opt) {
        int len = input.length();
        int pos = cursor + 2;
        if (len > 2 && pos < len) input = input.substring(0, pos) + " " + input.substring(pos);
        cursorTime = millis();
      }

      if (status.del) {
        int len = input.length();
        int pos = cursor + 2;
        if (len > 2 && pos < len) input.remove(pos, 1);
        cursorTime = millis();
      }

      if (status.backspace) {
        int len = input.length();
        int pos = cursor + 2;
        if (len > 2 && pos <= len) {
          input.remove(pos - 1, 1);
          cursor--;
          cursorTime = millis();
        }
      }

      if (status.up) {
        if (ohistOffset < OHIST_LENGTH - OHIST_DISP_LENGTH) {
          ohistOffset++;
          print_hist(ohistOffset);
        }
      }

      if (status.down) {
        if (ohistOffset > 0) {
          ohistOffset--;
          print_hist(ohistOffset);
        }
      }

      if (status.left) {
        if (cursor > 0) {
          cursor--;
          cursorTime = millis();
        }
      }

      if (status.right) {
        if (cursor < input.length() - 2) {
          cursor++;
          cursorTime = millis();
        }
      }

      if (status.f11) {
        if (ihistOffset == 0) inputIhist = input.substring(2);
        if (ihistOffset < IHIST_LENGTH - 1 && ihistOffset < ihistMax) {
          ihistOffset++;
          String cmd = ihistory[IHIST_LENGTH - ihistOffset];
          input = "> " + cmd;
          cursor = cmd.length();
          cursorTime = millis();
        }
      }

      if (status.f12) {
        String cmd;
        if (ihistOffset != 0) {
          ihistOffset--;
          cmd = ihistOffset ? ihistory[IHIST_LENGTH - ihistOffset] : inputIhist;
          input = "> " + cmd;
          cursor = cmd.length();
          cursorTime = millis();
        }
      }

      if (status.enter) {
        ohistOffset = 0;
        ihistOffset = 0;

        String cmd = input.substring(2);
        for (int i = 0; i < cmd.length(); i++) {
          if (isGraph(cmd.charAt(i))) {
            ihistMax++;
            add_ihistory_lines(cmd);
            break;
          }
        }
        add_ohistory_lines(cmd + " ");

        forth_vm(cmd.c_str(), forth_output);
        print_hist();

        input = "> ";
        cursor = 0;
        cursorTime = millis();
        print_input();
      }
    }

    bool cursorVisible = (millis() - cursorTime) / 1000 % 2 == 0;
    if (keyboardEventOccured || cursorVisiblePrev != cursorVisible) {
      cursorVisiblePrev = cursorVisible;
      int inputOffset = cursor - MAX_STRING_LENGTH;
      inputOffset = inputOffset > 0 ? inputOffset : 0;
      print_input(inputOffset, cursorVisible);
    }
    vTaskDelay(100);
  }
}

static void add_ohistory_lines(String input)
{
  input.replace("\r", "");
  int iLen = input.length();

  auto append = [&](String& target, int& pos) {
    int tLen = target.length();
    while (pos < iLen && tLen < MAX_STRING_LENGTH) {
      char c = input[pos];
      if (c == '\n') {
        target += '\n';
        pos++;
        return;
      }
      target += c;
      pos++;
      tLen++;
    }
  };

  int pos = 0;

  String& last = ohistory[OHIST_LENGTH - 1];
  if (last.length() > 0 && last[last.length() - 1] != '\n' && last.length() < MAX_STRING_LENGTH) {
    append(last, pos);
  }

  while (pos < iLen) {
    String chunk;
    append(chunk, pos);
    int cLen = chunk.length();
    if (cLen > 0) {
      for (int i = 0; i < OHIST_LENGTH - 1; i++) {
        ohistory[i] = ohistory[i + 1];
      }
      ohistory[OHIST_LENGTH - 1] = chunk;
    }
    if (cLen == MAX_STRING_LENGTH && input[pos] == '\n') pos++;
  }
}

static void add_ihistory_lines(String input)
{
  for (int i = 0; i < IHIST_LENGTH - 1; i++) {
    ihistory[i] = ihistory[i + 1];
  }
  ihistory[IHIST_LENGTH - 1] = input;
}

static void print_hist(int offset)
{
  assert(offset >= 0);
  M5GFX& display = M5Cardputer.Display;
  int step = display.height() / OHIST_DISP_LENGTH;
  int pos = 1;
  display.fillRect(0, 0, display.width(), display.height() - step / 2 - Y_OFFSET, BLACK);
  for (int i = OHIST_LENGTH - OHIST_DISP_LENGTH + 1 - offset; i < OHIST_LENGTH - offset; i++) {
    display.setCursor(X_OFFSET, step * pos++ - step / 2 - Y_OFFSET);
    display.print(ohistory[i]);
  }
};

static void hw_init()
{
  Serial.begin(115200);

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
}
