#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
struct cJSON;
namespace tabremoto {
// Every operation is serialized by the runtime worker. No singleton crypto
// state.
class Vault {
public:
  explicit Vault(std::string directory);
  ~Vault();
  Vault(const Vault &) = delete;
  Vault &operator=(const Vault &) = delete;
  bool exists() const;
  bool unlocked() const { return open_; }
  bool create(const std::string &password);
  bool unlock(const std::string &password, bool recover = false);
  void lock();
  bool change_password(const std::string &password);
  bool secret(std::string &id, const std::string &label,
              const std::string &kind, const std::string &value);
  bool identity(std::string &id, const std::string &label,
                const std::string &user, const std::string &domain,
                const std::string &secret_id, const std::string &auth,
                const std::string &host_id = "");
  bool connection(std::string &id, const std::string &label,
                  const std::string &protocol, const std::string &host,
                  int port, const std::string &identity_id, bool favorite,
                  const std::string &group = "",
                  const std::string &transport = "auto",
                  const std::string &vpn = "", bool required = false);
  bool remove(const std::string &collection, const std::string &id);
  // 0 unknown, 1 match, 2 changed, -1 invalid/locked. No implicit TOFU.
  int check_trust(const std::string &protocol, const std::string &host,
                  int port, const std::string &fingerprint);
  bool trust(const std::string &protocol, const std::string &host, int port,
             const std::string &fingerprint,
             const std::string &expected_previous, const std::string &reason);
  std::string metadata()
      const; // Never includes secret values, trust changes or decrypted keys.
  bool session_data(const std::string &id, std::string &host, int &port,
                    std::string &user, std::string &kind, std::string &value,
                    std::string &pin, std::string &transport, std::string &vpn,
                    bool &required, const std::string &protocol = "ssh",
                    std::string *domain = nullptr);
  bool secret_data(const std::string &id, std::string &kind,
                   std::string &value);
  std::string public_catalog() const;
  bool import_catalog(const std::string &json);
  bool restore_encrypted(const std::string &source,
                         const std::string &backup_password);
  bool backup_encrypted(const std::string &destination);
  unsigned count(const char *collection) const;
  std::string error;
  uint32_t generation() const { return generation_; }

private:
  std::string directory_;
  cJSON *data_ = nullptr;
  bool open_ = false;
  int slot_ = -1;
  uint32_t generation_ = 0, iterations_ = 16384;
  std::array<unsigned char, 32> key_{};
  std::array<unsigned char, 96> envelope_{};
  bool save();
  bool require_open();
  bool fail(const char *message);
  bool wrap(const std::string &password,
            std::array<unsigned char, 96> &envelope);
  bool replace(const char *collection, cJSON *record, std::string &id,
               const std::string &host_id = "");
};
std::string normalize_endpoint(std::string host);
void secure_clear(void *data, size_t size);
} // namespace tabremoto
