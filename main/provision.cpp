#include "rdp.hpp"
#include "rdp_ui.hpp"
#include "sdkconfig.h"
#include "ui.hpp"
#include "security_ui.hpp"
#ifndef CONFIG_TABREMOTO_DIAGNOSTICS
#define CONFIG_TABREMOTO_DIAGNOSTICS 0
#endif
#include "bootloader_random.h"
#include "cJSON.h"
#include "crypto_gate.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "network.hpp"
#include "provision.hpp"
#include "remote_ui.hpp"
#include "ssh.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
namespace {
std::array<unsigned char, 32> key{};
int64_t expires = 0;
std::string last_request_id;
bool last_request_accepted=false;
const unsigned char aad[] = "TabRemoto provision v1";
std::string value(cJSON *j, const char *name) {
  auto *item = cJSON_GetObjectItemCaseSensitive(j, name);
  return cJSON_IsString(item) ? item->valuestring : "";
}
void wipe(cJSON *j) {
  if (!j)
    return;
  for (auto *c = j->child; c; c = c->next)
    wipe(c);
  if (j->valuestring)
    mbedtls_platform_zeroize(j->valuestring, strlen(j->valuestring));
}
} // namespace
bool provision_open(cJSON *reply) {
  if (!crypto_gate_enter_for(2000))
    return false;
  bootloader_random_enable();
  esp_fill_random(key.data(), key.size());
  bootloader_random_disable();
  crypto_gate_leave();
  expires = esp_timer_get_time() + 120000000;
  char hex[65]{};
  for (unsigned i = 0; i < 32; i++)
    snprintf(hex + i * 2, 3, "%02x", key[i]);
  cJSON_AddStringToObject(reply, "key", hex);
  cJSON_AddBoolToObject(reply,"receipt",true);
  mbedtls_platform_zeroize(hex, sizeof(hex));
  return true;
}
void provision_status(cJSON *reply) {
  cJSON_AddStringToObject(reply,"id",last_request_id.c_str());
  cJSON_AddBoolToObject(reply,"accepted",last_request_accepted);
}
bool provision_apply(const char *hex) {
  std::array<unsigned char, 1800> bytes{};
  std::array<unsigned char, 1800> plain{};
  size_t size = strlen(hex) / 2;
  bool ok = strlen(hex) % 2 == 0 && size >= 29 && size <= bytes.size() &&
            esp_timer_get_time() < expires && expires != 0;
  expires = 0;
  auto digit = [](char c) -> int {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
  };
  if (ok)
    for (unsigned i = 0; i < size; i++) {
      int a = digit(hex[i * 2]), b = digit(hex[i * 2 + 1]);
      if (a < 0 || b < 0) {
        ok = false;
        break;
      }
      bytes[i] = (a << 4) | b;
    }
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  if (ok)
    ok =
        mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key.data(), 256) == 0 &&
        mbedtls_gcm_auth_decrypt(&ctx, size - 28, bytes.data(), 12, aad,
                                 sizeof(aad) - 1, bytes.data() + size - 16, 16,
                                 bytes.data() + 12, plain.data()) == 0;
  mbedtls_gcm_free(&ctx);
  mbedtls_platform_zeroize(key.data(), key.size());
  cJSON *j = ok && !memchr(plain.data(), 0, size - 28)
                 ? cJSON_ParseWithLength((char *)plain.data(), size - 28)
                 : nullptr;
  ok = j != nullptr;
  std::string request_id=ok?value(j,"request_id"):"";
  if(!request_id.empty() && (request_id.size()!=32 || request_id.find_first_not_of("0123456789abcdef")!=std::string::npos)) ok=false;
  if (ok) {
    auto op = value(j, "op");
    if (op == "wifi") {
      std::string password = value(j, "password");
      ok = tabremoto::network_wifi(value(j, "ssid"), password);
      mbedtls_platform_zeroize(password.data(), password.size());
    } else if (op == "clock") {
      auto *n = cJSON_GetObjectItemCaseSensitive(j, "epoch");
      ok = cJSON_IsNumber(n) && n->valuedouble > 1700000000 &&
           n->valuedouble < 4102444800;
      if (ok) {
        timeval t{(time_t)n->valuedouble, 0};
        ok = settimeofday(&t, nullptr) == 0;
      }
    } else if (CONFIG_TABREMOTO_DIAGNOSTICS && op == "rdp-clipboard-send") {
      ok = tabremoto::rdp_clipboard_offer(value(j, "text"));
    } else if (CONFIG_TABREMOTO_DIAGNOSTICS && op == "ui-click") {
      ok = tabremoto::ui_click_label(value(j, "label"));
    } else if(CONFIG_TABREMOTO_DIAGNOSTICS && op=="ui-pointer"){
      auto *samples=cJSON_GetObjectItemCaseSensitive(j,"samples");std::vector<TabremotoPointerSample> points;
      ok=cJSON_IsArray(samples)&&cJSON_GetArraySize(samples)>0&&cJSON_GetArraySize(samples)<=64;
      for(auto *p=samples?samples->child:nullptr;ok&&p;p=p->next){
       auto *x=cJSON_GetObjectItemCaseSensitive(p,"x"),*y=cJSON_GetObjectItemCaseSensitive(p,"y"),*pressed=cJSON_GetObjectItemCaseSensitive(p,"pressed"),*delay=cJSON_GetObjectItemCaseSensitive(p,"delay_ms");
       ok=cJSON_IsNumber(x)&&cJSON_IsNumber(y)&&cJSON_IsBool(pressed)&&cJSON_IsNumber(delay)&&delay->valueint>=0;
       if(ok)points.push_back({x->valueint,y->valueint,unsigned(delay->valueint),cJSON_IsTrue(pressed)!=0});
      }
      ok=ok&&tabremoto_ui_pointer_gesture(points);
    } else if(CONFIG_TABREMOTO_DIAGNOSTICS && op=="ui-capture"){
      auto name=value(j,"name");ok=!name.empty()&&name.size()<32&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos;
      ok=ok&&tabremoto::ui_snapshot();
      if(ok){auto path=std::string("/sdcard/TabRemoto/cache/launcher-ui-")+name+".ppm";ok=rename("/sdcard/TabRemoto/cache/screen.ppm",path.c_str())==0;}
    } else if(CONFIG_TABREMOTO_DIAGNOSTICS && op=="security-layout"){
      auto *page=cJSON_GetObjectItemCaseSensitive(j,"page");ok=cJSON_IsNumber(page)&&page->valueint>=0&&tabremoto::security_ui_layout_preview(unsigned(page->valueint));
    } else if (CONFIG_TABREMOTO_DIAGNOSTICS && op == "ui-keys") {
      auto *keys = cJSON_GetObjectItemCaseSensitive(j, "keys");
      std::vector<uint8_t> raw;
      ok = cJSON_IsArray(keys) && cJSON_GetArraySize(keys) > 0 &&
           cJSON_GetArraySize(keys) <= 256;
      for (auto *item = keys ? keys->child : nullptr; ok && item;
           item = item->next) {
        ok = cJSON_IsNumber(item) && item->valuedouble >= 0 &&
             item->valuedouble <= 255 && item->valuedouble == item->valueint;
        if (ok)
          raw.push_back(item->valueint);
      }
      ok = ok && tabremoto::ui_inject_keys(raw);
    } else if (CONFIG_TABREMOTO_DIAGNOSTICS && op == "ssh-input") {
      ok = tabremoto::ssh_input(value(j, "text"));
    } else if (op == "rdp" ||
               (CONFIG_TABREMOTO_DIAGNOSTICS && op == "rdp-ui")) {
      auto *r = new tabremoto::RdpRequest;
      r->transport=value(j,"transport");if(r->transport.empty())r->transport="auto";
      r->vpn_profile=value(j,"vpn_profile");
      r->require_vpn=!r->vpn_profile.empty() || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"require_vpn"));
      auto *width = cJSON_GetObjectItemCaseSensitive(j, "width"),
           *height = cJSON_GetObjectItemCaseSensitive(j, "height"),
           *reconnect = cJSON_GetObjectItemCaseSensitive(j, "reconnect");
      if (cJSON_IsNumber(width))
        r->width = width->valueint;
      if (cJSON_IsNumber(height))
        r->height = height->valueint;
      if (cJSON_IsBool(reconnect))
        r->reconnect = cJSON_IsTrue(reconnect);

      r->host = value(j, "host");
      r->user = value(j, "user");
      r->domain = value(j, "domain");
      r->fingerprint = value(j, "fingerprint");
      auto *port = cJSON_GetObjectItemCaseSensitive(j, "port");
      if (cJSON_IsNumber(port))
        r->port = port->valueint;
      auto *password = cJSON_GetObjectItemCaseSensitive(j, "password");
      if (cJSON_IsString(password))
        snprintf(r->password.data(), r->password.size(), "%s",
                 password->valuestring);
#if CONFIG_TABREMOTO_DIAGNOSTICS
      ok = op == "rdp-ui" ? tabremoto::rdp_ui_test_connect(r)
                          : tabremoto::rdp_connect(r);
#else
      ok = tabremoto::rdp_connect(r);
#endif
      if (!ok)
        delete r;
    } else if (op == "ssh" ||
               (CONFIG_TABREMOTO_DIAGNOSTICS && op == "ssh-ui")) {
      auto *r = new tabremoto::SshRequest;
      r->host = value(j, "host");
      r->user = value(j, "user");
      r->fingerprint = value(j, "fingerprint");
      r->transport = value(j, "transport");
      if (r->transport.empty())
        r->transport = "auto";
      r->command = value(j, "command");
      r->key = value(j, "key");
      r->key_file = value(j, "key_file");
      r->vpn_profile = value(j, "vpn_profile");
      r->require_vpn =
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "require_vpn"));
      auto *phrase = cJSON_GetObjectItemCaseSensitive(j, "passphrase");
      if (cJSON_IsString(phrase))
        snprintf(r->passphrase.data(), r->passphrase.size(), "%s",
                 phrase->valuestring);
      auto *port = cJSON_GetObjectItemCaseSensitive(j, "port");
      if (cJSON_IsNumber(port))
        r->port = port->valueint;
      auto *password = cJSON_GetObjectItemCaseSensitive(j, "password");
      if (cJSON_IsString(password))
        snprintf(r->password.data(), r->password.size(), "%s",
                 password->valuestring);
#if CONFIG_TABREMOTO_DIAGNOSTICS
      ok = op == "ssh-ui" ? tabremoto::remote_ui_test_connect(r)
                          : tabremoto::ssh_connect(r);
#else
      ok = tabremoto::ssh_connect(r);
#endif
      if (!ok)
        delete r;
    } else if (op == "vpn-file") {
      ok = tabremoto::network_vpn_file(value(j, "file"), value(j, "transport"));
    } else if (op == "vpn") {
      auto config = value(j, "config");
      ok = tabremoto::network_vpn(config, value(j, "transport"),
                                  value(j, "profile"));
      mbedtls_platform_zeroize(config.data(), config.size());
    } else
      ok = false;
  }
  if(!request_id.empty() && request_id.size()==32 && request_id.find_first_not_of("0123456789abcdef")==std::string::npos) {last_request_id=request_id;last_request_accepted=ok;}
  wipe(j);
  cJSON_Delete(j);
  mbedtls_platform_zeroize(plain.data(), plain.size());
  mbedtls_platform_zeroize(bytes.data(), bytes.size());
  return ok;
}
