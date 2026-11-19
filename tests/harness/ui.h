#ifndef GUPPY_TEST_UI_H
#define GUPPY_TEST_UI_H

// Finding and tapping widgets the way a user would.
//
// A widget counts as "shown" only if a tap at its centre would reach it
// (or its clickable parent): not hidden, not scrolled out, and not covered
// by another panel. This matters because guppyscreen "closes" most panels
// by moving them behind the main screen, not by hiding them.
//
// Every function takes the LVGL lock itself. Call them from the test
// thread only, and not while holding the lock.

#include "lvgl/lvgl.h"
#include "nav.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {

enum class Match { Exact, Contains };

// fn on obj and every object under it, depth first. Hold the LVGL lock.
void walk(lv_obj_t *obj, const std::function<void(lv_obj_t *)> &fn);

// First shown label whose text matches, or nullptr.
lv_obj_t *find_label(const std::string &text, Match match = Match::Exact);

// Texts of all shown labels under `root` (including root itself), in tree order.
std::vector<std::string> texts_in(lv_obj_t *root);

// Texts of the labels in the same container as the label `text`, e.g. the
// value and target next to a sensor's name. Empty if the label is not shown.
std::vector<std::string> row_texts(const std::string &text);

// First shown widget of class `cls` (e.g. &lv_canvas_class), or nullptr.
lv_obj_t *find_shown(const lv_obj_class_t *cls);

// The container of the shown label `text` (a slider's row), or nullptr if
// the label is not shown.
lv_obj_t *label_container(const std::string &text);

// Whether `obj` has a covering background of `palette`'s main colour (the
// tile the numpad is for).
bool has_bg(lv_obj_t *obj, lv_palette_t palette);

// Enter `value` on the open numpad and press OK. Fails the test if no numpad
// is open.
void enter_on_numpad(const std::string &value);

// Button matrices on the screen (shown or not) whose button `idx` reads `text`.
std::vector<lv_obj_t *> btnmatrices(const std::string &text, uint32_t idx = 0);

// Would a tap at obj's centre reach it (see "shown" above)?
bool is_shown(lv_obj_t *obj);

// Tap a tab of the navigation bar, then let the app settle.
void tap_tab(Nav::Tab tab);

// Tap the centre of `obj` with the fake touch panel (press, then release).
void tap(lv_obj_t *obj);

// Tap the screen point (x, y).
void tap_at(lv_coord_t x, lv_coord_t y);

// Tap the shown label `text`. Fails the test if there is none.
void tap_label(const std::string &text, Match match = Match::Exact);

// Is there a visible table (e.g. the print file list) with a row containing `text`?
bool table_has_row(const std::string &text);

// Tap the first visible table row containing `text`. Fails the test if there is none.
void tap_table_row(const std::string &text);

// All shown label texts, " | "-separated. For failure messages.
std::string dump_screen();

// " | "-separated list, for failure messages.
std::string join(const std::vector<std::string> &items);

}  // namespace ui

#endif  // GUPPY_TEST_UI_H
