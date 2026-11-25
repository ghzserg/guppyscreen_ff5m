// How Home's temperature chart is redrawn when new points come in.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <mutex>
#include <vector>

namespace {

// Areas invalidated outside rendering, collected through the display's rounder_cb.
std::vector<lv_area_t> invalidated;

void record(lv_disp_drv_t *, lv_area_t *area) {
  lv_disp_t *refreshing = _lv_refr_get_disp_refreshing();
  if (refreshing == NULL || !refreshing->rendering_in_progress) invalidated.push_back(*area);
}

// Whether any of `chart`'s lines has a point at `value`.
bool has_point(lv_obj_t *chart, lv_coord_t value) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s != NULL; s = lv_chart_get_series_next(chart, s)) {
    lv_coord_t *y = lv_chart_get_y_array(chart, s);
    for (uint16_t i = 0; i < lv_chart_get_point_count(chart); i++) {
      if (y[i] == value) return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("new chart points redraw only the chart and once for all sensors") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  lv_obj_t *chart = ui::find_shown(&lv_chart_class);
  REQUIRE(chart != nullptr);
  app::pump(2100);  // a sensor takes a point at most every 2 s

  lv_area_t chart_area = ui::coords(chart);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_disp_get_default()->driver->rounder_cb = record;
    invalidated.clear();
  }
  // In step with the 2 s redraws: a point, its redraw, then one more period, so
  // that the next redraw is 2 s away and the extruder takes a point again.
  moonraker.push_status({{"extruder", {{"temperature", 90.0}}}});
  REQUIRE(app::wait_until([&] {  // not the tile: the chart
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_area_t common;
    for (const lv_area_t &a : invalidated) {
      if (_lv_area_intersect(&common, &a, &chart_area)) return true;
    }
    return false;
  }));
  app::pump(2100);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    invalidated.clear();
  }
  // Each sensor in its own notification, as Moonraker sends them when only one changes.
  moonraker.push_status({{"extruder", {{"temperature", 100.0}}}});
  moonraker.push_status({{"heater_bed", {{"temperature", 50.0}}}});
  app::settle(moonraker);
  // Not in the chart before it is redrawn: a partial redraw (a popup closing) would
  // show the shifted lines in one part of the chart and the old ones in the rest.
  CHECK(!has_point(chart, 100));
  app::pump(2100);  // the chart is redrawn within 2 s
  CHECK(has_point(chart, 100));

  std::vector<lv_area_t> on_chart;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_disp_get_default()->driver->rounder_cb = NULL;
    for (const lv_area_t &a : invalidated) {
      lv_area_t common;
      if (_lv_area_intersect(&common, &a, &chart_area)) on_chart.push_back(a);
    }
  }
  REQUIRE(!on_chart.empty());
  CHECK(on_chart.size() == 1);  // both points in one redraw
  for (const lv_area_t &a : on_chart) {
    INFO("invalidated " << a.x1 << "," << a.y1 << " - " << a.x2 << "," << a.y2 << ", chart " << chart_area.x1 << ","
                        << chart_area.y1 << " - " << chart_area.x2 << "," << chart_area.y2);
    CHECK(_lv_area_is_in(&a, &chart_area, 0));  // not the margins for the axis labels around it
  }
}
