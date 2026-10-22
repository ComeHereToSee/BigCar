# BigCar 在线后端（Go）

BigCar 的在线服务：账号、在线车库/钱包、商店、组队、匹配、排位天梯以及专用服务器（Dedicated Server）分配。
接口契约见 [`docs/BACKEND_API.md`](../docs/BACKEND_API.md)，游戏数据格式见 [`docs/DATA_SCHEMA.md`](../docs/DATA_SCHEMA.md)。
单人战役与局域网/自定义对局完全离线，不会调用本服务。

* Go 1.24，仅依赖标准库与 `github.com/jackc/pgx/v5`（Postgres）。
* 路由使用 Go 1.22+ 的 `net/http` 模式路由，无 Web 框架。
* 未配置数据库时使用内存存储（开发/测试用，重启即丢失数据）。

## 目录结构

```
Backend/
  cmd/bigcar-backend/      入口：参数/环境变量、日志、后台循环、优雅退出、--healthcheck
  internal/apperr/         API 错误码（snake_case）与 HTTP 状态映射
  internal/data/           读取并校验 Content/BigCar/Data/*.json，计算 dataHash；公式（升级价格、XP、奖励、段位）
    testdata/              自带的完整测试数据集（遵循 DATA_SCHEMA.md）
    datatest/              测试辅助：加载 testdata
  internal/profile/        Profile 文档（与游戏本地存档同一 JSON 结构）、车库操作、发放物品、XP/等级、统计
  internal/auth/           游客/注册/登录/绑定、HMAC Token、PBKDF2 密码
  internal/store/          商店报价、每日特惠（PCG32）、金币包结账与 Webhook、赛季通行证
  internal/payments/       支付渠道接口 payments.Provider、sandbox 渠道、X-Signature 校验
  internal/party/          组队（6 位邀请码、最多 3 人、队长移交、准备、踢人）
  internal/matchmaking/    队列、匹配票据、每秒一次的匹配器（可同步调用 Tick(now)）
  internal/servers/        专用服务器注册表（心跳/超时/分配）、对局开始/结果/中止、奖励与 Glicko-2 结算
  internal/rating/         Glicko-2（τ = 0.5，按 Glickman 论文实现）
  internal/ladder/         天梯榜、/me、当前赛季
  internal/storage/        Store 接口；内存实现与 Postgres 实现（内嵌 SQL 迁移）
    storagetest/           Postgres 集成测试辅助（每个测试独立 schema）
  internal/httpapi/        路由、JSON/错误格式、鉴权、服务器密钥、限流、CORS、请求体大小限制、端到端测试
  Dockerfile               多阶段构建（构建上下文为仓库根目录）
  docker-compose.yml       后端 + Postgres
  Makefile                 常用命令
```

## 本地运行

```bash
cd Backend
go run ./cmd/bigcar-backend --dev            # 内存存储、sandbox 支付、宽松 CORS，监听 :8080
# 或使用自带测试数据（真实数据尚未就绪时）：
go run ./cmd/bigcar-backend --dev --data-dir internal/data/testdata

curl -s localhost:8080/healthz
curl -s -XPOST localhost:8080/v1/auth/guest -d '{"deviceId":"my-device-0001"}'
```

`--dev` 会在未配置时使用固定的开发用密钥（token secret / server key = `dev-server-key` / webhook secret = `dev-webhook-secret`），
**切勿在生产环境使用 `--dev`**。

### 配置

所有参数都可用环境变量设置（参数优先）：

| 参数 | 环境变量 | 默认值 | 说明 |
|---|---|---|---|
| `--addr` | `BIGCAR_ADDR` | `:8080` | 监听地址 |
| `--data-dir` | `BIGCAR_DATA_DIR` | `../Content/BigCar/Data` | 游戏数据目录（相对 `Backend/`） |
| `--database-url` | `BIGCAR_DATABASE_URL` | 空 | Postgres 连接串；为空则使用内存存储 |
| `--token-secret` | `BIGCAR_TOKEN_SECRET` | 无（生产必填，≥ 32 字符） | 玩家 Token 的 HMAC 密钥 |
| `--server-key` | `BIGCAR_SERVER_KEY` | 无（生产必填，≥ 8 字符） | 专用服务器共享密钥（`X-Server-Key`） |
| `--webhook-secret` | `BIGCAR_WEBHOOK_SECRET` | 无 | 支付 Webhook 的 HMAC 密钥（`X-Signature`） |
| `--dev` | `BIGCAR_DEV=1` | 关 | 开发模式：启用 sandbox 支付、宽松 CORS、文本日志 |
| `--trust-proxy` | `BIGCAR_TRUST_PROXY=1` | 关 | 位于反向代理之后时，用 `X-Forwarded-For` 最后一项作为客户端 IP（用于限流） |
| `--log-level` | `BIGCAR_LOG_LEVEL` | `info` | `debug` / `info` / `warn` / `error` |
| `--log-format` | `BIGCAR_LOG_FORMAT` | `json`（`--dev` 时 `text`） | 结构化日志格式（`log/slog`） |
| `--healthcheck` | – | – | 请求本机 `/healthz` 后退出（容器健康检查用） |

### Postgres

设置 `--database-url` 后，启动时自动执行内嵌的 SQL 迁移（`internal/storage/migrations/*.sql`，用 advisory lock 防止多副本并发迁移）。

* `accounts`：账号（设备号 / 用户名 + 密码哈希）
* `profiles`：Profile 文档（JSONB）+ `version` 列
* `ratings`：天梯投影表（每次保存 Profile 时同步），带排行索引
* `orders`：金币订单；`idempotency_keys`：幂等记录（每小时清理 24 小时前的记录）；`match_results`：已结算对局（主键保证只结算一次）

并发安全：所有修改 Profile 的操作都在事务内用 `SELECT … FOR UPDATE` 锁定该行，并在更新时校验 `version`；
序列化失败 / 死锁 / 版本冲突会自动重试。因此并发购买不会重复扣款（有测试覆盖）。

### Docker

镜像会打包 `Content/BigCar/Data`，所以 **构建上下文是仓库根目录**：

```bash
# 在仓库根目录
docker build -f Backend/Dockerfile -t bigcar-backend .

# 或在 Backend/ 目录用 compose 启动后端 + Postgres
cd Backend && docker compose up --build
```

`Backend/Dockerfile.dockerignore`（BuildKit）只把 `Backend/` 和 `Content/BigCar/Data/` 发送给构建器。运行镜像为 distroless（nonroot），
内置 `HEALTHCHECK`（`bigcar-backend --healthcheck`）。

## 测试

```bash
cd Backend
gofmt -l .                 # 必须无输出
go vet ./...
go test -race ./...        # CI 同样执行这三步（或 make check）

# Postgres 集成测试（存储一致性测试 + 完整 HTTP 端到端流程），未设置变量时自动跳过：
BIGCAR_TEST_DATABASE_URL='postgres://bigcar:bigcar@127.0.0.1:5432/bigcar?sslmode=disable' go test -race ./...
```

* 单元测试使用 `internal/data/testdata` 中的自带数据集。
* `TestRealDataDir` 会校验真实数据目录 `../Content/BigCar/Data`；目录或任一必需文件缺失时跳过。
* `internal/httpapi/e2e_test.go`：用 `httptest.Server` 走完整流程——两个游客、购买/升级/装备、组队、`ranked_1v1` 排队、
  模拟专用服务器心跳领取分配、match-start、match-result，检查奖励、评分与天梯；以及中止对局、限流、并发购买等。

## 与 Unreal 专用服务器对接

所有服务器接口都需要请求头 `X-Server-Key: <BIGCAR_SERVER_KEY>`。一个服务器进程只承载一场对局：
启动 → `idle` 心跳 → 收到分配 → 对局 → 上报结果 → 进程退出（由编排器重启）。

1. **心跳**（每 5 秒）：

   ```http
   POST /v1/servers/heartbeat
   X-Server-Key: ...
   {"serverId":"cn-east-7f3a","address":"203.0.113.7","port":7777,"region":"cn-east","version":"1.0.0","state":"idle"}
   ```

   * `serverId` 需在进程生命周期内保持不变（1–64 个 `A-Z a-z 0-9 _ . : -` 字符）；`address`/`port` 是客户端连接用的公网地址。
   * 超过 20 秒没有心跳的服务器会被移除，其上未结束的对局按 `server_lost` 中止。
   * 响应 `{"assignment": null}` 表示空闲；匹配成功后响应中会带上 `Assignment`（含每个玩家的 `joinToken` 与 `loadout`）。
2. **确认分配**：收到分配后，后续心跳改为 `"state":"reserved","matchId":"m_…"`（开赛后为 `in_match`）。
   在确认之前分配会在每次 `idle` 心跳中重复下发；确认之后若又上报 `idle`，后端视为该服务器丢失对局并中止它。
   30 秒内未确认的分配也会被中止。
3. **玩家进入**：客户端执行 `open <address>?MatchId=<id>?JoinToken=<token>?PlayerId=<pid>`，服务器用分配中的 `joinToken` 校验。
   `fillWithBots: true` 时，空位（以及 `maxWaitSec` 内未连上的玩家）由 `botAi` 难度的 AI 补齐；`teams` 中空数组即为 AI 位。
4. **开赛**：`POST /v1/servers/match-start {"matchId":"m_…"}`。
5. **结果**：`POST /v1/servers/match-result`（`MatchResult`，见 API 文档），返回 `{"rewards":[…]}`。
6. **中止**：`POST /v1/servers/match-abort {"matchId":"m_…","reason":"crash"}`，该对局的票据变为 `cancelled`（`reason: "server_aborted"`），玩家可重新排队。

**服务器身份**：match-start / match-result / match-abort 必须带上服务器 ID——请求体字段 `"serverId"` 或请求头 `X-Server-Id`
（两者都给时必须一致）。后端据此保证“只有被分配的服务器才能上报、且只能上报一次”。

结果校验：只接受分配名单内的玩家（名单外的条目，例如 AI，会被忽略；重复条目返回 400），队伍以分配为准，
数值被截断（`score ≤ 200000`、`takedowns ≤ 100` 等），名单中缺席的玩家按掉线处理（无胜利加成，排位记负）。

## 实现说明

* **Token**：`v1.<b64url(payload)>.<b64url(sig)>`，payload `{"pid","exp"}`，`sig = HMAC-SHA256(secret, "v1." + b64url(payload))`，无填充 base64url，有效期 7 天。
* **密码**：PBKDF2-HMAC-SHA256，210 000 次迭代，16 字节盐，存储格式 `pbkdf2_sha256$<iter>$<salt>$<key>`。
* **幂等**：所有修改 Profile 的 POST 都接受 `Idempotency-Key`（≤ 128 字符）；首次成功响应与修改在同一事务中保存，重复请求原样返回
  （响应头 `Idempotent-Replayed: true`）；同一个 key 用于不同请求返回 409。失败的请求不记录。金币结账的订单号由 (玩家, key) 派生，重复结账返回同一订单。
* **每日特惠**：UTC 日序号 `d = floor(unix/86400)`，PCG32（PCG-XSH-RR 64/32，与 `pcg_basic.c` 一致）以 `initstate = d, initseq = 54` 播种；
  对 `i = 0..slots-1` 依次：`j = i + bounded(len(pool) - i)`，交换 `idx[i], idx[j]`，再 `discount = discounts[bounded(len(discounts))]`；
  价格 `roundTo100(基础价 × (1 − discount))` 信用点。详见 `internal/store/pcg32.go` 与 `store.go`。
* **dataHash**：等于在数据目录执行 `sha256sum <已加载文件按文件名排序> | sha256sum` 的结果（必需的 9 个文件，存在时加上 `brands.json`）。
* **限流**：玩家接口每玩家 20 次/秒（突发 40）；认证接口每 IP 5 次/秒（突发 10）；公开接口每 IP 20 次/秒。超限返回 429 `rate_limited`（带 `Retry-After`）。
* **请求体**：普通请求 ≤ 64 KiB，对局结果 ≤ 1 MiB；请求中的未知字段会被忽略（向前兼容）。
* **错误**：`{"error":{"code":"…","message":"…"}}`。HTTP 状态：`bad_request`/`invalid_slot` 400、`unauthorized` 401、
  `insufficient_funds` 402、`forbidden`/`not_owned`/`level_locked`/`not_leader` 403、`not_found` 404、
  `conflict`/`already_owned`/`max_level`/`limit_reached`/`party_full`/`not_ready` 409、`rate_limited` 429、`internal` 500。
* **状态范围**：组队、匹配票据、服务器注册表都保存在进程内存中（单实例部署）。后端重启后玩家需重新组队/排队，
  进行中的对局无法再上报结果。多实例部署需要把这些状态迁移到共享存储（如 Redis）。
