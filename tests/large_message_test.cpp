// A Moonraker message larger than 64 kB (on zMod the printer.objects.subscribe
// reply is ~500 kB, mostly configfile) must not break the messages after it.
// guppyscreen trims malloc and libhv drops its receive buffer after such a
// message.

#include "doctest/doctest.h"
#include "hv/websocket_parser.h"
#include "hv/hbase.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include <algorithm>
#include <fstream>

namespace {

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

TEST_CASE("WebSocket frame uses a network-order 64-bit length") {
  const auto flags = static_cast<websocket_flags>(WS_FIN | WS_OP_TEXT);
  for (size_t len : {size_t(65535), size_t(65536), size_t(0x12345)}) {
    std::string payload(len, 'a');
    std::vector<char> frame(websocket_calc_frame_size(flags, len));
    REQUIRE(websocket_build_frame(frame.data(), flags, nullptr, payload.data(), len) == frame.size());
    CHECK(static_cast<uint8_t>(frame[0]) == 0x81);
    const size_t header = len <= 65535 ? 4 : 10;
    CHECK(static_cast<uint8_t>(frame[1]) == (header == 4 ? 126 : 127));
    uint64_t decoded = 0;
    for (size_t i = 2; i < header; ++i) decoded = (decoded << 8) | static_cast<uint8_t>(frame[i]);
    CHECK(decoded == len);
    CHECK(std::equal(payload.begin(), payload.end(), frame.begin() + header));
  }
}

TEST_CASE("size parser handles terabyte suffix without an oversized shift") {
  CHECK(hv_parse_size("2K") == 2048);
  CHECK(hv_parse_size("1T") == static_cast<size_t>(uint64_t(1) << 40));
}

TEST_CASE("large subscribe reply is followed by live updates") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));

  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  // 300 macros of ~1 kB each, like a zMod config.
  json &config = sub["status"]["configfile"]["config"];
  for (int i = 0; i < 300; i++) {
    config["gcode_macro BIG_" + std::to_string(i)] = {{"gcode", std::string(1000, 'G')}};
  }
  REQUIRE(sub.dump().size() > 300 * 1000);
  moonraker.set_result("printer.objects.subscribe", sub);

  app::start(moonraker);
  app::wait_ready(moonraker);

  auto extruder = ui::row_texts("Extruder");
  CHECK_MESSAGE(contains(extruder, "31"), "Extruder row: " << ui::join(extruder));

  // Small messages after the big one, then another big one.
  for (int i = 0; i < 3; i++) {
    double t = 200.0 + i;
    moonraker.push_status({{"extruder", {{"temperature", t}}}});
    std::string want = std::to_string((int)t);
    bool updated = app::wait_until([&] { return contains(ui::row_texts("Extruder"), want); });
    CHECK_MESSAGE(updated, "want " << want << "; Extruder row: " << ui::join(ui::row_texts("Extruder")));
  }

  moonraker.push_status({{"configfile", sub["status"]["configfile"]}, {"heater_bed", {{"temperature", 55.0}}}});
  bool bed = app::wait_until([] { return contains(ui::row_texts("Bed"), "55"); });
  CHECK_MESSAGE(bed, "Bed row: " << ui::join(ui::row_texts("Bed")));

  moonraker.push_status({{"extruder", {{"temperature", 210.0}}}});
  bool after = app::wait_until([] { return contains(ui::row_texts("Extruder"), "210"); });
  CHECK_MESSAGE(after, "Extruder row: " << ui::join(ui::row_texts("Extruder")));
}
