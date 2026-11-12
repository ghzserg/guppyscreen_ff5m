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

#include <string>
#include <vector>

namespace ui {

enum class Match { Exact, Contains };

// First shown label whose text matches, or nullptr.
lv_obj_t *find_label(const std::string &text, Match match = Match::Exact);

// Texts of all shown labels under `root` (including root itself), in tree order.
std::vector<std::string> texts_in(lv_obj_t *root);

// Texts of the labels in the same container as the label `text`, e.g. the
// value and target next to a sensor's name. Empty if the label is not shown.
std::vector<std::string> row_texts(const std::string &text);

// Tap the centre of `obj` with the fake touch panel (press, then release).
void tap(lv_obj_t *obj);

// Tap the shown label `text`. Fails the test if there is none.
void tap_label(const std::string &text, Match match = Match::Exact);

// All shown label texts, " | "-separated. For failure messages.
std::string dump_screen();

// " | "-separated list, for failure messages.
std::string join(const std::vector<std::string> &items);

}  // namespace ui

#endif  // GUPPY_TEST_UI_H
