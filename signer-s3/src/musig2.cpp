// ============================================================
// musig2.cpp — MuSig2 Two-Round Partial Signing
// MVP implementation for CKB Fiber Network channel signing.
// Ref: BIP-327 MuSig2 spec, simplified for 2-of-2 case.
// ============================================================

#include "musig2.h"
#include "crypto.h"
#include <string.h>
#include <Arduino.h>
#include "esp_random.h"

extern "C" {
#include "trezor_crypto/ecdsa.h"
#include "trezor_crypto/secp256k1.h"
#include "trezor_crypto/sha2.h"
#include "trezor_crypto/memzero.h"
}

// ── Session store ────────────────────────────────────────────
static MuSig2Session s_sessions[MUSIG2_MAX_SESSIONS];

void musig2_init(void) {
    memset(s_sessions, 0, sizeof(s_sessions));
}

static MuSig2Session* find_session(const char *sid) {
    for (int i = 0; i < MUSIG2_MAX_SESSIONS; i++) {
        if (s_sessions[i].active &&
            strncmp(s_sessions[i].session_id, sid,
                    MUSIG2_SESSION_ID_LEN * 2) == 0) {
            return &s_sessions[i];
        }
    }
    return nullptr;
}

static MuSig2Session* alloc_session(void) {
    for (int i = 0; i < MUSIG2_MAX_SESSIONS; i++) {
        if (!s_sessions[i].active) return &s_sessions[i];
    }
    // Evict the first one (simple LRU stub)
    return &s_sessions[0];
}

// ── Nonce generation ─────────────────────────────────────────
// Derive secret nonces via HKDF from:
//   IKM = privkey || random_32_bytes
//   info = "musig2-nonce-1" / "musig2-nonce-2"
// This ensures nonces are deterministic + unique per session.
static bool derive_nonce(const uint8_t privkey[32],
                         const uint8_t rand_bytes[32],
                         const char *label,
                         uint8_t nonce_out[32]) {
    uint8_t ikm[64];
    memcpy(ikm, privkey, 32);
    memcpy(ikm + 32, rand_bytes, 32);

    static const uint8_t salt[] = "musig2-nonce-salt";
    bool ok = crypto_hkdf_sha256(ikm, 64,
                                  salt, sizeof(salt) - 1,
                                  (const uint8_t *)label, strlen(label),
                                  nonce_out, 32);
    crypto_memzero(ikm, sizeof(ikm));
    return ok;
}

// ── Round 1 ──────────────────────────────────────────────────
bool musig2_round1(const char    *session_id_hex,
                   const uint8_t  privkey[32],
                   const uint8_t  message[32],
                   char           R1_hex_out[67],
                   char           R2_hex_out[67]) {
    if (!session_id_hex || !privkey || !message ||
        !R1_hex_out || !R2_hex_out) return false;

    // Check for duplicate session
    if (find_session(session_id_hex) != nullptr) {
        // Session already exists; return existing nonces
        MuSig2Session *s = find_session(session_id_hex);
        bytes_to_hex(s->R1, 33, R1_hex_out);
        bytes_to_hex(s->R2, 33, R2_hex_out);
        return true;
    }

    MuSig2Session *s = alloc_session();
    if (!s) return false;

    memset(s, 0, sizeof(MuSig2Session));
    strncpy(s->session_id, session_id_hex, sizeof(s->session_id) - 1);
    memcpy(s->privkey, privkey, 32);
    memcpy(s->message, message, 32);

    // Generate fresh random bytes for nonce derivation
    uint8_t rand_bytes[32];
    esp_fill_random(rand_bytes, 32);

    // Derive k1, k2
    if (!derive_nonce(privkey, rand_bytes, "musig2-nonce-1", s->k1)) goto fail;
    if (!derive_nonce(privkey, rand_bytes, "musig2-nonce-2", s->k2)) goto fail;

    // Validate k1, k2 are valid secp256k1 scalars
    {
        const ecdsa_curve *curve = &secp256k1;
        bignum256 bn;
        bn_read_be(s->k1, &bn);
        if (bn_is_zero(&bn) || !bn_is_less(&bn, &curve->order)) goto fail;
        bn_read_be(s->k2, &bn);
        if (bn_is_zero(&bn) || !bn_is_less(&bn, &curve->order)) goto fail;
    }

    // Compute R1 = k1 * G, R2 = k2 * G
    ecdsa_get_public_key33(&secp256k1, s->k1, s->R1);
    ecdsa_get_public_key33(&secp256k1, s->k2, s->R2);

    s->active = true;

    bytes_to_hex(s->R1, 33, R1_hex_out);
    bytes_to_hex(s->R2, 33, R2_hex_out);

    crypto_memzero(rand_bytes, sizeof(rand_bytes));
    return true;

fail:
    crypto_memzero(s, sizeof(MuSig2Session));
    crypto_memzero(rand_bytes, sizeof(rand_bytes));
    return false;
}

// ── Round 2 ──────────────────────────────────────────────────
// BIP-327 partial signature computation (2-of-2 simplified):
//
// 1. Parse aggregated nonce: agg_nonce = R_agg1 || R_agg2 (two 33-byte compressed points)
// 2. Compute b = SHA256("MuSig/nonceblind" || agg_nonce || message)
// 3. Compute R = R_agg1 + b * R_agg2   (effective nonce point)
// 4. Determine R.x and parity (negate k if R has odd y)
// 5. Compute aggregate pubkey (simple 2-of-2: P = P_our + P_counterparty)
//    Note: Full BIP-327 uses key aggregation with tagged hashes — MVP uses additive
// 6. Compute challenge e = SHA256("BIP0340/challenge" || R.x || P || message)
// 7. Compute partial_sig s = k_eff + e * privkey
//    where k_eff = k1 + b * k2 (with parity negation)
//
// NOTE: This is a simplified MVP. Full BIP-327 requires:
//   - Key aggregation coefficients (a_i = H_agg(L || P_i))
//   - Proper tagged hash domain separation
//   - Compatibility with secp256k1 Schnorr verification (BIP-340)
//
bool musig2_round2(const char *session_id_hex,
                   const char *agg_nonce_hex,
                   const char *message_hex,
                   const char *counterparty_pub_hex,
                   char        partial_sig_out[65]) {
    if (!session_id_hex || !agg_nonce_hex || !message_hex ||
        !counterparty_pub_hex || !partial_sig_out) return false;

    MuSig2Session *s = find_session(session_id_hex);
    if (!s) return false;

    // Parse agg_nonce (66 bytes: R_agg1 || R_agg2, each 33 bytes compressed)
    uint8_t agg_nonce_bytes[66];
    if (hex_to_bytes(agg_nonce_hex, agg_nonce_bytes, 66) != 66) return false;

    // Parse message (32 bytes)
    uint8_t message[32];
    if (hex_to_bytes(message_hex, message, 32) != 32) return false;

    // Parse counterparty pubkey (33 bytes)
    uint8_t cp_pub[33];
    if (hex_to_bytes(counterparty_pub_hex, cp_pub, 33) != 33) return false;

    // ── Step 1: Compute nonce binding value b ─────────────────
    // b = SHA256(TAG || agg_nonce || message)
    // TAG = SHA256("MuSig/nonceblind") padded per BIP-340 tagged hash spec
    SHA256_CTX sha;
    uint8_t b_hash[32];

    // BIP-340 tagged hash: SHA256(SHA256(tag) || SHA256(tag) || data)
    static const char tag_nonce_blind[] = "MuSig/nonceblind";
    uint8_t tag_hash[32];
    sha256_Raw((const uint8_t *)tag_nonce_blind, strlen(tag_nonce_blind), tag_hash);

    sha256_Init(&sha);
    sha256_Update(&sha, tag_hash, 32);       // SHA256(tag)
    sha256_Update(&sha, tag_hash, 32);       // SHA256(tag) again
    sha256_Update(&sha, agg_nonce_bytes, 66); // agg_nonce
    sha256_Update(&sha, message, 32);         // message
    sha256_Final(&sha, b_hash);

    // ── Step 2: Compute effective nonce R = R1 + b*R2 ─────────
    // Parse R_agg1 and R_agg2 as curve points
    curve_point R1_pt, R2_pt, R_eff;
    if (!ecdsa_read_pubkey(&secp256k1, agg_nonce_bytes,      &R1_pt)) return false;
    if (!ecdsa_read_pubkey(&secp256k1, agg_nonce_bytes + 33, &R2_pt)) return false;

    // Compute b * R2
    bignum256 b_scalar;
    bn_read_be(b_hash, &b_scalar);
    bn_mod(&b_scalar, &secp256k1.order);

    curve_point bR2;
    point_multiply(&secp256k1, &b_scalar, &R2_pt, &bR2);
    point_add(&secp256k1, &R1_pt, &bR2);   // R1_pt now holds R1 + b*R2 = R_eff
    memcpy(&R_eff, &R1_pt, sizeof(curve_point));

    // ── Step 3: Determine if R has odd y (need to negate) ─────
    // BIP-340: if R.y is odd, negate the nonce scalars
    bool negate_nonce = bn_is_odd(&R_eff.y);

    // ── Step 4: Compute k_eff = k1 + b*k2 (with possible negation) ──
    bignum256 k1_bn, k2_bn, k_eff_bn;
    bn_read_be(s->k1, &k1_bn);
    bn_read_be(s->k2, &k2_bn);

    // k_eff = k1 + b * k2 mod order
    bignum256 bk2;
    memcpy(&bk2, &k2_bn, sizeof(bignum256));
    bn_multiply(&b_scalar, &bk2, &secp256k1.order);
    bn_addmod(&k1_bn, &bk2, &secp256k1.order);
    memcpy(&k_eff_bn, &k1_bn, sizeof(bignum256));

    if (negate_nonce) {
        // k_eff = order - k_eff
        bignum256 neg_k;
        bn_subtract(&secp256k1.order, &k_eff_bn, &neg_k);
        memcpy(&k_eff_bn, &neg_k, sizeof(bignum256));
    }

    // ── Step 5: Compute aggregated pubkey (MVP: additive) ─────
    // P = P_our + P_counterparty
    // Full BIP-327 uses key-aggregation coefficients — this is simplified.
    curve_point P_our, P_cp, P_agg;
    uint8_t our_pub[33];
    ecdsa_get_public_key33(&secp256k1, s->privkey, our_pub);
    if (!ecdsa_read_pubkey(&secp256k1, our_pub, &P_our)) return false;
    if (!ecdsa_read_pubkey(&secp256k1, cp_pub, &P_cp)) return false;
    memcpy(&P_agg, &P_our, sizeof(curve_point));
    point_add(&secp256k1, &P_cp, &P_agg);  // P_agg = P_our + P_cp

    // Encode P_agg as compressed 33 bytes (manual compression)
    uint8_t P_agg_bytes[33];
    bn_write_be(&P_agg.x, P_agg_bytes + 1);
    P_agg_bytes[0] = bn_is_odd(&P_agg.y) ? 0x03 : 0x02;

    // If P_agg has odd y, negate our privkey for signing
    bignum256 priv_bn;
    bn_read_be(s->privkey, &priv_bn);
    if (bn_is_odd(&P_agg.y)) {
        bignum256 neg_priv;
        bn_subtract(&secp256k1.order, &priv_bn, &neg_priv);
        memcpy(&priv_bn, &neg_priv, sizeof(bignum256));
    }

    // ── Step 6: Compute challenge e ───────────────────────────
    // e = SHA256_tagged("BIP0340/challenge", R.x || P_agg || message)
    static const char tag_challenge[] = "BIP0340/challenge";
    uint8_t ch_tag_hash[32];
    sha256_Raw((const uint8_t *)tag_challenge, strlen(tag_challenge), ch_tag_hash);

    uint8_t R_x[32];
    bn_write_be(&R_eff.x, R_x);

    SHA256_CTX sha_e;
    uint8_t e_hash[32];
    sha256_Init(&sha_e);
    sha256_Update(&sha_e, ch_tag_hash, 32);
    sha256_Update(&sha_e, ch_tag_hash, 32);
    sha256_Update(&sha_e, R_x, 32);
    sha256_Update(&sha_e, P_agg_bytes + 1, 32);  // x-only P
    sha256_Update(&sha_e, message, 32);
    sha256_Final(&sha_e, e_hash);

    bignum256 e_bn;
    bn_read_be(e_hash, &e_bn);
    bn_mod(&e_bn, &secp256k1.order);

    // ── Step 7: partial_sig s = k_eff + e * privkey mod order ─
    // s = k_eff + e * privkey
    bignum256 s_bn;
    memcpy(&s_bn, &e_bn, sizeof(bignum256));
    bn_multiply(&priv_bn, &s_bn, &secp256k1.order);
    bn_addmod(&k_eff_bn, &s_bn, &secp256k1.order);
    memcpy(&s_bn, &k_eff_bn, sizeof(bignum256));

    uint8_t partial_sig[32];
    bn_write_be(&s_bn, partial_sig);
    bytes_to_hex(partial_sig, 32, partial_sig_out);

    // ── Cleanup session after round2 ─────────────────────────
    musig2_clear_session(session_id_hex);
    return true;
}

// ── Session cleanup ──────────────────────────────────────────
void musig2_clear_session(const char *session_id_hex) {
    for (int i = 0; i < MUSIG2_MAX_SESSIONS; i++) {
        if (s_sessions[i].active &&
            strncmp(s_sessions[i].session_id, session_id_hex,
                    MUSIG2_SESSION_ID_LEN * 2) == 0) {
            crypto_memzero(&s_sessions[i], sizeof(MuSig2Session));
            s_sessions[i].active = false;
            return;
        }
    }
}

void musig2_clear_all_sessions(void) {
    for (int i = 0; i < MUSIG2_MAX_SESSIONS; i++) {
        if (s_sessions[i].active) {
            crypto_memzero(&s_sessions[i], sizeof(MuSig2Session));
        }
    }
    memset(s_sessions, 0, sizeof(s_sessions));
}
