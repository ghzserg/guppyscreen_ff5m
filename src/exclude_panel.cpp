#include "exclude_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <vector>

LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(back);

namespace {

struct ExObj {
  std::string name;
  std::vector<std::pair<float, float>> poly;
  bool excluded = false;
};

std::vector<ExObj> objs;
// The canvas buffer (map_w x map_h, 32-bit: about 900 KB) exists only while
// the panel is open and shows the map. Otherwise the canvas points at a
// single static pixel.
std::vector<uint8_t> map_buf;
lv_color_t idle_px[1];
lv_obj_t *map_cont = NULL;
lv_obj_t *zoom_in_btn = NULL;
lv_obj_t *zoom_out_btn = NULL;
KWebSocketClient *ws_ptr = NULL;
bool active = false;
bool has_polygons = false;
float zoom = 1.0f;
float pan_x = 0, pan_y = 0;
lv_obj_t *pan_btn[4] = {NULL, NULL, NULL, NULL};
float min_x = 0, min_y = 0, max_x = 1, max_y = 1;
int map_w = 0, map_h = 0;

void compute_bounds() {
  State *s = State::get_instance();
  auto mn = s->get_data("/printer_state/toolhead/axis_minimum"_json_pointer);
  auto mx = s->get_data("/printer_state/toolhead/axis_maximum"_json_pointer);
  if (mn.is_array() && mx.is_array() && mn.size() >= 2 && mx.size() >= 2) {
    min_x = mn[0].get<float>(); min_y = mn[1].get<float>();
    max_x = mx[0].get<float>(); max_y = mx[1].get<float>();
    return;
  }
  bool first = true;
  for (const auto &o : objs)
    for (const auto &p : o.poly) {
      if (first) { min_x = max_x = p.first; min_y = max_y = p.second; first = false; }
      min_x = std::min(min_x, p.first); max_x = std::max(max_x, p.first);
      min_y = std::min(min_y, p.second); max_y = std::max(max_y, p.second);
    }
  if (max_x - min_x < 1) max_x = min_x + 1;
  if (max_y - min_y < 1) max_y = min_y + 1;
}

void to_px(float x, float y, lv_point_t &p) {
  float base = std::min(map_w / (max_x - min_x), map_h / (max_y - min_y)) * 0.95f;
  float s = base * zoom;
  float cx = (min_x + max_x) / 2, cy = (min_y + max_y) / 2;
  p.x = (lv_coord_t)(map_w / 2 + pan_x + (x - cx) * s);
  p.y = (lv_coord_t)(map_h / 2 + pan_y - (y - cy) * s);
}

void from_px(float px, float py, float &x, float &y) {
  float base = std::min(map_w / (max_x - min_x), map_h / (max_y - min_y)) * 0.95f;
  float s = base * zoom;
  float cx = (min_x + max_x) / 2, cy = (min_y + max_y) / 2;
  x = cx + (px - map_w / 2 - pan_x) / s;
  y = cy - (py - map_h / 2 - pan_y) / s;
}

bool point_in_poly(float x, float y, const std::vector<std::pair<float, float>> &poly) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    if ((poly[i].second > y) != (poly[j].second > y) &&
        x < (poly[j].first - poly[i].first) * (y - poly[i].second) / (poly[j].second - poly[i].second) + poly[i].first)
      in = !in;
  }
  return in;
}

void map_alloc() {
  if (!map_cont || !map_buf.empty()) return;
  map_buf.assign(LV_CANVAS_BUF_SIZE_TRUE_COLOR(map_w, map_h), 0);
  lv_canvas_set_buffer(map_cont, map_buf.data(), map_w, map_h, LV_IMG_CF_TRUE_COLOR);
}

void map_free() {
  if (map_cont) lv_canvas_set_buffer(map_cont, idle_px, 1, 1, LV_IMG_CF_TRUE_COLOR);
  std::vector<uint8_t>().swap(map_buf);  // clear() would keep the memory
}

void redraw() {
  if (!map_cont || map_buf.empty()) return;
  lv_canvas_fill_bg(map_cont, lv_color_black(), LV_OPA_COVER);
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.width = 3;
  for (const auto &o : objs) {
    if (o.poly.size() < 3) continue;
    dsc.color = o.excluded ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN);
    std::vector<lv_point_t> pts(o.poly.size() + 1);
    for (size_t i = 0; i < o.poly.size(); ++i) to_px(o.poly[i].first, o.poly[i].second, pts[i]);
    pts.back() = pts.front();
    lv_canvas_draw_line(map_cont, pts.data(), pts.size(), &dsc);
  }
  lv_obj_invalidate(map_cont);
}

void map_click_cb(lv_event_t *e) {
  (void)e;
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);
  lv_area_t a;
  lv_obj_get_coords(map_cont, &a);
  float x, y;
  from_px(p.x - a.x1, p.y - a.y1, x, y);
  for (auto &o : objs) {
    if (o.poly.size() < 3 || !point_in_poly(x, y, o.poly)) continue;
    if (o.excluded) break;
    o.excluded = true;
    if (ws_ptr) ws_ptr->gcode_script("EXCLUDE_OBJECT NAME=" + o.name);
    redraw();
    break;
  }
}

void pan_cb(lv_event_t *e) {
  lv_obj_t *t = lv_event_get_target(e);
  float step = map_w / 8.0f;
  if (t == pan_btn[0]) pan_x += step;
  else if (t == pan_btn[1]) pan_x -= step;
  else if (t == pan_btn[2]) pan_y += step;
  else if (t == pan_btn[3]) pan_y -= step;
  float lim_x = map_w * zoom / 2, lim_y = map_h * zoom / 2;
  pan_x = std::max(-lim_x, std::min(lim_x, pan_x));
  pan_y = std::max(-lim_y, std::min(lim_y, pan_y));
  redraw();
}

void zoom_cb(lv_event_t *e) {
  lv_obj_t *t = lv_event_get_target(e);
  if (t == zoom_in_btn) zoom = std::min(zoom * 1.25f, 4.0f);
  else zoom = std::max(zoom / 1.25f, 1.0f);
  if (zoom == 1.0f) pan_x = pan_y = 0;
  redraw();
}

void load_objects(const json &objects, const json &excluded) {
  objs.clear();
  has_polygons = false;
  std::vector<std::string> ex;
  for (const auto &n : excluded) ex.push_back(n.get<std::string>());
  for (const auto &o : objects) {
    if (!o.contains("name")) continue;
    ExObj e;
    e.name = o["name"].get<std::string>();
    e.excluded = std::find(ex.begin(), ex.end(), e.name) != ex.end();
    if (o.contains("polygon")) {
      for (const auto &p : o["polygon"])
        if (p.size() >= 2) e.poly.push_back({p[0].get<float>(), p[1].get<float>()});
    }
    if (e.poly.size() >= 3) has_polygons = true;
    objs.push_back(e);
  }
}

}

ExcludePanel::ExcludePanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , exclude_cont(lv_obj_create(cont))
  , back_btn(cont, &back, _("Back") /* "Назад" */, &ExcludePanel::_handle_callback, this)
{
  background();
  ws_ptr = &ws;
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_center(exclude_cont);
  lv_obj_set_size(exclude_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(exclude_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  map_w = lv_disp_get_hor_res(NULL) - 140;
  map_h = lv_disp_get_ver_res(NULL) - 130;
  map_cont = lv_canvas_create(cont);
  map_free();
  lv_obj_align(map_cont, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_add_flag(map_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(map_cont, map_click_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(map_cont, LV_OBJ_FLAG_HIDDEN);

  zoom_in_btn = lv_btn_create(cont);
  lv_obj_set_size(zoom_in_btn, 60, 60);
  lv_obj_t *zin_lbl = lv_label_create(zoom_in_btn);
  lv_label_set_text(zin_lbl, "+");
  lv_obj_set_style_text_font(zin_lbl, &notosemi_20, 0);
  lv_obj_center(zin_lbl);
  lv_obj_align(zoom_in_btn, LV_ALIGN_BOTTOM_RIGHT, -260, -20);
  lv_obj_add_event_cb(zoom_in_btn, zoom_cb, LV_EVENT_CLICKED, NULL);

  zoom_out_btn = lv_btn_create(cont);
  lv_obj_set_size(zoom_out_btn, 60, 60);
  lv_obj_t *zout_lbl = lv_label_create(zoom_out_btn);
  lv_label_set_text(zout_lbl, "-");
  lv_obj_set_style_text_font(zout_lbl, &notosemi_20, 0);
  lv_obj_center(zout_lbl);
  lv_obj_align(zoom_out_btn, LV_ALIGN_BOTTOM_RIGHT, -190, -20);
  lv_obj_add_event_cb(zoom_out_btn, zoom_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(zoom_in_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(zoom_out_btn, LV_OBJ_FLAG_HIDDEN);

  const char *pan_text[4] = {LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_UP, LV_SYMBOL_DOWN};
  lv_align_t pan_align[4] = {LV_ALIGN_TOP_RIGHT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_TOP_RIGHT};
  lv_coord_t pan_x_off[4] = {-70, 0, -35, -35};
  lv_coord_t pan_y_off[4] = {160, 160, 90, 230};
  for (int i = 0; i < 4; ++i) {
    pan_btn[i] = lv_btn_create(cont);
    lv_obj_set_size(pan_btn[i], 60, 60);
    lv_obj_t *pan_lbl = lv_label_create(pan_btn[i]);
    lv_label_set_text(pan_lbl, pan_text[i]);
    lv_obj_center(pan_lbl);
    lv_obj_align(pan_btn[i], pan_align[i], pan_x_off[i], pan_y_off[i]);
    lv_obj_add_event_cb(pan_btn[i], pan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(pan_btn[i], LV_OBJ_FLAG_HIDDEN);
  }

  ws.register_notify_update(this);
}

ExcludePanel::~ExcludePanel() {
  reset();
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
  map_cont = NULL;
  ws.unregister_notify_update(this);
}

void ExcludePanel::reset() {
    for (auto item : switches) {
        delete item;
    }
    switches.clear();
}

static void sync_view(lv_obj_t *exclude_cont, lv_obj_t *zin, lv_obj_t *zout) {
  if (has_polygons) {
    if (active) map_alloc();
    lv_obj_add_flag(exclude_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(map_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(zin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(zout, LV_OBJ_FLAG_HIDDEN);
    for (auto b : pan_btn) lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);
    compute_bounds();
    redraw();
  } else {
    lv_obj_clear_flag(exclude_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(map_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(zin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(zout, LV_OBJ_FLAG_HIDDEN);
    for (auto b : pan_btn) lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
  }
}

void ExcludePanel::init() {
    lv_obj_set_layout(exclude_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(exclude_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(exclude_cont, LV_PCT(100));

    State *s = State::get_instance();
    auto v = s->get_data("/printer_state/exclude_object"_json_pointer);
    if (v.is_null()) return;

    load_objects(v["objects"], v["excluded_objects"]);
    reset();
    if (!has_polygons) {
      for (const auto &o : objs) {
        SwitchContainer *item = new SwitchContainer(exclude_cont, o.name.c_str(), this, ws, o.name);
        o.excluded ? item->off() : item->on();
        switches.push_back(item);
      }
    }
    sync_view(exclude_cont, zoom_in_btn, zoom_out_btn);
}

void ExcludePanel::foreground() {
  lv_obj_move_foreground(cont);
  active = true;
  init();
}

void ExcludePanel::background() {
  active = false;
  map_free();
  lv_obj_move_background(cont);
}

void ExcludePanel::consume(json &j) {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (!active) return;

    auto v = j["/params/0/exclude_object/objects"_json_pointer];
    auto ex = j["/params/0/exclude_object/excluded_objects"_json_pointer];
    if (v.is_null() && ex.is_null()) return;

    if (v.is_null()) {
      std::vector<std::string> names;
      for (const auto &n : ex) names.push_back(n.get<std::string>());
      for (auto &o : objs)
        o.excluded = std::find(names.begin(), names.end(), o.name) != names.end();
      for (auto item : switches) {
        if (std::find(names.begin(), names.end(), item->get_name()) != names.end()) item->off();
        else item->on();
      }
      redraw();
      return;
    }

    load_objects(v, ex.is_null() ? json::array() : ex);
    reset();
    if (!has_polygons) {
      for (const auto &o : objs) {
        SwitchContainer *item = new SwitchContainer(exclude_cont, o.name.c_str(), this, ws, o.name);
        o.excluded ? item->off() : item->on();
        switches.push_back(item);
      }
    }
    sync_view(exclude_cont, zoom_in_btn, zoom_out_btn);
}

void ExcludePanel::handle_callback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn.get_container()) {
    background();
    return;
  }
}
