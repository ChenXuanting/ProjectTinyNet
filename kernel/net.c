#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

struct netif netif;

err_t
linkoutput(struct netif *netif, struct pbuf *p)
{
  struct pbuf *q;

  for (q = p; q; q = q->next) {
    if(virtio_net_send(q->payload, q->len))
      return ERR_IF;
  }

  return ERR_OK;
}

int
linkinput(struct netif *netif)
{
  int len;
  struct pbuf *p;

  p = pbuf_alloc(PBUF_RAW, 1514, PBUF_RAM);
  if(!p)
    return 0;

  len = virtio_net_recv(p->payload, p->len);

  if(len > 0){
    /* shrink pbuf to actual size */
    pbuf_realloc(p, len);

    if(netif->input(p, netif) == ERR_OK)
      return len;

    printf("linkinput: drop packet (%d bytes)\n", len);
  }

  pbuf_free(p);
  return len;
}

err_t
linkinit(struct netif *netif)
{
  virtio_net_init(&netif->hwaddr);

  netif->hwaddr_len = ETH_HWADDR_LEN;
  netif->linkoutput = linkoutput;
  netif->output = etharp_output;
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;

  return ERR_OK;
}

void
netadd(void)
{
  int i;
  char addr[IPADDR_STRLEN_MAX], netmask[IPADDR_STRLEN_MAX], gw[IPADDR_STRLEN_MAX];

  if(!netif_add_noaddr(&netif, NULL, linkinit, netif_input))
    panic("netadd");

  netif.name[0] = 'e';
  netif.name[1] = 'n';
  netif_set_link_up(&netif);
  netif_set_up(&netif);

  printf("net: mac ");
  for(i = 0; i < ETH_HWADDR_LEN; ++i){
    if(i)
      printf(":");
    if(netif.hwaddr[i] < 0x10)
      printf("0");
    printf("%x", netif.hwaddr[i]);
  }
  printf("\n");

  dhcp_start(&netif);
  /* wait until DHCP succeeds */
  while(!dhcp_supplied_address(&netif)){
    linkinput(&netif);
    sys_check_timeouts();
  }

  ipaddr_ntoa_r(netif_ip_addr4(&netif), addr, sizeof(addr));
  ipaddr_ntoa_r(netif_ip_netmask4(&netif), netmask, sizeof(netmask));
  ipaddr_ntoa_r(netif_ip_gw4(&netif), gw, sizeof(gw));
  printf("net: addr %s netmask %s gw %s\n", addr, netmask, gw);
}

int
nettimer(void)
{
  sys_check_timeouts();
  return linkinput(&netif);
}

void
netinit(void)
{
  lwip_init();
  netadd();
  netif_set_default(&netif);
}

uint32
sys_now(void)
{
  /* good enough for qemu */
  return r_mtime() / 10000;
}

unsigned long
r_mtime(void)
{
  return *(uint64*)CLINT_MTIME;
}
