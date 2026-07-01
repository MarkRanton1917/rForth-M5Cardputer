#include "M5Cardputer.h"

extern "C" void app_main()
{
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextColor(GREEN);
  M5Cardputer.Display.setTextDatum(middle_center);
  M5Cardputer.Display.setTextFont(&fonts::FreeSerifBoldItalic18pt7b);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.drawString("Press Any Key", M5Cardputer.Display.width() / 2, M5Cardputer.Display.height() / 2);
  for (;;) {
    M5Cardputer.update();
    // max press 3 button at the same time
    if (M5Cardputer.Keyboard.isChange()) {
      if (M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        String keyStr = "";
        for (auto i : status.word) {
          if (keyStr != "") {
            keyStr = keyStr + "+" + i;
          }
          else {
            keyStr += i;
          }
        }
        M5Cardputer.Display.clear();
        M5Cardputer.Display.drawString(keyStr, M5Cardputer.Display.width() / 2, M5Cardputer.Display.height() / 2);
      }
      else {
        M5Cardputer.Display.clear();
        M5Cardputer.Display.drawString(
          "Press Any Key", M5Cardputer.Display.width() / 2, M5Cardputer.Display.height() / 2);
      }
    }
  }
}