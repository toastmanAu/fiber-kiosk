/*
 * signer-uart.js — UART bridge to ESP32-P4 Fiber Signer
 *
 * Sends JSON-RPC frames over serial, returns responses.
 * Frame format: <JSON>\n (newline delimited)
 *
 * If signer not connected, returns degraded-mode responses
 * (node can still run, just no hardware-backed signing).
 */

'use strict';

const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

const SIGNER_PORT = process.env.SIGNER_PORT || '/dev/ttyACM0';
const BAUD        = 921600;
const TIMEOUT_MS  = 5000;

let port   = null;
let parser = null;
let s_connected = false;
let s_unlocked  = false;
let s_pubkey    = '';
let s_firmware  = '';

/* Pending requests keyed by id */
const pending = new Map();
let   next_id = 1;

function init() {
    try {
        port = new SerialPort({ path: SIGNER_PORT, baudRate: BAUD, autoOpen: false });
        parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

        parser.on('data', line => {
            try {
                const resp = JSON.parse(line.trim());
                const cb = pending.get(resp.id);
                if (cb) { pending.delete(resp.id); cb(null, resp.result || null, resp.error || null); }
            } catch(e) { /* ignore malformed */ }
        });

        port.open(err => {
            if (err) {
                console.warn(`[signer] not connected: ${err.message}`);
                s_connected = false;
                return;
            }
            s_connected = true;
            console.log(`[signer] connected on ${SIGNER_PORT}`);
            // Query status on connect
            _call('get_status', {}).then(r => {
                if (r) {
                    s_unlocked = r.locked === false;
                    s_pubkey   = r.node_pubkey || '';
                    s_firmware = r.firmware    || '';
                }
            }).catch(() => {});
        });

        port.on('close', () => {
            s_connected = false;
            s_unlocked  = false;
            console.warn('[signer] disconnected');
            /* Retry after 10s */
            setTimeout(init, 10000);
        });

    } catch(e) {
        console.warn('[signer] init error:', e.message);
        s_connected = false;
    }
}

function _call(method, params) {
    return new Promise((resolve, reject) => {
        if (!s_connected || !port || !port.isOpen) {
            return reject(new Error('Signer not connected'));
        }
        const id = next_id++;
        const req = JSON.stringify({ id, method, params }) + '\n';

        const timer = setTimeout(() => {
            pending.delete(id);
            reject(new Error('Signer timeout'));
        }, TIMEOUT_MS);

        pending.set(id, (err, result, rpcErr) => {
            clearTimeout(timer);
            if (err) return reject(err);
            if (rpcErr) return reject(new Error(rpcErr.message || 'Signer RPC error'));
            resolve(result);
        });

        port.write(req, err => { if (err) { pending.delete(id); clearTimeout(timer); reject(err); } });
    });
}

/* ── Public API ─────────────────────────────────────────────── */

async function status() {
    if (!s_connected) {
        return { connected: false, unlocked: false, pubkey: '', firmware: '' };
    }
    try {
        const r = await _call('get_status', {});
        s_unlocked = r?.locked === false;
        s_pubkey   = r?.node_pubkey || s_pubkey;
        s_firmware = r?.firmware    || s_firmware;
        return { connected: true, unlocked: s_unlocked, pubkey: s_pubkey, firmware: s_firmware };
    } catch(e) {
        return { connected: s_connected, unlocked: false, error: e.message };
    }
}

async function unlock(pin) {
    if (!s_connected) return { error: 'Signer not connected' };
    try {
        const r = await _call('unlock', { pin });
        s_unlocked = r?.unlocked === true;
        return { unlocked: s_unlocked, session_expires: r?.session_expires };
    } catch(e) {
        return { unlocked: false, error: e.message };
    }
}

async function lock() {
    s_unlocked = false;
    if (!s_connected) return;
    try { await _call('lock', {}); } catch(e) { /* ignore */ }
}

async function signTx(path, tx_hash, display_label) {
    if (!s_connected) throw new Error('Signer not connected');
    if (!s_unlocked)  throw new Error('Signer locked — unlock with PIN first');
    const r = await _call('sign_tx', { path, tx_hash, display: display_label });
    return r?.signature;
}

async function musig2Round1(session_id, path, message) {
    const r = await _call('musig2_round1', { session_id, path, message });
    return r?.pubnonce;
}

async function musig2Round2(session_id, agg_pubnonce, message, counterparty_pubkey) {
    const r = await _call('musig2_round2', { session_id, agg_pubnonce, message, counterparty_pubkey });
    return r?.partial_sig;
}

/* Init on load */
init();

module.exports = { status, unlock, lock, signTx, musig2Round1, musig2Round2 };
