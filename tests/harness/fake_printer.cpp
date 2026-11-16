#include "fake_printer.h"

#include "png_writer.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {

constexpr double AMBIENT = 25.0;

double round2(double v) {
  return std::round(v * 100) / 100;
}

std::string upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), ::toupper);
  return s;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  return s;
}

void mkdirs(const std::string &path) {
  for (size_t i = 1; i <= path.size(); i++) {
    if (i == path.size() || path[i] == '/') {
      mkdir(path.substr(0, i).c_str(), 0755);
    }
  }
}

std::string dir_of(const std::string &path) {
  size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string stem_of(const std::string &path) {
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string thumb_name(const PrintFile &f, int size) {
  return ".thumbs/" + stem_of(f.path) + "-" + std::to_string(size) + "x" + std::to_string(size) + ".png";
}

int total_layers(const PrintFile &f) {
  return std::max(1, (int)std::lround(f.object_height / f.layer_height));
}

// Is (x, y) inside the convex polygon `p` (clockwise or not)?
bool inside(double x, double y, const std::vector<std::pair<double, double>> &p) {
  int sign = 0;
  for (size_t i = 0; i < p.size(); i++) {
    auto [x1, y1] = p[i];
    auto [x2, y2] = p[(i + 1) % p.size()];
    double cross = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
    int s = cross > 0 ? 1 : (cross < 0 ? -1 : 0);
    if (s != 0) {
      if (sign != 0 && s != sign) return false;
      sign = s;
    }
  }
  return true;
}

// An isometric cube in `color`, on a transparent background.
std::vector<unsigned char> cube_image(int size, uint32_t color) {
  std::vector<unsigned char> px(size * size * 4, 0);
  double cx = size / 2.0, cy = size / 2.0, a = size * 0.36, w = a * std::cos(M_PI / 6);
  using Poly = std::vector<std::pair<double, double>>;
  Poly top = {{cx, cy - a}, {cx + w, cy - a / 2}, {cx, cy}, {cx - w, cy - a / 2}};
  Poly left = {{cx - w, cy - a / 2}, {cx, cy}, {cx, cy + a}, {cx - w, cy + a / 2}};
  Poly right = {{cx, cy}, {cx + w, cy - a / 2}, {cx + w, cy + a / 2}, {cx, cy + a}};
  int r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff;
  auto shade = [](int c, double k) { return (unsigned char)std::clamp((int)std::lround(c * k), 0, 255); };
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      double fx = x + 0.5, fy = y + 0.5, k = 0;
      if (inside(fx, fy, top)) k = 1.3;
      else if (inside(fx, fy, left)) k = 1.0;
      else if (inside(fx, fy, right)) k = 0.7;
      else continue;
      unsigned char *p = &px[(y * size + x) * 4];
      p[0] = shade(r, k);
      p[1] = shade(g, k);
      p[2] = shade(b, k);
      p[3] = 0xff;
    }
  }
  return px;
}

}  // namespace

std::vector<PrintFile> FakePrinter::parse_files(const json &j) {
  if (!j.is_array()) {
    throw std::runtime_error("print files: expected a list [ {\"path\": ...}, ... ]");
  }
  std::vector<PrintFile> files;
  for (size_t i = 0; i < j.size(); i++) {
    const json &e = j[i];
    std::string where = "print files: item " + std::to_string(i + 1);
    if (!e.is_object() || !e.contains("path") || !e["path"].is_string() || e["path"].get<std::string>().empty()) {
      throw std::runtime_error(where + ": needs \"path\": \"name.gcode\"");
    }
    PrintFile f;
    f.path = e["path"].get<std::string>();
    try {
      f.estimated_time = e.value("estimated_time", f.estimated_time);
      f.filament_total = e.value("filament_total", f.filament_total);
      f.filament_weight_total = e.value("filament_weight_total", f.filament_weight_total);
      f.layer_height = e.value("layer_height", f.layer_height);
      f.object_height = e.value("object_height", f.object_height);
      f.extruder_temp = e.value("extruder_temp", f.extruder_temp);
      f.bed_temp = e.value("bed_temp", f.bed_temp);
      if (e.contains("color")) {
        std::string c = e["color"].get<std::string>();
        if (c.size() != 7 || c[0] != '#') throw std::runtime_error("bad color");
        f.color = std::stoul(c.substr(1), nullptr, 16);
      }
    } catch (const std::exception &) {
      throw std::runtime_error(where + " (" + f.path + "): numbers must be numbers, \"color\" like \"#e53935\"");
    }
    if (f.estimated_time <= 0 || f.layer_height <= 0 || f.object_height <= 0) {
      throw std::runtime_error(where + " (" + f.path + "): estimated_time, layer_height and object_height must be > 0");
    }
    // What the fake heaters reach at full power (bed ~125 °C), with a margin.
    if (f.extruder_temp < 0 || f.extruder_temp > 300 || f.bed_temp < 0 || f.bed_temp > 120) {
      throw std::runtime_error(where + " (" + f.path + "): extruder_temp must be 0..300, bed_temp 0..120");
    }
    files.push_back(f);
  }
  return files;
}

FakePrinter::FakePrinter(FakeMoonraker &moonraker, std::vector<PrintFile> files, Options options)
  : moonraker_(moonraker), files_(std::move(files)), opt_(options)
{
  json subscribed = moonraker_.result("printer.objects.subscribe");
  if (subscribed.is_object()) {
    z_offset_ = subscribed.value("/status/gcode_move/homing_origin/2"_json_pointer, 0.0);
  }

  // Start from the fixture's temperatures; ~40 s to 220 °C / 60 °C at heat_speed 1.
  heaters_ = {
    {"extruder", 31.2, 0, 0, 6.0, 0.012},
    {"heater_bed", 27.8, 0, 0, 1.2, 0.012},
  };

  write_thumbnails();

  json list = json::array();
  long now = (long)std::time(nullptr);
  for (size_t i = 0; i < files_.size(); i++) {
    list.push_back({{"path", files_[i].path}, {"modified", now - (long)i * 7200},
                    {"size", (long)files_[i].filament_total * 40}, {"permissions", "rw"}});
  }
  moonraker_.set_result("server.files.list", list);
  moonraker_.set_result("server.files.roots", json::array({
    {{"name", "config"}, {"path", "/opt/config"}, {"permissions", "rw"}},
    {{"name", "gcodes"}, {"path", opt_.gcodes_dir.empty() ? "/data" : opt_.gcodes_dir}, {"permissions", "rw"}},
  }));
  moonraker_.set_handler("server.files.metadata", [this](const json &params) {
    const PrintFile *f = find_file(params.value("filename", ""));
    return f ? metadata(*f) : json::object();
  });
  moonraker_.set_handler("printer.gcode.script", [this](const json &p) { return on_gcode(p); });
  moonraker_.set_handler("printer.print.start", [this](const json &p) { return on_print_start(p); });
  moonraker_.set_handler("printer.print.pause", [this](const json &) { return on_print_pause(); });
  moonraker_.set_handler("printer.print.resume", [this](const json &) { return on_print_resume(); });
  moonraker_.set_handler("printer.print.cancel", [this](const json &) { return on_print_cancel(); });

  thread_ = std::thread([this] { run(); });
}

FakePrinter::~FakePrinter() {
  // The app may still send requests: they must not reach this object.
  for (const char *m : {"server.files.metadata", "printer.gcode.script",
                        "printer.print.start", "printer.print.pause", "printer.print.resume",
                        "printer.print.cancel"}) {
    moonraker_.set_handler(m, nullptr);
  }
  stop_ = true;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void FakePrinter::write_thumbnails() const {
  if (opt_.gcodes_dir.empty()) {
    return;
  }
  for (const auto &f : files_) {
    std::string dir = opt_.gcodes_dir + "/" + dir_of(f.path);
    mkdirs(dir + ".thumbs");
    for (int size : {32, 300}) {
      test_png::write(dir + thumb_name(f, size), cube_image(size, f.color), size, size);
    }
  }
}

const PrintFile *FakePrinter::find_file(const std::string &path) const {
  for (const auto &f : files_) {
    if (f.path == path) return &f;
  }
  return nullptr;
}

json FakePrinter::metadata(const PrintFile &f) const {
  json thumbs = json::array();
  if (!opt_.gcodes_dir.empty()) {
    for (int size : {32, 300}) {
      thumbs.push_back({{"width", size}, {"height", size}, {"size", size * size * 4},
                        {"relative_path", thumb_name(f, size)}});
    }
  }
  return {
    {"filename", f.path}, {"size", (long)f.filament_total * 40}, {"modified", (long)std::time(nullptr)},
    {"slicer", "OrcaSlicer"}, {"slicer_version", "2.2.0"},
    {"layer_height", f.layer_height}, {"first_layer_height", f.layer_height},
    {"object_height", f.object_height}, {"filament_total", f.filament_total},
    {"filament_weight_total", f.filament_weight_total}, {"filament_type", "PLA"},
    {"estimated_time", f.estimated_time},
    {"first_layer_extr_temp", f.extruder_temp}, {"first_layer_bed_temp", f.bed_temp},
    {"thumbnails", thumbs},
  };
}

void FakePrinter::set_target(const std::string &heater, double target) {
  for (auto &h : heaters_) {
    if (h.name == heater) {
      h.target = std::clamp(target, 0.0, 350.0);
    }
  }
}

// Heater and Fine Tune commands; everything else is accepted and ignored.
json FakePrinter::on_gcode(const json &params) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::istringstream lines(params.value("script", ""));
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream words(line);
    std::string cmd, w;
    words >> cmd;
    cmd = upper(cmd);
    std::map<std::string, std::string> args;  // "S" -> "200", "HEATER" -> "extruder"
    while (words >> w) {
      size_t eq = w.find('=');
      if (eq != std::string::npos) args[upper(w.substr(0, eq))] = w.substr(eq + 1);
      else if (!w.empty()) args[upper(w.substr(0, 1))] = w.substr(1);
    }
    auto num = [&](const char *key) -> double {
      try { return std::stod(args.at(key)); } catch (...) { return -1; }
    };
    if (cmd == "SET_HEATER_TEMPERATURE" && args.count("HEATER") && num("TARGET") >= 0) {
      set_target(lower(args["HEATER"]), num("TARGET"));
    } else if ((cmd == "M104" || cmd == "M109") && num("S") >= 0) {
      set_target("extruder", num("S"));
    } else if ((cmd == "M140" || cmd == "M190") && num("S") >= 0) {
      set_target("heater_bed", num("S"));
    } else if (cmd == "TURN_OFF_HEATERS") {
      for (auto &h : heaters_) h.target = 0;
    } else if (cmd == "M220" && num("S") > 0) {
      pending_.merge_patch({{"gcode_move", {{"speed_factor", num("S") / 100}}}});
    } else if (cmd == "M221" && num("S") > 0) {
      pending_.merge_patch({{"gcode_move", {{"extrude_factor", num("S") / 100}}}});
    } else if (cmd == "SET_GCODE_OFFSET" && (args.count("Z") || args.count("Z_ADJUST"))) {
      try {
        z_offset_ = args.count("Z") ? std::stod(args["Z"]) : z_offset_ + std::stod(args["Z_ADJUST"]);
      } catch (...) {
        continue;
      }
      pending_.merge_patch({{"gcode_move", {{"homing_origin", {0.0, 0.0, z_offset_, 0.0}}}}});
    } else if (cmd == "SET_PRESSURE_ADVANCE" && num("ADVANCE") >= 0) {
      pending_.merge_patch({{"extruder", {{"pressure_advance", num("ADVANCE")}}}});
    }
  }
  return "ok";
}

json FakePrinter::on_print_start(const json &params) {
  std::lock_guard<std::mutex> lock(mutex_);
  const PrintFile *f = find_file(params.value("filename", ""));
  if (f == nullptr || phase_ != Phase::Idle) {
    return "ok";  // Moonraker would answer an error; the app ignores it either way
  }
  printing_ = f;
  progress_ = 0;
  total_duration_ = 0;
  phase_ = Phase::Heating;
  set_target("extruder", f->extruder_temp);
  set_target("heater_bed", f->bed_temp);
  pending_.merge_patch({
    {"print_stats", {{"filename", f->path}, {"state", "printing"}, {"print_duration", 0.0},
                     {"total_duration", 0.0}, {"filament_used", 0.0}, {"message", ""},
                     {"info", {{"total_layer", total_layers(*f)}, {"current_layer", 0}}}}},
    {"virtual_sdcard", {{"file_path", f->path}, {"progress", 0.0}, {"is_active", true}, {"file_position", 0}}},
    {"pause_resume", {{"is_paused", false}}},
    {"idle_timeout", {{"state", "Printing"}}},
  });
  return "ok";
}

json FakePrinter::on_print_pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (phase_ == Phase::Heating || phase_ == Phase::Printing) {
    phase_ = Phase::Paused;
    pending_.merge_patch({{"print_stats", {{"state", "paused"}}}, {"pause_resume", {{"is_paused", true}}},
                          {"virtual_sdcard", {{"is_active", false}}}});
  }
  return "ok";
}

json FakePrinter::on_print_resume() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (phase_ == Phase::Paused) {
    phase_ = Phase::Heating;  // back to Printing at once if still hot
    pending_.merge_patch({{"print_stats", {{"state", "printing"}}}, {"pause_resume", {{"is_paused", false}}},
                          {"virtual_sdcard", {{"is_active", true}}}});
  }
  return "ok";
}

json FakePrinter::on_print_cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (phase_ != Phase::Idle) {
    phase_ = Phase::Idle;
    for (auto &h : heaters_) h.target = 0;
    pending_.merge_patch({{"print_stats", {{"state", "cancelled"}}}, {"pause_resume", {{"is_paused", false}}},
                          {"virtual_sdcard", {{"is_active", false}}}, {"idle_timeout", {{"state", "Ready"}}},
                          {"output_pin fan0", {{"value", 0.0}}}, {"motion_report", {{"live_velocity", 0.0},
                                                                                  {"live_extruder_velocity", 0.0}}}});
  }
  return "ok";
}

json FakePrinter::tick(double dt) {
  std::lock_guard<std::mutex> lock(mutex_);
  static std::mt19937 rng(12345);
  std::normal_distribution<double> noise(0.0, 0.08);
  json st = json::object();

  // Heaters: full power until close, then proportional around the power
  // that holds the target against the heat loss.
  double hdt = dt * opt_.heat_speed;
  bool heated = true;
  for (auto &h : heaters_) {
    double hold = h.loss * (h.target - AMBIENT) / h.max_rate;
    h.power = h.target <= 0 ? 0 : std::clamp(hold + 0.08 * (h.target - h.temperature), 0.0, 1.0);
    h.temperature += (h.power * h.max_rate - h.loss * (h.temperature - AMBIENT)) * hdt;
    if (h.target > 0 && h.temperature < h.target - 2) heated = false;
    st[h.name] = {{"temperature", round2(h.temperature + noise(rng))}, {"target", h.target},
                  {"power", round2(h.power)}};
  }

  if (phase_ == Phase::Heating && heated) {
    phase_ = Phase::Printing;
  }
  if (printing_ && phase_ != Phase::Idle) {
    total_duration_ += dt * printing_->estimated_time / opt_.print_seconds;
    bool moving = phase_ == Phase::Printing;
    if (moving) {
      progress_ = std::min(1.0, progress_ + dt / opt_.print_seconds);
    }
    const PrintFile &f = *printing_;
    int layers = total_layers(f);
    int layer = progress_ > 0 ? std::min(layers, 1 + (int)(progress_ * layers)) : 0;
    double z = layer * f.layer_height;
    noise_phase_ += dt;
    double speed = moving ? 150 + 60 * std::sin(noise_phase_ * 1.7) : 0;
    double e_speed = moving ? f.filament_total / f.estimated_time * (speed / 150) : 0;
    st["print_stats"] = {{"print_duration", progress_ * f.estimated_time}, {"total_duration", total_duration_},
                         {"filament_used", progress_ * f.filament_total},
                         {"info", {{"total_layer", layers}, {"current_layer", layer}}}};
    st["virtual_sdcard"] = {{"progress", progress_}, {"file_position", (long)(progress_ * f.filament_total * 40)}};
    st["display_status"] = {{"progress", progress_}};
    st["motion_report"] = {{"live_velocity", round2(speed)}, {"live_extruder_velocity", round2(e_speed)}};
    st["gcode_move"] = {{"gcode_position", {100.0, 100.0, z, 0.0}}};
    st["output_pin fan0"] = {{"value", moving && layer > 1 ? 1.0 : 0.0}};

    if (progress_ >= 1.0) {
      phase_ = Phase::Idle;
      for (auto &h : heaters_) h.target = 0;
      pending_.merge_patch({{"print_stats", {{"state", "complete"}}}, {"virtual_sdcard", {{"is_active", false}}},
                            {"idle_timeout", {{"state", "Ready"}}}, {"output_pin fan0", {{"value", 0.0}}},
                            {"motion_report", {{"live_velocity", 0.0}, {"live_extruder_velocity", 0.0}}}});
    }
  }

  st.merge_patch(pending_);
  pending_ = json::object();
  return st;
}

void FakePrinter::run() {
  auto next = std::chrono::steady_clock::now();
  const double dt = opt_.tick_ms / 1000.0;
  while (!stop_) {
    next += std::chrono::milliseconds(opt_.tick_ms);
    std::this_thread::sleep_until(next);
    auto now = std::chrono::steady_clock::now();
    if (now > next + std::chrono::milliseconds(opt_.tick_ms)) {
      next = now;  // suspended (e.g. laptop sleep): don't replay the missed ticks
    }
    json st = tick(dt);
    if (!moonraker_.client_connected()) {
      continue;
    }
    try {
      moonraker_.push_status(st);
    } catch (const std::exception &) {
      // the client went away between the check and the send
    }
  }
}
