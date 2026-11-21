#include "vault.hpp"
#include "cJSON.h"
#include "mbedtls/gcm.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
#include "openssl/crypto.h"
#include "openssl/evp.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "bootloader_random.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#endif
namespace tabremoto {
namespace {
constexpr size_t max_file = 65536, header_size = 148;
const char magic[8] = {'T', 'R', 'V', 'A', 'U', 'L', 'T', '2'};
void put32(unsigned char *p, uint32_t v) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = (v >> (i * 8)) & 255;
}
uint32_t get32(const unsigned char *p) {
  uint32_t v = 0;
  for (unsigned i = 0; i < 4; i++)
    v |= uint32_t(p[i]) << (i * 8);
  return v;
}
void wipe_json(cJSON *j) {
  if (!j)
    return;
  for (auto *c = j->child; c; c = c->next)
    wipe_json(c);
  if (j->valuestring)
    secure_clear(j->valuestring, strlen(j->valuestring));
}
struct Json {
  cJSON *p;
  explicit Json(cJSON *value = nullptr) : p(value) {}
  ~Json() {
    wipe_json(p);
    cJSON_Delete(p);
  }
  cJSON *release() {
    auto *v = p;
    p = nullptr;
    return v;
  }
};
struct Bytes : std::vector<unsigned char> {
  using std::vector<unsigned char>::vector;
  ~Bytes() {
    if (!empty())
      secure_clear(data(), size());
  }
};
struct Key {
  unsigned char bytes[32]{};
  ~Key() { secure_clear(bytes, sizeof(bytes)); }
};
bool random_bytes(void *p, size_t n) {
#ifdef ESP_PLATFORM
  // Runtime excludes the OpenSSL probe while using the P4 SAR entropy source.
  bootloader_random_enable();
  esp_fill_random(p, n);
  bootloader_random_disable();
  return true;
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return false;
  unsigned char *b = (unsigned char *)p;
  bool ok = true;
  while (n) {
    auto r = read(fd, b, n);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0) {
      ok = false;
      break;
    }
    b += r;
    n -= r;
  }
  close(fd);
  return ok;
#endif
}
bool kdf_memory_available() {
#ifdef ESP_PLATFORM
  // The upstream scrypt V table uses 64 KiB pages with the same N/r/p.
  // Check the full memory budget and an individual page, not a 16 MiB span.
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>16910000 && heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)>=65536;
#else
  return true;
#endif
}
constexpr const char *kdf_memory_error="Memoria disponivel insuficiente para o cofre; feche outra sessao";
bool kdf(const std::string &password, const unsigned char *salt, uint32_t cost,
         unsigned char *key) {
  if (cost != 16384)
    return false;
  if (!OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr))
    return false;
  return EVP_PBE_scrypt(password.data(), password.size(), salt, 16, cost, 8, 1,
                        24ULL * 1024 * 1024, key, 32) == 1;
}

bool crypt(bool encrypt, const unsigned char *key, const unsigned char *nonce,
           const unsigned char *aad, size_t an, const unsigned char *in,
           size_t n, unsigned char *out, unsigned char *tag) {
  mbedtls_gcm_context c;
  mbedtls_gcm_init(&c);
  int r = mbedtls_gcm_setkey(&c, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (!r)
    r = encrypt ? mbedtls_gcm_crypt_and_tag(&c, MBEDTLS_GCM_ENCRYPT, n, nonce,
                                            12, aad, an, in, out, 16, tag)
                : mbedtls_gcm_auth_decrypt(&c, n, nonce, 12, aad, an, tag, 16,
                                           in, out);
  mbedtls_gcm_free(&c);
  return r == 0;
}
bool exists_file(const std::string &p) {
  struct stat s{};
  return stat(p.c_str(), &s) == 0;
}
bool read_file(const std::string &path, Bytes &b) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return false;
  unsigned char chunk[1024];
  size_t n;
  bool ok = true;
  while ((n = fread(chunk, 1, sizeof(chunk), f))) {
    if (b.size() + n > max_file) {
      ok = false;
      break;
    }
    b.insert(b.end(), chunk, chunk + n);
  }
  if (ferror(f))
    ok = false;
  fclose(f);
  secure_clear(chunk, sizeof(chunk));
  return ok;
}
bool write_all(int fd, const unsigned char *p, size_t n) {
  while (n) {
    auto v = write(fd, p, n);
    if (v < 0 && errno == EINTR)
      continue;
    if (v <= 0)
      return false;
    p += v;
    n -= v;
  }
  return true;
}
std::string str(const cJSON *j, const char *key) {
  auto *v = cJSON_GetObjectItemCaseSensitive(j, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
bool text(const std::string &s, size_t max, bool empty = false) {
  if ((!empty && s.empty()) || s.size() > max)
    return false;
  for (unsigned char c : s)
    if (c < 32 || c == 127)
      return false;
  return true;
}
bool id_valid(const std::string &id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
bool fingerprint_valid(const std::string &s) {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
std::string endpoint(std::string host) {
  if (!text(host, 253))
    return {};
  unsigned char address[16];
  char canonical[INET6_ADDRSTRLEN];
  if (inet_pton(AF_INET, host.c_str(), address) == 1) {
    inet_ntop(AF_INET, address, canonical, sizeof(canonical));
    return canonical;
  }
  if (inet_pton(AF_INET6, host.c_str(), address) == 1) {
    inet_ntop(AF_INET6, address, canonical, sizeof(canonical));
    return canonical;
  }
  if (host.back() == '.')
    host.pop_back();
  if (host.empty() || host.front() == '.' || host.front() == '-' ||
      host.back() == '-')
    return {};
  for (char &c : host) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-'))
      return {};
    c = char(std::tolower((unsigned char)c));
  }
  return host;
}
cJSON *array(cJSON *j, const char *name) {
  return cJSON_GetObjectItemCaseSensitive(j, name);
}
cJSON *lookup(cJSON *j, const char *list, const std::string &id) {
  for (auto *r = array(j, list)->child; r; r = r->next)
    if (str(r, "id") == id)
      return r;
  return nullptr;
}
void add(cJSON *j, const char *name, const std::string &value) {
  cJSON_AddStringToObject(j, name, value.c_str());
}
void timestamp(cJSON *j, const char *field) {
  cJSON_AddNumberToObject(j, field, (double)time(nullptr));
}
std::string new_id() {
  unsigned char raw[16];
  if (!random_bytes(raw, sizeof(raw)))
    return {};
  char hex[33]{};
  for (unsigned i = 0; i < 16; i++)
    snprintf(hex + i * 2, 3, "%02x", raw[i]);
  secure_clear(raw, sizeof(raw));
  return hex;
}
bool shape(const cJSON *r, std::initializer_list<const char *> fields) {
  if (!cJSON_IsObject(r) || cJSON_GetArraySize(r) != (int)fields.size())
    return false;
  for (auto f : fields)
    if (!cJSON_GetObjectItemCaseSensitive(r, f))
      return false;
  return true;
}
bool validate(cJSON *j) {
  if (!shape(j, {"schema", "connections", "identities", "secrets", "trust",
                 "events"}))
    return false;
  auto *schema = array(j, "schema");
  if (!cJSON_IsNumber(schema) || schema->valuedouble != 1)
    return false;
  for (auto list :
       {"connections", "identities", "secrets", "trust", "events"}) {
    auto *a = array(j, list);
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) > 128)
      return false;
  }
  for (auto list : {"connections", "identities", "secrets"})
    for (auto *r = array(j, list)->child; r; r = r->next) {
      auto id = str(r, "id");
      if (!id_valid(id) || !text(str(r, "label"), 80))
        return false;
      for (auto *q = r->next; q; q = q->next)
        if (str(q, "id") == id)
          return false;
      if (!strcmp(list, "secrets")) {
        if (!shape(r, {"id", "label", "kind", "value", "created"}))
          return false;
        auto kind = str(r, "kind"), v = str(r, "value");
        bool valid = kind == "password" || kind == "key" ||
                     kind == "passphrase" || kind == "vpn" || kind == "ca" ||
                     kind == "wifi";
        bool bounded = !v.empty() && v.size() <= 8192;
        secure_clear(v.data(), v.size());
        if (!valid || !bounded)
          return false;
      } else if (!strcmp(list, "identities")) {
        if (!shape(r, {"id", "label", "user", "domain", "secret_id", "auth"}) ||
            !text(str(r, "user"), 128) || !text(str(r, "domain"), 128, true))
          return false;
        auto ref = str(r, "secret_id"), auth = str(r, "auth");
        if (auth != "ask" && auth != "password" && auth != "key")
          return false;
        if ((auth == "ask" && !ref.empty()) ||
            (auth != "ask" && (!id_valid(ref) || !lookup(j, "secrets", ref))))
          return false;
        if (auth != "ask" && str(lookup(j, "secrets", ref), "kind") != auth)
          return false;
      } else {
        if (!shape(r, {"id", "label", "protocol", "host", "port", "identity_id",
                       "favorite", "group"}) &&
            !shape(r, {"id", "label", "protocol", "host", "port", "identity_id",
                       "favorite", "group", "transport", "vpn_ref",
                       "vpn_required"}))
          return false;
        if (array(r, "transport")) {
          auto tr = str(r, "transport"), vpn = str(r, "vpn_ref");
          if ((tr != "auto" && tr != "wifi" && tr != "ethernet") ||
              !cJSON_IsBool(array(r, "vpn_required")) ||
              !cJSON_IsString(array(r, "vpn_ref")) ||
              (!vpn.empty() &&
               (!lookup(j, "secrets", vpn) ||
                str(lookup(j, "secrets", vpn), "kind") != "vpn")))
            return false;
        }
        auto proto = str(r, "protocol"), ref = str(r, "identity_id");
        auto *port = array(r, "port");
        if ((proto != "ssh" && proto != "rdp") || str(r, "host").empty() ||
            endpoint(str(r, "host")) != str(r, "host") ||
            !cJSON_IsNumber(port) || port->valuedouble < 1 ||
            port->valuedouble > 65535 ||
            port->valuedouble != (int)port->valuedouble ||
            !cJSON_IsBool(array(r, "favorite")) ||
            !text(str(r, "group"), 80, true) ||
            (!ref.empty() && (!id_valid(ref) || !lookup(j, "identities", ref))))
          return false;
      }
    }
  for (auto list : {"trust", "events"})
    for (auto *r = array(j, list)->child; r; r = r->next) {
      if (!shape(r, {"protocol", "host", "port", "fingerprint", "previous",
                     "reason", "first", "last"}))
        return false;
      auto p = str(r, "protocol"), h = str(r, "host"),
           f = str(r, "fingerprint"), prev = str(r, "previous");
      auto *port = array(r, "port");
      if ((p != "ssh" && p != "rdp") || h.empty() || endpoint(h) != h ||
          !fingerprint_valid(f) ||
          (!prev.empty() && !fingerprint_valid(prev)) ||
          !text(str(r, "reason"), 160) || !cJSON_IsNumber(port) ||
          port->valuedouble < 1 || port->valuedouble > 65535 ||
          port->valuedouble != (int)port->valuedouble ||
          !cJSON_IsNumber(array(r, "first")) ||
          !cJSON_IsNumber(array(r, "last")))
        return false;
    }
  return true;
}
cJSON *parse(const Bytes &plain) {
  if (plain.empty() || memchr(plain.data(), 0, plain.size()))
    return nullptr;
  // Reject excessive nesting before cJSON recursion; strings/escapes are
  // honored.
  unsigned depth = 0;
  bool string = false, escape = false;
  for (unsigned char c : plain) {
    if (string) {
      if (escape)
        escape = false;
      else if (c == '\\')
        escape = true;
      else if (c == '"')
        string = false;
    } else if (c == '"')
      string = true;
    else if (c == '{' || c == '[') {
      if (++depth > 8)
        return nullptr;
    } else if (c == '}' || c == ']') {
      if (!depth--)
        return nullptr;
    }
  }
  return cJSON_ParseWithLengthOpts((const char *)plain.data(), plain.size(),
                                   nullptr, false);
}
bool header(const Bytes &b) {
  return b.size() >= header_size && b.size() <= max_file &&
         !memcmp(b.data(), magic, 8) && get32(b.data() + 8) == 2 &&
         get32(b.data() + 12) > 0 && get32(b.data() + 16) == 16384 &&
         get32(b.data() + 20) == b.size() - header_size;
}
// Envelope: salt16 + nonce12 + wrapped data key32 + tag16 + reserved20.
} // namespace
std::string normalize_endpoint(std::string host) {
  return endpoint(std::move(host));
}
void secure_clear(void *p, size_t n) {
  if (p && n)
    mbedtls_platform_zeroize(p, n);
}
Vault::Vault(std::string directory) : directory_(std::move(directory)) {}
Vault::~Vault() { lock(); }
bool Vault::fail(const char *message) {
  error = message;
  return false;
}
bool Vault::require_open() { return open_ || fail("Cofre bloqueado"); }
bool Vault::exists() const {
  return exists_file(directory_ + "/store.a") ||
         exists_file(directory_ + "/store.b");
}
void Vault::lock() {
  wipe_json(data_);
  cJSON_Delete(data_);
  data_ = nullptr;
  secure_clear(key_.data(), key_.size());
  secure_clear(envelope_.data(), envelope_.size());
  open_ = false;
  generation_ = 0;
  slot_ = -1;
}
bool Vault::wrap(const std::string &password,
                 std::array<unsigned char, 96> &out) {
  if (password.size() != 8 ||
      !std::all_of(password.begin(), password.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9');
      }))
    return fail("Senha mestra: 8 letras ou numeros");
  if(!kdf_memory_available()) return fail(kdf_memory_error);
  out.fill(0);
  Key kek;
  if (!random_bytes(out.data(), 28) ||
      !kdf(password, out.data(), iterations_, kek.bytes))
    return fail("Falha na KDF/entropia");
  unsigned char aad[12];
  memcpy(aad, magic, 8);
  put32(aad + 8, iterations_);
  return crypt(true, kek.bytes, out.data() + 16, aad, sizeof(aad), key_.data(),
               32, out.data() + 28, out.data() + 60) ||
         fail("Falha ao proteger chave");
}
bool Vault::create(const std::string &password) {
  if (exists())
    return fail("Cofre existente preservado");
  lock();
  if (!random_bytes(key_.data(), key_.size()) || !wrap(password, envelope_)) {
    lock();
    return false;
  }
  data_ = cJSON_Parse("{\"schema\":1,\"connections\":[],\"identities\":[],"
                      "\"secrets\":[],\"trust\":[],\"events\":[]}");
  open_ = data_ != nullptr;
  if (!open_ || !save()) {
    lock();
    return false;
  }
  return true;
}
bool Vault::save() {
  if (!require_open() || !validate(data_))
    return fail("Cadastro invalido; arquivo preservado");
  if (generation_ == UINT32_MAX)
    return fail("Geracao esgotada");
  // A fixed buffer avoids cJSON reallocations leaving old plaintext behind.
  Bytes plaintext(max_file - header_size + 8);
  if (!cJSON_PrintPreallocated(data_, (char *)plaintext.data(),
                               plaintext.size(), false))
    return fail("Limite do cofre excedido");
  auto *encoded = (char *)plaintext.data();
  size_t n = strlen(encoded);
  if (n > max_file - header_size)
    return fail("Limite do cofre excedido");
  Bytes bytes(header_size + n);
  memcpy(bytes.data(), magic, 8);
  put32(bytes.data() + 8, 2);
  put32(bytes.data() + 12, generation_ + 1);
  put32(bytes.data() + 16, iterations_);
  put32(bytes.data() + 20, n);
  memcpy(bytes.data() + 24, envelope_.data(), 96);
  bool ok = random_bytes(bytes.data() + 120, 12) &&
            crypt(true, key_.data(), bytes.data() + 120, bytes.data(), 132,
                  (unsigned char *)encoded, n, bytes.data() + header_size,
                  bytes.data() + 132);
  secure_clear(plaintext.data(), plaintext.size());
  if (!ok)
    return fail("Falha AEAD");
  std::string temporary = directory_ + "/pending-XXXXXX";
  std::vector<char> path(temporary.begin(), temporary.end());
  path.push_back(0);
  int fd = mkstemp(path.data());
  if (fd < 0)
    return fail("SD indisponivel para escrita");
  ok = write_all(fd, bytes.data(), bytes.size()) && fsync(fd) == 0;
  if (close(fd) != 0)
    ok = false;
  Bytes verify;
  if (ok)
    ok = read_file(path.data(), verify) && verify == bytes;
  int target = slot_ == 0 ? 1 : 0;
  std::string dest = directory_ + (target ? "/store.b" : "/store.a");
  // Only replace the inactive generation; current committed data is retained.
  if (ok && exists_file(dest))
    ok = unlink(dest.c_str()) == 0;
  if (ok)
    ok = rename(path.data(), dest.c_str()) == 0;
  if (!ok) {
    unlink(path.data());
    return fail("Falha na gravacao; geracao anterior preservada");
  }
  slot_ = target;
  ++generation_;
  error.clear();
  return true;
}
bool Vault::unlock(const std::string &password, bool recover) {
  lock();
  if (password.size() > 256 || password.empty())
    return fail("Senha invalida");
  if(!kdf_memory_available()) return fail(kdf_memory_error);
  Bytes slots[2];
  bool present[2] = {exists_file(directory_ + "/store.a"),
                     exists_file(directory_ + "/store.b")};
  bool valid[2] = {false, false};
  for (int i = 0; i < 2; i++)
    if (present[i])
      valid[i] =
          read_file(directory_ + (i ? "/store.b" : "/store.a"), slots[i]) &&
          header(slots[i]);
  if (!present[0] && !present[1])
    return fail("Cofre ainda nao criado");
  if (!recover && ((present[0] && !valid[0]) || (present[1] && !valid[1])))
    return fail("Arquivo danificado; recuperacao explicita necessaria");
  int order[2] = {0, 1};
  if ((valid[1] && !valid[0]) ||
      (valid[1] && valid[0] &&
       get32(slots[1].data() + 12) > get32(slots[0].data() + 12)))
    std::swap(order[0], order[1]);
  for (int index : order) {
    if (!valid[index])
      continue;
    auto &b = slots[index];
    Key kek, dek;
    auto rounds = get32(b.data() + 16);
    unsigned char aad[12];
    memcpy(aad, magic, 8);
    put32(aad + 8, rounds);
    bool ok = kdf(password, b.data() + 24, rounds, kek.bytes) &&
              crypt(false, kek.bytes, b.data() + 40, aad, 12, b.data() + 52, 32,
                    dek.bytes, b.data() + 84);
    Bytes plain(b.size() - header_size);
    if (ok)
      ok = crypt(false, dek.bytes, b.data() + 120, b.data(), 132,
                 b.data() + header_size, plain.size(), plain.data(),
                 b.data() + 132);
    Json parsed(ok ? parse(plain) : nullptr);
    if (ok)
      ok = parsed.p && validate(parsed.p);
    if (ok) {
      data_ = parsed.release();
      std::copy(dek.bytes, dek.bytes + 32, key_.begin());
      std::copy(b.data() + 24, b.data() + 120, envelope_.begin());
      iterations_ = rounds;
      generation_ = get32(b.data() + 12);
      slot_ = index;
      open_ = true;
      error.clear();
      return true;
    }
    if (!recover)
      return fail("Senha incorreta ou cofre adulterado; nenhum dado alterado");
  }
  return fail("Nao foi possivel autenticar o cofre");
}
bool Vault::change_password(const std::string &password) {
  if (!require_open())
    return false;
  auto before = envelope_;
  if (!wrap(password, envelope_)) {
    envelope_ = before;
    secure_clear(before.data(), before.size());
    return false;
  }
  bool ok = save();
  if (!ok)
    envelope_ = before;
  secure_clear(before.data(), before.size());
  return ok;
}
bool Vault::replace(const char *list, cJSON *raw, std::string &id,
                    const std::string &host_id) {
  Json record(raw);
  if (!require_open())
    return false;
  if (!host_id.empty() &&
      (!lookup(data_, "connections", host_id) || strcmp(list, "identities")))
    return fail("Host de vinculo inexistente");
  auto *a = array(data_, list);
  if (!id.empty() && !lookup(data_, list, id))
    return fail("ID inexistente");
  if (id.empty())
    id = new_id();
  if (!id_valid(id))
    return fail("ID invalido");
  if (!lookup(data_, list, id) && cJSON_GetArraySize(a) >= 128)
    return fail("Limite de registros");
  add(record.p, "id", id);
  Json previous(cJSON_Duplicate(data_, true));
  if (!previous.p)
    return fail("Sem memoria");
  auto *old = lookup(data_, list, id);
  if (old) {
    int pos = 0;
    for (auto *c = a->child; c != old; c = c->next)
      ++pos;
    wipe_json(old);
    cJSON_ReplaceItemInArray(a, pos, record.release());
  } else
    cJSON_AddItemToArray(a, record.release());
  if (!host_id.empty())
    cJSON_SetValuestring(
        array(lookup(data_, "connections", host_id), "identity_id"),
        id.c_str());
  if (!save()) {
    wipe_json(data_);
    cJSON_Delete(data_);
    data_ = previous.release();
    return false;
  }
  return true;
}
bool Vault::secret(std::string &id, const std::string &label,
                   const std::string &kind, const std::string &value) {
  if (!require_open())
    return false;
  if (value.find('\0') != std::string::npos)
    return fail("Segredo contem byte nulo");
  Json r(cJSON_CreateObject());
  add(r.p, "label", label);
  add(r.p, "kind", kind);
  add(r.p, "value", value);
  timestamp(r.p, "created");
  return replace("secrets", r.release(), id);
}
bool Vault::identity(std::string &id, const std::string &label,
                     const std::string &user, const std::string &domain,
                     const std::string &ref, const std::string &auth,
                     const std::string &host_id) {
  if (!require_open())
    return false;
  Json r(cJSON_CreateObject());
  add(r.p, "label", label);
  add(r.p, "user", user);
  add(r.p, "domain", domain);
  add(r.p, "secret_id", ref);
  add(r.p, "auth", auth);
  return replace("identities", r.release(), id, host_id);
}
bool Vault::connection(std::string &id, const std::string &label,
                       const std::string &protocol, const std::string &host,
                       int port, const std::string &ref, bool favorite,
                       const std::string &group, const std::string &transport,
                       const std::string &vpn, bool required) {
  if (!require_open())
    return false;
  Json r(cJSON_CreateObject());
  add(r.p, "label", label);
  add(r.p, "protocol", protocol);
  add(r.p, "host", endpoint(host));
  add(r.p, "identity_id", ref);
  add(r.p, "group", group);
  add(r.p, "transport", transport);
  add(r.p, "vpn_ref", vpn);
  cJSON_AddBoolToObject(r.p, "vpn_required", required);
  cJSON_AddNumberToObject(r.p, "port", port);
  cJSON_AddBoolToObject(r.p, "favorite", favorite);
  return replace("connections", r.release(), id);
}
bool Vault::remove(const std::string &list, const std::string &id) {
  if (!require_open())
    return false;
  if (list != "connections" && list != "identities" && list != "secrets")
    return fail("Colecao invalida");
  auto *found = lookup(data_, list.c_str(), id);
  if (!found)
    return fail("ID inexistente");
  if (list == "secrets")
    for (auto *r = array(data_, "connections")->child; r; r = r->next)
      if (str(r, "vpn_ref") == id)
        return fail("Perfil VPN vinculado a conexao");
  const char *depend = list == "secrets"      ? "identities"
                       : list == "identities" ? "connections"
                                              : nullptr;
  const char *field = list == "secrets" ? "secret_id" : "identity_id";
  if (depend)
    for (auto *r = array(data_, depend)->child; r; r = r->next)
      if (str(r, field) == id)
        return fail("Registro vinculado; remova os vinculos antes de excluir");
  Json previous(cJSON_Duplicate(data_, true));
  if (!previous.p)
    return fail("Sem memoria");
  wipe_json(found);
  cJSON_DetachItemViaPointer(array(data_, list.c_str()), found);
  cJSON_Delete(found);
  if (!save()) {
    wipe_json(data_);
    cJSON_Delete(data_);
    data_ = previous.release();
    return false;
  }
  return true;
}
int Vault::check_trust(const std::string &protocol, const std::string &host,
                       int port, const std::string &fingerprint) {
  if (!require_open() || (protocol != "ssh" && protocol != "rdp") ||
      endpoint(host).empty() || port < 1 || port > 65535 ||
      !fingerprint_valid(fingerprint)) {
    fail("Endpoint/fingerprint invalido ou cofre bloqueado");
    return -1;
  }
  for (auto *r = array(data_, "trust")->child; r; r = r->next)
    if (str(r, "protocol") == protocol && str(r, "host") == endpoint(host) &&
        array(r, "port")->valueint == port)
      return str(r, "fingerprint") == fingerprint ? 1 : 2;
  return 0;
}
bool Vault::trust(const std::string &protocol, const std::string &host,
                  int port, const std::string &fingerprint,
                  const std::string &expected, const std::string &reason) {
  int match = check_trust(protocol, host, port, fingerprint);
  if (match < 0 || !text(reason, 160))
    return fail("Decisao requer endpoint, fingerprint e motivo validos");
  auto canonical = endpoint(host);
  auto *trusted = array(data_, "trust");
  cJSON *old = nullptr;
  for (auto *r = trusted->child; r; r = r->next)
    if (str(r, "protocol") == protocol && str(r, "host") == canonical &&
        array(r, "port")->valueint == port)
      old = r;
  if ((old ? str(old, "fingerprint") : "") != expected)
    return fail("Fingerprint anterior diverge; revise a alteracao");
  Json backup(cJSON_Duplicate(data_, true));
  Json r(cJSON_CreateObject());
  if (!backup.p || !r.p)
    return fail("Sem memoria");
  add(r.p, "protocol", protocol);
  add(r.p, "host", canonical);
  cJSON_AddNumberToObject(r.p, "port", port);
  add(r.p, "fingerprint", fingerprint);
  add(r.p, "previous", expected);
  add(r.p, "reason", reason);
  cJSON_AddNumberToObject(r.p, "first",
                          old ? array(old, "first")->valuedouble
                              : (double)time(nullptr));
  timestamp(r.p, "last");
  auto *history = array(data_, "events");
  if (cJSON_GetArraySize(history) >= 128)
    cJSON_DeleteItemFromArray(history, 0);
  cJSON_AddItemToArray(history, cJSON_Duplicate(r.p, true));
  if (old) {
    cJSON_DetachItemViaPointer(trusted, old);
    cJSON_Delete(old);
  }
  cJSON_AddItemToArray(trusted, r.release());
  if (!save()) {
    wipe_json(data_);
    cJSON_Delete(data_);
    data_ = backup.release();
    return false;
  }
  return true;
}
unsigned Vault::count(const char *collection) const {
  return open_ ? (unsigned)cJSON_GetArraySize(array(data_, collection)) : 0;
}
std::string Vault::metadata() const {
  if (!open_)
    return {};
  Json j(cJSON_CreateObject());
  cJSON_AddNumberToObject(j.p, "schema", 1);
  for (auto list : {"connections", "identities"})
    cJSON_AddItemToObject(j.p, list, cJSON_Duplicate(array(data_, list), true));
  // Inventory includes key labels/kinds/timestamps, never contents or
  // passphrases.
  auto *secrets = cJSON_AddArrayToObject(j.p, "secrets");
  for (auto *r = array(data_, "secrets")->child; r; r = r->next) {
    auto *s = cJSON_CreateObject();
    for (auto f : {"id", "label", "kind"})
      add(s, f, str(r, f));
    cJSON_AddNumberToObject(s, "created", array(r, "created")->valuedouble);
    cJSON_AddItemToArray(secrets, s);
  }
  cJSON_AddItemToObject(j.p, "trust",
                        cJSON_Duplicate(array(data_, "trust"), true));
  cJSON_AddItemToObject(j.p, "events",
                        cJSON_Duplicate(array(data_, "events"), true));
  char *printed = cJSON_PrintUnformatted(j.p);
  if (!printed)
    return {};
  std::string result = printed;
  cJSON_free(printed);
  return result;
}
std::string Vault::public_catalog() const {
  if (!open_)
    return {};
  Json j(cJSON_CreateObject());
  cJSON_AddNumberToObject(j.p, "schema", 1);
  cJSON_AddItemToObject(j.p, "connections",
                        cJSON_Duplicate(array(data_, "connections"), true));
  auto *identities = cJSON_Duplicate(array(data_, "identities"), true);
  cJSON_AddItemToObject(j.p, "identities", identities);
  for (auto *r = identities->child; r; r = r->next) {
    cJSON_SetValuestring(array(r, "secret_id"), "");
    cJSON_SetValuestring(array(r, "auth"), "ask");
  }
  for (auto *r = array(j.p, "connections")->child; r; r = r->next) {
    if (array(r, "vpn_ref"))
      cJSON_SetValuestring(array(r, "vpn_ref"), "");
  }
  char *text = cJSON_PrintUnformatted(j.p);
  if (!text)
    return {};
  std::string result = text;
  cJSON_free(text);
  return result;
}
bool Vault::import_catalog(const std::string &text) {
  if (!require_open())
    return false;
  if (text.size() > max_file - header_size)
    return fail("Importacao excede limite");
  Bytes bytes(text.begin(), text.end());
  Json imported(parse(bytes));
  if (!shape(imported.p, {"schema", "connections", "identities"}) ||
      !cJSON_IsNumber(array(imported.p, "schema")) ||
      array(imported.p, "schema")->valuedouble != 1)
    return fail("Schema de importacao nao suportado");
  Json candidate(cJSON_Duplicate(data_, true));
  if (!candidate.p)
    return fail("Sem memoria");
  for (auto list : {"identities", "connections"}) {
    auto *source = array(imported.p, list);
    auto *dest = array(candidate.p, list);
    if (!cJSON_IsArray(source) ||
        cJSON_GetArraySize(source) + cJSON_GetArraySize(dest) > 128)
      return fail("Lista de importacao invalida");
    for (auto *r = source->child; r; r = r->next) {
      if (lookup(candidate.p, list, str(r, "id")))
        return fail("ID ja existe; nenhum registro foi sobrescrito");
      cJSON_AddItemToArray(dest, cJSON_Duplicate(r, true));
    }
  }
  if (!validate(candidate.p))
    return fail("Cadastro importado invalido ou referencia ausente");
  auto *previous = data_;
  data_ = candidate.release();
  if (!save()) {
    wipe_json(data_);
    cJSON_Delete(data_);
    data_ = previous;
    return false;
  }
  wipe_json(previous);
  cJSON_Delete(previous);
  return true;
}
bool Vault::restore_encrypted(const std::string &source,
                              const std::string &password) {
  // Authentication of the backup is complete before replacing in-memory data
  // or touching the inactive live generation. Keep the current master key.
  if (!require_open())
    return false;
  if(!kdf_memory_available()) return fail(kdf_memory_error);
  Bytes raw;
  if (!read_file(source, raw) || !header(raw))
    return fail("Backup ausente, danificado ou formato invalido");
  if (password.empty() || password.size() > 256)
    return fail("Senha do backup invalida");
  Key kek, dek;
  const auto rounds = get32(raw.data() + 16);
  unsigned char aad[12];
  memcpy(aad, magic, 8);
  put32(aad + 8, rounds);
  bool ok = kdf(password, raw.data() + 24, rounds, kek.bytes) &&
            crypt(false, kek.bytes, raw.data() + 40, aad, 12, raw.data() + 52,
                  32, dek.bytes, raw.data() + 84);
  Bytes plain(raw.size() - header_size);
  if (ok)
    ok = crypt(false, dek.bytes, raw.data() + 120, raw.data(), 132,
               raw.data() + header_size, plain.size(), plain.data(),
               raw.data() + 132);
  Json restored(ok ? parse(plain) : nullptr);
  if (!ok || !restored.p || !validate(restored.p))
    return fail(
        "Senha do backup incorreta ou backup adulterado; cofre preservado");
  Json previous(data_);
  data_ = restored.release();
  if (!save()) {
    Json rejected(data_);
    data_ = previous.release();
    return false;
  }
  return true;
}

bool Vault::backup_encrypted(const std::string &destination) {
  if (!require_open())
    return false;
  Bytes raw;
  if (!read_file(directory_ + (slot_ ? "/store.b" : "/store.a"), raw) ||
      !header(raw))
    return fail("Falha ao ler backup");
  int fd = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return fail("Backup existente ou SD indisponivel");
  bool ok = write_all(fd, raw.data(), raw.size()) && fsync(fd) == 0;
  if (close(fd) != 0)
    ok = false;
  Bytes check;
  if (ok)
    ok = read_file(destination, check) && check == raw;
  if (!ok) {
    unlink(destination.c_str());
    return fail("Falha no backup criptografado");
  }
  return true;
}

bool Vault::secret_data(const std::string &id, std::string &kind,
                        std::string &value) {
  if (!require_open())
    return false;
  auto *r = lookup(data_, "secrets", id);
  if (!r)
    return fail("Segredo ausente");
  kind = str(r, "kind");
  value = str(r, "value");
  return true;
}
bool Vault::session_data(const std::string &id, std::string &host, int &port,
                         std::string &user, std::string &kind,
                         std::string &value, std::string &pin,
                         std::string &transport, std::string &vpn,
                         bool &required, const std::string &protocol,
                         std::string *domain) {
  if (!require_open())
    return false;
  pin.clear();
  if (domain)
    domain->clear();
  auto *c = lookup(data_, "connections", id);
  if (!c || str(c, "protocol") != protocol)
    return fail("Selecione conexao do protocolo correto");
  transport = str(c, "transport");
  if (transport.empty())
    transport = "auto";
  vpn = str(c, "vpn_ref");
  required = cJSON_IsTrue(array(c, "vpn_required"));
  host = str(c, "host");
  port = array(c, "port")->valueint;
  auto *identity = lookup(data_, "identities", str(c, "identity_id"));
  if (!identity)
    return fail("Vincule uma identidade antes de conectar");
  user = str(identity, "user");
  if (domain)
    *domain = str(identity, "domain");
  kind = str(identity, "auth");
  if (kind != "ask" && !secret_data(str(identity, "secret_id"), kind, value))
    return false;
  for (auto *r = array(data_, "trust")->child; r; r = r->next)
    if (str(r, "protocol") == protocol && str(r, "host") == host &&
        array(r, "port")->valueint == port)
      pin = str(r, "fingerprint");
  return true;
}

} // namespace tabremoto
