// Панели стоят друг на друге (Nav поднимает нужную наверх, остальные не
// скрываются). Изменения в закрытой панели не должны перерисовывать экран,
// а когда её снова открывают, она должна быть нарисована с новыми значениями.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <mutex>
#include <vector>

namespace {

// Области, помеченные для перерисовки вне самой отрисовки (через rounder_cb).
std::vector<lv_area_t> invalidated;

void record(lv_disp_drv_t *, lv_area_t *area) {
  lv_disp_t *refreshing = _lv_refr_get_disp_refreshing();
  if (refreshing == NULL || !refreshing->rendering_in_progress) invalidated.push_back(*area);
}

// Пиксели области на экране, как их последний раз вывел LVGL.
std::vector<uint32_t> pixels(const lv_area_t &a) {
  std::vector<uint32_t> px;
  for (int y = a.y1; y <= a.y2; y++)
    for (int x = a.x1; x <= a.x2; x++) px.push_back(lv_color_to32(fake_hw::pixel(x, y)));
  return px;
}

}  // namespace

TEST_CASE("a change under a covering screen is not redrawn until the screen is shown again") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  moonraker.push_status({{"heater_bed", {{"temperature", 47.0}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("47") != nullptr; }));
  lv_obj_t *label = ui::find_label("47");
  const lv_area_t area = ui::coords(label);
  app::pump(100);
  const auto before = pixels(area);

  ui::tap_tab(Nav::PRINT);  // список файлов закрывает Home
  app::pump(100);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_disp_get_default()->driver->rounder_cb = record;
    invalidated.clear();
  }
  moonraker.push_status({{"heater_bed", {{"temperature", 153.0}}}});
  app::settle(moonraker);
  app::pump(300);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(std::string(lv_label_get_text(label)) == "153");  // значение обновлено
    lv_area_t common;
    for (const lv_area_t &a : invalidated) {
      CHECK_MESSAGE(!_lv_area_intersect(&common, &a, &area),
                    "redrawn " << a.x1 << "," << a.y1 << ".." << a.x2 << "," << a.y2);
    }
    lv_disp_get_default()->driver->rounder_cb = NULL;
  }

  ui::tap_tab(Nav::HOME);
  app::pump(100);
  CHECK(pixels(area) != before);  // нарисовано новое значение
}
