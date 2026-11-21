#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
namespace tabremoto {
enum class SecurityAction {
  Create,
  Unlock,
  Recover,
  Lock,
  Password,
  Secret,
  SecretFile,
  Identity,
  Connection,
  Remove,
  Trust,
  Export,
  Import,
  Backup,
  Restore,
  PrepareBoot,
  CheckBoot,
  Ssh,
  Rdp,
  Wifi,
  Vpn,
  Selftest
};
struct SecurityRequest {
  SecurityAction action = SecurityAction::Lock;
  std::array<char, 257> password{};
  std::array<char, 8193> value{};
  std::string file, host_ref, id, label, user, domain, ref, kind,
      transport = "auto", vpn_ref, auth = "ask", protocol = "ssh", host, group,
      fingerprint, previous, reason, collection;
  unsigned rdp_width = 1280, rdp_height = 720;
  bool rdp_reconnect = true;
  int port = 22;
  bool favorite = false, vpn_required = false;
  ~SecurityRequest();
};
struct SecurityStatus {
  bool ready = false, busy = false, exists = false, unlocked = false;
  unsigned revision = 0, generation = 0, connections = 0, identities = 0,
           secrets = 0, trust = 0;
  unsigned selftest = 0;
  uint32_t operation_ms = 0;
  std::string message = "Cofre bloqueado";
  std::shared_ptr<const std::string> metadata;
};
void security_start();
SecurityStatus security_status();
// Ownership transfers only when accepted. Callers delete rejected jobs.
bool security_submit(SecurityRequest *request);
void security_activity();
bool security_busy();
} // namespace tabremoto
