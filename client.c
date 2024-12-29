#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "tusb.h"  // Add this for USB

#define SERVER_IP "192.168.4.1"
#define SERVER_PORT 80
#define TX_BUFFER_SIZE 512

// Debug macro
#ifdef DEBUG
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

typedef struct TCP_CLIENT_T_ {
    struct tcp_pcb *pcb;
    bool connected;
    uint8_t tx_buffer[TX_BUFFER_SIZE];
    uint16_t tx_len;
} TCP_CLIENT_T;

static TCP_CLIENT_T* g_state = NULL;

static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        DEBUG_PRINTF("Connection closed by server\n");
        ((TCP_CLIENT_T*)arg)->connected = false;
        return ERR_OK;
    }

    uint8_t *data = (uint8_t*)p->payload;
    for (uint16_t i = 0; i < p->len; i++) {
        putchar(data[i]);  // Use putchar for USB serial
    }

    DEBUG_PRINTF("Received %d bytes from server: %.*s\n", p->len, p->len, (char*)p->payload);

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void tcp_client_err(void *arg, err_t err) {
    DEBUG_PRINTF("TCP error: %d\n", err);
    ((TCP_CLIENT_T*)arg)->connected = false;
}

static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    state->tx_len = 0;
    return ERR_OK;
}

static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err != ERR_OK) return err;

    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    state->connected = true;
    state->tx_len = 0;

    tcp_recv(tpcb, tcp_client_recv);
    tcp_sent(tpcb, tcp_client_sent);
    tcp_nagle_disable(tpcb);

    return ERR_OK;
}


static void try_send_data(TCP_CLIENT_T *state) {
    if (state->tx_len > 0 && state->connected && state->pcb) {
        cyw43_arch_lwip_begin();
        err_t err = tcp_write(state->pcb, state->tx_buffer, state->tx_len, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            err = tcp_output(state->pcb);
            if (err == ERR_OK) {
                DEBUG_PRINTF("Data sent: %.*s\n", state->tx_len, state->tx_buffer);
                state->tx_len = 0;
            } else {
                DEBUG_PRINTF("Failed to flush the TCP output\n");
            }
        } else {
            DEBUG_PRINTF("TCP write error: %d\n", err);
        }
        cyw43_arch_lwip_end();
    }
}

static void check_usb_rx() {
    if (!g_state || !g_state->connected || !g_state->pcb) return;

    while (g_state->tx_len < TX_BUFFER_SIZE) {
        int c = getchar_timeout_us(10);
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
    stdio_set_translate_crlf(&stdio_usb, false);  // Disable newline translation

    sleep_ms(1000);  // Give USB time to initialize

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

    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));
    if (!state) {
        DEBUG_PRINTF("Failed to allocate memory for TCP_CLIENT_T\n");
        return 1;
    }

    state->connected = false;
    state->tx_len = 0;  // Initialize tx_len here
    state->pcb = NULL;

    g_state = state;

    ip_addr_t remote_addr;
    ipaddr_aton(SERVER_IP, &remote_addr);

    state->pcb = tcp_new_ip_type(IP_GET_TYPE(&remote_addr));
    if (!state->pcb) {
        DEBUG_PRINTF("Failed to create new PCB\n");
        return 1;
    }

    tcp_arg(state->pcb, state);
    tcp_err(state->pcb, tcp_client_err);

    DEBUG_PRINTF("Connecting to server...\n");
    if (tcp_connect(state->pcb, &remote_addr, SERVER_PORT, tcp_client_connected) != ERR_OK) {
        DEBUG_PRINTF("Failed to connect to server!\n");
        return 1;
    }

    while (1) {
        check_usb_rx();
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, state->connected);
        // No sleep, just yield to allow WiFi processing
        tight_loop_contents();
    }

    return 0;
}