// 智排课系统 AI 中继：把桌面端的编排请求透传给上游大模型，密钥只留在服务端。
//
// 设计要点：
//  - 工具（tools）由客户端下发，服务端只做透传与限额，所以客户端加工具无需改服务端；
//  - 鉴权：X-App-Token（应用令牌），常量时间比较；
//  - 限额：按 IP 的请求窗口、按令牌的每日 token 预算、并发上限；工具数量与 max_tokens 不封顶
//    （客户端一次复杂提问可能连发几十个请求，靠 IP 窗口与每日预算兜底）；
//  - 内测门禁：/v1/invite/status 查设备是否已通过，/v1/invite/verify 输码登记设备（只存 hash）；
//  - 错误日志反馈：/v1/feedback 收「用户描述 + 客户端日志正文」，落 logs/feedback/；
//  - 审计：每次请求落一行 JSONL（不含对话内容，只记用量与耗时）；
//  - 流式：默认 SSE 透传；客户端请求 stream=false 时返回普通 JSON（便于早期联调与测试）。
import express from 'express';
import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';
import { appendFileSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const APP_DIR = join(__dirname, '..');

// ---------------------------------------------------------------- 配置
const cfg = {
    port: Number(process.env.PORT || 8791),
    host: process.env.HOST || '127.0.0.1',
    appToken: process.env.APP_TOKEN || '',
    upstreamBase: process.env.UPSTREAM_BASE || 'https://api.deepseek.com',
    upstreamKey: process.env.UPSTREAM_KEY || '',
    defaultModel: process.env.DEFAULT_MODEL || 'deepseek-flash',
    // 0 = 不限制：max_tokens 原样透传给上游（客户端没给就不传，由模型自己定）；
    // 工具数量同样不设上限。需要收紧时用环境变量给具体数值即可。
    maxTokensCap: Number(process.env.MAX_TOKENS_CAP || 0),
    defaultMaxTokens: Number(process.env.DEFAULT_MAX_TOKENS || 0),
    dailyTokenBudget: Number(process.env.DAILY_TOKEN_BUDGET || 2000000),
    ipWindowMs: Number(process.env.IP_WINDOW_MS || 300000), // 5 分钟
    // 工具调用不设轮数上限，客户端一次提问可能连发几十个请求；这里只留一个防滥用上限
    ipMaxRequests: Number(process.env.IP_MAX_REQUESTS || 400),
    maxConcurrent: Number(process.env.MAX_CONCURRENT || 4),
    maxTools: Number(process.env.MAX_TOOLS || 0),
    logDir: process.env.LOG_DIR || join(APP_DIR, 'logs'),
    version: '1.0.0',
};

if (!cfg.appToken || !cfg.upstreamKey) {
    console.error('[relay] 缺少 APP_TOKEN 或 UPSTREAM_KEY，请检查 .env');
    process.exit(1);
}
mkdirSync(cfg.logDir, { recursive: true });

// ---------------------------------------------------------------- 限额状态
const ipHits = new Map(); // ip -> number[]（时间戳）
let concurrent = 0;
let daily = { day: new Date().toISOString().slice(0, 10), tokens: 0, requests: 0 };

function dayKey() {
    return new Date().toISOString().slice(0, 10);
}
function checkDaily() {
    const today = dayKey();
    if (daily.day !== today)
        daily = { day: today, tokens: 0, requests: 0 };
    return daily.tokens < cfg.dailyTokenBudget;
}
function countIp(ip) {
    const now = Date.now();
    const hits = (ipHits.get(ip) || []).filter((t) => now - t < cfg.ipWindowMs);
    hits.push(now);
    ipHits.set(ip, hits);
    // 顺手清理，避免 Map 无限增长
    if (ipHits.size > 5000) {
        for (const [k, v] of ipHits)
            if (!v.some((t) => now - t < cfg.ipWindowMs))
                ipHits.delete(k);
    }
    return hits.length;
}
function tokenEqual(a, b) {
    const ha = createHash('sha256').update(String(a)).digest();
    const hb = createHash('sha256').update(String(b)).digest();
    return timingSafeEqual(ha, hb);
}
function audit(record) {
    try {
        appendFileSync(join(cfg.logDir, `relay-${dayKey()}.jsonl`),
                       JSON.stringify({ ts: new Date().toISOString(), ...record }) + '\n');
    } catch (e) {
        console.error('[relay] 审计写入失败', e.message);
    }
}

// ---------------------------------------------------------------- 版本检查
// 桌面端「检查更新」的首选通道：自建域名国内直连，数据直接取自公开仓的 GitHub API。
// 第三方反代与 jsDelivr 都会缓存 version.json（发版后数分钟仍返回旧版本，客户端据此
// 误判「已是最新」），所以这里用短 TTL + 手动检查强制刷新把窗口压到最小。
const cfgVersion = {
    api: process.env.VERSION_API
        || 'https://api.github.com/repos/tangkris1101-alt/SmartScheduler-Releases/releases/latest',
    ttlMs: Number(process.env.VERSION_TTL_MS || 60000),
    forceMinMs: Number(process.env.VERSION_FORCE_MIN_MS || 15000),
    ipWindowMs: 300000,
    ipMaxRequests: 30,
};
let versionCache = { at: 0, payload: null, lastForce: 0 };
const versionHits = new Map();

function countVersionIp(ip) {
    const now = Date.now();
    const hits = (versionHits.get(ip) || []).filter((t) => now - t < cfgVersion.ipWindowMs);
    hits.push(now);
    versionHits.set(ip, hits);
    if (versionHits.size > 5000) {
        for (const [k, v] of versionHits)
            if (!v.some((t) => now - t < cfgVersion.ipWindowMs))
                versionHits.delete(k);
    }
    return hits.length;
}

// release 的资产里优先取安装包（setup 优先，其次是便携包外的任意 exe）
function pickSetupAsset(assets) {
    const list = Array.isArray(assets) ? assets : [];
    return list.find((a) => /-setup\.exe$/i.test(a.name || ''))
        || list.find((a) => /\.exe$/i.test(a.name || ''))
        || null;
}

async function fetchUpstreamVersion() {
    const res = await fetch(cfgVersion.api, {
        headers: { accept: 'application/vnd.github+json', 'user-agent': 'SmartScheduler-Relay' },
        signal: AbortSignal.timeout(8000),
    });
    if (!res.ok)
        throw new Error(`GitHub API ${res.status}`);
    const rel = await res.json();
    const tag = String(rel.tag_name || '');
    if (!tag)
        throw new Error('release 缺少 tag_name');
    const asset = pickSetupAsset(rel.assets);
    const digest = String((asset && asset.digest) || '');
    return {
        tag,
        setup: asset ? asset.name : `SmartScheduler-${tag.replace(/^v/, '')}-setup.exe`,
        size: asset ? Number(asset.size || 0) : 0,
        sha256: digest.startsWith('sha256:') ? digest.slice(7) : '',
        publishedAt: String(rel.published_at || ''),
        notes: String(rel.body || '').slice(0, 400),
    };
}

// force=true 时（手动检查）最多每 forceMinMs 向上游要一次，其余情况吃缓存；
// 上游失败但有旧缓存时降级返回旧值（客户端会看到 cached=true 并继续问其它通道）
async function versionPayload(force) {
    const now = Date.now();
    const age = now - versionCache.at;
    const usable = versionCache.payload && age < cfgVersion.ttlMs;
    if (usable && !force)
        return { ...versionCache.payload, cached: true, ageMs: age };
    if (force && versionCache.payload && now - versionCache.lastForce < cfgVersion.forceMinMs)
        return { ...versionCache.payload, cached: true, ageMs: age };
    if (force || !versionCache.payload)
        versionCache.lastForce = now;
    try {
        const payload = await fetchUpstreamVersion();
        versionCache = { ...versionCache, at: now, payload };
        return { ...payload, cached: false, ageMs: 0 };
    } catch (err) {
        if (versionCache.payload) {
            return { ...versionCache.payload, cached: true, ageMs: age,
                     upstreamError: String(err.message || err) };
        }
        throw err;
    }
}

// ---------------------------------------------------------------- 内测邀请码 / 设备登记
// 门禁：应用首次启动要求输入邀请码，服务端只存 sha256（不存明文）；校验通过后把设备号
// 记进 data/invite-devices.jsonl，之后每次启动查 /v1/invite/status 即可跳过输入。
const cfgInvite = {
    // 规范化规则：去掉空格与连字符、转大写。留空 = 不启用门禁（返回 approved:true，避免把人锁在外面）
    codeHash: (process.env.INVITE_CODE_HASH || '').trim().toLowerCase(),
    dataDir: process.env.DATA_DIR || join(APP_DIR, 'data'),
    ipWindowMs: Number(process.env.INVITE_IP_WINDOW_MS || 600000), // 10 分钟
    ipMaxAttempts: Number(process.env.INVITE_IP_MAX || 20),        // 单 IP 最多试几次
};
const approvedDevices = new Set();
const inviteAttempts = new Map();

function normalizeCode(s) {
    return String(s || '').replace(/[\s-]/g, '').toUpperCase();
}
function sha256(s) {
    return createHash('sha256').update(String(s)).digest('hex');
}
function deviceFile() {
    return join(cfgInvite.dataDir, 'invite-devices.jsonl');
}
function loadApprovedDevices() {
    let n = 0;
    try {
        for (const line of readFileSync(deviceFile(), 'utf8').split('\n')) {
            if (!line.trim())
                continue;
            try {
                const r = JSON.parse(line);
                if (r && r.device) {
                    approvedDevices.add(r.device);
                    n++;
                }
            } catch { /* 单行坏了不影响其它行 */ }
        }
    } catch { /* 首次运行还没有文件 */ }
    return n;
}
function rememberDevice(device, app, ip) {
    approvedDevices.add(device);
    mkdirSync(cfgInvite.dataDir, { recursive: true });
    appendFileSync(deviceFile(),
                   JSON.stringify({ device, app, ip, at: new Date().toISOString() }) + '\n');
}
function countInviteAttempt(ip) {
    const now = Date.now();
    const hits = (inviteAttempts.get(ip) || []).filter((t) => now - t < cfgInvite.ipWindowMs);
    hits.push(now);
    inviteAttempts.set(ip, hits);
    return hits.length;
}
function clientIp(req) {
    return (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '?')
        .toString().split(',')[0].trim();
}

// ---------------------------------------------------------------- 错误日志反馈
const cfgFeedback = {
    maxLogBytes: Number(process.env.FEEDBACK_MAX_BYTES || 512 * 1024),
    ipWindowMs: Number(process.env.FEEDBACK_IP_WINDOW_MS || 600000), // 10 分钟
    ipMaxReports: Number(process.env.FEEDBACK_IP_MAX || 10),
};
const feedbackHits = new Map();

function countFeedback(ip) {
    const now = Date.now();
    const hits = (feedbackHits.get(ip) || []).filter((t) => now - t < cfgFeedback.ipWindowMs);
    hits.push(now);
    feedbackHits.set(ip, hits);
    return hits.length;
}

// ---------------------------------------------------------------- 入参校验
function validate(body) {
    if (!body || typeof body !== 'object')
        return '请求体必须是 JSON 对象';
    const { messages, tools } = body;
    if (!Array.isArray(messages) || messages.length === 0)
        return 'messages 不能为空';
    if (messages.length > 60)
        return 'messages 过长（最多 60 条）';
    for (const m of messages) {
        if (!m || typeof m.role !== 'string' || typeof m.content !== 'string'
            && !Array.isArray(m.tool_calls))
            return 'messages 元素格式不正确';
    }
    if (tools !== undefined) {
        if (!Array.isArray(tools))
            return 'tools 必须是数组';
        if (cfg.maxTools > 0 && tools.length > cfg.maxTools)
            return `tools 过多（最多 ${cfg.maxTools} 个）`;
        for (const t of tools) {
            if (!t?.function?.name || typeof t.function.name !== 'string')
                return 'tools 元素缺少 function.name';
        }
    }
    return '';
}

// ---------------------------------------------------------------- 应用
const app = express();
app.disable('x-powered-by');
// 反馈接口要带日志正文（客户端截到 ~400KB），所以整体放宽到 640KB
app.use(express.json({ limit: '640kb' }));

app.get('/v1/health', (req, res) => {
    res.json({
        ok: true,
        version: cfg.version,
        upstream: new URL(cfg.upstreamBase).host,
        model: cfg.defaultModel,
        day: dayKey(),
        dailyTokens: daily.tokens,
        dailyBudget: cfg.dailyTokenBudget,
    });
});

// ---------------------------------------------------------------- 内测门禁 + 反馈
// 设备是否已通过邀请码：应用每次启动查一次，approved=true 就跳过输入邀请码
app.get('/v1/invite/status', (req, res) => {
    res.set('Cache-Control', 'no-store');
    const device = String(req.query.device || '').slice(0, 64);
    if (!device)
        return res.status(400).json({ ok: false, error: '缺少设备标识' });
    const gate = cfgInvite.codeHash.length > 0; // 没配邀请码 = 不启用门禁
    res.json({ ok: true, gate, approved: !gate || approvedDevices.has(device) });
});

// 校验邀请码：通过则登记设备（之后该设备免输）
app.post('/v1/invite/verify', (req, res) => {
    const ip = clientIp(req);
    const attempts = countInviteAttempt(ip);
    if (attempts > cfgInvite.ipMaxAttempts) {
        audit({ ip, path: 'invite/verify', status: 429, reason: 'ip' });
        return res.status(429).json({ ok: false, error: '尝试次数过多，请稍后再试' });
    }
    if (!cfgInvite.codeHash)
        return res.json({ ok: true, gate: false, approved: true });

    const body = req.body || {};
    const device = String(body.device || '').slice(0, 64);
    const appVer = String(body.app || '').slice(0, 24);
    if (!device)
        return res.status(400).json({ ok: false, error: '缺少设备标识' });

    const ok = tokenEqual(sha256(normalizeCode(body.code)), cfgInvite.codeHash);
    if (!ok) {
        audit({ ip, path: 'invite/verify', status: 401, reason: 'bad_code', client: appVer });
        return res.status(401).json({ ok: false, error: '邀请码不正确' });
    }
    rememberDevice(device, appVer, ip);
    audit({ ip, path: 'invite/verify', status: 200, device, client: appVer });
    console.log(`[relay] 邀请码通过：device=${device} app=${appVer} ip=${ip}`);
    res.json({ ok: true, approved: true });
});

// 错误日志反馈：描述 + 客户端日志正文（客户端已截断），落盘便于人工看
app.post('/v1/feedback', (req, res) => {
    const ip = clientIp(req);
    const token = String(req.headers['x-app-token'] || '');
    if (!cfg.appToken || !tokenEqual(token, cfg.appToken)) {
        audit({ ip, path: 'feedback', status: 401 });
        return res.status(401).json({ ok: false, error: '应用令牌无效' });
    }
    if (countFeedback(ip) > cfgFeedback.ipMaxReports) {
        audit({ ip, path: 'feedback', status: 429, reason: 'ip' });
        return res.status(429).json({ ok: false, error: '反馈提交过于频繁，请稍后再试' });
    }
    const body = req.body || {};
    const description = String(body.description || '').slice(0, 4000);
    const log = typeof body.log === 'string' ? body.log.slice(-cfgFeedback.maxLogBytes) : '';
    if (!description.trim() && !log) {
        audit({ ip, path: 'feedback', status: 400, reason: 'empty' });
        return res.status(400).json({ ok: false, error: '请写点描述，或勾选附带日志' });
    }
    const device = String(body.device || '').slice(0, 64);
    const appVer = String(body.app || '').slice(0, 24);
    const os = String(body.os || '').slice(0, 80);
    const id = `${dayKey().replace(/-/g, '')}-${randomBytes(4).toString('hex')}`;
    try {
        const dir = join(cfg.logDir, 'feedback');
        mkdirSync(dir, { recursive: true });
        if (log)
            writeFileSync(join(dir, `${id}.log`), log, 'utf8');
        appendFileSync(join(cfg.logDir, `feedback-${dayKey()}.jsonl`),
                       JSON.stringify({ id, at: new Date().toISOString(), device, app: appVer,
                                        os, ip, description, logBytes: log.length }) + '\n');
    } catch (err) {
        console.error('[relay] 反馈落盘失败', err.message);
        return res.status(500).json({ ok: false, error: '服务端写入失败' });
    }
    audit({ ip, path: 'feedback', status: 200, device, client: appVer, bytes: log.length });
    console.log(`[relay] 收到反馈 ${id}（device=${device} 日志=${log.length} 字节）`);
    res.json({ ok: true, id });
});

// 桌面端「检查更新」的首选通道：数据直接来自 GitHub API（详见文件上方 cfgVersion 注释）
app.get('/v1/version', async (req, res) => {
    res.set('Cache-Control', 'no-store');
    const ip = (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '?')
                   .toString().split(',')[0].trim();
    if (countVersionIp(ip) > cfgVersion.ipMaxRequests)
        return res.status(429).json({ ok: false, error: 'too_many_requests' });
    const force = String(req.query.fresh || '') === '1';
    try {
        const payload = await versionPayload(force);
        res.json({ ok: true, ...payload, app: String(req.query.app || '').slice(0, 24) });
    } catch (err) {
        // 客户端据此回退到其它通道：这里绝不能返回 200/空 tag，否则会把它钉死在「已是最新」
        res.status(503).json({ ok: false, error: 'upstream_unavailable',
                               detail: String(err.message || err) });
    }
});

app.post('/v1/ai/orchestrate', async (req, res) => {
    const started = Date.now();
    const ip = (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '?')
                   .toString().split(',')[0].trim();
    const token = req.headers['x-app-token'] || '';
    const client = req.headers['x-client-version'] || '';

    if (!cfg.appToken || !tokenEqual(token, cfg.appToken)) {
        audit({ ip, path: 'orchestrate', status: 401, client });
        return res.status(401).json({ error: '应用令牌无效' });
    }
    if (countIp(ip) > cfg.ipMaxRequests) {
        audit({ ip, path: 'orchestrate', status: 429, reason: 'ip', client });
        return res.status(429).json({ error: '请求过于频繁，请稍后再试' });
    }
    if (!checkDaily()) {
        audit({ ip, path: 'orchestrate', status: 429, reason: 'daily', client });
        return res.status(429).json({ error: '今日 AI 额度已用完' });
    }
    if (concurrent >= cfg.maxConcurrent) {
        audit({ ip, path: 'orchestrate', status: 429, reason: 'concurrent', client });
        return res.status(429).json({ error: '服务器繁忙，请稍后再试' });
    }
    const bad = validate(req.body);
    if (bad) {
        audit({ ip, path: 'orchestrate', status: 400, reason: bad, client });
        return res.status(400).json({ error: bad });
    }

    const stream = req.body.stream !== false;
    const payload = {
        model: req.body.model || cfg.defaultModel,
        messages: req.body.messages,
        tools: req.body.tools,
        tool_choice: req.body.tool_choice || (req.body.tools?.length ? 'auto' : undefined),
        temperature: typeof req.body.temperature === 'number' ? req.body.temperature : 0.2,
        stream,
    };
    // max_tokens：客户端给了就原样透传（默认不封顶），没给且没配默认值就不传，交给上游模型自己定。
    // 之前默认封在 1024，长回答会被截断。
    const asked = Number(req.body.max_tokens);
    if (asked > 0)
        payload.max_tokens = cfg.maxTokensCap > 0 ? Math.min(asked, cfg.maxTokensCap) : asked;
    else if (cfg.defaultMaxTokens > 0)
        payload.max_tokens = cfg.defaultMaxTokens;
    if (stream)
        payload.stream_options = { include_usage: true };

    concurrent++;
    let status = 200;
    let usage = null;
    let toolCalls = 0;
    try {
        const upstream = await fetch(`${cfg.upstreamBase}/chat/completions`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                Authorization: `Bearer ${cfg.upstreamKey}`,
            },
            body: JSON.stringify(payload),
        });
        status = upstream.status;
        if (!upstream.ok) {
            const text = await upstream.text();
            audit({ ip, path: 'orchestrate', status, client, ms: Date.now() - started });
            return res.status(status).json({ error: '上游调用失败', detail: text.slice(0, 500) });
        }

        if (!stream) {
            const data = await upstream.json();
            usage = data.usage || null;
            toolCalls = data.choices?.[0]?.message?.tool_calls?.length || 0;
            return res.json(data);
        }

        res.status(200).set({
            'Content-Type': 'text/event-stream; charset=utf-8',
            'Cache-Control': 'no-cache, no-transform',
            Connection: 'keep-alive',
            'X-Accel-Buffering': 'no', // 关掉 nginx 缓冲，SSE 才实时
        });
        res.flushHeaders?.();

        const reader = upstream.body.getReader();
        const decoder = new TextDecoder();
        let buffer = '';
        for (;;) {
            const { done, value } = await reader.read();
            if (done)
                break;
            buffer += decoder.decode(value, { stream: true });
            let idx;
            while ((idx = buffer.indexOf('\n')) >= 0) {
                const line = buffer.slice(0, idx + 1);
                buffer = buffer.slice(idx + 1);
                res.write(line);
                const trimmed = line.trim();
                if (trimmed.startsWith('data:')) {
                    const json = trimmed.slice(5).trim();
                    if (json && json !== '[DONE]') {
                        try {
                            const obj = JSON.parse(json);
                            if (obj.usage)
                                usage = obj.usage;
                            const tc = obj.choices?.[0]?.delta?.tool_calls;
                            if (Array.isArray(tc))
                                toolCalls = Math.max(toolCalls, tc.length);
                        } catch { /* 无关片段 */ }
                    }
                }
            }
        }
        res.end();
    } catch (e) {
        status = 502;
        if (!res.headersSent)
            res.status(502).json({ error: '中继异常', detail: String(e.message || e) });
        else
            res.end();
    } finally {
        concurrent--;
        const total = (usage?.prompt_tokens || 0) + (usage?.completion_tokens || 0);
        daily.tokens += total;
        daily.requests += 1;
        audit({
            ip, path: 'orchestrate', status, client, ms: Date.now() - started, model: payload.model,
            stream, prompt_tokens: usage?.prompt_tokens || 0,
            completion_tokens: usage?.completion_tokens || 0, tool_calls: toolCalls,
        });
    }
});

// 采纳率埋点：客户端上报用户对 AI 待办卡的取舍（只记计数，不记内容）
app.post('/v1/ai/feedback', (req, res) => {
    const token = req.headers['x-app-token'] || '';
    if (!cfg.appToken || !tokenEqual(token, cfg.appToken))
        return res.status(401).json({ error: '应用令牌无效' });
    const b = req.body || {};
    audit({
        ip: (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '?').toString(),
        path: 'feedback',
        status: 200,
        applied: Number(b.applied) || 0,
        rejected: Number(b.rejected) || 0,
        turns: Number(b.turns) || 0,
    });
    res.json({ ok: true });
});

const server = app.listen(cfg.port, cfg.host, () => {
    const devices = loadApprovedDevices();
    console.log(`[relay] listening on http://${cfg.host}:${cfg.port}  model=${cfg.defaultModel}`
                + `  upstream=${cfg.upstreamBase}`);
    console.log(`[relay] 内测门禁 ${cfgInvite.codeHash ? '已启用' : '未启用（未配置 INVITE_CODE_HASH）'}`
                + `  已登记设备 ${devices} 台`);
});

function shutdown(sig) {
    console.log(`[relay] ${sig} → 关闭`);
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(0), 3000);
}
process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));
