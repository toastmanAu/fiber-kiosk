/*
 * bridge.c — libcurl HTTP client for fiber-bridge API
 */
#include "bridge.h"
#include "config.h"
#include "state.h"
#include "../vendor/cjson/cJSON.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ── Response buffer ─────────────────────────────────────────────── */
typedef struct {
    char  *data;
    size_t len;
} CurlBuf;

static size_t _write_cb(void *ptr, size_t size, size_t nmemb, CurlBuf *buf) {
    size_t bytes = size * nmemb;
    buf->data = realloc(buf->data, buf->len + bytes + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

static char s_base_url[256];

void fk_bridge_init(const char *base_url) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    strncpy(s_base_url, base_url, sizeof(s_base_url)-1);
}

/* ── Generic GET ─────────────────────────────────────────────────── */
static cJSON *bridge_get(const char *path) {
    char url[512];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    CurlBuf buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || !buf.data) { free(buf.data); return NULL; }
    cJSON *json = cJSON_Parse(buf.data);
    free(buf.data);
    return json;
}

/* ── Generic POST ────────────────────────────────────────────────── */
static cJSON *bridge_post(const char *path, cJSON *body) {
    char url[512];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);

    char *body_str = cJSON_PrintUnformatted(body);
    if (!body_str) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) { free(body_str); return NULL; }

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

    CurlBuf buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    free(body_str);

    if (res != CURLE_OK || !buf.data) { free(buf.data); return NULL; }
    cJSON *json = cJSON_Parse(buf.data);
    free(buf.data);
    return json;
}

/* ── Poll ────────────────────────────────────────────────────────── */
int fk_bridge_poll(void) {
    /* GET /status — node info */
    cJSON *status = bridge_get("/status");
    if (!status) {
        pthread_mutex_lock(&g_state_mutex);
        g_state.bridge_ok = false;
        snprintf(g_state.last_error, sizeof(g_state.last_error), "Bridge unreachable");
        pthread_mutex_unlock(&g_state_mutex);
        return -1;
    }

    /* GET /channels */
    cJSON *chans = bridge_get("/channels");

    pthread_mutex_lock(&g_state_mutex);
    g_state.bridge_ok = true;

    /* Node info */
    cJSON *node_id  = cJSON_GetObjectItem(status, "node_id");
    cJSON *version  = cJSON_GetObjectItem(status, "version");
    cJSON *peers    = cJSON_GetObjectItem(status, "peer_count");

    if (node_id && node_id->valuestring)
        strncpy(g_state.node_id, node_id->valuestring, sizeof(g_state.node_id)-1);
    if (version && version->valuestring)
        strncpy(g_state.node_version, version->valuestring, sizeof(g_state.node_version)-1);
    if (peers)
        g_state.peer_count = peers->valueint;

    /* Channels */
    if (chans) {
        cJSON *arr = cJSON_GetObjectItem(chans, "channels");
        int n = 0;
        cJSON *ch;
        cJSON_ArrayForEach(ch, arr) {
            if (n >= FK_MAX_CHANNELS) break;
            FkChannel *fc = &g_state.channels[n];
            memset(fc, 0, sizeof(*fc));

            cJSON *cid    = cJSON_GetObjectItem(ch, "channel_id");
            cJSON *pid    = cJSON_GetObjectItem(ch, "peer_id");
            cJSON *lbal   = cJSON_GetObjectItem(ch, "local_balance");
            cJSON *rbal   = cJSON_GetObjectItem(ch, "remote_balance");
            cJSON *cap    = cJSON_GetObjectItem(ch, "capacity");
            cJSON *state  = cJSON_GetObjectItem(ch, "state");
            cJSON *pub    = cJSON_GetObjectItem(ch, "is_public");

            if (cid && cid->valuestring)
                strncpy(fc->channel_id, cid->valuestring, sizeof(fc->channel_id)-1);
            if (pid && pid->valuestring)
                strncpy(fc->peer_id, pid->valuestring, sizeof(fc->peer_id)-1);
            if (lbal) fc->local_balance_ckb  = (int64_t)lbal->valuedouble;
            if (rbal) fc->remote_balance_ckb = (int64_t)rbal->valuedouble;
            if (cap)  fc->capacity_ckb       = (int64_t)cap->valuedouble;
            if (state && state->valuestring) {
                if      (!strcmp(state->valuestring, "Open"))    fc->state = CHAN_OPEN;
                else if (!strcmp(state->valuestring, "Pending")) fc->state = CHAN_PENDING;
                else if (!strcmp(state->valuestring, "Closing")) fc->state = CHAN_CLOSING;
                else                                              fc->state = CHAN_CLOSED;
            }
            if (pub) fc->is_public = cJSON_IsTrue(pub);
            n++;
        }
        g_state.channel_len   = n;
        g_state.channel_count = n;
    }

    /* Timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_state.last_update_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    pthread_mutex_unlock(&g_state_mutex);

    cJSON_Delete(status);
    if (chans) cJSON_Delete(chans);
    return 0;
}

/* ── Send payment ────────────────────────────────────────────────── */
int fk_bridge_send_payment(const char *invoice, int64_t max_fee_shannons,
                            char *payment_hash_out, char *err, int errlen) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "invoice", invoice);
    cJSON_AddNumberToObject(body, "max_fee_shannons", (double)max_fee_shannons);

    cJSON *resp = bridge_post("/pay", body);
    cJSON_Delete(body);

    if (!resp) { snprintf(err, errlen, "Bridge unreachable"); return -1; }

    cJSON *ph = cJSON_GetObjectItem(resp, "payment_hash");
    cJSON *er = cJSON_GetObjectItem(resp, "error");

    int rc = 0;
    if (er && er->valuestring) {
        snprintf(err, errlen, "%s", er->valuestring);
        rc = -1;
    } else if (ph && ph->valuestring && payment_hash_out) {
        strncpy(payment_hash_out, ph->valuestring, 66);
    }
    cJSON_Delete(resp);
    return rc;
}

/* ── New invoice ─────────────────────────────────────────────────── */
int fk_bridge_new_invoice(int64_t amount_shannons, const char *description,
                           int expiry_sec, char *invoice_out, char *payment_hash_out,
                           char *err, int errlen) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "amount_shannons", (double)amount_shannons);
    cJSON_AddStringToObject(body, "description", description);
    cJSON_AddNumberToObject(body, "expiry_sec", expiry_sec);

    cJSON *resp = bridge_post("/invoice/new", body);
    cJSON_Delete(body);

    if (!resp) { snprintf(err, errlen, "Bridge unreachable"); return -1; }

    cJSON *inv = cJSON_GetObjectItem(resp, "invoice");
    cJSON *ph  = cJSON_GetObjectItem(resp, "payment_hash");
    cJSON *er  = cJSON_GetObjectItem(resp, "error");

    int rc = 0;
    if (er && er->valuestring) {
        snprintf(err, errlen, "%s", er->valuestring);
        rc = -1;
    } else {
        if (inv && inv->valuestring && invoice_out)
            strncpy(invoice_out, inv->valuestring, 511);
        if (ph && ph->valuestring && payment_hash_out)
            strncpy(payment_hash_out, ph->valuestring, 66);
    }
    cJSON_Delete(resp);
    return rc;
}

/* ── Check invoice ───────────────────────────────────────────────── */
int fk_bridge_check_invoice(const char *payment_hash) {
    char path[128];
    snprintf(path, sizeof(path), "/invoice/%s", payment_hash);
    cJSON *resp = bridge_get(path);
    if (!resp) return -1;

    cJSON *status = cJSON_GetObjectItem(resp, "status");
    int rc = 0;
    if (status && status->valuestring) {
        if (!strcmp(status->valuestring, "Paid")) rc = 1;
        else if (!strcmp(status->valuestring, "Expired") ||
                 !strcmp(status->valuestring, "Cancelled")) rc = -1;
    }
    cJSON_Delete(resp);
    return rc;
}

/* ── Signer status ───────────────────────────────────────────────── */
int fk_bridge_signer_status(bool *connected, bool *unlocked, char *pubkey, char *fw) {
    cJSON *resp = bridge_get("/signer/status");
    if (!resp) { *connected = false; return -1; }

    cJSON *conn = cJSON_GetObjectItem(resp, "connected");
    cJSON *unl  = cJSON_GetObjectItem(resp, "unlocked");
    cJSON *pk   = cJSON_GetObjectItem(resp, "pubkey");
    cJSON *fwv  = cJSON_GetObjectItem(resp, "firmware");

    if (connected) *connected = cJSON_IsTrue(conn);
    if (unlocked)  *unlocked  = cJSON_IsTrue(unl);
    if (pubkey && pk  && pk->valuestring)  strncpy(pubkey, pk->valuestring,  66);
    if (fw     && fwv && fwv->valuestring) strncpy(fw,     fwv->valuestring, 31);

    cJSON_Delete(resp);
    return 0;
}

/* ── Signer unlock ───────────────────────────────────────────────── */
int fk_bridge_signer_unlock(const char *pin, char *err, int errlen) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "pin", pin);
    cJSON *resp = bridge_post("/signer/unlock", body);
    cJSON_Delete(body);

    if (!resp) { snprintf(err, errlen, "Bridge unreachable"); return -1; }
    cJSON *ok = cJSON_GetObjectItem(resp, "unlocked");
    cJSON *er = cJSON_GetObjectItem(resp, "error");
    int rc = (ok && cJSON_IsTrue(ok)) ? 0 : -1;
    if (er && er->valuestring) snprintf(err, errlen, "%s", er->valuestring);
    cJSON_Delete(resp);
    return rc;
}

int fk_bridge_signer_lock(void) {
    cJSON *body = cJSON_CreateObject();
    cJSON *resp = bridge_post("/signer/lock", body);
    cJSON_Delete(body);
    if (resp) cJSON_Delete(resp);
    return 0;
}
