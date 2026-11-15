#ifndef __GRID_LAYOUT_H__
#define __GRID_LAYOUT_H__

// A panel's grid layout, overridable from guppyconfig.json:
//
//   "layout": {"<panel>": {
//     "columns": [1, 1, "content", "80px"],      // fr, content or pixels
//     "rows":    [1, 1, 1],
//     "padding": 10,               // or {"top": 0, "right": 10, "bottom": 0, "left": 10}
//     "gap": 6,                    // or {"row": 6, "column": 12}
//     "items": {"<item>": {"col": 3, "row": 1, "col_span": 1, "row_span": 1,
//                          "hidden": false, "align": "center",
//                          "align_x": "start", "align_y": "end"}}}}
//
// Anything missing or of the wrong type keeps the panel's hard-coded value
// (a warning is logged). Configured cells are clamped to the grid.
// The panels and items are listed in preview/README.md.

#include "hv/json.hpp"
#include "lvgl/lvgl.h"

#include <string>
#include <vector>

using json = nlohmann::json;

class GridLayout {
 public:
  // An item placed by some panel, for listing and tests.
  struct Item {
    std::string panel;
    std::string id;
    lv_obj_t *obj;
    int col, col_span, row, row_span;  // where it went (after the config)
    bool hidden;                       // hidden by the config
    int grid_cols, grid_rows;          // the panel's grid size
    lv_grid_align_t x_align, y_align;
  };

  // `layout`: the panel's object under "/layout", or null. `def_cols` and
  // `def_rows`: the panel's own tracks (without LV_GRID_TEMPLATE_LAST).
  GridLayout(const std::string &panel, const json &layout,
             std::vector<lv_coord_t> def_cols, std::vector<lv_coord_t> def_rows);

  // Reads "/layout/<panel>" from Config without adding keys to it.
  static GridLayout from_config(const std::string &panel,
                                std::vector<lv_coord_t> def_cols, std::vector<lv_coord_t> def_rows);

  // Every item placed so far, in order.
  static const std::vector<Item> &placed();

  // The "layout" config that reproduces every panel as it is now: tracks
  // and the cell of each item, in the order the panels place them.
  static nlohmann::ordered_json current_layout();

  // Sets the grid tracks on `cont`, and the configured padding and gap.
  // Call it after the panel's own padding styles, so the config wins.
  // The track arrays live in this object, which must outlive `cont` (LVGL
  // keeps the pointers).
  void apply_tracks(lv_obj_t *cont);

  // lv_obj_set_grid_cell() with the same arguments as defaults, overridden
  // by the config's item `id`, which may also hide `obj`.
  void place(lv_obj_t *obj, const std::string &id,
             lv_grid_align_t x_align, int col, int col_span,
             lv_grid_align_t y_align, int row, int row_span);

 private:
  std::string panel;
  json items;
  json padding;  // as configured, or null
  json gap;
  std::vector<lv_coord_t> cols;
  std::vector<lv_coord_t> rows;
  bool tracks_configured = false;
};

#endif  // __GRID_LAYOUT_H__
