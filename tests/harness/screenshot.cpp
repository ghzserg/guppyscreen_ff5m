#include "screenshot.h"

#include "fake_hw.h"
#include "guppyscreen.h"

#include <fstream>
#include <mutex>
#include <stdexcept>

namespace screenshot {

void save(const std::string &path) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::ofstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot write screenshot: " + path);
  file << "P6\n" << fake_hw::WIDTH << ' ' << fake_hw::HEIGHT << "\n255\n";
  for (int y = 0; y < fake_hw::HEIGHT; ++y) {
    for (int x = 0; x < fake_hw::WIDTH; ++x) {
      lv_color32_t color;
      color.full = lv_color_to32(fake_hw::pixel(x, y));
      const char rgb[] = {
        static_cast<char>(LV_COLOR_GET_R32(color)),
        static_cast<char>(LV_COLOR_GET_G32(color)),
        static_cast<char>(LV_COLOR_GET_B32(color))
      };
      file.write(rgb, sizeof(rgb));
    }
  }
  if (!file) throw std::runtime_error("incomplete screenshot: " + path);
}

}  // namespace screenshot
