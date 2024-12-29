#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "tusb.h"

#define UDP_PORT 80
#define TX_BUFFER_SIZE 8192

#ifdef DEBUG_PRINTF
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

typedef struct UDP_SERVER_T_ {
    struct udp_pcb *udp_pcb;
    bool complete;
    ip_addr_t gw;
    uint8_t tx_buffer[TX_BUFFER_SIZE];
    uint16_t tx_len;
    // Store last client info
    ip_addr_t client_addr;
    u16_t client_port;
    bool client_active;
} UDP_SERVER_T;

static UDP_SERVER_T* g_state = NULL;

static void udp_server_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port) {
    UDP_SERVER_T *state = (UDP_SERVER_T*)arg;
    if (!p) return;

    // Store client info for replies
    ip_addr_copy(state->client_addr, *addr);
    state->client_port = port;
    state->client_active = true;

    uint8_t *data = (uint8_t*)p->payload;
    for (uint16_t i = 0; i < p->len; i++) {
        putchar(data[i]);
    }

    stdio_flush();
    DEBUG_PRINT("UDP Received Data (length: %d): %s\n", p->len, (char*)p->payload);
    pbuf_free(p);
}

static bool udp_server_open(void *arg) {
    UDP_SERVER_T *state = (UDP_SERVER_T*)arg;

    state->udp_pcb = udp_new();
    if (!state->udp_pcb) return false;

    err_t err = udp_bind(state->udp_pcb, IP_ANY_TYPE, UDP_PORT);
    if (err) {
        udp_remove(state->udp_pcb);
        return false;
    }

    udp_recv(state->udp_pcb, udp_server_recv, state);
    DEBUG_PRINT("UDP Server opened on port %d\n", UDP_PORT);

    return true;
}

static void try_send_data(UDP_SERVER_T *state) {
    if (state->tx_len > 0 && state->client_active) {
        cyw43_arch_lwip_begin();
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, state->tx_len, PBUF_RAM);
        if (p) {
            memcpy(p->payload, state->tx_buffer, state->tx_len);
            // Send to last known client
            err_t err = udp_sendto(state->udp_pcb, p, &state->client_addr, state->client_port);
            if (err == ERR_OK) {
                DEBUG_PRINT("Data sent to client: %.*s\n", state->tx_len, state->tx_buffer);
            } else {
                DEBUG_PRINT("UDP send error: %d\n", err);
            }
            pbuf_free(p);
            state->tx_len = 0;
        }
        cyw43_arch_lwip_end();
    }
}

static void check_usb_rx() {
    if (!g_state) return;

    while (g_state->tx_len < TX_BUFFER_SIZE) {
        int c = getchar_timeout_us(10);
        if (c == PICO_ERROR_TIMEOUT) break;

        g_state->tx_buffer[g_state->tx_len++] = (uint8_t)c;
    }

    if (g_state->tx_len > 0) {
        DEBUG_PRINT("USB RX: Buffer length = %d\n", g_state->tx_len);
        try_send_data(g_state);
        check_usb_rx();
    }
}

int main() {
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);

    sleep_ms(1000);
    tusb_init();
    while (!tud_cdc_connected()) {
        tight_loop_contents();
    }

    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        DEBUG_PRINT("Failed to initialize WiFi\n");
        return 1;
    }

    UDP_SERVER_T *state = calloc(1, sizeof(UDP_SERVER_T));
    if (!state) return 1;

    g_state = state;
    state->client_active = false;

    cyw43_arch_enable_ap_mode("PicoAP", "12345678", CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

    ip4_addr_t mask;
    IP4_ADDR(ip_2_ip4(&state->gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);

    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    dns_server_t dns_server;
    dns_server_init(&dns_server, &state->gw);

    if (!udp_server_open(state)) return 1;

    DEBUG_PRINT("Server started\n");

    while (!state->complete) {
        check_usb_rx();
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, state->client_active);
        tight_loop_contents();
    }

    return 0;
}
