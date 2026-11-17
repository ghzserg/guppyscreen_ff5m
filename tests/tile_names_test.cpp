// Имена датчиков на плитках Home помещаются целиком: шрифт по умолчанию
// в релизной сборке (без патча lvgl 0003) 20 px, плитка рассчитана на 16 px.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <mutex>
#include <string>
#include <vector>

namespace {

void check_names_fit(const char *fixture, const json &patch, const std::vector<std::string> &names) {
  FakeMoonraker moonraker(app::fixture(fixture));
  app::start(moonraker, patch);
  app::wait_ready(moonraker);
  for (const auto &name : names) {
    lv_obj_t *label = ui::find_label(name);
    REQUIRE_MESSAGE(label != nullptr, name << " not shown: " << ui::dump_screen());
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_update_layout(lv_scr_act());
    lv_point_t size;
    lv_txt_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, 0),
                    lv_obj_get_style_text_letter_space(label, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    INFO(name << ": text " << size.x << " px, label " << lv_obj_get_content_width(label) << " px");
    CHECK(size.x <= lv_obj_get_content_width(label));
  }
}

}  // namespace

TEST_CASE("AD5X sensor names fit their tiles") {
  check_names_fit("ad5x_basic.json", app::zmod_sensors("guppyconfig_ad5x.json", "AD5X"),
                  {"Extruder", "Bed", "Head", "Weight"});
}

TEST_CASE("C5 Pro sensor names fit their tiles") {
  check_names_fit("c5pro_basic.json", app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"),
                  {"T0", "T1", "T2", "T3", "Bed", "Chamber"});
}
