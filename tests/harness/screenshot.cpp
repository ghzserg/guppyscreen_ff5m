#include "screenshot.h"

#include "fake_hw.h"
#include "guppyscreen.h"
#include "hv/json.hpp"

#include <fstream>
#include <mutex>
#include <sstream>
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

void memory(const std::string &path) {
  nlohmann::json data = nlohmann::json::object();
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    std::istringstream field(line);
    std::string name;
    long kb;
    if (field >> name >> kb &&
        (name == "VmRSS:" || name == "VmHWM:" || name == "RssAnon:" ||
         name == "RssFile:" || name == "VmData:")) {
      name.pop_back();
      data[name] = kb;
    }
  }
  std::ifstream rollup("/proc/self/smaps_rollup");
  while (std::getline(rollup, line)) {
    std::istringstream field(line);
    std::string name;
    long kb;
    if (field >> name >> kb &&
        (name == "Pss:" || name == "Anonymous:" || name == "Rss:")) {
      name.pop_back();
      data["smaps_" + name] = kb;
    }
  }
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write memory sample: " + path);
  out << data.dump(2) << '\n';
}

}  // namespace screenshot
