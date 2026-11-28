#ifndef __K_THEME_H__
#define __K_THEME_H__

#include "hv/json.hpp"
#include "lvgl/lvgl.h"

#include <array>

namespace Theme {
struct Color {
  const char *name;
  uint32_t primary;
};

extern const std::array<Color, 6> colors;
constexpr uint32_t secondary = 0xF44336;
extern lv_style_t accent_bg;
extern lv_style_t accent_outline;

uint32_t index(const nlohmann::json &value);
}

#endif // __K_THEME_H__
