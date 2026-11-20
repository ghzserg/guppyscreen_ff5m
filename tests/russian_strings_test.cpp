// Русский текст в строках кода показывается на всех языках: он должен быть
// переводом msgid, а не самой строкой.

#include "doctest/doctest.h"

#include "app.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

// Строковые литералы файла `path` с кириллицей, комментарии пропущены.
std::vector<std::string> russian_literals(const std::string &path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string s = ss.str();
  std::vector<std::string> found;
  for (size_t i = 0; i < s.size(); i++) {
    if (s.compare(i, 2, "//") == 0) {
      i = s.find('\n', i);
      if (i == std::string::npos) break;
    } else if (s.compare(i, 2, "/*") == 0) {
      i = s.find("*/", i + 2);
      if (i == std::string::npos) break;
      i++;
    } else if (s[i] == '\'' && i + 2 < s.size()) {
      i += s[i + 1] == '\\' ? 3 : 2;  // '"', '\''
    } else if (s[i] == '"') {
      size_t j;
      if (i > 0 && s[i - 1] == 'R') {  // R"delim(...)delim"
        size_t open = s.find('(', i);
        std::string end = ")" + s.substr(i + 1, open - i - 1) + "\"";
        j = s.find(end, open);
        if (j == std::string::npos) break;
        j += end.size() - 1;
      } else {
        for (j = i + 1; j < s.size() && s[j] != '"'; j += s[j] == '\\' ? 2 : 1) {}
      }
      // Кириллица в UTF-8 начинается с байта 0xD0 или 0xD1.
      std::string lit = s.substr(i, j - i + 1);
      if (lit.find('\xD0') != std::string::npos || lit.find('\xD1') != std::string::npos) found.push_back(lit);
      i = j;
    }
  }
  return found;
}

}  // namespace

TEST_CASE("no Russian text in the sources' strings outside translations") {
  namespace fs = std::filesystem;
  fs::path src = app::fixture("../../src");
  int files = 0;
  for (const auto &e : fs::directory_iterator(src)) {
    std::string ext = e.path().extension();
    // prompt_panel.cpp узнаёт сообщения макросов Klipper по их русскому тексту.
    if ((ext != ".cpp" && ext != ".h") || e.path().filename() == "prompt_panel.cpp") continue;
    files++;
    for (const auto &lit : russian_literals(e.path())) {
      std::string where = e.path().filename().string() + ": " + lit;
      FAIL_CHECK(where);
    }
  }
  CHECK(files > 50);
}
