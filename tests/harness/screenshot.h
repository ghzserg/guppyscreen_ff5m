#ifndef GUPPY_TEST_SCREENSHOT_H
#define GUPPY_TEST_SCREENSHOT_H

#include <string>

namespace screenshot {
// Сохраняет реальный кадровый буфер LVGL в PPM (без новых библиотек).
void save(const std::string &path);
}

#endif
