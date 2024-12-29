#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "tusb.h"

#define SERVER_IP "192.168.4.1"
#define SERVER_PORT 80
#define TX_BUFFER_SIZE 32

#ifdef DEBUG
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

typedef struct UDP_CLIENT_T_ {
    struct udp_pcb *pcb;
    ip_addr_t remote_addr;
    uint8_t tx_buffer[TX_BUFFER_SIZE];
    uint16_t tx_len;
} UDP_CLIENT_T;

static UDP_CLIENT_T* g_state = NULL;

static void udp_client_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port) {
    if (!p) return;

    uint8_t *data = (uint8_t*)p->payload;
    for (uint16_t i = 0; i < p->len; i++) {
        putchar(data[i]);
    }

    DEBUG_PRINTF("Received %d bytes: %.*s\n", p->len, p->len, (char*)p->payload);
    pbuf_free(p);
}

static void try_send_data(UDP_CLIENT_T *state) {
    if (state->tx_len > 0 && state->pcb) {
        cyw43_arch_lwip_begin();
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, state->tx_len, PBUF_RAM);
        if (p) {
            memcpy(p->payload, state->tx_buffer, state->tx_len);
            err_t err = udp_sendto(state->pcb, p, &state->remote_addr, SERVER_PORT);
            if (err == ERR_OK) {
                DEBUG_PRINTF("Data sent: %.*s\n", state->tx_len, state->tx_buffer);
            } else {
                DEBUG_PRINTF("UDP send error: %d\n", err);
            }
            pbuf_free(p);
            state->tx_len = 0;
        }
        cyw43_arch_lwip_end();
    }
}

static void check_usb_rx() {
    if (!g_state || !g_state->pcb) return;

    while (g_state->tx_len < TX_BUFFER_SIZE) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) break;

        g_state->tx_buffer[g_state->tx_len++] = (uint8_t)c;
    }

    if (g_state->tx_len > 0) {
        DEBUG_PRINTF("USB RX: Buffer length = %d\n", g_state->tx_len);
        try_send_data(g_state);
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

    if (cyw43_arch_init()) {
        DEBUG_PRINTF("Failed to initialize CYW43\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

    DEBUG_PRINTF("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms("PicoAP", "12345678", CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        DEBUG_PRINTF("Failed to connect to WiFi!\n");
        return 1;
    }
    DEBUG_PRINTF("Connected to WiFi.\n");

    UDP_CLIENT_T *state = calloc(1, sizeof(UDP_CLIENT_T));
    if (!state) {
        DEBUG_PRINTF("Failed to allocate memory\n");
        return 1;
    }

    state->tx_len = 0;
    g_state = state;

    ipaddr_aton(SERVER_IP, &state->remote_addr);

    state->pcb = udp_new();
    if (!state->pcb) {
        DEBUG_PRINTF("Failed to create new PCB\n");
        return 1;
    }

    udp_recv(state->pcb, udp_client_recv, state);

    // Bind to any port
    err_t err = udp_bind(state->pcb, IP_ADDR_ANY, 0);
    if (err != ERR_OK) {
        DEBUG_PRINTF("Failed to bind UDP PCB\n");
        return 1;
    }

    DEBUG_PRINTF("UDP client ready\n");

    while (1) {
        check_usb_rx();
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        tight_loop_contents();
    }

    return 0;
}
