#include "ui.h"

#include "app.h"
#include "fake_hw.h"

#include "guppyscreen.h"

#include "doctest/doctest.h"

#include <functional>
#include <mutex>

namespace ui {

static std::mutex &lv_lock() {
  return GuppyScreen::get()->get_lock();
}

static lv_point_t centre(lv_obj_t *obj) {
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  return {(lv_coord_t)((a.x1 + a.x2) / 2), (lv_coord_t)((a.y1 + a.y2) / 2)};
}

static bool is_self_or_ancestor(lv_obj_t *candidate, lv_obj_t *obj) {
  for (lv_obj_t *o = obj; o != NULL; o = lv_obj_get_parent(o)) {
    if (o == candidate) return true;
  }
  return false;
}

// Would a tap at obj's centre reach obj (or the clickable parent handling it)?
// Uses the same search, in the same layer order, as LVGL's input handling.
static bool shown(lv_obj_t *obj) {
  if (!lv_obj_is_visible(obj)) return false;
  lv_point_t p = centre(obj);
  for (lv_obj_t *root : {lv_layer_sys(), lv_layer_top(), lv_scr_act()}) {
    lv_obj_t *hit = lv_indev_search_obj(root, &p);
    if (hit != NULL) {
      return is_self_or_ancestor(hit, obj);
    }
  }
  return false;
}

static void walk(lv_obj_t *obj, const std::function<void(lv_obj_t *)> &fn) {
  fn(obj);
  uint32_t n = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < n; i++) {
    walk(lv_obj_get_child(obj, i), fn);
  }
}

static void walk_all(const std::function<void(lv_obj_t *)> &fn) {
  lv_obj_update_layout(lv_scr_act());
  for (lv_obj_t *root : {lv_scr_act(), lv_layer_top(), lv_layer_sys()}) {
    walk(root, fn);
  }
}

static bool is_label(lv_obj_t *obj) {
  return lv_obj_check_type(obj, &lv_label_class);
}

static bool matches(const std::string &label, const std::string &text, Match match) {
  return match == Match::Exact ? label == text : label.find(text) != std::string::npos;
}

static lv_obj_t *find_label_locked(const std::string &text, Match match) {
  lv_obj_t *found = NULL;
  walk_all([&](lv_obj_t *obj) {
    if (found == NULL && is_label(obj)
        && matches(lv_label_get_text(obj), text, match) && shown(obj)) {
      found = obj;
    }
  });
  return found;
}

static std::vector<std::string> texts_in_locked(lv_obj_t *root) {
  std::vector<std::string> texts;
  walk(root, [&](lv_obj_t *obj) {
    if (is_label(obj) && shown(obj)) {
      texts.push_back(lv_label_get_text(obj));
    }
  });
  return texts;
}

lv_obj_t *find_label(const std::string &text, Match match) {
  std::lock_guard<std::mutex> lock(lv_lock());
  return find_label_locked(text, match);
}

std::vector<std::string> texts_in(lv_obj_t *root) {
  std::lock_guard<std::mutex> lock(lv_lock());
  lv_obj_update_layout(lv_scr_act());
  return texts_in_locked(root);
}

std::vector<std::string> row_texts(const std::string &text) {
  std::lock_guard<std::mutex> lock(lv_lock());
  lv_obj_t *label = find_label_locked(text, Match::Exact);
  if (label == NULL) return {};
  return texts_in_locked(lv_obj_get_parent(label));
}

lv_obj_t *find_shown(const lv_obj_class_t *cls) {
  std::lock_guard<std::mutex> lock(lv_lock());
  lv_obj_t *found = NULL;
  walk_all([&](lv_obj_t *obj) {
    if (found == NULL && lv_obj_check_type(obj, cls) && shown(obj)) found = obj;
  });
  return found;
}

void tap_at(lv_coord_t x, lv_coord_t y) {
  // LVGL polls the input device every LV_INDEV_DEF_READ_PERIOD (30 ms).
  fake_hw::touch_set(x, y, true);
  app::pump(100);
  fake_hw::touch_set(x, y, false);
  app::pump(100);
}

void tap(lv_obj_t *obj) {
  lv_point_t p;
  {
    std::lock_guard<std::mutex> lock(lv_lock());
    lv_obj_update_layout(lv_scr_act());
    p = centre(obj);
  }
  tap_at(p.x, p.y);
}

void tap_label(const std::string &text, Match match) {
  lv_obj_t *label = find_label(text, match);
  REQUIRE_MESSAGE(label != NULL, "no label \"" << text << "\" on screen; screen shows: " << dump_screen());
  tap(label);
}

std::string dump_screen() {
  std::lock_guard<std::mutex> lock(lv_lock());
  std::vector<std::string> texts;
  walk_all([&](lv_obj_t *obj) {
    if (is_label(obj) && shown(obj)) {
      texts.push_back(lv_label_get_text(obj));
    }
  });
  return join(texts);
}

std::string join(const std::vector<std::string> &items) {
  std::string out;
  for (const auto &s : items) {
    if (!out.empty()) out += " | ";
    out += s;
  }
  return "[" + out + "]";
}

}  // namespace ui
