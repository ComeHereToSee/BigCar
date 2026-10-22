# 云端构建、测试、打包与部署

BigCar 的所有编译、测试、打包都在 GitHub Actions 上进行。仓库里有两条工作流：

| 工作流 | 触发 | 是否需要额外配置 |
|---|---|---|
| [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) | 每次 push、每个 PR | 不需要 |
| [`.github/workflows/unreal.yml`](../.github/workflows/unreal.yml) | main 分支 push；改动 `Source/`、`Config/`、`Content/`、`BigCar.uproject` 的 PR；手动触发 | 需要关联 Epic 账号（一次性） |

## 1. ci.yml —— 每次提交都会跑

| Job | 内容 |
|---|---|
| `core` | CrashCore（车辆物理、碰撞判定、伤害、计分、AI、比赛规则、存档、经济、画质设置、音频 DSP）在 Ubuntu GCC、Ubuntu Clang、Windows MSVC、macOS AppleClang 上以 *警告即错误* 编译，运行全部单元测试 |
| `core-sanitizers` | 用 AddressSanitizer + UndefinedBehaviorSanitizer 重新跑一遍单元测试和模拟器 |
| `format` | `clang-format` 检查 C++ 代码风格 |
| `simulation` | `crashsim` 无头 AI 对战：所有模式各打若干局，检查比赛能正常结束、有碰撞与击毁、数值有限；同档次车辆 1v1 胜率平衡报告（写入 Actions 摘要页） |
| `data` | `Tools/validate_data.py` 校验 `Content/BigCar/Data` 下全部 JSON（交叉引用、数值范围、禁止远程装备、本地化键齐全等） |
| `backend` | Go 后端 `go vet` + `go test -race`，并启动 PostgreSQL 服务跑数据库集成测试 |
| `backend-image` | 构建后端 Docker 镜像；在 main 分支上发布到 `ghcr.io/<owner>/bigcar-backend` |

## 2. unreal.yml —— 虚幻引擎编译、自动化测试、打包

使用 Epic 官方的 Linux 开发容器 `ghcr.io/epicgames/unreal-engine`，在云端完成：

1. 编译 `BigCarEditor`（编辑器目标，包含 BigCar 与 CrashCore 两个模块）；
2. 无头运行 UE 自动化测试（`Automation RunTests BigCar`，`-nullrhi`），测试报告作为构件上传，失败的用例会直接标注在 Actions 页面；
3. （main 分支 / 手动触发）`BuildCookRun` 烹饪并打包 **Linux 客户端** 和 **Linux 专用服务器**，上传为构件；
4. （main 分支）用打包好的服务器构建 Docker 镜像，发布到 `ghcr.io/<owner>/bigcar-server`。

### 2.1 一次性配置：获取 Epic 官方容器的访问权限

Epic 的引擎容器只对关联了 Epic Games 账号的 GitHub 用户开放：

1. 登录 [Epic Games 账号](https://www.epicgames.com/account/connections) → **应用和账户（Connections）** → 关联你的 GitHub 账号；
2. 在 GitHub 上接受 **EpicGames** 组织发来的邀请（邮件或 <https://github.com/EpicGames> 页面）；
3. 在 GitHub → Settings → Developer settings → Personal access tokens (classic) 创建一个 **classic** 令牌，只勾选 `read:packages`；
4. 打开本仓库 → Settings → Secrets and variables → Actions：
   * **Secrets** 新建 `UE_GHCR_USER` = 你的 GitHub 用户名，`UE_GHCR_TOKEN` = 上一步的令牌；
   * （可选）**Variables** 新建：
     * `UE_IMAGE` —— 引擎镜像，默认 `ghcr.io/epicgames/unreal-engine:dev-slim-5.5.4`；Epic 的标签格式为 `dev-slim-<完整版本号>`；
     * `UE_RUNNER` —— 运行器标签，默认 `ubuntu-latest`（见下文）；
     * `UE_WINDOWS_RUNNER` = `true` —— 额外在自托管 Windows 机器上打包 Win64 版本（见 2.3）。

配置完成后到 Actions → Unreal → Run workflow 手动跑一次即可。没有配置密钥时，这条工作流会输出配置提示并直接通过，不会让 PR 变红。

> 访问 Epic 的引擎容器需要遵守 [Unreal Engine EULA](https://www.unrealengine.com/eula)；镜像只在 CI 运行期间拉取使用，不会被重新发布。

### 2.2 运行器（Runner）规格

UE 开发镜像解压后约 50–80 GB，编译 + 烹饪还需要 30 GB 以上空间和较多内存：

| 运行器 | 说明 |
|---|---|
| GitHub 标准 `ubuntu-latest`（4 核 / 16 GB / ~14 GB 可用磁盘 + /mnt 临时盘） | 工作流会先清理预装软件并把 Docker 存储挪到 `/mnt`，**可能**勉强够用，但很慢且有磁盘不足的风险 |
| **GitHub 大型运行器**（推荐，例如 16 核 / 64 GB / 600 GB SSD） | 在组织设置 → Actions → Runners 新建大型运行器，把它的标签填进变量 `UE_RUNNER`。编辑器编译约 20–40 分钟 |
| 自托管 Linux 运行器 | 装好 Docker、≥ 200 GB 磁盘即可；`UE_RUNNER` 填它的标签。镜像只拉取一次，之后构建最快 |

### 2.3 Windows 版本

Epic 只提供 Linux 容器，Windows 版本需要一台安装了 UE 5.5（Epic 启动器或源码版）和 Visual Studio 2022 的 Windows 机器作为自托管运行器：

1. 仓库 Settings → Actions → Runners → New self-hosted runner（Windows），按提示安装，并添加标签 `windows`、`unreal`；
2. 如引擎不在默认目录 `C:\Program Files\Epic Games\UE_5.5`，给运行器设置环境变量 `UE_ROOT`；
3. 仓库变量 `UE_WINDOWS_RUNNER` 设为 `true`。

之后每次 main 分支更新都会打包 Shipping 版 `BigCar-Win64.zip` 并作为构件上传。DLSS / FSR / XeSS 插件需要预先放到该机器的仓库工作目录或引擎插件目录（见 [UPSCALERS.md](UPSCALERS.md)）。

### 2.4 构件（Artifacts）

| 名称 | 内容 |
|---|---|
| `unreal-linux-logs` | 编辑器编译日志、自动化测试日志与 HTML/JSON 测试报告、打包日志 |
| `unreal-linux-packages` | `BigCar-Linux.tar.gz`（客户端）、`BigCar-LinuxServer.tar.gz`（专用服务器） |
| `unreal-win64` | `BigCar-Win64.zip`（仅在启用 Windows 运行器时） |

## 3. 线上环境部署（后端 + 专用服务器）

`Deploy/docker-compose.yml` 启动完整的在线环境：PostgreSQL、后端（账号/商城/组队/匹配/天梯）以及 3 个专用游戏服务器进程。
镜像默认使用上面两条工作流发布到 GHCR 的 `bigcar-backend` 和 `bigcar-server`。

```bash
cp Deploy/.env.example Deploy/.env      # 填写数据库密码、Token 密钥、服务器密钥、本机公网地址
docker compose -f Deploy/docker-compose.yml --env-file Deploy/.env up -d
```

* 后端监听 `8080/tcp`（建议前面加 HTTPS 反向代理，并设置 `BIGCAR_TRUST_PROXY=1`）；
* 游戏服务器监听 `7777–7779/udp`，启动后每 5 秒向后端心跳，被分配对局后加载对应模式与竞技场，等待匹配到的玩家加入，比赛结束上报结果（天梯积分、奖励由后端结算）后进程退出，再由 `restart: always` 拉起新的空闲进程；
* 需要更多并发对局时复制 `gameserver-N` 段并换端口，或改用 Kubernetes / Agones 按同样的环境变量部署；
* 游戏客户端通过项目设置 *BigCar Online*（`DefaultEngine.ini` 中的 `[/Script/BigCar.BCOnlineSettings]` → `BackendUrl`）或启动参数 `-BackendUrl=https://...` 指向后端。

GHCR 上的包默认是私有的；部署机器需要 `docker login ghcr.io`（`read:packages` 令牌），或在包设置中把可见性改为 Public。

## 4. 本地复现 CI（可选）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure
python3 Tools/validate_data.py
(cd Backend && go vet ./... && go test ./...)
```
