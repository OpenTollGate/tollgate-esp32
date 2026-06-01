#include "tollgate_core_stratum_proxy.h"
#include "tollgate_core_mining.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/poll.h>

#ifndef TEST_HOST
#include "lwipopts.h"
#endif

static const char *TAG = "tg_stratum";

#define RECV_BUF_SIZE 1024
#define SEND_BUF_SIZE 1024
#define SERVER_TASK_STACK 6144
#define MAX_LINE 512

static uint16_t s_port = 3333;
static volatile bool s_running = false;
static TaskHandle_t s_task_handle = NULL;
static int s_server_fd = -1;

static tollgate_stratum_job_t s_current_job = {0};
static tollgate_stratum_proxy_stats_t s_stats = {0};
static double s_difficulty = 1.0;
static tollgate_share_cb s_share_cb = NULL;

typedef struct {
    int fd;
    uint32_t ip;
    char worker[64];
    bool authorized;
    uint32_t req_id;
} miner_client_t;

static miner_client_t s_miners[TOLLGATE_PROXY_MAX_MINERS];
static SemaphoreHandle_t s_miners_mutex = NULL;

static void broadcast_to_miners(const char *msg, int len)
{
    if (!s_miners_mutex) return;
    xSemaphoreTake(s_miners_mutex, pdMS_TO_TICKS(1000));
    for (int i = 0; i < TOLLGATE_PROXY_MAX_MINERS; i++) {
        if (s_miners[i].fd >= 0 && s_miners[i].authorized) {
            send(s_miners[i].fd, msg, len, MSG_DONTWAIT);
        }
    }
    xSemaphoreGive(s_miners_mutex);
}

static int register_miner(int fd, uint32_t ip)
{
    xSemaphoreTake(s_miners_mutex, pdMS_TO_TICKS(1000));
    for (int i = 0; i < TOLLGATE_PROXY_MAX_MINERS; i++) {
        if (s_miners[i].fd < 0) {
            s_miners[i].fd = fd;
            s_miners[i].ip = ip;
            s_miners[i].worker[0] = '\0';
            s_miners[i].authorized = false;
            s_miners[i].req_id = 0;
            xSemaphoreGive(s_miners_mutex);
            return i;
        }
    }
    xSemaphoreGive(s_miners_mutex);
    return -1;
}

static void unregister_miner(int fd)
{
    xSemaphoreTake(s_miners_mutex, pdMS_TO_TICKS(1000));
    for (int i = 0; i < TOLLGATE_PROXY_MAX_MINERS; i++) {
        if (s_miners[i].fd == fd) {
            s_miners[i].fd = -1;
            s_miners[i].authorized = false;
            s_stats.active_miners--;
            break;
        }
    }
    xSemaphoreGive(s_miners_mutex);
}

static int find_miner(int fd)
{
    for (int i = 0; i < TOLLGATE_PROXY_MAX_MINERS; i++) {
        if (s_miners[i].fd == fd) return i;
    }
    return -1;
}

static void build_target_from_difficulty(double diff, uint8_t *target, int *target_len)
{
    *target_len = 32;
    memset(target, 0xFF, 32);

    if (diff <= 0.0 || diff > 1e15) return;

    double pdiff_max = 0x00000000FFFF0000ULL;
    if (diff >= pdiff_max) {
        memset(target, 0, 32);
        target[7] = 0xFF;
        return;
    }

    uint64_t target_val = (uint64_t)(pdiff_max / diff);
    if (target_val == 0) target_val = 1;

    memset(target, 0, 32);
    for (int i = 0; i < 8 && target_val > 0; i++) {
        target[7 - i] = (uint8_t)(target_val & 0xFF);
        target_val >>= 8;
    }
}

static bool check_pow(const uint8_t header[80], const uint8_t *target, int target_len)
{
    uint8_t hash[32];
    uint8_t tmp[32];
    mbedtls_sha256(header, 80, tmp, 0);
    mbedtls_sha256(tmp, 32, hash, 0);

    uint8_t hash_be[32];
    for (int i = 0; i < 32; i++) hash_be[i] = hash[31 - i];

    for (int i = 0; i < target_len && i < 32; i++) {
        if (hash_be[i] < target[i]) return true;
        if (hash_be[i] > target[i]) return false;
    }
    return true;
}

static void build_header(const tollgate_stratum_job_t *job, uint32_t nonce,
                          uint32_t ntime, uint32_t version, uint8_t out[80])
{
    memset(out, 0, 80);

    out[0] = (version >> 24) & 0xFF;
    out[1] = (version >> 16) & 0xFF;
    out[2] = (version >> 8) & 0xFF;
    out[3] = version & 0xFF;

    if (job) {
        memcpy(out + 4, job->prevhash, 32);
        memcpy(out + 36, job->merkle_root, 32);
    }

    out[68] = (ntime >> 24) & 0xFF;
    out[69] = (ntime >> 16) & 0xFF;
    out[70] = (ntime >> 8) & 0xFF;
    out[71] = ntime & 0xFF;

    uint32_t nbits = job ? job->nbits : 0;
    out[72] = (nbits >> 24) & 0xFF;
    out[73] = (nbits >> 16) & 0xFF;
    out[74] = (nbits >> 8) & 0xFF;
    out[75] = nbits & 0xFF;

    out[76] = (nonce >> 24) & 0xFF;
    out[77] = (nonce >> 16) & 0xFF;
    out[78] = (nonce >> 8) & 0xFF;
    out[79] = nonce & 0xFF;
}

static int send_response(int fd, uint32_t id, const char *result, bool error)
{
    char *buf = malloc(SEND_BUF_SIZE);
    if (!buf) return -1;
    int len;
    if (error) {
        len = snprintf(buf, SEND_BUF_SIZE,
                       "{\"id\":%lu,\"result\":null,\"error\":[%d,\"%s\"]}\n",
                       (unsigned long)id, 20, result);
    } else {
        len = snprintf(buf, SEND_BUF_SIZE,
                       "{\"id\":%lu,\"result\":%s,\"error\":null}\n",
                       (unsigned long)id, result);
    }
    int ret = send(fd, buf, len, MSG_DONTWAIT);
    free(buf);
    return ret;
}

static void handle_subscribe(int fd, miner_client_t *miner, uint32_t id)
{
    char *buf = malloc(SEND_BUF_SIZE);
    if (!buf) return;
    int len = snprintf(buf, SEND_BUF_SIZE,
                       "{\"id\":%lu,\"result\":[[[\"mining.notify\",\"%08lx\"]],"
                       "\"%08lx\",8],\"error\":null}\n",
                       (unsigned long)id,
                       (unsigned long)miner->ip,
                       (unsigned long)(miner->ip ^ 0x5a5a5a5a));
    send(fd, buf, len, MSG_DONTWAIT);

    if (s_current_job.valid) {
        char prevhash_hex[65];
        for (int i = 0; i < 32; i++) {
            snprintf(prevhash_hex + i * 2, 3, "%02x", s_current_job.prevhash[i]);
        }
        int jlen = snprintf(buf, SEND_BUF_SIZE,
                            "{\"id\":null,\"method\":\"mining.notify\","
                            "\"params\":[\"%lu\",\"%s\",\"\",\"\",\"\","
                            "\"%08lx\",\"%08lx\",\"%08lx\",%s]}\n",
                            (unsigned long)s_current_job.job_id,
                            prevhash_hex,
                            (unsigned long)s_current_job.version,
                            (unsigned long)s_current_job.nbits,
                            (unsigned long)s_current_job.ntime,
                            s_current_job.clean ? "true" : "false");
        send(fd, buf, jlen, MSG_DONTWAIT);
    }
    free(buf);
}

static void handle_set_difficulty(int fd)
{
    char *buf = malloc(SEND_BUF_SIZE);
    if (!buf) return;
    int len = snprintf(buf, SEND_BUF_SIZE,
                       "{\"id\":null,\"method\":\"mining.set_difficulty\","
                       "\"params\":[%.1f]}\n",
                       s_difficulty);
    send(fd, buf, len, MSG_DONTWAIT);
    free(buf);
}

static void handle_line(int fd, miner_client_t *miner, char *line)
{
    if (strlen(line) == 0) return;

    char *method_start = strstr(line, "\"method\"");
    if (!method_start) return;

    char method[64] = {0};
    char *m = strstr(method_start, "\":\"");
    if (!m) return;
    m += 3;
    int mi = 0;
    while (*m && *m != '"' && mi < 63) {
        method[mi++] = *m++;
    }

    uint32_t id = 0;
    char *id_start = strstr(line, "\"id\"");
    if (id_start) {
        char *id_val = strstr(id_start, ":");
        if (id_val) {
            id_val++;
            while (*id_val == ' ') id_val++;
            id = (uint32_t)strtoul(id_val, NULL, 10);
        }
    }

    if (strcmp(method, "mining.subscribe") == 0) {
        ESP_LOGI(TAG, "Miner 0x%08lx: subscribe", (unsigned long)miner->ip);
        handle_subscribe(fd, miner, id);
        handle_set_difficulty(fd);
    }
    else if (strcmp(method, "mining.authorize") == 0) {
        char *params = strstr(line, "\"params\"");
        if (params) {
            char *p = strchr(params, '[');
            if (p) {
                p++;
                while (*p == ' ' || *p == '"') p++;
                int wi = 0;
                while (*p && *p != '"' && wi < 63) {
                    miner->worker[wi++] = *p++;
                }
                miner->worker[wi] = '\0';
            }
        }
        miner->authorized = true;
        send_response(fd, id, "true", false);
        ESP_LOGI(TAG, "Miner 0x%08lx: authorized as '%s'",
                 (unsigned long)miner->ip, miner->worker);
    }
    else if (strcmp(method, "mining.submit") == 0) {
        s_stats.total_shares++;

        if (!s_current_job.valid) {
            s_stats.total_rejected++;
            send_response(fd, id, "no current job", true);
            return;
        }

        uint32_t submit_job_id = 0;
        uint32_t ntime = 0;
        uint32_t nonce = 0;
        uint32_t version = s_current_job.version;

        char *params = strstr(line, "\"params\"");
        if (params) {
            char *p = strchr(params, '[');
            if (p) {
                char *tok = strtok(p + 1, ",");
                int pidx = 0;
                while (tok && pidx < 5) {
                    while (*tok == ' ' || *tok == '"') tok++;
                    char *end = tok + strlen(tok) - 1;
                    while (end > tok && (*end == '"' || *end == ' ' || *end == ']')) {
                        *end-- = '\0';
                    }

                    if (pidx == 1) submit_job_id = (uint32_t)strtoul(tok, NULL, 10);
                    else if (pidx == 2) ntime = (uint32_t)strtoul(tok, NULL, 16);
                    else if (pidx == 3) nonce = (uint32_t)strtoul(tok, NULL, 16);
                    else if (pidx == 4) version = (uint32_t)strtoul(tok, NULL, 16);

                    tok = strtok(NULL, ",");
                    pidx++;
                }
            }
        }

        if (submit_job_id != s_current_job.job_id) {
            s_stats.total_rejected++;
            send_response(fd, id, "stale job", true);
            return;
        }

        uint8_t header[80];
        build_header(&s_current_job, nonce, ntime, version, header);

        uint8_t local_target[32];
        int local_target_len;
        build_target_from_difficulty(s_difficulty, local_target, &local_target_len);

        bool valid = check_pow(header, local_target, local_target_len);

        if (valid) {
            s_stats.total_accepted++;
            tollgate_core_mining_update_hashrate(miner->ip, true);
            send_response(fd, id, "true", false);

            if (s_share_cb) {
                s_share_cb(miner->ip, submit_job_id, nonce, ntime, version);
            }
        } else {
            s_stats.total_rejected++;
            send_response(fd, id, "low difficulty", true);
            tollgate_core_mining_update_hashrate(miner->ip, false);
        }
    }
    else if (strcmp(method, "mining.extranonce.subscribe") == 0) {
        send_response(fd, id, "true", false);
    }
    else if (strcmp(method, "mining.suggest_difficulty") == 0) {
        send_response(fd, id, "true", false);
    }
    else {
        send_response(fd, id, "unknown method", true);
    }
}

static void proxy_server_task(void *arg)
{
    char *recv_buf = malloc(RECV_BUF_SIZE);
    if (!recv_buf) {
        ESP_LOGE(TAG, "OOM for recv buffer");
        close(s_server_fd);
        s_server_fd = -1;
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "proxy_server_task started, server_fd=%d, fd_set size=%d, s_running=%d",
             s_server_fd, (int)sizeof(fd_set), s_running);

    while (s_running) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(s_server_fd, &read_set);

        int active = select(s_server_fd + 1, &read_set, NULL, NULL, NULL);
        if (active < 0) {
            ESP_LOGE(TAG, "select() error: errno=%d", errno);
            break;
        }
        if (active == 0) continue;

        if (!FD_ISSET(s_server_fd, &read_set)) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(s_server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            ESP_LOGE(TAG, "accept() failed: errno=%d", errno);
            continue;
        }

        uint32_t client_ip = client_addr.sin_addr.s_addr;
        int idx = register_miner(client_fd, client_ip);
        if (idx < 0) {
            ESP_LOGW(TAG, "Max miners, rejecting");
            close(client_fd);
            continue;
        }
        s_stats.active_miners++;

        struct timeval rto = { .tv_sec = 30, .tv_usec = 0 };
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));

        ESP_LOGI(TAG, "Miner connected from 0x%08lx (slot %d), handling...",
                 (unsigned long)client_ip, idx);

        while (s_running) {
            int len = recv(client_fd, recv_buf, RECV_BUF_SIZE - 1, 0);
            if (len <= 0) {
                ESP_LOGI(TAG, "Miner recv returned %d (errno=%d)", len, errno);
                break;
            }
            recv_buf[len] = '\0';
            char *line_start = recv_buf;
            for (int j = 0; j < len; j++) {
                if (recv_buf[j] == '\n' || recv_buf[j] == '\r') {
                    recv_buf[j] = '\0';
                    if (j > 0 && line_start < recv_buf + len && *line_start != '\0') {
                        handle_line(client_fd, &s_miners[idx], line_start);
                    }
                    line_start = recv_buf + j + 1;
                }
            }
            if (line_start < recv_buf + len && *line_start != '\0') {
                handle_line(client_fd, &s_miners[idx], line_start);
            }
        }

        ESP_LOGI(TAG, "Miner disconnected from slot %d", idx);
        close(client_fd);
        unregister_miner(client_fd);
    }

    free(recv_buf);
    for (int i = 0; i < TOLLGATE_PROXY_MAX_MINERS; i++) {
        if (s_miners[i].fd >= 0) {
            close(s_miners[i].fd);
            s_miners[i].fd = -1;
        }
    }
    close(s_server_fd);
    s_server_fd = -1;
    s_running = false;
    vTaskDelete(NULL);
}

esp_err_t tollgate_core_stratum_proxy_init(uint16_t port)
{
    if (s_running) {
        ESP_LOGI(TAG, "Stratum proxy already running on port %u, skipping", (unsigned)s_port);
        return ESP_OK;
    }

    s_port = port;
    memset(&s_current_job, 0, sizeof(s_current_job));
    memset(&s_stats, 0, sizeof(s_stats));
    s_difficulty = 1.0;

    for (int i = 0; i < TOLLGATE_PROXY_MAX_MINERS; i++) {
        s_miners[i].fd = -1;
    }

    if (!s_miners_mutex) {
        s_miners_mutex = xSemaphoreCreateMutex();
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket (errno=%d)", errno);
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(s_server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        ESP_LOGE(TAG, "Failed to bind to port %u (errno=%d)", (unsigned)port, errno);
        close(s_server_fd);
        s_server_fd = -1;
        return ESP_FAIL;
    }

    if (listen(s_server_fd, 4) != 0) {
        ESP_LOGE(TAG, "Failed to listen on port %u (errno=%d)", (unsigned)port, errno);
        close(s_server_fd);
        s_server_fd = -1;
        return ESP_FAIL;
    }

    s_running = true;
    ESP_LOGI(TAG, "Stratum proxy listening on port %u, server_fd=%d",
             (unsigned)port, s_server_fd);

    BaseType_t ret = xTaskCreate(proxy_server_task, "stratum_proxy", SERVER_TASK_STACK,
                                  NULL, 4, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create proxy task");
        close(s_server_fd);
        s_server_fd = -1;
        s_running = false;
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    {
        ESP_LOGI(TAG, "SELF-TEST: connecting to 127.0.0.1:%u ...", (unsigned)port);
        struct sockaddr_in self_addr;
        memset(&self_addr, 0, sizeof(self_addr));
        self_addr.sin_family = AF_INET;
        self_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        self_addr.sin_port = htons(port);

        int test_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (test_fd >= 0) {
            ESP_LOGI(TAG, "SELF-TEST: test_fd=%d", test_fd);
            struct timeval tmo = { .tv_sec = 3, .tv_usec = 0 };
            setsockopt(test_fd, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));
            setsockopt(test_fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));

            int cr = connect(test_fd, (struct sockaddr *)&self_addr, sizeof(self_addr));
            if (cr == 0) {
                ESP_LOGI(TAG, "SELF-TEST PASS: loopback connect to port %u succeeded, fd=%d", (unsigned)port, test_fd);
                const char *test_msg = "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}\n";
                int slen = send(test_fd, test_msg, strlen(test_msg), 0);
                ESP_LOGI(TAG, "SELF-TEST: sent %d bytes", slen);
                char resp[256];
                int rlen = recv(test_fd, resp, sizeof(resp) - 1, 0);
                if (rlen > 0) {
                    resp[rlen] = '\0';
                    ESP_LOGI(TAG, "SELF-TEST: got response (%d bytes): %.80s", rlen, resp);
                } else {
                    ESP_LOGW(TAG, "SELF-TEST: no response (recv=%d, errno=%d)", rlen, errno);
                }
            } else {
                ESP_LOGE(TAG, "SELF-TEST FAIL: loopback connect to 127.0.0.1:%u failed (errno=%d %s)",
                         (unsigned)port, errno, strerror(errno));
            }
            close(test_fd);
        } else {
            ESP_LOGE(TAG, "SELF-TEST: failed to create test socket (errno=%d)", errno);
        }
    }

    return ESP_OK;
}

void tollgate_core_stratum_proxy_set_job(const tollgate_stratum_job_t *job)
{
    if (!job) return;

    memcpy(&s_current_job, job, sizeof(tollgate_stratum_job_t));
    s_stats.nbits = job->nbits;
    s_stats.current_hashprice = tollgate_core_mining_get_current_hashprice();

    if (!job->valid) return;

    char prevhash_hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(prevhash_hex + i * 2, 3, "%02x", job->prevhash[i]);
    }

    char *notify = malloc(SEND_BUF_SIZE);
    if (!notify) return;
    int len = snprintf(notify, SEND_BUF_SIZE,
                       "{\"id\":null,\"method\":\"mining.notify\","
                       "\"params\":[\"%lu\",\"%s\",\"\",\"\",\"\","
                       "\"%08lx\",\"%08lx\",\"%08lx\",%s]}\n",
                       (unsigned long)job->job_id,
                       prevhash_hex,
                       (unsigned long)job->version,
                       (unsigned long)job->nbits,
                       (unsigned long)job->ntime,
                       job->clean ? "true" : "false");

    broadcast_to_miners(notify, len);
    free(notify);
}

void tollgate_core_stratum_proxy_set_difficulty(double difficulty)
{
    s_difficulty = difficulty;

    char *msg = malloc(SEND_BUF_SIZE);
    if (!msg) return;
    int len = snprintf(msg, SEND_BUF_SIZE,
                       "{\"id\":null,\"method\":\"mining.set_difficulty\","
                       "\"params\":[%.1f]}\n",
                       difficulty);
    broadcast_to_miners(msg, len);
    free(msg);
}

const tollgate_stratum_job_t *tollgate_core_stratum_proxy_get_current_job(void)
{
    return &s_current_job;
}

void tollgate_core_stratum_proxy_get_stats(tollgate_stratum_proxy_stats_t *stats)
{
    if (stats) {
        *stats = s_stats;
        stats->current_hashprice = tollgate_core_mining_get_current_hashprice();
    }
}

void tollgate_core_stratum_proxy_set_share_callback(tollgate_share_cb cb)
{
    s_share_cb = cb;
}

void tollgate_core_stratum_proxy_stop(void)
{
    s_running = false;
    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_task_handle = NULL;
    }
}
