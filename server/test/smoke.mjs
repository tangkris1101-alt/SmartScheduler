// 中继服务离线自测：内置假上游，验证鉴权 / 参数校验 / 流式透传 / 限额 / 埋点。
// 用法：node test/smoke.mjs   （不需要真实上游密钥，可在 CI 里跑）
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const APP_DIR = join(__dirname, '..');

const UPSTREAM_PORT = 18787;
const RELAY_PORT = 18791;
const APP_TOKEN = 'smoke-token';
const IP_MAX_REQUESTS = 5;

let failures = 0;
function check(name, cond, extra = '') {
    if (cond) {
        console.log(`  ✓ ${name}`);
    } else {
        failures++;
        console.log(`  ✗ ${name} ${extra}`);
    }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- 假上游：SSE 返回一段带 tool_calls 与 usage 的数据
const upstream = createServer((req, res) => {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
        const payload = JSON.parse(body || '{}');
        if (!payload.stream) {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({
                model: payload.model,
                choices: [{ finish_reason: 'tool_calls',
                            message: { content: 'ok',
                                       tool_calls: [{ id: 'call_1', type: 'function',
                                                      function: { name: 'batch_preassign',
                                                                  arguments: '{"period":8}' } }] } }],
                usage: { prompt_tokens: 11, completion_tokens: 3 },
            }));
            return;
        }
        res.writeHead(200, { 'Content-Type': 'text/event-stream' });
        const chunks = [
            { choices: [{ delta: { content: '正在' } }] },
            { choices: [{ delta: { tool_calls: [{ index: 0, id: 'call_1',
                                                  function: { name: 'batch_preassign' } }] } }] },
            { choices: [{ delta: {}, finish_reason: 'tool_calls' }] },
            { choices: [], usage: { prompt_tokens: 11, completion_tokens: 3 } },
        ];
        for (const c of chunks)
            res.write(`data: ${JSON.stringify(c)}\n\n`);
        res.write('data: [DONE]\n\n');
        res.end();
    });
});

async function main() {
    await new Promise((r) => upstream.listen(UPSTREAM_PORT, '127.0.0.1', r));
    console.log(`假上游已启动 :${UPSTREAM_PORT}`);

    const relay = spawn(process.execPath, ['src/index.js'], {
        cwd: APP_DIR,
        env: {
            ...process.env,
            PORT: String(RELAY_PORT),
            HOST: '127.0.0.1',
            APP_TOKEN,
            UPSTREAM_BASE: `http://127.0.0.1:${UPSTREAM_PORT}`,
            UPSTREAM_KEY: 'smoke-key',
            IP_MAX_REQUESTS: String(IP_MAX_REQUESTS),
            LOG_DIR: join(APP_DIR, 'logs'),
        },
        stdio: ['ignore', 'pipe', 'pipe'],
    });
    relay.stderr.on('data', (d) => process.stderr.write(`[relay] ${d}`));

    const base = `http://127.0.0.1:${RELAY_PORT}`;
    for (let i = 0; i < 40; ++i) {
        try {
            const r = await fetch(`${base}/v1/health`);
            if (r.ok)
                break;
        } catch { /* 还没起来 */ }
        await sleep(150);
    }

    console.log('健康检查');
    const health = await (await fetch(`${base}/v1/health`)).json();
    check('health.ok', health.ok === true);
    check('health.upstream', typeof health.upstream === 'string');

    console.log('鉴权与校验');
    const noToken = await fetch(`${base}/v1/ai/orchestrate`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ messages: [{ role: 'user', content: 'hi' }] }),
    });
    check('无令牌 → 401', noToken.status === 401);
    const badBody = await fetch(`${base}/v1/ai/orchestrate`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-App-Token': APP_TOKEN },
        body: JSON.stringify({ messages: [] }),
    });
    check('空 messages → 400', badBody.status === 400);

    console.log('非流式透传');
    const json = await fetch(`${base}/v1/ai/orchestrate`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-App-Token': APP_TOKEN },
        body: JSON.stringify({ messages: [{ role: 'user', content: '把高一1班周三第3节改成数学' }],
                               tools: [{ type: 'function', function: { name: 'move_cell' } }],
                               stream: false }),
    }).then((r) => r.json());
    check('返回 tool_calls', json.choices?.[0]?.message?.tool_calls?.length === 1);
    check('返回 usage', json.usage?.total_tokens === undefined && json.usage?.prompt_tokens === 11);

    console.log('流式透传（SSE）');
    const sse = await fetch(`${base}/v1/ai/orchestrate`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-App-Token': APP_TOKEN },
        body: JSON.stringify({ messages: [{ role: 'user', content: 'hi' }], stream: true }),
    });
    check('SSE 响应头', (sse.headers.get('content-type') || '').includes('text/event-stream'));
    const text = await sse.text();
    check('SSE 含 data 行', text.includes('data: '));
    check('SSE 含 [DONE]', text.includes('[DONE]'));
    check('SSE 含 tool_calls 片段', text.includes('batch_preassign'));

    console.log('埋点');
    const fb = await fetch(`${base}/v1/ai/feedback`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-App-Token': APP_TOKEN },
        body: JSON.stringify({ applied: 2, rejected: 1, turns: 3 }),
    }).then((r) => r.json());
    check('feedback.ok', fb.ok === true);

    console.log('限额（IP 窗口）');
    let limited = false;
    for (let i = 0; i < IP_MAX_REQUESTS + 2; ++i) {
        const r = await fetch(`${base}/v1/ai/orchestrate`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-App-Token': APP_TOKEN },
            body: JSON.stringify({ messages: [{ role: 'user', content: 'ping' }], stream: false }),
        });
        if (r.status === 429) {
            limited = true;
            break;
        }
    }
    check('超过窗口限额 → 429', limited);

    relay.kill('SIGTERM');
    upstream.close();
    await sleep(200);
    console.log(failures === 0 ? '\n全部通过' : `\n失败 ${failures} 项`);
    process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => {
    console.error(e);
    process.exit(1);
});
