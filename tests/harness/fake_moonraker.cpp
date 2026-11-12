#include "fake_moonraker.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <stdexcept>

// Ask the kernel for a free port. Racy in theory, fine for local tests.
static int pick_free_port() {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  socklen_t len = sizeof(addr);
  if (s < 0 || bind(s, (sockaddr *)&addr, sizeof(addr)) != 0 ||
      getsockname(s, (sockaddr *)&addr, &len) != 0) {
    throw std::runtime_error("FakeMoonraker: cannot pick a free port");
  }
  close(s);
  return ntohs(addr.sin_port);
}

FakeMoonraker::FakeMoonraker(const std::string &fixture_path)
  : port_(pick_free_port())
{
  std::ifstream in(fixture_path);
  if (!in) {
    throw std::runtime_error("FakeMoonraker: cannot open fixture " + fixture_path);
  }
  results_ = json::parse(in);

  hlog_disable();  // libhv would otherwise write libhv.<date>.log into the working directory

  service_.onopen = [this](const WebSocketChannelPtr &channel, const HttpRequestPtr &) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = channel;
  };
  service_.onmessage = [this](const WebSocketChannelPtr &channel, const std::string &msg) {
    on_message(channel, msg);
  };
  service_.onclose = [this](const WebSocketChannelPtr &channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (client_ == channel) {
      client_.reset();
    }
  };

  server_.registerWebSocketService(&service_);
  server_.setHost("127.0.0.1");
  server_.setPort(port_);
  server_.setThreadNum(1);
  if (server_.start() != 0) {
    throw std::runtime_error("FakeMoonraker: cannot start server");
  }
}

FakeMoonraker::~FakeMoonraker() {
  server_.stop();
}

void FakeMoonraker::forget_requests() {
  std::lock_guard<std::mutex> lock(mutex_);
  decltype(requests_)().swap(requests_);
  decltype(gcodes_)().swap(gcodes_);
  decltype(unhandled_)().swap(unhandled_);
}

void FakeMoonraker::drop_client() {
  WebSocketChannelPtr c;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    c = client_;
  }
  if (c) {
    c->close();
  }
}

void FakeMoonraker::on_message(const WebSocketChannelPtr &channel, const std::string &msg) {
  json req = json::parse(msg);
  std::string method = req.value("method", "");
  json params = req.contains("params") ? req["params"] : json::object();

  json result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.emplace_back(method, params);
    if (method == "printer.gcode.script") {
      gcodes_.push_back(params.value("script", ""));
      result = "ok";
    } else if (results_.contains(method)) {
      result = results_[method];
    } else {
      unhandled_.push_back(method);
      result = json::object();
    }
  }

  if (req.contains("id")) {
    json resp = {{"jsonrpc", "2.0"}, {"id", req["id"]}, {"result", result}};
    channel->send(resp.dump());
  }
}

void FakeMoonraker::send(const json &j) {
  WebSocketChannelPtr client;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    client = client_;
  }
  if (!client) {
    throw std::runtime_error("FakeMoonraker: no client connected");
  }
  client->send(j.dump());
}

void FakeMoonraker::set_result(const std::string &method, const json &result) {
  std::lock_guard<std::mutex> lock(mutex_);
  results_[method] = result;
}

void FakeMoonraker::push_status(const json &status) {
  push_notification("notify_status_update", json::array({status, 1000.0}));
}

void FakeMoonraker::push_notification(const std::string &method, const json &params) {
  send({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

bool FakeMoonraker::client_connected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return client_ != nullptr;
}

int FakeMoonraker::request_count(const std::string &method) const {
  std::lock_guard<std::mutex> lock(mutex_);
  int n = 0;
  for (const auto &r : requests_) {
    if (r.first == method) n++;
  }
  return n;
}

std::vector<json> FakeMoonraker::requests(const std::string &method) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<json> out;
  for (const auto &r : requests_) {
    if (r.first == method) out.push_back(r.second);
  }
  return out;
}

std::vector<std::string> FakeMoonraker::gcodes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return gcodes_;
}

std::vector<std::string> FakeMoonraker::unhandled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return unhandled_;
}
