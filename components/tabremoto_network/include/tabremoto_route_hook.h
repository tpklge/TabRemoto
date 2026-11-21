#pragma once
#include "lwip/ip4_addr.h"
#include "lwip/ip6_addr.h"
struct netif;
#ifdef __cplusplus
extern "C" {
#endif
struct netif* tabremoto_ip6_route(const ip6_addr_t*,const ip6_addr_t*);
struct netif* tabremoto_ip4_route(const ip4_addr_t*,const ip4_addr_t*);
#ifdef __cplusplus
}
#endif
#undef LWIP_HOOK_IP4_ROUTE_SRC
#define LWIP_HOOK_IP4_ROUTE_SRC tabremoto_ip4_route

#undef LWIP_HOOK_IP6_ROUTE
#define LWIP_HOOK_IP6_ROUTE tabremoto_ip6_route
