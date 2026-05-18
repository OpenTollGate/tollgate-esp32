#include "tollgate_core_dns.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <sys/param.h>

#define MAX_AUTH_IPS 10
#define DNS_BUF_SIZE 512
#define DNS_PORT 53
#define DOT_PORT 853
#define DNS_TASK_STACK 4096
#define DOT_TASK_STACK 3072
#define DNS_TASK_PRIO 5
#define DOT_TASK_PRIO 5
#define DNS_FORWARD_TIMEOUT_MS 2000
#define NXDOMAIN_TTL 30
#define HIJACK_TTL 10

static const char *TAG = "tg_core_dns";

#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint16_t name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t len;
    uint32_t addr;
} dns_answer_t;
#pragma pack(pop)

typedef struct {
    uint32_t ip;
} auth_entry_t;

static auth_entry_t s_auth_list[MAX_AUTH_IPS];
static int s_auth_count = 0;
static TaskHandle_t s_dns_task = NULL;
static TaskHandle_t s_dot_task = NULL;
static volatile bool s_dns_running = false;
static esp_ip4_addr_t s_ap_ip;
static esp_ip4_addr_t s_upstream_dns;

static bool is_authenticated(uint32_t ip)
{
    for (int i = 0; i < s_auth_count; i++) {
        if (s_auth_list[i].ip == ip) return true;
    }
    return false;
}

static void parse_dns_name(const uint8_t *buf, int buf_len, int offset, char *out, int out_len)
{
    int pos = offset;
    int out_pos = 0;
    int jumped = 0;
    int jump_pos = 0;
    while (pos < buf_len && out_pos < out_len - 1) {
        uint8_t len = buf[pos];
        if (len == 0) break;
        if ((len & 0xC0) == 0xC0) {
            if (!jumped) jump_pos = pos + 2;
            pos = ((len & 0x3F) << 8) | buf[pos + 1];
            jumped = 1;
            continue;
        }
        if (out_pos > 0 && out_pos < out_len - 1) out[out_pos++] = '.';
        pos++;
        for (int i = 0; i < len && pos < buf_len && out_pos < out_len - 1; i++) {
            out[out_pos++] = buf[pos++];
        }
    }
    out[out_pos] = '\0';
}

static int build_nxdomain(uint8_t *response, int req_len)
{
    dns_header_t *hdr = (dns_header_t *)response;
    hdr->flags = htons(0x8403);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    return req_len;
}

static int build_redirect_response(uint8_t *response, int req_len)
{
    memmove(response, response, req_len);
    dns_header_t *hdr = (dns_header_t *)response;
    hdr->flags = htons(0x8180);
    hdr->ancount = htons(1);
    hdr->nscount = 0;
    hdr->arcount = 0;
    int resp_len = req_len;
    dns_answer_t ans;
    ans.name = htons(0xC00C);
    ans.type = htons(1);
    ans.class = htons(1);
    ans.ttl = htonl(HIJACK_TTL);
    ans.len = htons(4);
    ans.addr = s_ap_ip.addr;
    memcpy(response + resp_len, &ans, sizeof(ans));
    resp_len += sizeof(ans);
    return resp_len;
}

static int forward_dns(const uint8_t *req, int req_len, uint8_t *resp, int resp_buf_len,
                       uint16_t txn_id)
{
    int upstream_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (upstream_sock < 0) return -1;

    struct timeval tv = { .tv_sec = DNS_FORWARD_TIMEOUT_MS / 1000, .tv_usec = (DNS_FORWARD_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(upstream_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in upstream_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = s_upstream_dns.addr,
    };

    sendto(upstream_sock, req, req_len, 0, (struct sockaddr *)&upstream_addr, sizeof(upstream_addr));

    int n = recvfrom(upstream_sock, resp, resp_buf_len, 0, NULL, NULL);
    close(upstream_sock);

    if (n > 0) {
        if (n >= sizeof(dns_header_t)) {
            dns_header_t *hdr = (dns_header_t *)resp;
            hdr->id = htons(txn_id);
        }
    }
    return n;
}

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        s_dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(sock);
        s_dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port %d, AP IP=" IPSTR ", upstream DNS=" IPSTR,
             DNS_PORT, IP2STR(&s_ap_ip), IP2STR(&s_upstream_dns));

    uint8_t rx_buf[DNS_BUF_SIZE];
    uint8_t tx_buf[DNS_BUF_SIZE + sizeof(dns_answer_t)];

    while (s_dns_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int n = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                         (struct sockaddr *)&client_addr, &client_len);
        if (n < (int)sizeof(dns_header_t)) continue;

        uint32_t client_ip = client_addr.sin_addr.s_addr;
        dns_header_t *hdr = (dns_header_t *)rx_buf;
        uint16_t txn_id = ntohs(hdr->id);
        bool is_query = (ntohs(hdr->flags) & 0x8000) == 0;
        uint16_t qdcount = ntohs(hdr->qdcount);

        if (!is_query || qdcount == 0) continue;

        int q_offset = sizeof(dns_header_t);
        while (q_offset < n && rx_buf[q_offset] != 0) {
            q_offset += rx_buf[q_offset] + 1;
        }
        if (q_offset + 5 > n) continue;
        uint16_t qtype = (rx_buf[q_offset + 1] << 8) | rx_buf[q_offset + 2];
        int req_len = q_offset + 5;

        if (is_authenticated(client_ip)) {
            int resp_len = forward_dns(rx_buf, req_len, tx_buf, sizeof(tx_buf), txn_id);
            if (resp_len > 0) {
                sendto(sock, tx_buf, resp_len, 0, (struct sockaddr *)&client_addr, client_len);
            }
        } else {
            char qname[256] = {0};
            parse_dns_name(rx_buf, n, sizeof(dns_header_t), qname, sizeof(qname));
            ESP_LOGI(TAG, "Hijack DNS from " IPSTR ": %s (type=%d)",
                     IP2STR(&(esp_ip4_addr_t){.addr=client_ip}), qname, qtype);
            if (qtype == 1) {
                int resp_len = build_redirect_response(rx_buf, req_len);
                memcpy(tx_buf, rx_buf, resp_len);
                dns_header_t *resp_hdr = (dns_header_t *)tx_buf;
                resp_hdr->id = htons(txn_id);
                sendto(sock, tx_buf, resp_len, 0, (struct sockaddr *)&client_addr, client_len);
            } else {
                int resp_len = build_nxdomain(rx_buf, req_len);
                memcpy(tx_buf, rx_buf, resp_len);
                dns_header_t *resp_hdr = (dns_header_t *)tx_buf;
                resp_hdr->id = htons(txn_id);
                sendto(sock, tx_buf, resp_len, 0, (struct sockaddr *)&client_addr, client_len);
            }
        }
    }

    close(sock);
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

static void dot_reject_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DoT reject socket");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DOT_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DoT reject socket on port %d", DOT_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    listen(sock, 1);
    ESP_LOGI(TAG, "DoT reject server on port %d (forces DNS fallback to port 53)", DOT_PORT);

    while (s_dns_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock >= 0) {
            struct linger ling = { .l_onoff = 1, .l_linger = 0 };
            setsockopt(client_sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
            close(client_sock);
        }
    }

    close(sock);
    ESP_LOGI(TAG, "DoT reject server stopped");
    vTaskDelete(NULL);
}

esp_err_t tollgate_core_dns_start_internal(esp_ip4_addr_t ap_ip, esp_ip4_addr_t upstream_dns)
{
    if (s_dns_running) return ESP_OK;
    s_ap_ip = ap_ip;
    s_upstream_dns = upstream_dns;
    s_dns_running = true;
    xTaskCreate(dns_server_task, "dns_server", DNS_TASK_STACK, NULL, DNS_TASK_PRIO, &s_dns_task);
    xTaskCreate(dot_reject_task, "dot_reject", DOT_TASK_STACK, NULL, DOT_TASK_PRIO, &s_dot_task);
    return ESP_OK;
}

void tollgate_core_dns_stop(void)
{
    s_dns_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    s_dns_task = NULL;
}

void tollgate_core_dns_set_authenticated(uint32_t client_ip, bool authenticated)
{
    if (authenticated) {
        if (is_authenticated(client_ip)) return;
        if (s_auth_count < MAX_AUTH_IPS) {
            s_auth_list[s_auth_count].ip = client_ip;
            s_auth_count++;
        }
    } else {
        for (int i = 0; i < s_auth_count; i++) {
            if (s_auth_list[i].ip == client_ip) {
                s_auth_list[i] = s_auth_list[s_auth_count - 1];
                s_auth_count--;
                return;
            }
        }
    }
}

bool tollgate_core_dns_is_running(void)
{
    return s_dns_running;
}
