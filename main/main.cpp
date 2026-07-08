#include "rForth.h"
#include "M5Cardputer.h"
#include "M5GFX.h"

#define HIST_DISP_LENGTH 5
#define HIST_LENGTH 128
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

void add_lines(const String& input)
{
  int start = 0;
  int end = input.indexOf('\n');
  while (end != -1) {
    String line = input.substring(start, end);
    if (line.length() > 0) {
      for (int i = 0; i < HIST_LENGTH - 1; i++) {
        history[i] = history[i + 1];
      }
      history[HIST_LENGTH - 1] = line;
    }
    start = end + 1;
    end = input.indexOf('\n', start);
  }

  if (start < input.length()) {
    String line = input.substring(start);
    if (line.length() > 0) {
      for (int i = 0; i < HIST_LENGTH - 1; i++) {
        history[i] = history[i + 1];
      }
      history[HIST_LENGTH - 1] = line;
    }
  }
}

extern "C" void app_main()
{
  forth_init();

  String input;
  for (auto& t : history) {
    t.reserve(24);
  }
  input.reserve(24);
  output.reserve(256);

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  M5GFX& display = M5Cardputer.Display;
  Keyboard_Class& keyboard = M5Cardputer.Keyboard;

  display.setTextSize(1);
  display.setFont(&fonts::FreeMono9pt7b);
  display.setTextColor(ORANGE);

  int step = display.height() / HIST_DISP_LENGTH;

  input = "> ";
  display.setCursor(X_OFFSET, display.height() - step / 2 - Y_OFFSET);
  display.print(input);

  mem_stat();
  display.setCursor(X_OFFSET, step / 2 - Y_OFFSET);
  display.print(output);
  output = "";

  for (;;) {
    M5Cardputer.update();
    if (keyboard.isChange()) {
      if (keyboard.isPressed()) {
        Keyboard_Class::KeysState status = keyboard.keysState();
        for (auto i : status.word) {
          input += i;
        }

        if (status.del) {
          input.remove(input.length() - 1);
        }

        if (status.enter) {
          input = input.substring(2);

          forth_vm(input.c_str(), forth_output);
          input += output;

          add_lines(input);

          input = "> ";
          M5Cardputer.Display.fillRect(0, 0, display.width(), display.height(), BLACK);
          int pos = 1;
          for (int i = HIST_LENGTH - HIST_DISP_LENGTH + 1; i < HIST_LENGTH; i++) {
            display.setCursor(X_OFFSET, step * pos++ - step / 2 - Y_OFFSET);
            M5Cardputer.Display.print(history[i]);
          }
          output = "";
        }

        M5Cardputer.Display.fillRect(0, display.height() - step, M5Cardputer.Display.width(), step, BLACK);
        display.setCursor(X_OFFSET, display.height() - step / 2 - Y_OFFSET);
        display.print(input);
      }
    }
    vTaskDelay(1);
  }
}
