// Reconnecting to Moonraker (e.g. after a Moonraker restart) rebuilds the
// printer-dependent widgets. Repeated reconnects must not keep adding
// widgets or memory.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <cstring>
#include <functional>
#include <mutex>

#ifdef __SANITIZE_ADDRESS__
extern "C" size_t __sanitizer_get_current_allocated_bytes(void);  // ASan runtime
#endif

namespace {

size_t count_objs(lv_obj_t *obj) {
  size_t n = 1;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) n += count_objs(lv_obj_get_child(obj, i));
  return n;
}

// All LVGL objects on every screen and layer.
size_t lvgl_objects() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_disp_t *d = lv_disp_get_default();
  size_t n = 0;
  for (uint32_t i = 0; i < d->screen_cnt; i++) n += count_objs(d->screens[i]);
  n += count_objs(lv_layer_top()) + count_objs(lv_layer_sys());
  return n;
}

// Rows of the console's command list (history + Klipper's G-code help):
// the table that lists G28.
uint16_t console_rows() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_disp_t *d = lv_disp_get_default();
  uint16_t rows = 0;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_table_class)) {
      uint16_t n = lv_table_get_row_cnt(o);
      for (uint16_t r = 0; r < n; r++) {
        const char *v = lv_table_get_cell_value(o, r, 0);
        if (v && strcmp(v, "G28") == 0) rows = n;
      }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  for (uint32_t i = 0; i < d->screen_cnt; i++) walk(d->screens[i]);
  return rows;
}

void reconnect(FakeMoonraker &moonraker) {
  moonraker.forget_requests();  // the fake's request log would grow, not the app
  moonraker.drop_client();
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.objects.subscribe") > 0; }, 10000));
  app::wait_until([] { return ui::find_label("Waiting for the Klipper launch", ui::Match::Contains) == nullptr; });
  app::settle(moonraker);  // let the app finish what the reconnect started
}

}  // namespace

TEST_CASE("reconnects do not accumulate widgets or memory") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  // The fixture's printer.gcode.help has 4 commands; no console history.
  CHECK(console_rows() == 4);

  // The first reconnects may allocate lazily created things once.
  reconnect(moonraker);
  reconnect(moonraker);
  moonraker.forget_requests();
  size_t objs0 = lvgl_objects();
#ifdef __SANITIZE_ADDRESS__
  // Exact live bytes; malloc's own statistics also count its caches.
  size_t heap0 = __sanitizer_get_current_allocated_bytes();
#endif

  const int N = 5;
  for (int i = 0; i < N; i++) reconnect(moonraker);
  moonraker.forget_requests();

  CHECK(lvgl_objects() == objs0);
  CHECK(console_rows() == 4);
#ifdef __SANITIZE_ADDRESS__
  long growth = (long)__sanitizer_get_current_allocated_bytes() - (long)heap0;
  MESSAGE("live heap growth over " << N << " reconnects: " << growth << " bytes");
  CHECK(growth < 1024);
#endif

  // Still working after all that.
  ui::tap_label("Homing");
  CHECK(app::wait_until([] { return ui::find_label("G28") != nullptr; }));
}
