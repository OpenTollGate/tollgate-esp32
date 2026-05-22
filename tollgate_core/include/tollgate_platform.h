#ifndef TOLLGATE_PLATFORM_H
#define TOLLGATE_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint16_t     (*get_price_sats)(void);
    int32_t      (*get_step_ms)(void);
    int64_t      (*get_step_bytes)(void);
    const char*  (*get_mint_url)(void);
    const char*  (*get_metric)(void);

    int64_t      (*get_time_ms)(void);

    void         (*log_info)(const char *tag, const char *fmt, ...);
    void         (*log_warn)(const char *tag, const char *fmt, ...);
    void         (*log_error)(const char *tag, const char *fmt, ...);

    bool         (*wallet_receive)(const char *token);
    bool         (*wallet_send)(uint64_t amount, char *buf, size_t buf_len);
    uint64_t     (*wallet_balance)(void);

    int          (*http_post)(const char *url, const char *headers,
                             const char *body, int body_len,
                             char *resp, int resp_len);

    bool         (*create_task)(void (*fn)(void*), void *arg,
                               const char *name, int stack_bytes, int priority);

    int          (*socket_udp)(void);
    int          (*socket_tcp)(void);
    int          (*socket_bind)(int fd, uint32_t ip, uint16_t port);
    int          (*socket_listen)(int fd, int backlog);
    int          (*socket_accept)(int fd, uint32_t *client_ip, uint16_t *client_port);
    int          (*socket_recvfrom)(int fd, void *buf, int len,
                                    uint32_t *src_ip, uint16_t *src_port);
    int          (*socket_sendto)(int fd, const void *buf, int len,
                                  uint32_t dest_ip, uint16_t dest_port);
    int          (*socket_read)(int fd, void *buf, int len);
    int          (*socket_write)(int fd, const void *buf, int len);
    void         (*socket_close)(int fd);
    void         (*socket_set_recv_timeout)(int fd, int ms);

    bool         (*get_sta_mac_ip_list)(void *list_out, int max, int *count_out);
    bool         (*set_vendor_ie)(bool enable, const void *ie_data, int ie_len);
    int          (*arp_get_mac)(uint32_t ip, uint8_t *mac_out);
    void         (*napt_enable)(uint32_t ip, bool enable);

    bool         (*mining_enabled)(void);
    const char*  (*get_stratum_host)(void);
    uint16_t     (*get_stratum_port)(void);
    const char*  (*get_stratum_user)(void);
    const char*  (*get_stratum_pass)(void);
    uint16_t     (*get_mining_port)(void);
    uint64_t     (*get_hashprice_override)(void);

    void         (*fill_random)(void *buf, int len);

    int          (*get_accepted_mint_count)(void);
    const char*  (*get_accepted_mint)(int index);
    bool         (*is_mint_reachable)(const char *mint_url);
    bool         (*mac_for_ip)(uint32_t ip, char *mac_out, int mac_out_size);

} tollgate_platform_t;

#endif
