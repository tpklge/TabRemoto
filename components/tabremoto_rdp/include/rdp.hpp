#pragma once
#include <array>
#include "rdp_damage.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace tabremoto {
struct RdpRequest {
  std::string host, user, domain, fingerprint, transport="auto", vpn_profile;
  unsigned port = 3389, width = 1280, height = 720;
  bool reconnect = true, require_vpn=false;
  std::array<char, 257> password{};
  ~RdpRequest();
};
struct RdpFrame {
  unsigned width = 0, height = 0, stride = 0;
  std::shared_ptr<uint8_t> pixels;
  size_t bytes = 0, capacity = 0;
  uint64_t sequence=0;RdpDamage damage;
};
struct RdpStatus {
  enum Phase { Idle, Connecting, Verify, Ready, Failed, Closed, Closing, Suspended };
  Phase phase = Idle;
  unsigned revision = 0, frames = 0, key_events = 0, pointer_events = 0, left_clicks=0, right_clicks=0;
  uint32_t error = 0, selected_protocol = 0;
  uint64_t copied_pixels=0;unsigned copy_max_us=0,paint_calls=0;
  unsigned reconnect_attempts = 0, reconnections = 0, resize_errors = 0;
  bool clipboard_ready = false, dynamic_resize = false, resizing = false;
  unsigned width = 1280, height = 720;
  std::string host, user, transport, vpn_profile;
  std::string detail = "RDP pronto", fingerprint;
  std::shared_ptr<const RdpFrame> frame;
};
struct RdpClipboardStatus {
  bool ready = false, remote_available = false, waiting = false;
  unsigned revision = 0, sent = 0, received = 0;
  std::string text, detail = "Clipboard nao negociado";
};
RdpClipboardStatus rdp_clipboard_status();
bool rdp_clipboard_offer(const std::string &);
bool rdp_clipboard_fetch();
bool rdp_resize(unsigned width, unsigned height);
constexpr unsigned rdp_session_limit = 2;
struct RdpTab {
  unsigned id;
  RdpStatus status;
};
std::vector<RdpTab> rdp_sessions();
unsigned rdp_selected();
bool rdp_select(unsigned);
bool rdp_new_session();
void rdp_disconnect_all();
bool rdp_suspend_for_vault(unsigned timeout_ms=30000);
void rdp_resume_after_vault();
bool rdp_os_selftest();
std::string rdp_os_detail();
void rdp_start();
bool rdp_connect(RdpRequest *);
void rdp_disconnect();
bool rdp_accept(const std::string &);
RdpStatus rdp_status();
bool rdp_key(uint16_t code, bool down, bool unicode = false);
bool rdp_pointer(uint16_t x, uint16_t y, uint16_t flags);
bool rdp_click(uint16_t x,uint16_t y,bool right=false);
} // namespace tabremoto
