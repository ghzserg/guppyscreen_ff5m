#ifndef GUPPY_TEST_FAKE_PRINTER_H
#define GUPPY_TEST_FAKE_PRINTER_H

// A simulated printer behind a FakeMoonraker: G-code files with metadata and
// thumbnails, heaters that follow their targets, prints that heat up,
// progress and complete, and Fine Tune's commands (speed, flow, Z offset,
// pressure advance) reported back as Klipper would. Used by the browser preview (preview/) and tests.
//
// Construct it before app::start() (it sets the startup results). It pushes
// notify_status_update from its own thread, about every `tick_ms`, while a
// client is connected.

#include "fake_moonraker.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PrintFile {
  std::string path;              // relative to the gcodes root, e.g. "parts/cube.gcode"
  int estimated_time = 1800;     // s
  double filament_total = 2000;  // mm
  double filament_weight_total = 6;  // g
  double layer_height = 0.2;     // mm
  double object_height = 20;     // mm
  int extruder_temp = 220;
  int bed_temp = 60;
  uint32_t color = 0x2196F3;     // thumbnail colour, 0xRRGGBB
};

class FakePrinter {
 public:
  struct Options {
    double print_seconds = 120;  // real time a print takes once heated
    double heat_speed = 1;       // thermal model speed-up (tests use more)
    int tick_ms = 250;           // status push interval
    std::string gcodes_dir;      // thumbnails go to <gcodes_dir>/.thumbs; "" = none
  };

  // [{"path": "cube.gcode", "estimated_time": 1500, ..., "color": "#e53935"}, ...]
  // Throws std::runtime_error with a readable message on bad input.
  static std::vector<PrintFile> parse_files(const json &j);

  FakePrinter(FakeMoonraker &moonraker, std::vector<PrintFile> files, Options options);
  ~FakePrinter();

  FakePrinter(const FakePrinter &) = delete;
  FakePrinter &operator=(const FakePrinter &) = delete;

 private:
  struct Heater {
    std::string name;
    double temperature, target = 0, power = 0;
    double max_rate;   // °C/s at full power
    double loss;       // 1/s, heat loss towards ambient
  };
  enum class Phase { Idle, Heating, Printing, Paused };

  void write_thumbnails() const;
  json metadata(const PrintFile &f) const;
  const PrintFile *find_file(const std::string &path) const;

  // Called by FakeMoonraker under its own lock: touch only our state.
  json on_gcode(const json &params);
  json on_print_start(const json &params);
  json on_print_pause();
  json on_print_resume();
  json on_print_cancel();

  void set_target(const std::string &heater, double target);
  void run();
  json tick(double dt);  // advances the model, returns the status update

  FakeMoonraker &moonraker_;
  std::vector<PrintFile> files_;
  Options opt_;

  std::mutex mutex_;
  std::vector<Heater> heaters_;
  Phase phase_ = Phase::Idle;
  json pending_ = json::object();  // fields to push with the next tick (state changes)
  const PrintFile *printing_ = nullptr;
  double progress_ = 0;     // 0..1
  double total_duration_ = 0;
  double noise_phase_ = 0;
  // The Z offset SET_GCODE_OFFSET Z_ADJUST adds to, from the startup status.
  double z_offset_ = 0;

  std::atomic<bool> stop_{false};
  std::thread thread_;
};

#endif  // GUPPY_TEST_FAKE_PRINTER_H
