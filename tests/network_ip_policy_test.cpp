#include "network_ip_policy.hpp"
#include <cassert>
using namespace tabremoto;
static NetworkPrefix prefix(const char *s){NetworkPrefix p;assert(network_prefix_parse(s,p));return p;}
static NetworkIp ip(const char *s){NetworkIp p;assert(network_ip_parse(s,p));return p;}
int main(){
 NetworkPrefix parsed;NetworkIp host;
 assert(network_prefix_parse("10.90.0.2/24",parsed,&host));assert(network_ip_text(host)=="10.90.0.2"&&network_ip_text(parsed.network)=="10.90.0.0");
 assert(network_prefix_contains(parsed,ip("10.90.0.255")));assert(!network_prefix_contains(parsed,ip("10.90.1.2")));
 assert(network_prefix_parse("fd79:90::2/64",parsed,&host));assert(network_ip_text(host)=="fd79:90::2"&&network_ip_text(parsed.network)=="fd79:90::");assert(network_prefix_contains(parsed,ip("fd79:90::1234")));assert(!network_prefix_contains(parsed,ip("fd79:91::2")));
 for(auto bad:{"10.0.0.1/33","::/129","::/-1","::/","::/1/2","::/0012","10.0.0.1/no"})assert(!network_prefix_parse(bad,parsed));
 std::vector<NetworkRouteView> routes={{"full",{prefix("0.0.0.0/0"),prefix("::/0")},true,true},{"a",{prefix("10.90.0.0/24"),prefix("fd79:90::/64")},true,true},{"b",{prefix("10.90.0.0/24"),prefix("fd79:90::/64")},true,true}};
 for(auto dest:{ip("10.90.0.1"),ip("fd79:90::1")}){
  assert(network_route_choose(routes,dest).result==NetworkRouteChoice::Ambiguous);
  auto choice=network_route_choose(routes,dest,"a",true);assert(choice.result==NetworkRouteChoice::Tunnel&&choice.index==1);
  choice=network_route_choose(routes,dest,"b",true);assert(choice.result==NetworkRouteChoice::Tunnel&&choice.index==2);
 }
 routes[2].armed=false;assert(network_route_choose(routes,ip("10.90.0.1")).index==1);
 routes[1].up=false;assert(network_route_choose(routes,ip("10.90.0.1")).result==NetworkRouteChoice::Blocked); // No full-tunnel fallback.
 assert(network_route_choose(routes,ip("192.0.2.1"),"a",true).result==NetworkRouteChoice::Blocked);
 assert(network_route_choose(routes,ip("192.0.2.1"),"missing",true).result==NetworkRouteChoice::Blocked);
 assert(network_route_choose({},ip("192.0.2.1")).result==NetworkRouteChoice::Direct);
 assert(network_route_choose({},ip("192.0.2.1"),"",true).result==NetworkRouteChoice::Blocked);
 for(auto p:{prefix("0.0.0.0/0"),prefix("255.255.255.255/32"),prefix("::/0"),prefix("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128")})assert(network_prefix_contains(p,p.network));
}
