#ifndef GUPPY_TEST_SCREENSHOT_H
#define GUPPY_TEST_SCREENSHOT_H

#include <string>

namespace screenshot {
// Сохраняет кадровый буфер LVGL и данные /proc/self для тестового процесса.
void save(const std::string &path);
void memory(const std::string &path);
}

#endif
