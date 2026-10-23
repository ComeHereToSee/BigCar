# BigCar — 以碰撞取胜的 3D 车辆对抗游戏（Unreal Engine 5）

> 不是竞速，也不是驾驶模拟。BigCar 的唯一目标是 **在碰撞中取胜**：正面对撞、T 字侧撞、侧刮、追尾、PIT 甩尾、铲飞、碾压……
> 每一次撞击都按真实动能结算，按部位变形、掉件、爆胎、发动机起火，最后把对手撞成废铁。

[![CI](https://github.com/comeheretosee/bigcar/actions/workflows/ci.yml/badge.svg)](https://github.com/comeheretosee/bigcar/actions/workflows/ci.yml)
[![Unreal](https://github.com/comeheretosee/bigcar/actions/workflows/unreal.yml/badge.svg)](https://github.com/comeheretosee/bigcar/actions/workflows/unreal.yml)

## 游戏内容一览

| 方面 | 内容 |
|---|---|
| 车辆 | 89 台，覆盖 44 个品牌原型（家用轿车、SUV、皮卡、性能车、豪华车、超跑、Hypercar、越野车、卡车……），5 个档次 |
| 碰撞 | 8 种碰撞判定（正碰 / T 字侧撞 / 侧刮 / 追尾 / 铲飞 / 碾压 / PIT 甩尾 / 擦碰），6 个车身分区 + 发动机 + 车轮的独立耐久与护甲，程序化实时车身变形、掉件、碎片、起火爆炸 |
| 改装 | 9 大升级类别 × 5 级（发动机、变速箱、悬挂、轮胎、刹车、底盘结构、装甲、配重、氮气）；47 件近战装备（推土铲、楔形铲、撞角、尖刺轮毂、液压跳跃、电击保险杠……），**全部为接触式，没有任何远程武器** |
| 单机 | 8 个章节 64 个剧情关卡（400 余句剧情对话），每关 3 星挑战；每章一个可自由探索的开放枢纽地图（收集品、隐藏秘密、探索度百分比）；8 场 BOSS 战；33 个竞技场/地图 |
| 模式 | 混战 Derby、击毁赛、团队击毁、团队淘汰、推出擂台、山丘之王、悬赏、生存、破坏、BOSS、自由探索 |
| 联机 | 快速匹配、天梯排位（1v1 / 2v2 / 3v3，Glicko-2 积分 + 段位 + 赛季）、组队（最多 3 人，邀请码）、自定义/局域网房间、专用服务器 |
| 商城 | 金币 / 点券双货币、34 个商品与礼包、每日特惠（6 个随机折扣位）、127 件外观（涂装、轮毂、撞击火花、喇叭、称号、旗帜）、80 级赛季通行证、重复物品自动折算 |
| 画质 | 低 / 中 / 高 / 史诗 / 电影 5 档预设 + 28 项独立开关（阴影、纹理、特效、后期、全局光照、反射、硬件光追、虚拟阴影、体积雾、运动模糊、景深、胶片颗粒、碎片与变形细节……） |
| 超分 | TSR、TAA、FXAA、**NVIDIA DLSS（含帧生成与 Reflex）**、**AMD FSR 4 / FSR 3**、Intel XeSS；按显卡自动检测可用项 |
| 语言 | 简体中文（默认）、English |

> **关于真实车型**：所有车辆都按真实车型的尺寸、重量、功率、扭矩、极速建模，数据中保留 `reference` 字段记录对应的真实车型。
> 由于汽车品牌和车型外观受商标与外观设计保护，未获授权前游戏内显示的是虚构品牌名（`licensed: false`）。
> 拿到厂商授权后只需在 `Content/BigCar/Data/brands.json` 中把对应品牌改为 `licensed: true` 即可显示真名，详见 [docs/GDD.md](docs/GDD.md#真实车型与授权)。

## 仓库结构

```
BigCar.uproject              UE 5.5 工程（纯 C++，无二进制资源依赖）
Source/
  CrashCore/                 引擎无关的核心玩法库（C++20）：车辆物理、碰撞判定与伤害、计分、AI、
                             比赛规则、战役/存档/经济、画质设置解析、程序化音频 DSP
  BigCar/                    UE 游戏模块：车辆 Pawn、可变形车身、竞技场生成、游戏模式、网络同步、
                             Slate UI、在线服务客户端、专用服务器
Content/BigCar/Data/         数据驱动的游戏内容（JSON）：车辆、品牌、升级、装备、外观、竞技场、模式、
                             AI、战役、成长、商城、画质、多语言
Backend/                     在线后端（Go）：账号、云存档、商城与支付、组队、匹配、天梯、专用服务器调度
Tests/Core/                  CrashCore 单元测试（CMake/CTest，无需虚幻引擎）
Tools/                       crashsim 无头对战模拟器、数据校验、画质配置生成
Deploy/                      专用服务器镜像与完整线上环境（docker compose）
.github/workflows/           云端 CI/CD：ci.yml（核心/后端/数据/模拟）、unreal.yml（UE 编译/测试/打包）
docs/                        设计文档、数据规范、后端接口、云端构建、超分插件
```

## 云端构建与测试（GitHub Actions）

所有编译、测试、打包都在 GitHub Actions 上完成，本地不需要安装任何东西：

| 工作流 | 触发 | 内容 |
|---|---|---|
| `ci.yml` | 每次 push / PR | CrashCore 在 Linux(GCC/Clang) / Windows(MSVC) / macOS 上编译 + 单元测试；ASan/UBSan；代码格式检查；数据校验；无头 AI 对战平衡模拟；Go 后端测试（含 PostgreSQL 集成测试）；后端 Docker 镜像发布到 GHCR |
| `unreal.yml` | 改动 UE 代码/配置/内容时 | 在 Epic 官方 UE 5.5 容器中编译编辑器、跑 UE 自动化测试（无头）；main 分支上额外烹饪打包 Linux 客户端 + 专用服务器，并发布服务器 Docker 镜像 |

`unreal.yml` 需要一次性配置 Epic 账号关联和两个仓库密钥，步骤见 **[docs/BUILD_CI.md](docs/BUILD_CI.md)**。
未配置时该工作流会给出提示并直接通过，不影响其他检查。

## 本地快速体验（可选）

```bash
# 核心库 + 单元测试 + 无头对战模拟（任何有 CMake 和 C++20 编译器的机器）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/crashsim --data Content/BigCar/Data --mode derby --cars 8 --matches 20

# 在线后端（Go 1.24）
cd Backend && go run ./cmd/bigcar-backend --dev

# 虚幻工程：用 UE 5.5 打开 BigCar.uproject（会提示编译 C++ 模块），或：
#   UnrealEditor BigCar.uproject -game                         主菜单
#   UnrealEditor BigCar.uproject /Engine/Maps/Entry?game=match?Mode=derby?Bots=7 -game
```

默认按键：W/S 油门刹车、A/D 转向、空格手刹、左 Shift 氮气、E 装备技能、R 复位、C 回看、Tab 计分板、Esc 菜单；手柄 RT/LT/左摇杆/X/A/B/Y 等同。

## 文档

* [docs/GDD.md](docs/GDD.md) — 游戏设计：核心循环、碰撞与伤害、车辆、改装、模式、战役与探索、联机、经济与商城、画质
* [docs/BUILD_CI.md](docs/BUILD_CI.md) — 云端构建、测试、打包、发布与服务器部署
* [docs/UPSCALERS.md](docs/UPSCALERS.md) — DLSS / FSR / XeSS 插件安装与工作方式
* [docs/DATA_SCHEMA.md](docs/DATA_SCHEMA.md) — 内容数据格式（加车、加装备、加关卡都在这里）
* [docs/BACKEND_API.md](docs/BACKEND_API.md) — 在线后端 HTTP 接口
* [Backend/README.md](Backend/README.md) — 后端运行与配置

## 当前状态与限制

* **美术与音频是程序化占位**：车身是按真实尺寸程序生成的可变形网格，竞技场由数据生成，音效由合成器实时生成。
  这让整个工程不依赖任何二进制资源即可编译运行；替换为正式模型的方法见 [docs/GDD.md](docs/GDD.md#美术资源替换)。
* 虚幻部分的编译和自动化测试依赖 `unreal.yml`（需要 Epic 账号关联，见上）；引擎无关的核心玩法、物理、碰撞、AI、经济、后端
  已在每次提交时于云端完整测试。
* DLSS / FSR / XeSS 由厂商以插件形式发布，需按 [docs/UPSCALERS.md](docs/UPSCALERS.md) 放入 `Plugins/`；未安装时选项自动隐藏。
* 支付渠道目前是 sandbox（测试支付），上线前需接入真实支付服务商（`Backend/internal/payments` 已预留接口）。
