// Print panel file list: selecting a file shows its PNG thumbnail. LVGL
// decodes each PNG (ARGB8888: 300x300 -> 360 KB) and caches it; browsing
// must not keep the decoded thumbnails of files no longer shown.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "png_writer.h"
#include "utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <functional>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef __SANITIZE_ADDRESS__
extern "C" size_t __sanitizer_get_current_allocated_bytes(void);  // ASan runtime
#endif

namespace {

constexpr int THUMB = 300;
constexpr int N_FILES = 4;
// A decoded thumbnail: ARGB8888 (smaller than PngDecoder::MIN_PIXELS).
constexpr size_t DECODED = THUMB * THUMB * 4;

std::string make_gcodes_dir() {
  const char *tmp = getenv("TMPDIR");
  std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-gcodes-XXXXXX";
  REQUIRE(mkdtemp(tmpl.data()) != NULL);
  mkdir((tmpl + "/.thumbs").c_str(), 0755);
  for (int i = 0; i < N_FILES; i++) {
    std::vector<unsigned char> px(THUMB * THUMB * 4);
    for (size_t p = 0; p < px.size(); p++) px[p] = (unsigned char)((p * (i + 3)) % 251);
    std::string path = tmpl + "/.thumbs/part" + std::to_string(i) + ".png";
    test_png::write(path, px, THUMB, THUMB);
  }
  return tmpl;
}

// The file panel's image: the only lv_img whose source is a .png file.
std::string shown_thumbnail() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::string found;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_img_class) && lv_img_src_get_type(lv_img_get_src(o)) == LV_IMG_SRC_FILE) {
      const char *src = (const char *)lv_img_get_src(o);
      if (strstr(src, ".png") && lv_obj_is_visible(o)) found = src;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(lv_scr_act());
  return found;
}

// Serves part0..part<N_FILES-1>.gcode, each with one THUMB-sized thumbnail.
void serve_files(FakeMoonraker &moonraker, const std::string &gcodes) {
  moonraker.set_result("server.files.roots", json::array({
    {{"name", "config"}, {"path", "/opt/config"}, {"permissions", "rw"}},
    {{"name", "gcodes"}, {"path", gcodes}, {"permissions", "rw"}},
  }));
  json files = json::array();
  for (int i = 0; i < N_FILES; i++) {
    files.push_back({{"path", "part" + std::to_string(i) + ".gcode"}, {"modified", 1000 + i}, {"size", 100000}});
  }
  moonraker.set_result("server.files.list", files);
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    std::string fname = params.value("filename", "");
    std::string stem = fname.substr(0, fname.find('.'));
    return json{{"filename", fname}, {"size", 100000}, {"modified", 1000}, {"estimated_time", 600},
                {"filament_weight_total", 5.0},
                {"thumbnails", json::array({{{"width", THUMB}, {"height", THUMB}, {"size", 1000},
                                             {"relative_path", ".thumbs/" + stem + ".png"}}})}};
  });
}

// Zoom of every image showing a .png file (file list, print status, mini status).
std::vector<uint16_t> png_zooms() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::vector<uint16_t> zooms;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_img_class) && lv_img_src_get_type(lv_img_get_src(o)) == LV_IMG_SRC_FILE
        && strstr((const char *)lv_img_get_src(o), ".png")) {
      zooms.push_back(lv_img_get_zoom(o));
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(lv_scr_act());
  return zooms;
}

}  // namespace

TEST_CASE("browsing files shows thumbnails without keeping old ones decoded") {
  std::string gcodes = make_gcodes_dir();

  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);

  app::start(moonraker);
  app::wait_ready(moonraker);

#ifdef __SANITIZE_ADDRESS__
  size_t heap0 = __sanitizer_get_current_allocated_bytes();
#endif

  ui::tap_tab(Nav::PRINT);
  for (int i = N_FILES - 1; i >= 0; i--) {
    std::string name = "part" + std::to_string(i);
    ui::tap_table_row(name + ".gcode");
    std::string want = "A:" + gcodes + "/.thumbs/" + name + ".png";
    bool shown = app::wait_until([&] { return shown_thumbnail() == want; });
    CHECK_MESSAGE(shown, "want " << want << ", shown: " << shown_thumbnail());
    app::redraw();  // draw it, which decodes the PNG
  }

#ifdef __SANITIZE_ADDRESS__
  long growth = (long)__sanitizer_get_current_allocated_bytes() - (long)heap0;
  MESSAGE("live heap growth after browsing " << N_FILES << " files: " << growth << " bytes ("
          << (double)growth / DECODED << " decoded thumbnails)");
  // The thumbnail on screen stays decoded; earlier ones must not.
  CHECK(growth < (long)(2 * DECODED));
#endif

  // Leaving the file list frees the last one too (its leave action).
  // Measured right after leaving, before any redraw: drawing the main
  // screen's icons would evict the entry from the cache anyway on a PC,
  // where decoding is fast and gives the entry little "life", but not
  // reliably on the printer.
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    Nav::get().activate(Nav::HOME);
#ifdef __SANITIZE_ADDRESS__
    growth = (long)__sanitizer_get_current_allocated_bytes() - (long)heap0;
    MESSAGE("live heap growth right after closing the file list: " << growth << " bytes");
    CHECK(growth < (long)(DECODED / 2));
#endif
  }
  CHECK(app::wait_until([] { return ui::find_label("Extrude") != nullptr; }));
}

// KUtils::get_thumbnail returned width 0 when the best thumbnail was the
// first listed (e.g. the only one), and the print status panel divided by it.
TEST_CASE("a print whose file has a single thumbnail shows it scaled") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part0.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);

  // Print status panel: 192 px wide, mini status on Home: 76 px, its whole
  // height above the progress bar.
  auto zooms = png_zooms();
  INFO("zooms: " << zooms.size());
  CHECK(std::find(zooms.begin(), zooms.end(), 192 * 256 / THUMB) != zooms.end());
  CHECK(std::find(zooms.begin(), zooms.end(), 76 * 256 / THUMB) != zooms.end());
}

TEST_CASE("late print metadata cannot overwrite the next print") {
  const std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  moonraker.set_result("server.files.list", json::array());
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    const auto filename = params.at("filename").get<std::string>();
    const auto stem = filename.substr(0, filename.find('.'));
    const bool first_file = stem == "part0";
    return json{{"estimated_time", first_file ? 120 : 600}, {"layer_count", first_file ? 12 : 60},
                {"thumbnails", json::array({{{"width", THUMB},
                                             {"relative_path", ".thumbs/" + stem + ".png"}}})}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  auto start_print = [&](const char *filename) {
    moonraker.push_status({{"print_stats", {{"filename", filename}, {"state", "printing"},
                                             {"print_duration", 30}, {"info", {{"current_layer", 1}}}}}});
    REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }), ui::dump_screen());
  };
  auto cancel_print = [&] {
    moonraker.push_status({{"print_stats", {{"state", "cancelled"}}}});
    REQUIRE(app::wait_until([] { return ui::find_label("Pause") == nullptr; }));
  };
  const std::string first_thumb = "A:" + gcodes + "/.thumbs/part0.png";
  const std::string next_thumb = "A:" + gcodes + "/.thumbs/part1.png";

  start_print("part0.gcode");
  REQUIRE(app::wait_until([&] { return shown_thumbnail() == first_thumb; }));
  cancel_print();

  moonraker.hold_next_reply("server.files.metadata");
  start_print("part1.gcode");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") == 2; }));
  CHECK_MESSAGE(ui::find_label("1 / 0") != nullptr, ui::dump_screen());
  moonraker.release_reply();
  REQUIRE(app::wait_until([&] { return shown_thumbnail() == next_thumb; }));
  cancel_print();

  moonraker.hold_next_reply("server.files.metadata");
  start_print("part0.gcode");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") == 3; }));
  cancel_print();
  start_print("part1.gcode");
  REQUIRE(app::wait_until([&] { return shown_thumbnail() == next_thumb; }));
  moonraker.release_reply();
  moonraker.push_status({{"print_stats", {{"info", {{"current_layer", 2}}}}}});
  REQUIRE(app::wait_until([] {
    return ui::find_label("2 / 60") != nullptr || ui::find_label("2 / 12") != nullptr;
  }));

  CHECK(shown_thumbnail() == next_thumb);
  CHECK_MESSAGE(ui::find_label("2 / 60") != nullptr, ui::dump_screen());
  CHECK_MESSAGE(ui::find_label("ETA: " + KUtils::eta_string(570)) != nullptr, ui::dump_screen());
}

TEST_CASE("a print whose thumbnail metadata says width 0 shows no thumbnail") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    return json{{"filename", params.value("filename", "")}, {"size", 100000}, {"estimated_time", 600},
                {"thumbnails", json::array({{{"width", 0}, {"height", 0}, {"size", 1000},
                                             {"relative_path", ".thumbs/part0.png"}}})}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part0.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  CHECK(png_zooms().empty());
}

TEST_CASE("a thumbnail with width 0 in the metadata doesn't hide the others") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  // Width 0 is closer to the wanted 300 than 700 is.
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    return json{{"filename", params.value("filename", "")}, {"size", 100000}, {"estimated_time", 600},
                {"thumbnails", json::array({
                  {{"width", 0}, {"height", 0}, {"size", 1000}, {"relative_path", ".thumbs/part0.png"}},
                  {{"width", 700}, {"height", 700}, {"size", 1000}, {"relative_path", ".thumbs/part0.png"}},
                })}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part0.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  auto zooms = png_zooms();
  CHECK(std::find(zooms.begin(), zooms.end(), 192 * 256 / 700) != zooms.end());
}

// get_thumbnail threw on a thumbnail without a width or a relative path
// (nlohmann's get<std::string> on null), out of the websocket thread.
TEST_CASE("thumbnails without a width or a file path are skipped") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    return json{{"filename", params.value("filename", "")}, {"size", 100000}, {"estimated_time", 600},
                {"thumbnails", json::array({
                  {{"height", 300}, {"size", 1000}, {"relative_path", ".thumbs/part0.png"}},
                  {{"width", nullptr}, {"height", 300}, {"size", 1000}, {"relative_path", ".thumbs/part0.png"}},
                  {{"width", 300}, {"height", 300}, {"size", 1000}},
                  {{"width", 300}, {"height", 300}, {"size", 1000}, {"relative_path", ""}},
                  {{"width", 300}, {"height", 300}, {"size", 1000}, {"relative_path", ".thumbs/"}},
                  {{"width", 700}, {"height", 700}, {"size", 1000}, {"relative_path", ".thumbs/part0.png"}},
                })}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part0.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  auto zooms = png_zooms();
  CHECK(std::find(zooms.begin(), zooms.end(), 192 * 256 / 700) != zooms.end());
}

// Moonraker has no metadata for some files ("Metadata not available"). The
// file panel kept showing the file selected before, while Print would start
// the tapped one.
TEST_CASE("selecting a file without metadata shows that file and not the one before") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  moonraker.set_handler("server.files.metadata", [](const json &params) {
    std::string fname = params.value("filename", "");
    if (fname == "part1.gcode") return FakeMoonraker::error(404, "Metadata not available for <part1.gcode>");
    std::string stem = fname.substr(0, fname.find('.'));
    return json{{"filename", fname}, {"size", 100000}, {"modified", 1000}, {"estimated_time", 600},
                {"filament_weight_total", 5.0},
                {"thumbnails", json::array({{{"width", THUMB}, {"height", THUMB}, {"size", 1000},
                                             {"relative_path", ".thumbs/" + stem + ".png"}}})}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  ui::tap_table_row("part0.gcode");
  REQUIRE(app::wait_until([&] { return shown_thumbnail() == "A:" + gcodes + "/.thumbs/part0.png"; }));
  REQUIRE(ui::find_label("part0.gcode") != nullptr);

  ui::tap_table_row("part1.gcode");
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("part1.gcode") != nullptr && ui::find_label("part0.gcode") == nullptr; }),
                ui::dump_screen());
  CHECK(shown_thumbnail().empty());
  CHECK(ui::find_label("Size: (Unknown) MB", ui::Match::Contains) != nullptr);
}

// A metadata reply that comes after the user selected another file must
// not replace it: Print starts the selected one.
TEST_CASE("a late metadata reply does not replace the file selected since") {
  std::string gcodes = make_gcodes_dir();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  serve_files(moonraker, gcodes);
  std::atomic<bool> slow_part0{false};
  moonraker.set_handler("server.files.metadata", [&slow_part0](const json &params) {
    std::string fname = params.value("filename", "");
    if (fname == "part0.gcode" && slow_part0) std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::string stem = fname.substr(0, fname.find('.'));
    return json{{"filename", fname}, {"size", 100000}, {"modified", 1000}, {"estimated_time", 600},
                {"thumbnails", json::array({{{"width", THUMB}, {"height", THUMB}, {"size", 1000},
                                             {"relative_path", ".thumbs/" + stem + ".png"}}})}};
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  ui::tap_table_row("part1.gcode");
  REQUIRE(app::wait_until([&] { return shown_thumbnail() == "A:" + gcodes + "/.thumbs/part1.png"; }));
  slow_part0 = true;
  ui::tap_table_row("part0.gcode");  // its reply comes in a second
  app::pump(100);
  ui::tap_table_row("part1.gcode");
  REQUIRE(ui::find_label("part1.gcode") != nullptr);

  REQUIRE(app::wait_until([&] { return shown_thumbnail() == "A:" + gcodes + "/.thumbs/part1.png"; }));
  int part1_requests = 0;
  for (const auto &request : moonraker.requests("server.files.metadata")) {
    if (request.at("filename") == "part1.gcode") ++part1_requests;
  }
  CHECK(part1_requests == 2);
  CHECK_MESSAGE(ui::find_label("part1.gcode") != nullptr, ui::dump_screen());
  CHECK(ui::find_label("part0.gcode") == nullptr);
  CHECK(shown_thumbnail() == "A:" + gcodes + "/.thumbs/part1.png");
}

TEST_CASE("file browser requests metadata for filenames with quotes and backslashes") {
  const std::string filename = R"(part "A"\B.gcode)";
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.list", json::array({{{"path", filename}, {"modified", 1000}}}));
  moonraker.set_result("server.files.metadata", {{"size", 2097152}});
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  CHECK(moonraker.requests("server.files.metadata").back().at("filename") == filename);
  CHECK(app::wait_until([] { return ui::find_label("Size: 2 MB", ui::Match::Contains) != nullptr; }));
}

TEST_CASE("file browser keeps the file list when refresh fails") {
  const std::string filename = "existing.gcode";
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.list", json::array({{{"path", filename}, {"modified", 1000}}}));
  moonraker.set_result("server.files.metadata", {{"size", 2097152}});
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  REQUIRE(ui::find_label(filename) != nullptr);
  const int requests = moonraker.request_count("server.files.list");
  moonraker.set_handler("server.files.list", [](const json &) {
    return FakeMoonraker::error(500, "File list unavailable");
  });
  ui::tap_label("Refresh", ui::Match::Contains);
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.list") > requests; }));
  app::settle(moonraker);
  CHECK(ui::find_label(filename) != nullptr);
  CHECK(ui::find_label("Size: 2 MB", ui::Match::Contains) != nullptr);
}

TEST_CASE("file browser preserves the chosen sort in folders and after refresh") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.list", json::array({
    {{"path", "folder/alpha.gcode"}, {"modified", 2000}},
    {{"path", "folder/zulu.gcode"}, {"modified", 1000}},
  }));
  moonraker.set_result("server.files.metadata", json::object());
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  ui::tap_label("A-Z", ui::Match::Contains);
  ui::tap_table_row("folder");
  REQUIRE(ui::find_label("alpha.gcode") != nullptr);
  ui::tap_label("A-Z", ui::Match::Contains);
  REQUIRE(ui::find_label("zulu.gcode") != nullptr);

  const int requests = moonraker.request_count("server.files.list");
  ui::tap_label("Refresh", ui::Match::Contains);
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.list") > requests; }));
  app::settle(moonraker);
  CHECK(ui::find_label("zulu.gcode") != nullptr);

  ui::tap_table_row("..");
  ui::tap_table_row("folder");
  CHECK(ui::find_label("zulu.gcode") != nullptr);
}

TEST_CASE("clearing an image keeps its size without a No data placeholder") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *img = lv_img_create(lv_scr_act());
  lv_obj_set_size(img, 120, 80);
  lv_img_set_src(img, LV_SYMBOL_OK);
  KUtils::set_img_src(img, NULL);
  CHECK(lv_img_get_src(img) == NULL);
  CHECK(static_cast<int>(((lv_img_t *)img)->src_type) == LV_IMG_SRC_SYMBOL);
  lv_obj_del(img);
}
