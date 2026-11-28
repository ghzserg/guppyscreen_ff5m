#include "theme.h"

namespace Theme {
const std::array<Color, 6> colors{{
  {"blue",   0x2196F3},
  {"red",    0xF44336},
  {"green",  0x4CAF50},
  {"purple", 0x9C27B0},
  {"pink",   0xE91E63},
  {"yellow", 0xFFEB3B}
}};

lv_style_t accent_bg;
lv_style_t accent_outline;

uint32_t index(const nlohmann::json &value) {
  if (value.is_string()) {
    for (uint32_t i = 0; i < colors.size(); ++i)
      if (value == colors[i].name) return i;
  }
  return 0;
}
}
