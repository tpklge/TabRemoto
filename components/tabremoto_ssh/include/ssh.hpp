#pragma once
#include "terminal.hpp"
#include <array>
#include <string>
#include <vector>
namespace tabremoto {
struct SshRequest {
  std::string key_file, host, user, identity, key,
      transport = "auto", fingerprint, command, vpn_profile;
  std::array<char, 257> password{}, passphrase{};
  int port = 22;
  bool require_vpn = false;
  ~SshRequest();
};
struct SshStatus {
  enum Phase {
    Idle,
    Connecting,
    Verify,
    Authenticating,
    Ready,
    Failed,
    Closed
  };
  Phase phase = Idle;
  unsigned revision = 0;
  std::string detail = "SSH parado", fingerprint, key_type, host, user, output,
              listing, local_listing, transport, vpn_profile;
  std::shared_ptr<const TerminalFrame> screen;
  uint64_t rx = 0, tx = 0, transfer_bytes = 0, transfer_total = 0;
  unsigned queued = 0;
  bool transfer = false, transfer_ok = false;
};
struct FileRequest {
  enum Op { List, Download, Upload, ScpDownload, ScpUpload, Remove };
  Op operation = List;
  std::string remote = ".", local;
};
constexpr unsigned ssh_session_limit = 4;
struct SshTab {
  unsigned id;
  SshStatus status;
};
std::vector<SshTab> ssh_sessions();
unsigned ssh_selected();
bool ssh_select(unsigned);
bool ssh_new_session();
void ssh_disconnect_all();
void ssh_start();
bool ssh_connect(SshRequest *request);
void ssh_disconnect();
void ssh_cancel_files();
bool ssh_accept(const std::string &fingerprint);
SshStatus ssh_status();
bool ssh_key(TerminalKey, unsigned modifiers = 0);
bool ssh_input(const std::string &text);
bool ssh_files(const FileRequest &request);
// Only sandbox paths below app transfers; no parent traversal/symlinks
// accepted.
bool ssh_local_list();
bool ssh_local_path(const std::string &name, std::string &absolute);
} // namespace tabremoto
