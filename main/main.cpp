#include "rForth.h"
#include "M5Cardputer.h"
#include "M5GFX.h"

#define HIST_DISP_LENGTH 5
#define HIST_LENGTH 128
#define MAX_STRING_LENGTH 18
#define X_OFFSET 4
#define Y_OFFSET 8
#define FORTH_BUFFER_SIZE 128

String output;
String history[HIST_LENGTH];

void forth_output(int size, const char* msg)
{
  output += msg;
}

void mem_stat()
{
  size_t t = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  int64_t p = 1000L * f / t;
  float percent = static_cast<float>(p) * 0.1;
  int core = xPortGetCoreID();
  int freq = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  output += "rForth on core[" + String(core) + "]\nat " + String(freq) + "MHz,\nRAM " + String(percent) + "% free\n("
    + String(f >> 10) + "/" + String(t >> 10) + " KB)";
}

bool forth_include(const char* fname)
{
  return false;
}

static void add_history_lines(const String& input)
{
  int len = input.length();
  int pos = 0;

  while (pos < len) {
    String chunk;
    int count = 0;

    while (input[pos] == '\n')
      pos++;

    while (pos < len && count < MAX_STRING_LENGTH) {
      char c = input[pos];
      if (c == '\r') {
        pos++;
        continue;
      }
      if (c == '\n') break;
      chunk += c;
      count++;
      pos++;
    }

    if (chunk.length() > 0) {
      for (int i = 0; i < HIST_LENGTH - 1; i++) {
        history[i] = history[i + 1];
      }
      history[HIST_LENGTH - 1] = chunk;
    }
  }
}

extern "C" void app_main()
{
  forth_init();

  String input;

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  M5GFX& display = M5Cardputer.Display;
  Keyboard_Class& keyboard = M5Cardputer.Keyboard;

  display.setTextSize(1);
  display.setFont(&fonts::FreeMono9pt7b);
  display.setTextColor(ORANGE);

  int step = display.height() / HIST_DISP_LENGTH;

  auto print_hist = [&](int offset = 0) {
    assert(offset >= 0);
    int pos = 1;
    display.fillRect(0, 0, display.width(), display.height() - step / 2 - Y_OFFSET, BLACK);
    for (int i = HIST_LENGTH - HIST_DISP_LENGTH + 1 - offset; i < HIST_LENGTH - offset; i++) {
      display.setCursor(X_OFFSET, step * pos++ - step / 2 - Y_OFFSET);
      display.print(history[i]);
    }
  };

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
  display.setCursor(X_OFFSET, step / 2 - Y_OFFSET);
  add_history_lines(output);
  print_hist();
  output = "";

  input = "> ";
  print_input();

  int histOffset = 0;

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
        if (histOffset < HIST_LENGTH - HIST_DISP_LENGTH) {
          histOffset++;
          print_hist(histOffset);
        }
      }

      if (status.down) {
        if (histOffset > 0) {
          histOffset--;
          print_hist(histOffset);
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

      if (status.enter) {
        histOffset = 0;
        input = input.substring(2);

        forth_vm(input.c_str(), forth_output);
        input += " " + output;

        add_history_lines(input);
        print_hist();
        output = "";
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
