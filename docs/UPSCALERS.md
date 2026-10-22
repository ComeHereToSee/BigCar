# DLSS / FSR / XeSS 超分辨率与帧生成

BigCar 的画质菜单内置 Epic 自带的 **TSR / TAA / FXAA**，并支持三家显卡厂商的超分插件：

| 选项 | 厂商 | 插件（放到 `Plugins/` 下的目录名） | 帧生成 | 低延迟 |
|---|---|---|---|---|
| DLSS（超分 / DLAA） | NVIDIA | `DLSS`、`StreamlineCore`、`StreamlineDLSSG`、`StreamlineReflex` | DLSS 帧生成（RTX 40/50 系） | NVIDIA Reflex |
| FSR 4 | AMD | `FSR4` | FSR 帧插值 | — |
| FSR 3.1 | AMD | `FSR3` | FSR 帧插值 | — |
| XeSS | Intel | `XeSS` | — | — |

每个超分选项都有 原生(DLAA) / 质量 / 平衡 / 性能 / 超级性能 档位，另有独立的锐化滑条。

## 为什么插件不在仓库里

这些插件由 NVIDIA、AMD、Intel 按各自的许可证单独分发，体积大（含 DLL），且每个 UE 版本对应不同的插件版本，所以不提交到仓库（`.gitignore` 已忽略）。
`BigCar.uproject` 已把它们登记为 **可选插件**（`"Optional": true`）：

* 插件不存在时，编辑器和 UnrealBuildTool 会静默跳过，工程照常编译运行，菜单中只显示 TSR / TAA / FXAA；
* 插件存在时自动启用，游戏启动时检测到对应的控制台变量（如 `r.NGX.DLSS.Enable`）就会在菜单中显示该选项；
* 显卡不支持（例如在 AMD 显卡上选 DLSS）时插件不会注册其变量，选项同样自动隐藏；
* 首次启动时按 DLSS → FSR 4 → XeSS → FSR → FSR 3 的顺序选择第一个可用的超分并设为“质量”档。

## 安装步骤（UE 5.5）

1. 下载与 UE 5.5 匹配的插件版本：
   * NVIDIA：<https://developer.nvidia.com/rtx/dlss/get-started>（“DLSS 4 Plugin for Unreal Engine 5.5”）；
   * AMD：<https://gpuopen.com/learn/ue-fsr/>（FidelityFX Super Resolution Unreal Engine 插件）；
   * Intel：<https://github.com/GameTechDev/XeSSUnrealPlugin/releases>。
2. 把压缩包里的插件目录复制到工程的 `Plugins/` 下，例如：
   ```
   Plugins/DLSS/DLSS.uplugin
   Plugins/StreamlineCore/StreamlineCore.uplugin
   Plugins/StreamlineDLSSG/StreamlineDLSSG.uplugin
   Plugins/StreamlineReflex/StreamlineReflex.uplugin
   Plugins/FSR3/FSR3.uplugin
   Plugins/XeSS/XeSS.uplugin
   ```
3. 重新生成工程文件并编译（或交给 CI 的 Windows 运行器打包，见 [BUILD_CI.md](BUILD_CI.md#23-windows-版本)）。

> 云端 Windows 打包机需要在其工作目录或引擎的 `Engine/Plugins/Marketplace/` 下放置这些插件；Linux 构建不需要。

## 实现方式（给开发者）

* 所有画质项、预设、超分与帧生成的定义都在数据文件 `Content/BigCar/Data/graphics.json` 中，由 `Tools/gen_graphics.py` 生成；
  每个超分条目包含检测用的控制台变量（`detectCvar`）、启用/关闭时设置的变量，以及各档位的分辨率百分比或厂商质量档变量。
* `CrashCore::ResolveGraphics` 把玩家设置解析为最终的引擎设置（可缩放性组、控制台变量、屏幕百分比、后期开关），并处理互斥关系
  （例如：选择 DLSS 时其他超分及其帧生成全部关闭；帧生成只在所选超分支持时生效；所选超分不可用时自动回退到 TSR）。这部分逻辑有单元测试覆盖。
* `UBCSettingsSubsystem` 在游戏中应用解析结果，并存入用户配置。
* 厂商若在新版本插件中改了控制台变量名，只需修改 `Tools/gen_graphics.py` 并重新生成 `graphics.json`，不需要改 C++ 代码。
