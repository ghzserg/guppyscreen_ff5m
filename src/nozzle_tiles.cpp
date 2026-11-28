#include "nozzle_tiles.h"

#ifdef GUPPY_FF5M

#include "state.h"
#include "theme.h"

LV_IMG_DECLARE(extruder);

NozzleTiles::NozzleTiles(KWebSocketClient &c, lv_obj_t *parent, Numpad &np)
  : ws(c)
  , numpad(np)
  , cont(lv_obj_create(parent))
{
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(cont, SensorContainer::tile_gap(), 0);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);  // until it has tiles
}

int NozzleTiles::tool_of(const std::string &id) {
  static const std::string EXTRUDER = "extruder";
  if (id.rfind(EXTRUDER, 0) != 0) return -1;
  std::string n = id.substr(EXTRUDER.size());
  if (n.empty()) return 0;
  if (n.size() > 2 || n.find_first_not_of("0123456789") != std::string::npos) return -1;  // extruder_stepper, ...
  return std::stoi(n);
}

void NozzleTiles::create(const std::vector<Nozzle> &nozzles, SensorContainer &replaced, bool stripe, int selected,
                         TapCb cb) {
  this->nozzles = nozzles;
  tiles.clear();
  seeded = false;
  shown = -1;
  on_tap = std::move(cb);
  for (const Nozzle &n : nozzles) {
    auto tile = std::make_unique<SensorContainer>(ws, cont, &extruder, 150, n.name.c_str(), n.color,
                                                   false, true, numpad, n.id, (lv_obj_t *)NULL, (lv_chart_series_t *)NULL);
    tile->as_tile();
    lv_obj_t *obj = tile->get_sensor();
    if (!stripe) lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_user_data(obj, (void *)(intptr_t)tiles.size());
    lv_obj_add_event_cb(obj, &NozzleTiles::_handle_tap, LV_EVENT_SHORT_CLICKED, this);
    tiles.push_back(std::move(tile));
  }
  if (tiles.empty()) {  // no extruder among Home's sensors
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(replaced.get_sensor(), LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(replaced.get_sensor(), LV_OBJ_FLAG_HIDDEN);
  }
  show_selected(selected);
}

void NozzleTiles::show_selected(int tool) {
  if (!selectable()) return;  // one nozzle: nothing to select
  if (tool == shown) return;  // restyling redraws the tiles
  shown = tool;
  for (size_t i = 0; i < tiles.size(); i++) {
    lv_obj_t *obj = tiles[i]->get_sensor();
    if (nozzles[i].tool == tool) {
      lv_obj_remove_style(obj, &Theme::accent_bg, 0);
      lv_obj_add_style(obj, &Theme::accent_bg, 0);
    } else {
      lv_obj_remove_style(obj, &Theme::accent_bg, 0);
    }
  }
}

void NozzleTiles::set_disabled(bool disabled) {
  for (const auto &tile : tiles) {
    auto *obj = tile->get_sensor();
    if (lv_obj_has_state(obj, LV_STATE_DISABLED) == disabled) continue;
    if (disabled) {
      lv_obj_set_style_opa(obj, LV_OPA_50, LV_STATE_DISABLED);
      lv_obj_add_state(obj, LV_STATE_DISABLED);
    } else {
      lv_obj_clear_state(obj, LV_STATE_DISABLED);
    }
  }
}

int NozzleTiles::index_of(int tool) const {
  for (size_t i = 0; i < nozzles.size(); i++) {
    if (nozzles[i].tool == tool) return (int)i;
  }
  return -1;
}

int NozzleTiles::selected() const {
  if (index_of(shown) >= 0) return shown;
  return nozzles.empty() ? -1 : nozzles[0].tool;
}

SensorContainer *NozzleTiles::tile(int tool) {
  int i = index_of(tool);
  return i < 0 ? nullptr : tiles[i].get();
}

const NozzleTiles::Nozzle *NozzleTiles::nozzle(int tool) const {
  int i = index_of(tool);
  return i < 0 ? nullptr : &nozzles[i];
}

void NozzleTiles::set_targets(int target) {
  for (auto &t : tiles) t->update_target(target);
}

void NozzleTiles::update(const json &j, bool targets) {
  if (tiles.empty()) return;
  // const lookups, no pointers built per update: no nulls inserted into the
  // update or State, no allocations.
  const json *params = nullptr;
  auto p = j.find("params");
  if (p != j.end() && p->is_array() && !p->empty()) params = &(*p)[0];
  auto field = [&](const std::string &id, const char *name) -> json {
    if (params != nullptr) {
      auto obj = params->find(id);
      if (obj != params->end()) {
        auto f = obj->find(name);
        if (f != obj->end()) return *f;
      }
    }
    if (!seeded) return State::get_instance()->get_data_copy(json::json_pointer("/printer_state/" + id + "/" + name));
    return json();
  };
  for (auto &t : tiles) {
    auto value = field(t->get_id(), "temperature");
    if (value.is_number()) t->update_value(value.template get<int>());
    if (targets) {
      auto target = field(t->get_id(), "target");
      if (target.is_number()) t->update_target(target.template get<int>());
    }
  }
  seeded = true;
}

void NozzleTiles::_handle_tap(lv_event_t *event) {
  NozzleTiles *self = (NozzleTiles *)lv_event_get_user_data(event);
  lv_obj_t *obj = lv_event_get_current_target(event);
  if (lv_obj_has_state(obj, LV_STATE_DISABLED)) return;
  size_t i = (size_t)(intptr_t)lv_obj_get_user_data(obj);
  int tool = self->nozzles[i].tool;
  if (!self->on_tap || self->on_tap(tool)) self->show_selected(tool);
}

#endif  // GUPPY_FF5M
