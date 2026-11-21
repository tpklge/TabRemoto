#include "network.hpp"
#include "profiles.hpp"
#include "network_ip_policy.hpp"
#include "dns_resolver.hpp"
#include <fcntl.h>
#include "bootloader_random.h"
#include "bsp/m5stack_tab5.h"
#include "cJSON.h"
#include "crypto_gate.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "imports.hpp"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "iot_usbh_ecm.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "ncm_driver.h"
#include "network_numbers.hpp"
#include "network_routes.hpp"
#include "storage.hpp"
#include "usb/usb_host.h"
#include "wireguard-platform.h"
#include "wireguardif.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <vector>
namespace tabremoto {
namespace {
SemaphoreHandle_t mutex;
NetworkStatus state;
esp_netif_t *wifi_if, *eth_if;
std::atomic<uint32_t> epoch{0};
std::atomic<bool> wifi_ready{false};
std::atomic<bool> wifi_job{false};
std::atomic<bool> vpn_job{false};
std::atomic<unsigned> vpn_generation{0};
std::atomic<bool> wifi_wanted{false};
std::atomic<unsigned> reconnect_attempt{0};
std::string active_ssid,active_password;
std::atomic<uint32_t> reconnect_at{0},automatic_after{0};
using Cidr = NetworkCidr;
struct Vpn {
  bool armed = false, added = false, private_dns = false;
  netif tunnel{}, sink{};
  netif *physical = nullptr;
  ip4_addr_t endpoint{};
  std::vector<NetworkPrefix> routes;
  uint8_t peer = 255;
  std::string dns_servers;
  NetworkIp local6;
  unsigned generation = 0;
  std::string private_key, public_key, tag, transport;
  unsigned mtu = 1420;
};
std::array<Vpn, 4> vpns;
void change() { state.revision++; }
void detail(const std::string &s) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  state.detail = s;
  change();
  xSemaphoreGive(mutex);
}
netif *native(esp_netif_t *n) {
  return static_cast<netif *>(esp_netif_get_netif_impl(n));
}
netif *physical(const std::string &name) {
  if (name == "ethernet")
    return native(eth_if);
  if (name == "wifi")
    return native(wifi_if);
  auto *ethernet = native(eth_if);
  if (ethernet && netif_is_up(ethernet) && netif_is_link_up(ethernet) &&
      !ip4_addr_isany_val(*netif_ip4_addr(ethernet)))
    return ethernet;
  return native(wifi_if);
}
void events(void *, esp_event_base_t base, int32_t id, void *data) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    state.wifi = false;
    state.wifi_ip.clear();
    ++epoch;
    auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
    state.detail =
        "Wi-Fi desconectado (motivo " + std::to_string(event->reason) + ")";
    unsigned attempt = reconnect_attempt.load();
    if (wifi_wanted && !wifi_job && attempt < 5) {
      reconnect_attempt = attempt + 1;
      reconnect_at = uint32_t(esp_timer_get_time() / 1000) + (1000u << attempt);
      state.detail += "; reconexao agendada";
    }
  }
  if (base == IP_EVENT &&
      (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP)) {
    auto *info = (ip_event_got_ip_t *)data;
    char ip[20];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info->ip_info.ip));
    if (info->esp_netif == wifi_if) {
      state.wifi = true;
      state.wifi_ip = ip;
      reconnect_attempt = 0;
      reconnect_at = 0;
      if(!active_ssid.empty())profiles_save_wifi(active_ssid,active_password,!active_password.empty());
    } else if (info->esp_netif == eth_if) {
      state.ethernet = true;
      state.ethernet_ip = ip;
    }
    state.detail = "IP obtido";
    ++epoch;
  }
  if (base == IOT_ETH_EVENT && id == IOT_ETH_EVENT_DISCONNECTED) {
    state.ethernet = false;
    state.ethernet_ip.clear();
    ++epoch;
    state.detail = "Ethernet sem link";
  }
  change();
  xSemaphoreGive(mutex);
  if (base == IP_EVENT &&
      (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP))
    esp_netif_sntp_start();
}
struct WifiJob {
  std::string ssid, password;
  bool scan_only=false,automatic=false;
  ~WifiJob() { mbedtls_platform_zeroize(password.data(), password.size()); }
};
void wifi_worker(void *raw) {
  auto *job = (WifiJob *)raw;
  if (!wifi_ready) {
    detail("Inicializando C6/Wi-Fi");
    bsp_feature_enable(BSP_FEATURE_WIFI, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_err_t e = esp_hosted_connect_to_slave();
    if (e == ESP_OK) {
      wifi_if = esp_netif_create_default_wifi_sta();
      wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      cfg.nvs_enable = false;
      e = esp_wifi_init(&cfg);
    }
    if (e == ESP_OK)
      e = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (e == ESP_OK)
      e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e == ESP_OK)
      e = esp_wifi_start();
    if (e != ESP_OK) {
      detail(std::string("C6/Wi-Fi: ") + esp_err_to_name(e));
      delete job;
      wifi_job = false;
      vTaskDelete(nullptr);
      return;
    }
    wifi_ready = true;
  }
  if(!job->scan_only && !job->automatic){wifi_wanted=false;reconnect_at=0;esp_wifi_disconnect();}
  // Scan in the same worker as association, without exposing nearby SSIDs.
  wifi_scan_config_t scan{};
  scan.show_hidden = true;
  auto scanned = esp_wifi_scan_start(&scan, true);
  uint16_t ap_count = 0;
  if (scanned == ESP_OK)
    esp_wifi_scan_get_ap_num(&ap_count);
  ap_count=std::min<uint16_t>(ap_count,32);
  std::vector<wifi_ap_record_t> aps(ap_count);
  if (ap_count && scanned == ESP_OK)
    scanned = esp_wifi_scan_get_ap_records(&ap_count, aps.data());
  bool found = false;
  unsigned channel = 0;
  for (const auto &ap : aps) {
    if (job->ssid == reinterpret_cast<const char *>(ap.ssid)) {
      found = true;
      channel = ap.primary;
      break;
    }
  }
  auto *list = cJSON_CreateArray();
  for (const auto &ap : aps) {
    auto *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "ssid",
                            reinterpret_cast<const char *>(ap.ssid));
    cJSON_AddNumberToObject(item, "channel", ap.primary);
    cJSON_AddNumberToObject(item, "rssi", ap.rssi);
    cJSON_AddBoolToObject(item,"protected",ap.authmode!=WIFI_AUTH_OPEN);
    cJSON_AddItemToArray(list, item);
  }
  char *encoded = cJSON_PrintUnformatted(list);
  cJSON_Delete(list);
  xSemaphoreTake(mutex, portMAX_DELAY);
  state.wifi_scan_json = encoded ? encoded : "[]";
  cJSON_free(encoded);
  state.scan_count = ap_count;
  state.scan_match = found;
  state.scan_channel = channel;
  xSemaphoreGive(mutex);
  detail("Varredura Wi-Fi: " + std::to_string(ap_count) + " redes; alvo " +
         (found ? "encontrado canal " + std::to_string(channel) : "ausente") +
         "; " + esp_err_to_name(scanned));
  if(job->automatic){
    auto known=profiles_wifi();
    for(const auto &saved:known){
      if(!saved.protected_network || saved.password.empty())continue;
      auto ap=std::find_if(aps.begin(),aps.end(),[&](const auto &ap){return ap.authmode!=WIFI_AUTH_OPEN && saved.ssid==reinterpret_cast<const char*>(ap.ssid);});
      if(ap!=aps.end()){job->ssid=saved.ssid;job->password=saved.password;break;}
    }
  }
  if(job->scan_only || job->ssid.empty()){
    detail("Busca concluida: "+std::to_string(ap_count)+" redes. Redes abertas exigem conexao manual.");
    delete job;wifi_job=false;vTaskDelete(nullptr);return;
  }
  wifi_config_t cfg{};
  memcpy(cfg.sta.ssid, job->ssid.data(), job->ssid.size());
  memcpy(cfg.sta.password, job->password.data(), job->password.size());
  cfg.sta.threshold.authmode =
      job->password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  auto e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
  if (e == ESP_OK) {
    xSemaphoreTake(mutex,portMAX_DELAY);active_ssid=job->ssid;state.wifi_automatic=job->automatic;
    mbedtls_platform_zeroize(active_password.data(),active_password.size());active_password=job->password;xSemaphoreGive(mutex);
    wifi_wanted = !job->password.empty();
    e = esp_wifi_connect();
  }
  if (e != ESP_OK)
    wifi_wanted = false;
  mbedtls_platform_zeroize(&cfg, sizeof(cfg));
  detail(e == ESP_OK ? "Wi-Fi: aguardando DHCP" : esp_err_to_name(e));
  delete job;
  wifi_job = false;
  vTaskDelete(nullptr);
}
void network_maintenance(void *) {
  uint32_t signal_at = 0,auto_at=0;
  for (;;) {
    uint32_t now_ms = uint32_t(esp_timer_get_time() / 1000);
    uint32_t due = reconnect_at.load();
    if (due && int32_t(now_ms - due) >= 0 && !wifi_job.exchange(true)) {
      reconnect_at = 0;
      auto e = esp_wifi_connect();
      wifi_job = false;
      if (e != ESP_OK) {
        detail(std::string("Reconexao Wi-Fi: ") + esp_err_to_name(e));
        unsigned attempt = reconnect_attempt.load();
        if (wifi_wanted && attempt < 5) {
          reconnect_attempt = attempt + 1;
          reconnect_at = now_ms + (1000u << attempt);
        }
      }
    }
    if(profiles_ready() && !wifi_job && !reconnect_at && int32_t(now_ms-auto_at)>=0 && int32_t(now_ms-automatic_after.load())>=0){
      auto_at=now_ms+30000;bool connected;
      xSemaphoreTake(mutex,portMAX_DELAY);connected=state.wifi;xSemaphoreGive(mutex);
      auto known=profiles_wifi();bool secured=std::any_of(known.begin(),known.end(),[](const auto &w){return w.protected_network&&!w.password.empty();});
      if(!connected && secured && !wifi_job.exchange(true)){
        auto *job=new WifiJob;job->automatic=true;
        if(xTaskCreate(wifi_worker,"wifi_auto",8192,job,2,nullptr)!=pdPASS){delete job;wifi_job=false;}
      }
    }
    if (wifi_ready && int32_t(now_ms - signal_at) >= 0 && !wifi_job) {
      signal_at = now_ms + 3000;
      wifi_ap_record_t ap{};
      if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (state.rssi != ap.rssi) {
          state.rssi = ap.rssi;
          change();
        }
        xSemaphoreGive(mutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
void usb_event(usbh_cdc_device_event_t event,
               usbh_cdc_device_event_data_t *data, void *) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  state.usb_present = event == CDC_HOST_DEVICE_EVENT_CONNECTED;
  if (state.usb_present) {
    state.vid = data->new_dev.device_desc->idVendor;
    state.pid = data->new_dev.device_desc->idProduct;
  }
  change();
  xSemaphoreGive(mutex);
}
err_t drop(netif *, pbuf *, const ip4_addr_t *) { return ERR_RTE; }
bool contains(const std::vector<NetworkPrefix> &routes, const NetworkIp &ip) {
  for (const auto &r : routes) if (network_prefix_contains(r, ip)) return true;
  return false;
}
NetworkIp from4(uint32_t ip) { NetworkIp out; out.family=4; memcpy(out.bytes.data(),&ip,4); return out; }
ip_addr_t lwip_address(const NetworkIp &ip) {
  ip_addr_t out{};
  if(ip.family==4) {uint32_t v; memcpy(&v,ip.bytes.data(),4); ip_addr_set_ip4_u32(&out,v);}
  else {IP_SET_TYPE_VAL(out,IPADDR_TYPE_V6); memcpy(ip_2_ip6(&out)->addr,ip.bytes.data(),16);}
  return out;
}
bool vpn_up(const Vpn &v) {return v.added && v.physical && netif_is_up(v.physical) && netif_is_link_up(v.physical) && wireguardif_peer_is_up(const_cast<netif*>(&v.tunnel),v.peer,nullptr,nullptr)==ERR_OK;}
NetworkRouteChoice choose(const NetworkIp &ip,const std::string &profile="",bool required=false) {
  std::vector<NetworkRouteView> view;
  for(const auto &v:vpns) view.push_back({v.tag,v.routes,v.armed,vpn_up(v)});
  return network_route_choose(view,ip,profile,required);
}
err_t drop6(netif*,pbuf*,const ip6_addr_t*) {return ERR_RTE;}

std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \r\t"), b = s.find_last_not_of(" \r\t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool key_valid(const std::string &s) {
  unsigned char data[32];
  size_t n = 0;
  bool ok =
      mbedtls_base64_decode(data, sizeof(data), &n,
                            (const unsigned char *)s.data(), s.size()) == 0 &&
      n == 32;
  mbedtls_platform_zeroize(data, sizeof(data));
  return ok;
}
std::string physical_dns(netif *under) {
  std::string servers;
  auto *iface=under && under==native(eth_if)?eth_if:wifi_if;
  for(unsigned i=0;iface && i<2;++i) {
    esp_netif_dns_info_t info{};
    if(esp_netif_get_dns_info(iface,(esp_netif_dns_type_t)i,&info)!=ESP_OK) continue;
    NetworkIp ip;
    ip.family=info.ip.type==ESP_IPADDR_TYPE_V6?6:4;
    memcpy(ip.bytes.data(),ip.family==4?(void*)&info.ip.u_addr.ip4.addr:(void*)info.ip.u_addr.ip6.addr,ip.family==4?4:16);
    if(ip.empty()) continue;
    if(!servers.empty()) servers+=",";
    servers+=ip.family==6?"["+network_ip_text(ip)+"]":network_ip_text(ip);
  }
  return servers;
}
bool bind_physical_dns(int fd,const std::string &transport,unsigned index) {
  ifreq req{};
  LOCK_TCPIP_CORE();auto *n=physical(transport);
  bool ok=n && netif_get_index(n)==index && netif_is_up(n) && netif_is_link_up(n);
  if(ok) netif_index_to_name(index,req.ifr_name);
  UNLOCK_TCPIP_CORE();
  return ok && setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&req,sizeof(req))==0;
}
struct VpnJob {
  std::string config, transport, tag, file;
  unsigned generation;
  ~VpnJob() { mbedtls_platform_zeroize(config.data(), config.size()); }
};
void destroy_vpn(Vpn &vpn, bool clear) {
  if (vpn.added) {
    if (vpn.peer != 255) {
      wireguardif_disconnect(&vpn.tunnel, vpn.peer);
      wireguardif_remove_peer(&vpn.tunnel, vpn.peer);
    }
    wireguardif_shutdown(&vpn.tunnel);
    netif_remove(&vpn.tunnel);
    wireguardif_fini(&vpn.tunnel);
    vpn.added = false;
    vpn.peer = 255;
    ++vpn.generation;
  }
  if (clear) {
    vpn.armed = false;
    vpn.routes.clear();
    vpn.tag.clear();
    vpn.dns_servers.clear();
    vpn.private_dns=false;
  }
  mbedtls_platform_zeroize(vpn.private_key.data(), vpn.private_key.size());
  vpn.private_key.clear();
}
void vpn_run(VpnJob *job) {
  if (!job->file.empty()) {
    std::string error;
    bool lease = storage_capture_begin();
    bool ok = lease && import_read(job->file, job->config, error);
    if (lease)
      storage_capture_end();
    if (!ok) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      state.vpn_detail = error.empty() ? "SD ocupado ou ausente" : error;
      change();
      xSemaphoreGive(mutex);
      return;
    }
  }
  Vpn *selected = nullptr;
  bool created_here = false;
  static bool random_ready = false;
  if (!random_ready) {
    while (!crypto_gate_enter())
      vTaskDelay(1);
    bootloader_random_enable();
    random_ready = wireguard_platform_init() == ESP_OK;
    bootloader_random_disable();
    crypto_gate_leave();
    if (!random_ready) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      state.vpn_detail = "Inicializacao do RNG WireGuard falhou";
      change();
      xSemaphoreGive(mutex);
      return;
    }
  }
  std::map<std::string, std::string> values;
  std::string section, line, error;
  std::istringstream input(job->config);
  bool peer = false;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;
    if (line[0] == '[') {
      if (line == "[Interface]" && section.empty())
        section = "Interface";
      else if (line == "[Peer]" && !peer) {
        section = "Peer";
        peer = true;
      } else {
        error = "Secao duplicada ou nao suportada";
        break;
      }
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      error = "Linha WireGuard invalida";
      break;
    }
    auto k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
    std::string key = section + "." + k;
    const std::vector<std::string> supported = {
        "Interface.PrivateKey",    "Interface.Address",
        "Interface.DNS",           "Interface.MTU",
        "Peer.PublicKey",          "Peer.PresharedKey",
        "Peer.Endpoint",           "Peer.AllowedIPs",
        "Peer.PersistentKeepalive"};
    if (std::find(supported.begin(), supported.end(), key) == supported.end() ||
        values.count(key)) {
      error = "Opcao WireGuard nao suportada ou repetida";
      break;
    }
    values[key] = v;
  }
  std::vector<NetworkPrefix> routes;
  std::istringstream list(values["Peer.AllowedIPs"]);
  while (std::getline(list, line, ',')) {
    NetworkPrefix c;
    if (!network_prefix_parse(trim(line), c) || routes.size() >= 8) {
      error = "AllowedIPs: ate 8 CIDRs IPv4/IPv6";
      break;
    }
    routes.push_back(c);
  }
  NetworkPrefix address{};
  ip4_addr_t local{}, local_mask{};
  NetworkIp local6;
  std::istringstream addresses(values["Interface.Address"]);
  while(std::getline(addresses,line,',')) {
    NetworkPrefix prefix; NetworkIp host;
    if(!network_prefix_parse(trim(line),prefix,&host) || host.empty()) {error="Address IPv4/IPv6 invalido";break;}
    if(host.family==4) {
      if(local.addr) {error="Address IPv4 repetido";break;}
      memcpy(&local.addr,host.bytes.data(),4);
      auto mask=network_prefix_mask(prefix);memcpy(&local_mask.addr,mask.bytes.data(),4);address=prefix;
    } else {
      if(local6.family) {error="Address IPv6 repetido";break;}
      local6=host;
    }
  }
  if(!local.addr && !local6.family) error="Address obrigatorio";
  auto endpoint = values["Peer.Endpoint"];
  auto colon = endpoint.rfind(':');
  unsigned port = 0;
  if (colon == std::string::npos ||
      !bounded_unsigned(endpoint.substr(colon + 1), 1, 65535, port))
    error = "Porta do endpoint invalida";
  if (!key_valid(values["Interface.PrivateKey"]) ||
      !key_valid(values["Peer.PublicKey"]) ||
      (!values["Peer.PresharedKey"].empty() &&
       !key_valid(values["Peer.PresharedKey"])))
    error = "Chave WireGuard invalida";
  if (routes.empty() || port < 1 || port > 65535)
    error = "Endpoint/AllowedIPs invalidos";
  unsigned mtu = 1420, keep = 25;
  if (!values["Interface.MTU"].empty() &&
      !bounded_unsigned(values["Interface.MTU"], 576, 1420, mtu))
    error = "MTU: 576 a 1420";
  if (!values["Peer.PersistentKeepalive"].empty() &&
      !bounded_unsigned(values["Peer.PersistentKeepalive"], 0, 65535, keep))
    error = "Keepalive invalido";
  auto *under = physical(job->transport);
  if (!under || !netif_is_up(under) || !netif_is_link_up(under))
    error = "Interface fisica sem IP/link";
  std::string endpoint_host=endpoint.substr(0,colon);
  if(endpoint_host.size()>2 && endpoint_host.front()=='[' && endpoint_host.back()==']') endpoint_host=endpoint_host.substr(1,endpoint_host.size()-2);
  NetworkIp endpoint_ip;
  if(error.empty() && !network_ip_parse(endpoint_host,endpoint_ip)) {
    std::vector<DnsAddress> addresses; auto servers=physical_dns(under);
    unsigned index=under?netif_get_index(under):0;
    if(servers.empty() || !dns_resolve(endpoint_host,servers,[&](int fd){return bind_physical_dns(fd,job->transport,index);},addresses,error) || addresses.empty()) error="Endpoint sem DNS na interface fisica selecionada";
    else {
      auto selected=std::find_if(addresses.begin(),addresses.end(),[](const DnsAddress &a){return a.family==4;});
      if(selected==addresses.end()) selected=addresses.begin();
      endpoint_ip.family=selected->family;endpoint_ip.bytes=selected->bytes;
    }
  }
  if(error.empty() && endpoint_ip.family==6) {
    LOCK_TCPIP_CORE();bool available=false;
    for(unsigned i=0;i<LWIP_IPV6_NUM_ADDRESSES;++i)
      if(ip6_addr_isvalid(netif_ip6_addr_state(under,i)) && !ip6_addr_isany(netif_ip6_addr(under,i))) available=true;
    UNLOCK_TCPIP_CORE();
    if(!available) error="Interface fisica sem IPv6 para o endpoint; use IPv4";
  }
  std::string dns_servers;
  std::istringstream dns_list(values["Interface.DNS"]); unsigned dns_count=0;
  while(std::getline(dns_list,line,',')) {
    NetworkIp ip;
    if(!network_ip_parse(trim(line),ip) || !contains(routes,ip) || ++dns_count>2) {error="DNS: ate dois IPs dentro de AllowedIPs";break;}
    if(!dns_servers.empty()) dns_servers+=",";
    dns_servers += ip.family==6 ? "["+network_ip_text(ip)+"]" : network_ip_text(ip);
  }
  if(local6.family && mtu<1280) error="IPv6 requer MTU de pelo menos 1280";
  if (error.empty()) {
    LOCK_TCPIP_CORE();
    if (job->generation != vpn_generation.load()) {
      error = "Solicitacao VPN cancelada";
    } else {
      if (!job->tag.empty())
        for (auto &slot : vpns)
          if (slot.armed && slot.tag == job->tag) {
            selected = &slot;
            break;
          }
      if (!selected)
        for (auto &slot : vpns)
          if (!slot.armed) {
            selected = &slot;
            break;
          }
      if (!selected)
        error = "Limite de quatro perfis; remova um perfil parado para liberar "
                "a vaga";
      else if (selected->added)
        error = "Este perfil ja esta aberto";
      if (error.empty())
        for (auto &slot : vpns) {
          if (&slot == selected || !slot.armed)
            continue;
          if (slot.added &&
              ((local.addr && ip4_addr_cmp(&local, netif_ip4_addr(&slot.tunnel))) || (local6.family && local6==slot.local6))) {
            error = "Endereco local ja usado por outro tunel";
            break;
          }
        }
      if (error.empty() && selected->armed) {
        if (selected->routes.size() != routes.size())
          error = "Rotas mudaram; remova explicitamente o perfil parado antes";
        else
          for (unsigned i = 0; i < routes.size(); i++)
            if (!(selected->routes[i] == routes[i]))
              error = "Rotas mudaram; remova o perfil parado antes";
      }
      if (error.empty()) {
        auto &vpn = *selected;
        vpn.transport = job->transport;
        vpn.tag = job->tag.empty()
                      ? "tunel-" + std::to_string(selected - vpns.data() + 1)
                      : job->tag;
        vpn.physical = under;

        vpn.routes = routes;
        vpn.local6=local6;
        vpn.dns_servers=dns_servers;
        vpn.private_dns=!dns_servers.empty();
        ++vpn.generation;
        vpn.armed = true;
        vpn.private_key = values["Interface.PrivateKey"];
        vpn.public_key = values["Peer.PublicKey"];
        if (!vpn.sink.output) {
          vpn.sink.name[0] = 'b';
          vpn.sink.name[1] = 'h';
          vpn.sink.output = drop;
          vpn.sink.output_ip6=drop6;
          vpn.sink.mtu = 1420;
          vpn.sink.flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
        }
        wireguardif_init_data data{vpn.private_key.c_str(), 0, under};
        ip4_addr_t mask=local_mask, gw{};
        auto *added = netif_add(&vpn.tunnel, &local, &mask, &gw, &data,
                                wireguardif_init, ip_input);
        if (!added)
          error = "WireGuard netif falhou";
        else {
          vpn.added = true;
          created_here = true;
          vpn.tunnel.mtu = mtu;
          netif_set_up(&vpn.tunnel);
          if(local6.family) {
            // IDF's default IPv6 input hook rejects interfaces with an empty
            // link-local slot, even when a static ULA is already preferred.
            ip6_addr_t linklocal{};
            linklocal.addr[0]=lwip_htonl(0xfe800000);
            linklocal.addr[2]=lwip_htonl(0x54414235);
            linklocal.addr[3]=lwip_htonl(unsigned(selected-vpns.data())+1);
            ip6_addr_assign_zone(&linklocal,IP6_UNICAST,&vpn.tunnel);
            netif_ip6_addr_set(&vpn.tunnel,0,&linklocal);
            netif_ip6_addr_set_state(&vpn.tunnel,0,IP6_ADDR_PREFERRED);
            auto v6=lwip_address(local6); s8_t index=-1;
            if(netif_add_ip6_address(&vpn.tunnel,ip_2_ip6(&v6),&index)!=ERR_OK || index<0) error="Endereco IPv6 do tunel falhou";
            else netif_ip6_addr_set_state(&vpn.tunnel,index,IP6_ADDR_PREFERRED);
          }
          wireguardif_peer p{};
          p.public_key = vpn.public_key.c_str();
          unsigned char psk[32]{};
          size_t used = 0;
          if (!values["Peer.PresharedKey"].empty()) {
            mbedtls_base64_decode(
                psk, 32, &used,
                (const unsigned char *)values["Peer.PresharedKey"].data(),
                values["Peer.PresharedKey"].size());
            p.preshared_key = psk;
          }
          p.allowed_ip=lwip_address(routes[0].network);
          p.allowed_mask=lwip_address(network_prefix_mask(routes[0]));
          p.endpoint_ip=lwip_address(endpoint_ip);
          p.endport_port = port;
          p.keep_alive = keep;
          if (wireguardif_add_peer(&vpn.tunnel, &p, &vpn.peer) != ERR_OK)
            error = "WireGuard peer falhou";
          for (unsigned i = 1; error.empty() && i < routes.size(); i++) {
            ip_addr_t ip{}, mask{};
            ip=lwip_address(routes[i].network);
            mask=lwip_address(network_prefix_mask(routes[i]));
            if (wireguardif_add_allowed_ip(&vpn.tunnel, vpn.peer, ip, mask) !=
                ERR_OK)
              error = "Limite AllowedIPs do backend";
          }
          if (error.empty() &&
              wireguardif_connect(&vpn.tunnel, vpn.peer) != ERR_OK)
            error = "WireGuard inicio falhou";
          mbedtls_platform_zeroize(psk, 32);
        }
      }
    }
    if (!error.empty() && created_here && selected && selected->added)
      destroy_vpn(*selected, false);
    UNLOCK_TCPIP_CORE();
  }

  for (auto &pair : values)
    mbedtls_platform_zeroize(pair.second.data(), pair.second.size());
  LOCK_TCPIP_CORE();
  bool armed_any = std::any_of(vpns.begin(), vpns.end(),
                               [](const Vpn &v) { return v.armed; });
  UNLOCK_TCPIP_CORE();
  xSemaphoreTake(mutex, portMAX_DELAY);
  state.vpn_detail = error.empty() ? "Aguardando handshake real" : error;
  state.vpn_armed = armed_any;
  change();
  xSemaphoreGive(mutex);
  ++epoch;
}
void vpn_worker(void *raw) {
  {
    std::unique_ptr<VpnJob> job((VpnJob *)raw);
    vpn_run(job.get());
  }
  vpn_job = false;
  vTaskDelete(nullptr);
}
} // namespace
void network_start() {
  mutex = xSemaphoreCreateMutex();
  esp_netif_init();
  esp_event_loop_create_default();
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, events, nullptr);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, events, nullptr);
  esp_event_handler_register(IOT_ETH_EVENT, ESP_EVENT_ANY_ID, events, nullptr);
  esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  ntp.start = false;
  esp_netif_sntp_init(&ntp);
  state.initialized = true;
  xTaskCreate(network_maintenance, "network_watch", 4096, nullptr, 1, nullptr);
}
NetworkStatus network_status() {
  if (!mutex)
    return {};
  std::vector<VpnStatus> tunnels;
  bool any_up = false, armed = false;
  uint64_t rx = 0, tx = 0;
  uint32_t age = 0xffffffffu;
  std::string profile;
  LOCK_TCPIP_CORE();
  for (auto &vpn : vpns) {
    if (!vpn.armed)
      continue;
    VpnStatus entry;
    entry.id = vpn.tag;
    entry.transport = vpn.transport;
    entry.armed = true;
    entry.up = vpn.added &&
               wireguardif_peer_is_up(&vpn.tunnel, vpn.peer, nullptr,
                                      nullptr) == ERR_OK &&
               vpn.physical && netif_is_up(vpn.physical) &&
               netif_is_link_up(vpn.physical);
    if (vpn.added) {
      wireguardif_stats(&vpn.tunnel, vpn.peer, &entry.rx, &entry.tx,
                        &entry.handshake_age_ms);
      char ip[20];
      ip4addr_ntoa_r(netif_ip4_addr(&vpn.tunnel), ip, sizeof(ip));
      entry.ip = ip4_addr_isany_val(*netif_ip4_addr(&vpn.tunnel))?"":ip;
      if(vpn.local6.family) entry.ip+=(entry.ip.empty()?"":" / ")+network_ip_text(vpn.local6);
    }
    if (entry.up) {
      any_up = true;
      if (profile.empty())
        profile = entry.id;
      age = std::min(age, entry.handshake_age_ms);
    }
    armed = true;
    rx += entry.rx;
    tx += entry.tx;
    tunnels.push_back(std::move(entry));
  }
  UNLOCK_TCPIP_CORE();
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (state.vpn != any_up || state.rx != rx || state.tx != tx ||
      state.tunnels.size() != tunnels.size())
    change();
  state.vpn = any_up;
  state.vpn_armed = armed;
  state.rx = rx;
  state.tx = tx;
  state.handshake_age_ms = age;
  state.profile_id = profile;
  state.tunnels = std::move(tunnels);
  auto copy = state;
  xSemaphoreGive(mutex);
  return copy;
}
bool network_vpn_active(const std::string &tag) {
  auto status = network_status();
  for (auto &vpn : status.tunnels)
    if (vpn.up && (tag.empty() || vpn.id == tag))
      return true;
  return false;
}
bool network_wifi(const std::string &ssid, const std::string &password) {
  if (ssid.empty() || ssid.size() > 32 || password.size() > 63)
    return false;
  if (wifi_job.exchange(true))
    return false;
  reconnect_attempt = 0;
  reconnect_at = 0;
  automatic_after=uint32_t(esp_timer_get_time()/1000)+30000;
  detail("Preparando Wi-Fi");
  auto *job = new WifiJob{ssid, password};
  if (xTaskCreate(wifi_worker, "network_wifi", 8192, job, 3, nullptr) !=
      pdPASS) {
    delete job;
    wifi_job = false;
    return false;
  }
  return true;
}
bool network_wifi_busy(){return wifi_job;}
bool network_wifi_scan(){
 if(wifi_job.exchange(true))return false;
 auto *job=new WifiJob;job->scan_only=true;
 if(xTaskCreate(wifi_worker,"wifi_scan",8192,job,2,nullptr)==pdPASS)return true;
 delete job;wifi_job=false;return false;
}
bool network_wifi_cycle() {
  return wifi_ready && !wifi_job && wifi_wanted &&
         esp_wifi_disconnect() == ESP_OK;
}
static bool ethernet_enum_filter(const usb_device_desc_t *descriptor,
                                 uint8_t *configuration) {
  if (descriptor->idVendor == 0x0b95 && descriptor->idProduct == 0x1790 &&
      descriptor->bNumConfigurations >= 2) {
    *configuration = 2;
  }
  return true;
}
static void ethernet_usb_task(void *) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
      usb_host_device_free_all();
  }
}
bool network_ethernet() {
  static bool started = false;
  if (started)
    return true;
  if (bsp_feature_enable(BSP_FEATURE_USB, true) != ESP_OK)
    return false;
  usb_host_config_t hc{};
  hc.intr_flags = ESP_INTR_FLAG_LOWMED;
  hc.enum_filter_cb = ethernet_enum_filter;
  if (usb_host_install(&hc) != ESP_OK)
    return false;
  if (xTaskCreate(ethernet_usb_task, "usb_lib", 4096, nullptr, 10, nullptr) !=
      pdPASS) {
    usb_host_uninstall();
    return false;
  }
  usbh_cdc_driver_config_t cfg{};
  cfg.task_stack_size = 6144;
  cfg.task_priority = 5;
  cfg.task_coreid = -1;
  cfg.skip_init_usb_host_driver = true;
  if (usbh_cdc_driver_install(&cfg) != ESP_OK)
    return false;
  usbh_cdc_register_dev_event_cb(ESP_USB_DEVICE_MATCH_ID_ANY, usb_event,
                                 nullptr);
  static const usb_device_match_id_t match[] = {
      {.match_flags = (usb_dev_match_flags_t)(USB_DEVICE_ID_MATCH_INT_CLASS |
                                              USB_DEVICE_ID_MATCH_INT_SUBCLASS),
       .bInterfaceClass = 2,
       .bInterfaceSubClass = 13},
      {}};
  iot_eth_driver_t *driver = nullptr;
  iot_usbh_ncm_config_t ecfg{};
  ecfg.match_id_list = match;
  iot_eth_handle_t eth = nullptr;
  iot_eth_config_t e{};
  if (iot_eth_new_usb_ncm(&ecfg, &driver) != ESP_OK)
    return false;
  e.driver = driver;
  if (iot_eth_install(&e, &eth) != ESP_OK)
    return false;
  esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base.if_key = "TABREMOTO_ETH";
  base.if_desc = "USB NCM";
  base.route_prio = 60;
  esp_netif_config_t nc{};
  nc.base = &base;
  nc.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  eth_if = esp_netif_new(&nc);
  if (!eth_if ||
      esp_netif_attach(eth_if, iot_eth_new_netif_glue(eth)) != ESP_OK)
    return false;
  started = iot_eth_start(eth) == ESP_OK;
  return started;
}
namespace {
struct EthernetIpJob {
  bool dhcp;
  esp_netif_ip_info_t ip{};
  esp_netif_dns_info_t dns{};
};
std::atomic<bool> ethernet_ip_busy{false};
void ethernet_ip_worker(void *raw) {
  std::unique_ptr<EthernetIpJob> job(static_cast<EthernetIpJob *>(raw));
  ++epoch;
  auto rc = esp_netif_dhcpc_stop(eth_if);
  if (rc == ESP_OK || rc == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    if (job->dhcp)
      rc = esp_netif_dhcpc_start(eth_if);
    else {
      rc = esp_netif_set_ip_info(eth_if, &job->ip);
      if (rc == ESP_OK)
        rc = esp_netif_set_dns_info(eth_if, ESP_NETIF_DNS_MAIN, &job->dns);
    }
  }
  detail(rc == ESP_OK
             ? (job->dhcp
                    ? "Ethernet: DHCP solicitado"
                    : "Ethernet: IP fixo configurado; confira link e acesso")
             : esp_err_to_name(rc));
  ethernet_ip_busy = false;
  job.reset();
  vTaskDelete(nullptr);
}
} // namespace
bool network_ethernet_ip(bool dhcp, const std::string &address,
                         const std::string &mask, const std::string &gateway,
                         const std::string &dns) {
  if (!eth_if || ethernet_ip_busy.exchange(true))
    return false;
  auto *job = new EthernetIpJob;
  job->dhcp = dhcp;
  job->dns.ip.type = ESP_IPADDR_TYPE_V4;
  bool valid =
      dhcp || (ip4addr_aton(address.c_str(),
                            reinterpret_cast<ip4_addr_t *>(&job->ip.ip)) &&
               ip4addr_aton(mask.c_str(),
                            reinterpret_cast<ip4_addr_t *>(&job->ip.netmask)) &&
               ip4addr_aton(gateway.c_str(),
                            reinterpret_cast<ip4_addr_t *>(&job->ip.gw)) &&
               ip4addr_aton(dns.c_str(), reinterpret_cast<ip4_addr_t *>(
                                             &job->dns.ip.u_addr.ip4)));
  if (!dhcp && valid) {
    uint32_t bits = lwip_ntohl(job->ip.netmask.addr),
             ip = lwip_ntohl(job->ip.ip.addr), gw = lwip_ntohl(job->ip.gw.addr);
    uint32_t hostmask = ~bits;
    valid = bits && bits != 0xffffffffu && ((hostmask & (hostmask + 1)) == 0) &&
            (ip & hostmask) && (ip & hostmask) != hostmask && (ip >> 24) != 0 &&
            (ip >> 24) < 224 && (!gw || ((gw & bits) == (ip & bits)));
  }
  if (!valid || xTaskCreate(ethernet_ip_worker, "eth_config", 6144, job, 1,
                            nullptr) != pdPASS) {
    delete job;
    ethernet_ip_busy = false;
    return false;
  }
  return true;
}
bool network_vpn(const std::string &conf, const std::string &transport,
                 const std::string &tag) {
  if (conf.size() > 8192 ||
      (transport != "auto" && transport != "wifi" && transport != "ethernet") ||
      vpn_job.exchange(true))
    return false;
  auto *job = new VpnJob{conf, transport, tag, "", vpn_generation.load()};
  if (xTaskCreate(vpn_worker, "wg_connect", 12288, job, 2, nullptr) != pdPASS) {
    delete job;
    vpn_job = false;
    return false;
  }
  return true;
}
bool network_vpn_file(const std::string &path, const std::string &transport) {
  if (!import_path(path) || (transport != "wifi" && transport != "ethernet") ||
      vpn_job.exchange(true))
    return false;
  auto *job =
      new VpnJob{"", transport, "arquivo:" + path.substr(path.rfind('/') + 1),
                 path, vpn_generation.load()};
  if (xTaskCreate(vpn_worker, "wg_file", 12288, job, 1, nullptr) != pdPASS) {
    delete job;
    vpn_job = false;
    return false;
  }
  return true;
}
void network_vpn_stop(bool clear, const std::string &tag) {
  ++vpn_generation;
  LOCK_TCPIP_CORE();
  for (auto &vpn : vpns)
    if (tag.empty() || vpn.tag == tag)
      destroy_vpn(vpn, clear);
  bool armed = std::any_of(vpns.begin(), vpns.end(),
                           [](const Vpn &v) { return v.armed; });
  UNLOCK_TCPIP_CORE();
  xSemaphoreTake(mutex, portMAX_DELAY);
  state.vpn_armed = armed;
  state.vpn_detail =
      armed ? "Perfil parado; rotas protegidas continuam bloqueadas"
            : "VPN desligada";
  change();
  xSemaphoreGive(mutex);
  ++epoch;
}
namespace {
bool bind_address(int fd,const std::string &name,bool required,const NetworkIp &dest,const std::string &profile,NetworkBinding *binding=nullptr) {
  LOCK_TCPIP_CORE();
  auto choice=choose(dest,profile,required);
  netif *n=nullptr; Vpn *vpn=nullptr;
  if(choice.result==NetworkRouteChoice::Tunnel) {vpn=&vpns[choice.index];n=&vpn->tunnel;}
  else if(choice.result==NetworkRouteChoice::Direct) n=physical(name);
  bool ok=n && netif_is_up(n) && netif_is_link_up(n);
  if(vpn && name!="auto" && !name.empty() && physical(name)!=vpn->physical) ok=false;
  ifreq req{};
  if(ok) {
    netif_index_to_name(netif_get_index(n),req.ifr_name);
    if(binding) {binding->destination=dest;binding->transport=(vpn?vpn->physical:n)==native(eth_if)?"ethernet":"wifi";binding->profile=vpn?vpn->tag:"";binding->generation=vpn?vpn->generation:0;binding->interface_index=netif_get_index(n);binding->local4=netif_ip4_addr(vpn?vpn->physical:n)->addr;}
  }
  UNLOCK_TCPIP_CORE();
  return ok && setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&req,sizeof(req))==0;
}
}
bool network_bind(int fd,const std::string &name,bool required,uint32_t destination,const std::string &profile) {
  NetworkIp ip=from4(destination);
  if(!destination && required) {
    LOCK_TCPIP_CORE();
    for(const auto &v:vpns) if(v.armed && (profile.empty() || v.tag==profile) && !v.routes.empty()) {ip=v.routes[0].network;break;}
    UNLOCK_TCPIP_CORE();
  }
  return bind_address(fd,name,required,ip,profile);
}
bool network_private_dns() {
  LOCK_TCPIP_CORE(); bool found=false;
  for(const auto &v:vpns) if(v.armed && v.private_dns) found=true;
  UNLOCK_TCPIP_CORE();return found;
}
bool network_connection_valid(const NetworkBinding &b) {
  LOCK_TCPIP_CORE();
  auto choice=choose(b.destination,b.profile,!b.profile.empty());
  netif *n=nullptr;
  if(choice.result==NetworkRouteChoice::Tunnel && vpns[choice.index].generation==b.generation) n=&vpns[choice.index].tunnel;
  else if(choice.result==NetworkRouteChoice::Direct && b.profile.empty()) n=physical(b.transport);
  netif *under=choice.result==NetworkRouteChoice::Tunnel?vpns[choice.index].physical:n;
  bool valid=n && under && netif_get_index(n)==b.interface_index && netif_is_up(n) && netif_is_link_up(n) && netif_is_up(under) && netif_is_link_up(under) && netif_ip4_addr(under)->addr==b.local4;
  UNLOCK_TCPIP_CORE();return valid;
}
bool network_resolve(const std::string &host,const std::string &transport,bool required,const std::string &requested,std::vector<NetworkIp> &out,std::string &error,const std::atomic<bool> *cancel) {
  out.clear(); NetworkIp literal;
  if(transport!="auto" && transport!="wifi" && transport!="ethernet") {error="Transporte fisico invalido";return false;}
  if(network_ip_parse(host,literal)) {out.push_back(literal);return true;}
  std::string profile=requested,servers; int count=0;
  LOCK_TCPIP_CORE();
  if(profile.empty()) for(const auto &v:vpns) if(v.armed && v.private_dns) {profile=v.tag;++count;}
  if(count>1) error="Selecione o perfil VPN para resolver este host";
  if(error.empty() && !profile.empty()) {
    bool found=false;
    for(const auto &v:vpns) if(v.armed && v.tag==profile) {found=true;if(vpn_up(v)) servers=v.dns_servers;}
    if(!found || servers.empty()) error="DNS privado indisponivel; sem fallback";
  }
  auto *under=physical(transport); unsigned index=under?netif_get_index(under):0;
  UNLOCK_TCPIP_CORE();
  if(!error.empty()) return false;
  if(profile.empty()) {
    if(required) {error="Selecione o perfil VPN com DNS";return false;}
    servers=physical_dns(under);
    // A protected DNS destination must not escape on a physical socket merely
    // because a full-tunnel profile omitted its DNS setting. Endpoint bootstrap
    // uses its own explicit physical path, separate from session host lookups.
    std::istringstream configured_dns(servers);std::string server;
    while(std::getline(configured_dns,server,',')) {
      if(server.size()>2 && server.front()=='[' && server.back()==']') server=server.substr(1,server.size()-2);
      NetworkIp destination;
      if(!network_ip_parse(server,destination)) {error="DNS fisico invalido";return false;}
      LOCK_TCPIP_CORE();auto route=choose(destination);UNLOCK_TCPIP_CORE();
      if(route.result!=NetworkRouteChoice::Direct) {error="DNS fisico protegido por VPN; configure DNS no perfil";return false;}
    }
    if(servers.empty() || !index) {error="DNS fisico indisponivel";return false;}
  }
  std::vector<DnsAddress> addresses;
  auto binder=[&](int fd) {
    if(!profile.empty()) { // DNS servers were validated against this profile's AllowedIPs.
      LOCK_TCPIP_CORE();NetworkIp dest;
      for(const auto &v:vpns) if(v.armed && v.tag==profile && !v.routes.empty()) {dest=v.routes[0].network;break;}
      UNLOCK_TCPIP_CORE();return bind_address(fd,transport,true,dest,profile);
    }
    return bind_physical_dns(fd,transport,index);
  };
  if(!dns_resolve(host,servers,binder,addresses,error,cancel)) return false;
  for(const auto &a:addresses) {NetworkIp ip;ip.family=a.family;ip.bytes=a.bytes;out.push_back(ip);}
  return !out.empty();
}
int network_connect(const std::string &host,unsigned port,const std::string &transport,bool required,const std::string &profile,NetworkBinding &binding,std::string &error,const std::atomic<bool> *cancel) {
  std::vector<NetworkIp> addresses;
  if(!port || port>65535 || !network_resolve(host,transport,required,profile,addresses,error,cancel)) return -1;
  for(const auto &ip:addresses) {
    if(cancel && *cancel) break;
    int fd=socket(ip.family==4?AF_INET:AF_INET6,SOCK_STREAM,IPPROTO_TCP);if(fd<0) continue;
    if(!bind_address(fd,transport,required,ip,profile,&binding)) {close(fd);error="Rota indisponivel ou ambigua; selecione o perfil VPN";continue;}
    sockaddr_storage storage{};socklen_t length;
    if(ip.family==4) {auto *a=(sockaddr_in*)&storage;a->sin_family=AF_INET;a->sin_port=htons(port);memcpy(&a->sin_addr,ip.bytes.data(),4);length=sizeof(*a);}
    else {auto *a=(sockaddr_in6*)&storage;a->sin6_family=AF_INET6;a->sin6_port=htons(port);memcpy(&a->sin6_addr,ip.bytes.data(),16);length=sizeof(*a);}
    fcntl(fd,F_SETFL,O_NONBLOCK);int rc=connect(fd,(sockaddr*)&storage,length);
    for(unsigned elapsed=0;rc<0 && errno==EINPROGRESS && elapsed<100 && !(cancel && *cancel);++elapsed) {
      fd_set w;FD_ZERO(&w);FD_SET(fd,&w);timeval timeout{0,100000};int ready=select(fd+1,nullptr,&w,nullptr,&timeout);
      if(ready>0) {int e=0;socklen_t n=sizeof(e);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&n)==0 && !e && network_connection_valid(binding)) return fd;break;}
      if(ready<0) break;
      errno=EINPROGRESS;
    }
    if(rc==0 && network_connection_valid(binding)) return fd;
    close(fd);
  }
  if(error.empty()) error="Conexao TCP indisponivel";
  return -1;
}
bool network_probe(const std::string &host, int port, bool required) {
  ip4_addr_t ip;
  if (!ip4addr_aton(host.c_str(), &ip) || port < 1 || port > 65535)
    return false;
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    return false;
  bool ok = network_bind(fd, "auto", required, ip.addr);
  timeval time{5, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof(time));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = ip.addr;
  const char payload[] = "TabRemoto encrypted tunnel probe";
  char received[sizeof(payload)]{};
  ok = ok && connect(fd, (sockaddr *)&address, sizeof(address)) == 0 &&
       send(fd, payload, sizeof(payload), 0) == sizeof(payload);
  size_t n = 0;
  while (ok && n < sizeof(payload)) {
    int got = recv(fd, received + n, sizeof(payload) - n, 0);
    if (got <= 0) {
      ok = false;
      break;
    }
    n += got;
  }
  close(fd);
  return ok && memcmp(payload, received, sizeof(payload)) == 0;
}
uint32_t network_epoch() { return epoch; }
bool network_policy_selftest() {
  NetworkPrefix p;NetworkIp ip;
  return network_prefix_parse("fd79:90::/64",p) && network_ip_parse("fd79:90::11",ip) && network_prefix_contains(p,ip) && !network_prefix_parse("::/129",p);
}
netif *route(const ip4_addr_t *src,const ip4_addr_t *dest) {
  if(src && !ip4_addr_isany(src)) for(auto &v:vpns) if(v.added && ip4_addr_cmp(src,netif_ip4_addr(&v.tunnel))) return vpn_up(v)?&v.tunnel:&v.sink;
  auto c=choose(from4(dest->addr));
  if(c.result==NetworkRouteChoice::Tunnel) return &vpns[c.index].tunnel;
  if(c.result!=NetworkRouteChoice::Direct) return &vpns[c.index>=0?c.index:0].sink;
  if(src && !ip4_addr_isany(src)) for(auto *n=netif_list;n;n=n->next) if(netif_is_up(n) && netif_is_link_up(n) && ip4_addr_cmp(src,netif_ip4_addr(n))) return n;
  return nullptr;
}
netif *route6(const ip6_addr_t *src,const ip6_addr_t *dest) {
  NetworkIp ip;ip.family=6;memcpy(ip.bytes.data(),dest->addr,16);
  if(src) for(auto &v:vpns) if(v.added && v.local6.family && !memcmp(src->addr,v.local6.bytes.data(),16)) return vpn_up(v)?&v.tunnel:&v.sink;
  auto c=choose(ip);
  if(c.result==NetworkRouteChoice::Tunnel) return &vpns[c.index].tunnel;
  if(c.result!=NetworkRouteChoice::Direct) return &vpns[c.index>=0?c.index:0].sink;
  return nullptr;
}
} // namespace tabremoto
extern "C" netif *tabremoto_ip4_route(const ip4_addr_t *src,const ip4_addr_t *dest) {return tabremoto::route(src,dest);}
extern "C" netif *tabremoto_ip6_route(const ip6_addr_t *src,const ip6_addr_t *dest) {return tabremoto::route6(src,dest);}
