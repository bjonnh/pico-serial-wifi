#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "tusb.h"

#define TCP_PORT 80
#define TX_BUFFER_SIZE 512

#ifdef DEBUG_PRINTF
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
    struct tcp_pcb *client_pcb;
    uint8_t tx_buffer[TX_BUFFER_SIZE];
    uint16_t tx_len;
} TCP_SERVER_T;

static TCP_SERVER_T* g_state = NULL;

static err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

    if (!p) {
        state->client_pcb = NULL;
        return ERR_OK;
    }

    uint8_t *data = (uint8_t*)p->payload;
    for (uint16_t i = 0; i < p->len; i++) {
        putchar(data[i]);
    }

    DEBUG_PRINT("TCP Received Data (length: %d): %s\n", p->len, (char*)p->payload);

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    state->tx_len = 0;
    DEBUG_PRINT("Data sent successfully\n");
    return ERR_OK;
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

    if (err != ERR_OK || client_pcb == NULL) {
        return ERR_VAL;
    }

    state->client_pcb = client_pcb;
    state->tx_len = 0;

    DEBUG_PRINT("Client connected\n");

    tcp_arg(client_pcb, state);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_sent(client_pcb, tcp_server_sent);

    tcp_nagle_disable(client_pcb);

    return ERR_OK;
}

static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, TCP_PORT);
    if (err) return false;

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        if (pcb) tcp_close(pcb);
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    DEBUG_PRINT("TCP Server opened on port %d\n", TCP_PORT);

    return true;
}

static void try_send_data(TCP_SERVER_T *state) {
    if (state->tx_len > 0 && state->client_pcb) {
        cyw43_arch_lwip_begin();
        err_t err = tcp_write(state->client_pcb, state->tx_buffer, state->tx_len, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            err = tcp_output(state->client_pcb);
            if (err == ERR_OK) {
                DEBUG_PRINT("Data sent: %.*s\n", state->tx_len, state->tx_buffer);
                state->tx_len = 0;
            } else {
                DEBUG_PRINT("Failed to flush the TCP output\n");
            }
        } else {
            DEBUG_PRINT("TCP write error: %d\n", err);
        }
        cyw43_arch_lwip_end();
    }
}

static void check_usb_rx() {
    if (!g_state || !g_state->client_pcb) return;

    while (g_state->tx_len < TX_BUFFER_SIZE) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) break;

        g_state->tx_buffer[g_state->tx_len++] = (uint8_t)c;
    }

    if (g_state->tx_len > 0) {
        DEBUG_PRINT("USB RX: Buffer length = %d\n", g_state->tx_len);
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

    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        DEBUG_PRINT("Failed to initialize WiFi\n");
        return 1;
    }

    TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state) return 1;

    g_state = state;

    cyw43_arch_enable_ap_mode("PicoAP", "12345678", CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);


    // Increase WiFi power
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

    ip4_addr_t mask;
    IP4_ADDR(ip_2_ip4(&state->gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);

    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    dns_server_t dns_server;
    dns_server_init(&dns_server, &state->gw);

    if (!tcp_server_open(state)) return 1;

    DEBUG_PRINT("Server started\n");

    while (!state->complete) {
        check_usb_rx();
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, state->client_pcb != NULL);
        tight_loop_contents();
    }

    return 0;
}