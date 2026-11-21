#pragma once
#include <string>
#include <atomic>
#include "network_ip_policy.hpp"
#include <cstdint>
#include <vector>
struct esp_netif_obj;
namespace tabremoto {
struct VpnStatus {std::string id,transport,ip;bool armed=false,up=false;uint64_t rx=0,tx=0;uint32_t handshake_age_ms=0xffffffffu;};
struct NetworkStatus {std::vector<VpnStatus> tunnels;bool initialized=false,wifi=false,ethernet=false,usb_present=false,vpn=false,vpn_armed=false;unsigned scan_count=0,scan_channel=0;bool scan_match=false,wifi_automatic=false;int rssi=0;uint16_t vid=0,pid=0;unsigned revision=0;uint32_t handshake_age_ms=0xffffffffu;uint64_t rx=0,tx=0;std::string wifi_scan_json="[]",profile_id,wifi_ip,ethernet_ip,detail="Rede parada",vpn_detail="VPN desligada";};
void network_start();NetworkStatus network_status();
bool network_wifi(const std::string& ssid,const std::string& password);
bool network_ethernet();
bool network_ethernet_ip(bool dhcp,const std::string& ip="",const std::string& mask="",const std::string& gateway="",const std::string& dns="");
bool network_wifi_cycle();
bool network_wifi_scan();
bool network_wifi_busy();
bool network_vpn(const std::string& config,const std::string& physical,const std::string& profile_id="");
void network_vpn_stop(bool clear_policy=false,const std::string& profile="");
bool network_vpn_file(const std::string& path,const std::string& transport);
bool network_vpn_active(const std::string& profile);
// Enforce transport selection and VPN requirement before sending application bytes.
bool network_bind(int socket,const std::string& physical,bool require_vpn,uint32_t destination=0,const std::string& profile="");
struct NetworkBinding {NetworkIp destination;std::string transport,profile;unsigned generation=0,interface_index=0;uint32_t local4=0;};
bool network_connection_valid(const NetworkBinding &binding);
bool network_resolve(const std::string &host,const std::string &transport,bool required,const std::string &profile,std::vector<NetworkIp> &out,std::string &error,const std::atomic<bool> *cancel=nullptr);
int network_connect(const std::string &host,unsigned port,const std::string &transport,bool required,const std::string &profile,NetworkBinding &binding,std::string &error,const std::atomic<bool> *cancel=nullptr);
uint32_t network_epoch();
bool network_policy_selftest();
bool network_private_dns();
bool network_probe(const std::string& host,int port,bool vpn);
}
