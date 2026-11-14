#include "grid_layout.h"

#include "config.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <functional>
#include <map>

// One track: a number is a share (fr, 1..99), "content" sizes to the
// content, "<n>px" is fixed. Returns false if `t` is none of these.
static bool parse_track(const json &t, lv_coord_t &out) {
  if (t.is_number_integer()) {
    int fr = t.get<int>();
    if (fr < 1 || fr > 99) {
      return false;
    }
    out = LV_GRID_FR(fr);
    return true;
  }
  if (!t.is_string()) {
    return false;
  }
  const std::string s = t.get<std::string>();
  if (s == "content") {
    out = LV_GRID_CONTENT;
    return true;
  }
  if (s.size() > 2 && s.compare(s.size() - 2, 2, "px") == 0) {
    try {
      size_t used = 0;
      int px = std::stoi(s.substr(0, s.size() - 2), &used);
      if (used == s.size() - 2 && px >= 0 && px < 4096) {
        out = px;
        return true;
      }
    } catch (const std::exception &) {
    }
  }
  return false;
}

// The configured tracks, or `defaults` if `j` is missing or invalid.
// Always ends with LV_GRID_TEMPLATE_LAST. `configured` tells which.
static std::vector<lv_coord_t> read_tracks(const std::string &panel, const json &j, const char *name,
                                           std::vector<lv_coord_t> defaults, bool &configured) {
  std::vector<lv_coord_t> tracks;
  if (!j.is_null()) {
    bool ok = j.is_array() && !j.empty();
    for (size_t i = 0; ok && i < j.size(); i++) {
      lv_coord_t t;
      ok = parse_track(j[i], t);
      tracks.push_back(t);
    }
    if (!ok) {
      spdlog::warn("layout.{}: \"{}\" must be a non-empty list of numbers 1..99, \"content\" or \"<n>px\", got {}; using the default",
                   panel, name, j.dump());
      tracks.clear();
    }
  }
  configured = !tracks.empty();
  if (!configured) {
    tracks = std::move(defaults);
  }
  tracks.push_back(LV_GRID_TEMPLATE_LAST);
  return tracks;
}

#ifdef GUPPY_LAYOUT_REGISTRY
// What the panels placed, for tests and the preview (not on the printer).
static std::vector<GridLayout::Item> placed_items;
// Each panel's tracks as they would be written in the config.
static nlohmann::ordered_json panel_tracks = nlohmann::ordered_json::object();
// Each panel's grid container (the first one, for PID and Pro).
static std::map<std::string, lv_obj_t *> panel_conts;

static json track_json(lv_coord_t t) {
  if (t == LV_GRID_CONTENT) return "content";
  if (t > LV_GRID_CONTENT && t < LV_GRID_TEMPLATE_LAST) return t - LV_GRID_FR(0);
  return std::to_string(t) + "px";
}

static json tracks_json(const std::vector<lv_coord_t> &tracks) {
  json out = json::array();
  for (size_t i = 0; i + 1 < tracks.size(); i++) {  // without LV_GRID_TEMPLATE_LAST
    out.push_back(track_json(tracks[i]));
  }
  return out;
}

static const char *align_name(lv_grid_align_t a) {
  switch (a) {
    case LV_GRID_ALIGN_START: return "start";
    case LV_GRID_ALIGN_END: return "end";
    case LV_GRID_ALIGN_STRETCH: return "stretch";
    default: return "center";
  }
}

#endif  // GUPPY_LAYOUT_REGISTRY

GridLayout::GridLayout(const std::string &panel_name, const json &layout,
                       std::vector<lv_coord_t> def_cols, std::vector<lv_coord_t> def_rows)
  : panel(panel_name) {
  json none;
  if (!layout.is_null() && !layout.is_object()) {
    spdlog::warn("layout.{}: expected an object, got {}; using the default", panel, layout.dump());
  }
  const json &l = layout.is_object() ? layout : none;
  bool cols_set, rows_set;
  cols = read_tracks(panel, l.contains("columns") ? l["columns"] : none, "columns", std::move(def_cols), cols_set);
  rows = read_tracks(panel, l.contains("rows") ? l["rows"] : none, "rows", std::move(def_rows), rows_set);
  tracks_configured = cols_set || rows_set;
#ifdef GUPPY_LAYOUT_REGISTRY
  if (!panel_tracks.contains(panel)) {
    panel_tracks[panel] = {{"columns", tracks_json(cols)}, {"rows", tracks_json(rows)}};
  }
#endif
  if (l.contains("padding")) padding = l["padding"];
  if (l.contains("gap")) gap = l["gap"];
  if (l.contains("items")) {
    if (l["items"].is_object()) {
      items = l["items"];
    } else {
      spdlog::warn("layout.{}: \"items\" must be an object, got {}", panel, l["items"].dump());
    }
  }
}

GridLayout GridLayout::from_config(const std::string &panel,
                                   std::vector<lv_coord_t> def_cols, std::vector<lv_coord_t> def_rows) {
  // Not Config::get_json(): its operator[] would insert nulls that
  // Config::save() writes back to the file.
  json &root = Config::get_instance()->get_json("");
  const json *layout = nullptr;
  auto l = root.find("layout");
  if (l != root.end() && l->is_object()) {
    auto p = l->find(panel);
    if (p != l->end()) {
      layout = &*p;
    }
  }
  return GridLayout(panel, layout ? *layout : json(), std::move(def_cols), std::move(def_rows));
}

#ifdef GUPPY_LAYOUT_REGISTRY
const std::vector<GridLayout::Item> &GridLayout::placed() {
  return placed_items;
}

// A number if all values are equal, else an object of them.
static nlohmann::ordered_json same_or_object(std::initializer_list<std::pair<const char *, lv_coord_t>> v) {
  bool same = true;
  for (const auto &p : v) same = same && p.second == v.begin()->second;
  if (same) return v.begin()->second;
  nlohmann::ordered_json out = nlohmann::ordered_json::object();
  for (const auto &p : v) out[p.first] = p.second;
  return out;
}

nlohmann::ordered_json GridLayout::current_layout() {
  nlohmann::ordered_json out = nlohmann::ordered_json::object();
  for (const auto &i : placed_items) {
    if (!out.contains(i.panel)) {
      out[i.panel] = panel_tracks.contains(i.panel) ? panel_tracks[i.panel] : nlohmann::ordered_json::object();
      auto c = panel_conts.find(i.panel);
      if (c != panel_conts.end()) {
        lv_obj_t *cont = c->second;
        out[i.panel]["padding"] = same_or_object({
          {"top", lv_obj_get_style_pad_top(cont, LV_PART_MAIN)},
          {"right", lv_obj_get_style_pad_right(cont, LV_PART_MAIN)},
          {"bottom", lv_obj_get_style_pad_bottom(cont, LV_PART_MAIN)},
          {"left", lv_obj_get_style_pad_left(cont, LV_PART_MAIN)}});
        out[i.panel]["gap"] = same_or_object({
          {"row", lv_obj_get_style_pad_row(cont, LV_PART_MAIN)},
          {"column", lv_obj_get_style_pad_column(cont, LV_PART_MAIN)}});
      }
      out[i.panel]["items"] = nlohmann::ordered_json::object();
    }
    auto &items = out[i.panel]["items"];
    if (items.contains(i.id)) {
      continue;  // PID and Pro are created twice
    }
    nlohmann::ordered_json item = {{"col", i.col}, {"row", i.row}};
    if (i.col_span != 1) item["col_span"] = i.col_span;
    if (i.row_span != 1) item["row_span"] = i.row_span;
    if (i.x_align == i.y_align) {
      if (i.x_align != LV_GRID_ALIGN_CENTER) item["align"] = align_name(i.x_align);
    } else {
      item["align_x"] = align_name(i.x_align);
      item["align_y"] = align_name(i.y_align);
    }
    if (i.hidden) item["hidden"] = true;
    items[i.id] = item;
  }
  return out;
}
#endif  // GUPPY_LAYOUT_REGISTRY

// A padding or gap value: an integer 0..400.
static bool spacing_value(const json &v, lv_coord_t &out) {
  if (!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 400) return false;
  out = v.get<int>();
  return true;
}

// `spec` is a number (all `keys`) or an object of some of `keys`; calls
// set(key, value) for each. Logs and ignores what isn't valid.
static void apply_spacing(const std::string &where, const json &spec,
                          std::initializer_list<const char *> keys,
                          const std::function<void(const std::string &, lv_coord_t)> &set) {
  lv_coord_t v;
  if (spacing_value(spec, v)) {
    for (const char *k : keys) set(k, v);
    return;
  }
  if (!spec.is_object()) {
    spdlog::warn("layout.{} must be a number 0..400 or an object, got {}", where, spec.dump());
    return;
  }
  for (auto it = spec.begin(); it != spec.end(); ++it) {
    bool known = std::find_if(keys.begin(), keys.end(), [&](const char *k) { return it.key() == k; }) != keys.end();
    if (!known || !spacing_value(it.value(), v)) {
      spdlog::warn("layout.{}.{}: expected one of the sides with a number 0..400, got {}", where, it.key(), it.value().dump());
      continue;
    }
    set(it.key(), v);
  }
}

void GridLayout::apply_tracks(lv_obj_t *cont) {
  lv_obj_set_grid_dsc_array(cont, cols.data(), rows.data());
#ifdef GUPPY_LAYOUT_REGISTRY
  panel_conts.emplace(panel, cont);
#endif
  if (!padding.is_null()) {
    apply_spacing(panel + ".padding", padding, {"top", "right", "bottom", "left"},
                  [cont](const std::string &k, lv_coord_t v) {
                    if (k == "top") lv_obj_set_style_pad_top(cont, v, 0);
                    else if (k == "right") lv_obj_set_style_pad_right(cont, v, 0);
                    else if (k == "bottom") lv_obj_set_style_pad_bottom(cont, v, 0);
                    else lv_obj_set_style_pad_left(cont, v, 0);
                  });
  }
  if (!gap.is_null()) {
    apply_spacing(panel + ".gap", gap, {"row", "column"},
                  [cont](const std::string &k, lv_coord_t v) {
                    if (k == "row") lv_obj_set_style_pad_row(cont, v, 0);
                    else lv_obj_set_style_pad_column(cont, v, 0);
                  });
  }
}

// item[key] if it is an integer (`set` = true), else `def`.
static int int_field(const json &item, const std::string &where, const char *key, int def, bool &set) {
  auto it = item.find(key);
  if (it == item.end()) {
    return def;
  }
  if (!it->is_number_integer()) {
    spdlog::warn("layout.{}.{} must be an integer, got {}", where, key, it->dump());
    return def;
  }
  set = true;
  return it->get<int>();
}

// Sets `out` from item[key] ("center", "start", "end", "stretch") and
// returns true, if it is present and valid.
static bool align_field(const json &item, const std::string &where, const char *key, lv_grid_align_t &out) {
  auto al = item.find(key);
  if (al == item.end()) {
    return false;
  }
  std::string a = al->is_string() ? al->get<std::string>() : "";
  if (a == "center") out = LV_GRID_ALIGN_CENTER;
  else if (a == "start") out = LV_GRID_ALIGN_START;
  else if (a == "end") out = LV_GRID_ALIGN_END;
  else if (a == "stretch") out = LV_GRID_ALIGN_STRETCH;
  else {
    spdlog::warn("layout.{}.{} must be center, start, end or stretch, got {}", where, key, al->dump());
    return false;
  }
  return true;
}

void GridLayout::place(lv_obj_t *obj, const std::string &id,
                       lv_grid_align_t x_align, int col, int col_span,
                       lv_grid_align_t y_align, int row, int row_span) {
  const std::string where = panel + ".items." + id;

  static const json empty = json::object();
  const json *item = &empty;
  auto found = items.find(id);
  if (found != items.end()) {
    if (found->is_object()) {
      item = &*found;
    } else {
      spdlog::warn("layout.{} must be an object, got {}", where, found->dump());
    }
  }

  lv_grid_align_t both;
  if (align_field(*item, where, "align", both)) {
    x_align = y_align = both;
  }
  align_field(*item, where, "align_x", x_align);
  align_field(*item, where, "align_y", y_align);

  bool set = false;
  col = int_field(*item, where, "col", col, set);
  row = int_field(*item, where, "row", row, set);
  col_span = int_field(*item, where, "col_span", col_span, set);
  row_span = int_field(*item, where, "row_span", row_span, set);
  if (set || tracks_configured) {
    // Keep configured cells inside the grid. The panels' own cells are
    // passed through as they are (some deliberately lie outside it).
    const int ncols = cols.size() - 1;
    const int nrows = rows.size() - 1;
    col = std::clamp(col, 0, ncols - 1);
    row = std::clamp(row, 0, nrows - 1);
    col_span = std::clamp(col_span, 1, ncols - col);
    row_span = std::clamp(row_span, 1, nrows - row);
  }
  lv_obj_set_grid_cell(obj, x_align, col, col_span, y_align, row, row_span);

  // Hidden items keep their cell: some panels show items themselves
  // (calibration graphs and spinners), and they must appear in place.
  bool hide = false;
  auto hidden = item->find("hidden");
  if (hidden != item->end()) {
    if (!hidden->is_boolean()) {
      spdlog::warn("layout.{}.hidden must be true or false, got {}", where, hidden->dump());
    } else {
      hide = hidden->get<bool>();
    }
  }
  if (hide) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
#ifdef GUPPY_LAYOUT_REGISTRY
  placed_items.push_back({panel, id, obj, col, col_span, row, row_span, hide,
                          (int)cols.size() - 1, (int)rows.size() - 1, x_align, y_align});
#endif
}
