# 智排课系统 · AI 编排中继（api.class.feimsg.cn）

桌面端「AI 助手」把一句话需求发到这里，由服务端带着**只有服务端才知道的**大模型密钥
转发给 DeepSeek，并把工具调用（Tool Calls）原样回传；工具在桌面端本地执行。

## 为什么要有中继

- 客户端不内置大模型密钥（只带一个可轮换的应用令牌）
- 统一限流、配额与审计；上游模型可随时更换
- **工具定义由客户端下发**，所以桌面端加工具不需要改服务端

## 目录

```
server/
  src/index.js            中继服务（Express + Node 内置 fetch）
  test/smoke.mjs          离线自测（内置假上游，不访问真实模型）
  ecosystem.config.cjs    pm2 常驻配置
  .env.example            服务端配置样例（真实 .env 不提交，部署时保留）
```

## 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/v1/health` | 健康检查 + 当日用量 |
| GET | `/v1/version` | 版本检查通道：直连 GitHub API 取公开仓最新 release（`?fresh=1` 强制回源，60s 缓存，无缓存且上游不可用时返回 503 让客户端回退） |
| POST | `/v1/ai/orchestrate` | 编排主入口；`stream:false` 返回 JSON，默认 SSE 流式 |
| POST | `/v1/ai/feedback` | 采纳率埋点（只记条数，不记内容） |

鉴权：请求头 `X-App-Token: <应用令牌>`；`X-Client-Version` 可选（用于审计）。
`/v1/health` 与 `/v1/version` 匿名可用（客户端检查更新时尚无令牌上下文）。

限额（均可用环境变量覆盖）：单 IP 5 分钟 400 次、并发 ≤ 4、当日 token 预算 200 万。
**工具数量与 max_tokens 不封顶**：客户端一次复杂提问（如逐班查询）可能连发几十个请求，
`max_tokens` 原样透传给上游（客户端不给就不传，由模型自己定）；需要收紧时用
`MAX_TOOLS` / `MAX_TOKENS_CAP` / `DEFAULT_MAX_TOKENS` / `IP_MAX_REQUESTS` 给具体数值。

## 部署（CI/CD 自动）

`.github/workflows/server.yml`：`server/**` 的 push 会先跑离线自测，通过后
rsync 到服务器 → `npm ci --omit=dev` → `pm2 reload ai-relay` → 本机与公网健康检查。
需要的仓库 Secrets：`SERVER_HOST`、`SERVER_PORT`、`SERVER_USER`、`SERVER_SSH_KEY`。

服务端首次部署需要准备好（已完成，仅记录）：

1. `/www/wwwroot/api.class.feimsg.cn/app/.env`（含 `APP_TOKEN`、`UPSTREAM_KEY`，权限 600）
2. nginx 反代片段 `/www/server/panel/vhost/nginx/extension/api.class.feimsg.cn/ai.conf`
   （`/v1/` → `127.0.0.1:8791`，SSE 关闭缓冲）
3. `pm2 start ecosystem.config.cjs && pm2 save`

## 本地开发

```bash
cd server
npm install
node --env-file=.env src/index.js     # 需要 .env（可从 .env.example 复制）
node test/smoke.mjs                   # 离线自测，无需密钥
curl http://127.0.0.1:8791/v1/health
```
