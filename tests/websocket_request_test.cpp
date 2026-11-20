#include "doctest/doctest.h"
#include "app.h"
#include "websocket_client.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

TEST_CASE("parallel JSON-RPC requests keep their own replies and can send from a callback") {
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  server.set_handler("test.echo", [](const json &params) { return params.at("token"); });
  server.set_result("test.nested", "ok");

  std::promise<void> connected, nested;
  std::atomic<int> received{0}, mismatched{0};
  KWebSocketClient client(nullptr);
  const std::string url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/websocket";
  client.connect(url.c_str(), [&] { connected.set_value(); }, [] {});
  REQUIRE(connected.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);

  constexpr int count = 128;
  std::atomic<int> arrivals{0};
  auto sender = [&](int first) {
    for (int token = first; token < 2 * count; token += 2) {
      ++arrivals;
      while (arrivals < 2 * (token / 2 + 1)) std::this_thread::yield();
      client.send_jsonrpc("test.echo", {{"token", token}}, [&, token](json &reply) {
        if (reply.at("result") != token) ++mismatched;
        if (++received == 2 * count) {
          client.send_jsonrpc("test.nested", [&](json &next) {
            if (next.at("result") != "ok") ++mismatched;
            nested.set_value();
          });
        }
      });
    }
  };
  std::thread second(sender, 1);
  sender(0);
  second.join();
  CHECK(nested.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  client.stop();
  CHECK(received == 2 * count);
  CHECK(mismatched == 0);
}

TEST_CASE("an unanswered JSON-RPC request times out without closing the socket") {
  using namespace std::chrono_literals;
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  server.set_result("test.unanswered", "late");
  server.hold_next_reply("test.unanswered");
  server.set_result("test.after_timeout", "ok");

  std::promise<void> connected;
  std::promise<json> expired, answered;
  std::atomic<int> callbacks{0};
  KWebSocketClient client(nullptr, 100ms);
  const auto url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/websocket";
  client.connect(url.c_str(), [&] { connected.set_value(); }, [] {});
  REQUIRE(connected.get_future().wait_for(5s) == std::future_status::ready);

  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> pending = token;
  client.send_jsonrpc("test.unanswered", [&, token](json &reply) {
    if (++callbacks == 1) expired.set_value(reply);
  });
  token.reset();

  auto timeout = expired.get_future();
  REQUIRE(timeout.wait_for(5s) == std::future_status::ready);
  CHECK(timeout.get().at("error").at("message") == "Moonraker request timed out");
  server.release_reply();
  client.send_jsonrpc("test.after_timeout", [&](json &next) { answered.set_value(next); });
  auto reply = answered.get_future();
  REQUIRE(reply.wait_for(5s) == std::future_status::ready);
  CHECK(reply.get().at("result") == "ok");
  CHECK(pending.expired());
  CHECK(callbacks == 1);
  CHECK(server.request_count("test.unanswered") == 1);
  client.stop();
}
