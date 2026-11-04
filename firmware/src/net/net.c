// USB network adapter (RNDIS for Windows, CDC-ECM for macOS / Linux) with lwIP and a DHCP server.
// The box is 192.168.7.1; the PC gets 192.168.7.2 with no gateway and no DNS server, so its normal
// networking is not touched.
#include "net.h"
#include <string.h>
#include "dhserver.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "tusb.h"

#define IP4(a, b, c, d) {PP_HTONL(LWIP_MAKEU32(a, b, c, d))}

uint8_t tud_network_mac_address[6] = {0x02, 0x4B, 0x4D, 0x00, 0x00, 0x00};

static struct netif nif;
static struct pbuf *rx_frame;
static bool up;
static const ip4_addr_t addr = IP4(192, 168, 7, 1), mask = IP4(255, 255, 255, 0), gw = IP4(0, 0, 0, 0);
static dhcp_entry_t entries[] = {
    {{0}, IP4(192, 168, 7, 2), 24 * 60 * 60},
    {{0}, IP4(192, 168, 7, 3), 24 * 60 * 60},
};
static const dhcp_config_t dhcp = {
    .router = IP4(0, 0, 0, 0), .port = 67, .dns = IP4(0, 0, 0, 0), .domain = NULL, .num_entry = 2, .entries = entries,
};

uint32_t sys_now(void) { return to_ms_since_boot(get_absolute_time()); }

static err_t linkoutput(struct netif *n, struct pbuf *p) {
    (void)n;
    for (int i = 0; i < 2000; i++) {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        tud_task();
    }
    return ERR_TIMEOUT;
}

static err_t output4(struct netif *n, struct pbuf *p, const ip4_addr_t *a) { return etharp_output(n, p, a); }

static err_t nif_init(struct netif *n) {
    n->mtu = CFG_TUD_NET_MTU - 14;
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    n->name[0] = 'k';
    n->name[1] = 'm';
    n->linkoutput = linkoutput;
    n->output = output4;
    return ERR_OK;
}

void net_init(void) {
    // a stable, locally administered MAC per box (from the flash chip's unique ID)
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    tud_network_mac_address[3] = id.id[5];
    tud_network_mac_address[4] = id.id[6];
    tud_network_mac_address[5] = (uint8_t)(id.id[7] & 0xFE);
    lwip_init();
    nif.hwaddr_len = 6;
    memcpy(nif.hwaddr, tud_network_mac_address, 6);
    nif.hwaddr[5] ^= 1;   // the box's own MAC differs from the one the PC's adapter uses
    netif_add(&nif, &addr, &mask, &gw, NULL, nif_init, ethernet_input);
    netif_set_default(&nif);
    up = dhserv_init(&dhcp) == ERR_OK;
}

void net_task(void) {
    if (rx_frame) {
        if (ethernet_input(rx_frame, &nif) != ERR_OK) pbuf_free(rx_frame);
        rx_frame = NULL;
        tud_network_recv_renew();
    }
    sys_check_timeouts();
}

bool net_up(void) { return up; }

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (rx_frame) return false;
    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (!p) return false;
        pbuf_take(p, src, size);
        rx_frame = p;
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    (void)arg;
    struct pbuf *p = (struct pbuf *)ref;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void) {
    if (rx_frame) {
        pbuf_free(rx_frame);
        rx_frame = NULL;
    }
}
