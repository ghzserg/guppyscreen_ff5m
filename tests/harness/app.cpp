#include "app.h"

#include "fake_hw.h"
#include "ui.h"

#include "config.h"
#include "guppyscreen.h"

#include "doctest/doctest.h"

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>

namespace app {

static std::string temp_dir;

std::string fixture(const std::string &name) {
  return std::string(TESTS_DIR) + "/fixtures/" + name;
}

const std::string &dir() {
  return temp_dir;
}

static void copy_file(const std::string &from, const std::string &to) {
  std::ifstream in(from, std::ios::binary);
  std::ofstream out(to, std::ios::binary);
  if (!in || !out) {
    throw std::runtime_error("cannot copy " + from + " to " + to);
  }
  out << in.rdbuf();
}

static json test_config(int moonraker_port) {
  return {
    {"log_path", temp_dir + "/guppyscreen.log"},
    {"thumbnail_path", temp_dir + "/thumbnails"},
    {"display_sleep_sec", -1},
    {"default_printer", "FF5M"},
    {"printers", {{"FF5M", {
      {"moonraker_api_key", false},
      {"moonraker_host", "127.0.0.1"},
      {"moonraker_port", moonraker_port},
      {"log_level", "debug"},
      {"monitored_sensors", {
        {{"id", "extruder"}, {"display_name", "Extruder"}, {"controllable", true}, {"color", "red"}},
        {{"id", "heater_bed"}, {"display_name", "Bed"}, {"controllable", true}, {"color", "purple"}},
      }},
      {"fans", {
        {{"id", "output_pin fan0"}, {"display_name", "Toolhead Fan"}},
        {{"id", "output_pin fan1"}, {"display_name", "Back Fan"}},
        {{"id", "output_pin fan2"}, {"display_name", "Side Fan"}},
      }},
    }}}},
  };
}

void start(FakeMoonraker &moonraker) {
  if (!temp_dir.empty()) {
    throw std::logic_error("app::start() can be called only once per process");
  }

  const char *tmp = getenv("TMPDIR");
  std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-test-XXXXXX";
  if (mkdtemp(tmpl.data()) == NULL) {
    throw std::runtime_error("cannot create temp dir " + tmpl);
  }
  temp_dir = tmpl;
  mkdir((temp_dir + "/themes").c_str(), 0755);
  mkdir((temp_dir + "/thumbnails").c_str(), 0755);
  copy_file(std::string(REPO_DIR) + "/themes/blue.json", temp_dir + "/themes/blue.json");

  const std::string config_path = temp_dir + "/guppyconfig.json";
  std::ofstream(config_path) << test_config(moonraker.port()).dump(2);
  MESSAGE("app dir: " << temp_dir);

  Config::get_instance()->init(config_path, temp_dir + "/thumbnails");
  GuppyScreen::init(fake_hw::hal_init);
}

void pump(int ms) {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  do {
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      lv_timer_handler();
    }
    usleep(5000);
  } while (std::chrono::steady_clock::now() < end);
}

bool wait_until(const std::function<bool()> &pred, int timeout_ms) {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < end) {
    pump(5);
    if (pred()) {
      return true;
    }
  }
  return false;
}

void wait_ready(FakeMoonraker &moonraker) {
  bool ready = wait_until([&] {
    return moonraker.request_count("printer.objects.subscribe") > 0
      && !ui::find_label("Waiting for the Klipper launch", ui::Match::Contains);
  }, 10000);
  REQUIRE_MESSAGE(ready, "startup did not finish; screen shows: " << ui::dump_screen());
  pump(100);  // let the first layout and redraw settle
  auto missing = moonraker.unhandled();
  if (!missing.empty()) {
    MESSAGE("requests not in the fixture (answered with {}): " << ui::join(missing));
  }
}

}  // namespace app
