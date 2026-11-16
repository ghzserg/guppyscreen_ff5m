// guppy_preview: the real guppyscreen on the PC, its screen in a browser.
//
//   build-test/default/guppy_preview [--config preview/guppyconfig.json]
//       [--files preview/print_files.json] [--print-seconds 120]
//       [--port 8080] [--fixture tests/fixtures/ad5m_basic.json]
//
// The parent process watches the config and the print files list. Each time
// one changes and is valid, the app (a child process) is restarted;
// guppyscreen's singletons can't be re-initialised in one process. Invalid
// input keeps the running app and is reported on the page and in the terminal.
//
// The child runs the app like the tests do (tests/harness): an in-memory
// 800x480 display, a FakeMoonraker answering from the fixture with a
// FakePrinter behind it (files, prints, heaters), plus an HTTP server for the
// page (frames out, mouse in). See preview/README.md.

#include "fake_hw.h"
#include "fake_moonraker.h"
#include "fake_printer.h"
#include "png_writer.h"
#include "preview/access.h"

#include "config.h"
#include "grid_layout.h"
#include "guppyscreen.h"

#include "hv/HttpServer.h"

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace {

struct Options {
  std::string config = std::string(REPO_DIR) + "/preview/guppyconfig.json";
  std::string fixture = std::string(REPO_DIR) + "/tests/fixtures/ad5m_basic.json";
  std::string files = std::string(REPO_DIR) + "/preview/print_files.json";
  double print_seconds = 120;
  std::string run_dir = std::string(REPO_DIR) + "/build-test/preview-run";
  std::string host = "127.0.0.1";  // WSL2 forwards it to Windows localhost
  int port = 8080;
};

std::string read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_file(const std::string &path, const std::string &content) {
  std::ofstream(path, std::ios::binary) << content;
}

// Why `text` can't be used as the print files list, or "" if it can.
// `name` prefixes the message.
std::string files_text_error(const std::string &text, const std::string &name) {
  try {
    FakePrinter::parse_files(json::parse(text));
  } catch (const std::exception &e) {
    return name + ": " + e.what();
  }
  return "";
}

// Why `text` can't be used as the config, or "" if it can.
std::string config_text_error(const std::string &text, const std::string &path) {
  try {
    json j = json::parse(text);
    if (!j.is_object()) {
      return path + ": the top level must be an object { ... }";
    }
    // What prepare_run_dir() needs; the app itself copes with the rest.
    if (j.contains("default_printer") && !j["default_printer"].is_string()) {
      return path + ": \"default_printer\" must be a string";
    }
    if (j.contains("printers")) {
      const json &p = j["printers"];
      if (!p.is_object()) {
        return path + ": \"printers\" must be an object { \"<name>\": { ... } }";
      }
      std::string df = j.value("default_printer", "FF5M");
      if (p.contains(df) && !p[df].is_object()) {
        return path + ": \"printers\": \"" + df + "\" must be an object";
      }
    }
  } catch (const json::exception &e) {
    return path + ": " + e.what();
  }
  return "";
}

std::string files_error(const std::string &path) {
  std::ifstream in(path);
  return in ? files_text_error(read_file(path), path) : "cannot open " + path;
}

std::string config_error(const std::string &path) {
  std::ifstream in(path);
  return in ? config_text_error(read_file(path), path) : "cannot open " + path;
}

// Replaces the file at once (write a temporary file, then rename), so the
// parent never reads half of it.
bool replace_file(const std::string &path, const std::string &content) {
  std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    out << content;
    if (!out) return false;
  }
  return rename(tmp.c_str(), path.c_str()) == 0;
}

// ---------------------------------------------------------------- child --

// Mouse events from the page, read by LVGL one per poll so that a quick
// click (press and release between two polls) isn't lost.
std::mutex touch_mutex;
std::deque<lv_indev_data_t> touch_queue;
lv_indev_data_t touch_last{};

void touch_read(lv_indev_drv_t *, lv_indev_data_t *data) {
  std::lock_guard<std::mutex> lock(touch_mutex);
  if (!touch_queue.empty()) {
    touch_last = touch_queue.front();
    touch_queue.pop_front();
  }
  *data = touch_last;
  data->continue_reading = !touch_queue.empty();
}

void push_touch(int x, int y, bool down) {
  lv_indev_data_t d{};
  d.point.x = std::clamp(x, 0, fake_hw::WIDTH - 1);
  d.point.y = std::clamp(y, 0, fake_hw::HEIGHT - 1);
  d.state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  std::lock_guard<std::mutex> lock(touch_mutex);
  touch_queue.push_back(d);
}

// The screen as RGBA bytes.
std::string frame_rgba() {
  std::string out(fake_hw::WIDTH * fake_hw::HEIGHT * 4, '\0');
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  size_t i = 0;
  for (int y = 0; y < fake_hw::HEIGHT; y++) {
    for (int x = 0; x < fake_hw::WIDTH; x++) {
      lv_color_t c = fake_hw::pixel(x, y);
      out[i++] = c.ch.red;
      out[i++] = c.ch.green;
      out[i++] = c.ch.blue;
      out[i++] = (char)0xff;
    }
  }
  return out;
}

// Copies the user's config into the run dir with what the PC needs instead
// of the printer's paths, and the fake Moonraker's address.
std::string prepare_run_dir(const Options &o, int moonraker_port) {
  std::string themes = o.run_dir + "/themes";
  std::string thumbs = o.run_dir + "/thumbnails";
  mkdir(o.run_dir.c_str(), 0755);
  mkdir(themes.c_str(), 0755);
  mkdir(thumbs.c_str(), 0755);
  for (const char *t : {"blue", "green", "pink", "purple", "red", "yellow"}) {
    std::string from = std::string(REPO_DIR) + "/themes/" + t + ".json";
    write_file(themes + "/" + t + ".json", read_file(from));
  }

  json c = json::parse(read_file(o.config));
  c["log_path"] = o.run_dir + "/guppyscreen.log";
  c["thumbnail_path"] = thumbs;
  c["display_sleep_sec"] = -1;  // no screen saver
  if (!c.contains("default_printer")) {
    c["default_printer"] = "FF5M";
  }
  std::string df = c["default_printer"].get<std::string>();
  json &p = c["printers"][df];
  p["moonraker_host"] = "127.0.0.1";
  p["moonraker_port"] = moonraker_port;

  std::string path = o.run_dir + "/guppyconfig.json";
  write_file(path, c.dump(2));
  return path;
}

[[noreturn]] void run_app(const Options &o) {
  // guppyscreen also logs to stdout; keep the terminal for the preview's own
  // messages. The log is in <run dir>/guppyscreen.log as well.
  std::string out = o.run_dir + "/app.out";
  if (!freopen(out.c_str(), "w", stdout)) {
    std::cerr << "guppy_preview: cannot write " << out << std::endl;
  }

  FakeMoonraker moonraker(o.fixture);
  FakePrinter::Options po;
  po.print_seconds = o.print_seconds;
  po.gcodes_dir = o.run_dir + "/gcodes";
  FakePrinter printer(moonraker, FakePrinter::parse_files(json::parse(read_file(o.files))), po);
  std::string config_path = prepare_run_dir(o, moonraker.port());
  Config::get_instance()->init(config_path, o.run_dir + "/thumbnails");
  GuppyScreen::init(fake_hw::hal_init);

  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = touch_read;
    lv_indev_drv_register(&drv);
  }

  const std::string page_path = std::string(REPO_DIR) + "/preview/page.html";
  const std::string error_path = o.run_dir + "/config_error.txt";

  hv::HttpService router;
  // Only the preview's own page: see preview/access.h.
  router.preprocessor = [&o](HttpRequest *req, HttpResponse *resp) {
    std::string host = req->GetHeader("Host"), origin = req->GetHeader("Origin");
    if (preview_access::allowed(host, origin, o.port)) {
      return HTTP_STATUS_NEXT;
    }
    std::cerr << "guppy_preview: refused " << req->Path() << " (Host: " << host
              << ", Origin: " << origin << ")" << std::endl;
    resp->body = "forbidden: open the preview at http://localhost:" + std::to_string(o.port) + "\n";
    return 403;
  };
  router.GET("/", [page_path](HttpRequest *, HttpResponse *resp) {
    resp->content_type = TEXT_HTML;
    resp->body = read_file(page_path);  // read each time: edits show on reload
    return 200;
  });
  router.GET("/status", [&o, error_path](HttpRequest *, HttpResponse *resp) {
    return resp->Json(json{
      {"frame", fake_hw::flush_count.load()},
      {"error", read_file(error_path)},
      {"config", o.config},
      {"fixture", o.fixture},
      {"files", o.files},
      {"pid", getpid()},
    });
  });
  // The page's editor: ?name=config (guppyconfig.json) or ?name=files (print files).
  auto editable = [&o](HttpRequest *req) -> const std::string * {
    std::string name = req->GetParam("name");
    return name == "config" ? &o.config : name == "files" ? &o.files : nullptr;
  };
  router.GET("/file", [editable](HttpRequest *req, HttpResponse *resp) {
    const std::string *path = editable(req);
    if (!path) {
      std::cerr << "guppy_preview: GET " << req->url << ": unknown file" << std::endl;
      return 404;
    }
    resp->content_type = TEXT_PLAIN;
    resp->SetHeader("Cache-Control", "no-store");
    resp->body = read_file(*path);
    std::cerr << "guppy_preview: GET " << req->url << ": " << resp->body.size() << " bytes of " << *path << std::endl;
    return 200;
  });
  // Saves the text if it is valid; the parent sees the change and restarts
  // the app (this process) with it.
  router.POST("/file", [editable, &o](HttpRequest *req, HttpResponse *resp) {
    const std::string *path = editable(req);
    if (!path) return 404;
    std::string err = path == &o.config ? config_text_error(req->body, *path)
                                        : files_text_error(req->body, *path);
    if (err.empty() && !replace_file(*path, req->body)) {
      err = "cannot write " + *path;
    }
    resp->Json(json{{"ok", err.empty()}, {"error", err}});
    return err.empty() ? 200 : 400;
  });
  // Every grid item the panels placed: for the page's reference list.
  router.GET("/layout-items", [](HttpRequest *, HttpResponse *resp) {
    json out = json::array();
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    std::set<std::string> seen;
    for (const auto &i : GridLayout::placed()) {
      if (!seen.insert(i.panel + "/" + i.id).second) continue;  // PID and Pro exist twice
      out.push_back({{"panel", i.panel}, {"id", i.id}, {"col", i.col}, {"row", i.row},
                     {"col_span", i.col_span}, {"row_span", i.row_span}, {"hidden", i.hidden},
                     {"grid", {i.grid_cols, i.grid_rows}}});
    }
    return resp->Json(out);
  });
  router.GET("/frame.rgba", [](HttpRequest *req, HttpResponse *resp) {
    std::string since = req->GetParam("since");
    unsigned frame = fake_hw::flush_count.load();
    if (!since.empty() && std::to_string(frame) == since) {
      return 204;
    }
    resp->content_type = APPLICATION_OCTET_STREAM;
    resp->SetHeader("X-Frame", std::to_string(frame));
    resp->SetHeader("Cache-Control", "no-store");
    resp->body = frame_rgba();
    return 200;
  });
  router.GET("/frame.png", [&o](HttpRequest *, HttpResponse *resp) {
    std::string rgba = frame_rgba();
    std::string path = o.run_dir + "/frame.png";
    test_png::write(path, std::vector<unsigned char>(rgba.begin(), rgba.end()),
                    fake_hw::WIDTH, fake_hw::HEIGHT);
    resp->content_type = IMAGE_PNG;
    resp->SetHeader("Cache-Control", "no-store");
    resp->body = read_file(path);
    return 200;
  });
  // {"x": 0..799, "y": 0..479, "down": true|false}
  router.POST("/touch", [](HttpRequest *req, HttpResponse *resp) {
    try {
      json j = json::parse(req->body);
      push_touch(j.at("x").get<int>(), j.at("y").get<int>(), j.at("down").get<bool>());
    } catch (const json::exception &e) {
      resp->body = e.what();
      return 400;
    }
    return 200;
  });
  // A tap: press and release at (x, y), e.g. curl -d '{"x":100,"y":200}'.
  router.POST("/tap", [](HttpRequest *req, HttpResponse *resp) {
    try {
      json j = json::parse(req->body);
      int x = j.at("x").get<int>(), y = j.at("y").get<int>();
      push_touch(x, y, true);
      push_touch(x, y, false);
    } catch (const json::exception &e) {
      resp->body = e.what();
      return 400;
    }
    return 200;
  });

  hv::HttpServer server(&router);
  server.setHost(o.host.c_str());
  server.setPort(o.port);
  server.setThreadNum(2);
  if (server.start() != 0) {
    std::cerr << "guppy_preview: cannot listen on " << o.host << ":" << o.port << std::endl;
    std::_Exit(3);
  }

  // GuppyScreen::loop(), minus the screen-saver bookkeeping it does for the
  // real backlight.
  while (true) {
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      lv_timer_handler();
    }
    usleep(5000);
  }
}

// --------------------------------------------------------------- parent --

// Modification time in ns (editors may save twice within a second), 0 if missing.
long long mtime(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 ? st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec : 0;
}

pid_t child = 0;

void stop_child() {
  if (child > 0) {
    kill(child, SIGTERM);
    waitpid(child, nullptr, 0);
    child = 0;
  }
}

void on_signal(int) {
  stop_child();
  std::_Exit(0);
}

pid_t start_child(const Options &o) {
  std::fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    run_app(o);
  }
  return pid;
}

// `j` on one line with a space after ':' and ',': {"col": 2, "row": 0}.
std::string one_line(const nlohmann::ordered_json &j) {
  if (j.is_object()) {
    std::string out;
    for (auto it = j.begin(); it != j.end(); ++it) {
      out += (out.empty() ? "" : ", ") + json(it.key()).dump() + ": " + one_line(it.value());
    }
    return "{" + out + "}";
  }
  if (j.is_array()) {
    std::string out;
    for (const auto &v : j) out += (out.empty() ? "" : ", ") + one_line(v);
    return "[" + out + "]";
  }
  return j.dump();
}

// The "layout" block for guppyconfig.json, one line per item.
std::string format_layout(const nlohmann::ordered_json &layout) {
  std::string out = "\"layout\": {\n";
  bool first_panel = true;
  for (auto p = layout.begin(); p != layout.end(); ++p) {
    out += first_panel ? "" : ",\n";
    first_panel = false;
    out += "  " + json(p.key()).dump() + ": {\n";
    for (const char *key : {"columns", "rows", "padding", "gap"}) {
      if (p.value().contains(key)) out += "    \"" + std::string(key) + "\": " + one_line(p.value()[key]) + ",\n";
    }
    out += "    \"items\": {\n";
    const auto &items = p.value()["items"];
    size_t width = 0;
    for (auto it = items.begin(); it != items.end(); ++it) width = std::max(width, it.key().size());
    bool first = true;
    for (auto it = items.begin(); it != items.end(); ++it) {
      out += first ? "" : ",\n";
      first = false;
      std::string key = json(it.key()).dump() + ":";
      out += "      " + key + std::string(width + 4 - key.size(), ' ') + one_line(it.value());
    }
    out += "\n    }\n  }";
  }
  return out + "\n}\n";
}

// --dump-layout: start the app once, print its "layout" block, exit.
[[noreturn]] void dump_layout(Options o) {
  int out_fd = dup(1);
  o.run_dir += "/dump";
  mkdir(o.run_dir.c_str(), 0755);
  if (!freopen((o.run_dir + "/app.out").c_str(), "w", stdout)) {
    std::cerr << "guppy_preview: cannot write to " << o.run_dir << std::endl;
  }
  FakeMoonraker moonraker(o.fixture);
  Config::get_instance()->init(prepare_run_dir(o, moonraker.port()), o.run_dir + "/thumbnails");
  GuppyScreen::init(fake_hw::hal_init);
  std::string text;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    text = format_layout(GridLayout::current_layout());
  }
  if (write(out_fd, text.data(), text.size()) < 0) std::_Exit(1);
  std::_Exit(0);
}

}  // namespace

int main(int argc, char **argv) {
  Options o;
  bool dump = false;
  std::vector<char *> args(argv, argv + argc);
  for (auto it = args.begin(); it != args.end();) {
    if (std::string(*it) == "--dump-layout") { dump = true; it = args.erase(it); } else ++it;
  }
  argc = args.size();
  argv = args.data();
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string a = argv[i];
    if (a == "--config") o.config = argv[i + 1];
    else if (a == "--fixture") o.fixture = argv[i + 1];
    else if (a == "--files") o.files = argv[i + 1];
    else if (a == "--print-seconds") o.print_seconds = std::max(1.0, std::atof(argv[i + 1]));
    else if (a == "--port") o.port = std::atoi(argv[i + 1]);
    else if (a == "--host") o.host = argv[i + 1];
    else if (a == "--run-dir") o.run_dir = argv[i + 1];
    else {
      std::cerr << "usage: " << argv[0]
                << " [--config FILE] [--files FILE] [--print-seconds N] [--fixture FILE]"
                   " [--port N] [--host ADDR] [--run-dir DIR] [--dump-layout]\n"
                   "  --dump-layout: print the \"layout\" block of every screen as it is with the config, and exit\n";
      return 2;
    }
  }
  char buf[PATH_MAX];
  if (realpath(o.config.c_str(), buf)) o.config = buf;
  if (realpath(o.files.c_str(), buf)) o.files = buf;
  mkdir(o.run_dir.c_str(), 0755);
  const std::string error_path = o.run_dir + "/config_error.txt";

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  if (dump) {
    std::string err = config_error(o.config);
    if (!err.empty()) {
      std::cerr << "guppy_preview: " << err << std::endl;
      return 1;
    }
    dump_layout(o);
  }

  auto input_error = [&o] {
    std::string e = config_error(o.config);
    return e.empty() ? files_error(o.files) : e;
  };
  std::string err = input_error();
  if (!err.empty()) {
    std::cerr << "guppy_preview: " << err << std::endl;
    return 1;
  }
  write_file(error_path, "");

  std::cout << "guppy_preview: config " << o.config << "\n"
            << "guppy_preview: print files " << o.files << " (a print takes " << o.print_seconds << " s)\n"
            << "guppy_preview: open http://localhost:" << o.port << " in a browser; Ctrl+C to stop"
            << std::endl;

  auto inputs_mtime = [&o] { return std::make_pair(mtime(o.config), mtime(o.files)); };
  auto seen = inputs_mtime();
  child = start_child(o);
  while (true) {
    usleep(300 * 1000);

    int status;
    if (waitpid(child, &status, WNOHANG) == child) {
      std::cerr << "guppy_preview: the app exited (status " << status << "), see "
                << o.run_dir << "/app.out; waiting for a change" << std::endl;
      child = 0;
    }

    auto now = inputs_mtime();
    if (now == seen) {
      continue;
    }
    seen = now;
    usleep(100 * 1000);  // let the editor finish writing
    err = input_error();
    write_file(error_path, err);
    if (!err.empty()) {
      std::cerr << "guppy_preview: " << err << "\nguppy_preview: keeping the running app" << std::endl;
      continue;
    }
    std::cout << "guppy_preview: input changed, restarting" << std::endl;
    stop_child();
    child = start_child(o);
  }
}
