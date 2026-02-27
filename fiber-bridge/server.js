/*
 * fiber-bridge/server.js
 * ----------------------
 * Local HTTP API server wrapping:
 *   - Fiber node RPC (fnn, Biscuit auth)
 *   - ESP32-P4 signer (UART JSON-RPC)
 *
 * Endpoints consumed by fiber-kiosk (C app):
 *   GET  /status          — node_info + uptime
 *   GET  /channels        — list_channels
 *   POST /pay             — send_payment
 *   POST /invoice/new     — new_invoice
 *   GET  /invoice/:hash   — get_invoice status
 *   GET  /signer/status   — signer health
 *   POST /signer/unlock   — unlock signer (PIN)
 *   POST /signer/lock     — lock signer
 *
 * Config via environment:
 *   FIBER_RPC_URL   — http://127.0.0.1:8227
 *   FIBER_TOKEN     — biscuit auth token
 *   SIGNER_PORT     — /dev/ttyACM0
 *   BRIDGE_PORT     — 7777
 */

'use strict';

const http   = require('http');
const url    = require('url');
const { fiberRpc, checkFiberNode } = require('../fiber-htlc');
const signer = require('./signer-uart');

const PORT = parseInt(process.env.BRIDGE_PORT || '7777');

/* ── JSON helpers ─────────────────────────────────────────────── */
function jsonResp(res, data, status = 200) {
    const body = JSON.stringify(data);
    res.writeHead(status, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) });
    res.end(body);
}

function readBody(req) {
    return new Promise((resolve, reject) => {
        let data = '';
        req.on('data', c => data += c);
        req.on('end',  () => { try { resolve(JSON.parse(data || '{}')); } catch(e) { resolve({}); } });
        req.on('error', reject);
    });
}

/* ── Route table ──────────────────────────────────────────────── */
const server = http.createServer(async (req, res) => {
    const parsed = url.parse(req.url, true);
    const path   = parsed.pathname;
    const method = req.method;

    try {
        /* GET /status */
        if (method === 'GET' && path === '/status') {
            const info = await fiberRpc('node_info', {});
            jsonResp(res, {
                node_id:   info.node_id,
                version:   info.version,
                peer_count: info.connections ?? 0,
                ok:        true,
            });
            return;
        }

        /* GET /channels */
        if (method === 'GET' && path === '/channels') {
            const result = await fiberRpc('list_channels', { peer_id: null });
            const channels = (result.channels || []).map(ch => ({
                channel_id:      ch.channel_id,
                peer_id:         ch.peer_id,
                local_balance:   parseInt(ch.local_balance  || '0x0', 16),
                remote_balance:  parseInt(ch.remote_balance || '0x0', 16),
                capacity:        parseInt(ch.capacity       || '0x0', 16),
                state:           ch.state,
                is_public:       ch.is_public ?? false,
            }));
            jsonResp(res, { channels });
            return;
        }

        /* POST /pay */
        if (method === 'POST' && path === '/pay') {
            const body = await readBody(req);
            if (!body.invoice) { jsonResp(res, { error: 'invoice required' }, 400); return; }
            const result = await fiberRpc('send_payment', {
                invoice:  body.invoice,
                max_fee_amount: body.max_fee_shannons
                    ? '0x' + BigInt(body.max_fee_shannons).toString(16)
                    : undefined,
            });
            jsonResp(res, { payment_hash: result.payment_hash, status: result.status });
            return;
        }

        /* POST /invoice/new */
        if (method === 'POST' && path === '/invoice/new') {
            const body = await readBody(req);
            const amount_hex = '0x' + BigInt(body.amount_shannons || 0).toString(16);
            const result = await fiberRpc('new_invoice', {
                amount:      amount_hex,
                description: body.description || 'Fiber Kiosk',
                expiry:      body.expiry_sec  || 600,
            });
            const detail = await fiberRpc('parse_invoice', { invoice: result.invoice_address });
            jsonResp(res, {
                invoice:      result.invoice_address,
                payment_hash: detail?.invoice?.payment_hash,
            });
            return;
        }

        /* GET /invoice/:hash */
        const invMatch = path.match(/^\/invoice\/(.+)$/);
        if (method === 'GET' && invMatch) {
            const result = await fiberRpc('get_invoice', { payment_hash: invMatch[1] });
            jsonResp(res, {
                status:       result?.invoice?.status,
                payment_hash: invMatch[1],
            });
            return;
        }

        /* GET /signer/status */
        if (method === 'GET' && path === '/signer/status') {
            const s = await signer.status();
            jsonResp(res, s);
            return;
        }

        /* POST /signer/unlock */
        if (method === 'POST' && path === '/signer/unlock') {
            const body = await readBody(req);
            const result = await signer.unlock(body.pin || '');
            jsonResp(res, result);
            return;
        }

        /* POST /signer/lock */
        if (method === 'POST' && path === '/signer/lock') {
            await signer.lock();
            jsonResp(res, { locked: true });
            return;
        }

        /* 404 */
        jsonResp(res, { error: 'not found' }, 404);

    } catch (err) {
        console.error('[bridge] error:', err.message);
        jsonResp(res, { error: err.message }, 500);
    }
});

server.listen(PORT, '127.0.0.1', () => {
    console.log(`[fiber-bridge] listening on http://127.0.0.1:${PORT}`);
    console.log(`[fiber-bridge] Fiber RPC: ${process.env.FIBER_RPC_URL || 'http://127.0.0.1:8227'}`);
    console.log(`[fiber-bridge] Signer:    ${process.env.SIGNER_PORT  || '/dev/ttyACM0'}`);
});
