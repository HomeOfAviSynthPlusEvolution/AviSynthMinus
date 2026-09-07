# AviSynthMinus

[English](README.md) | **简体中文** | [日本語](README.ja.md)

AviSynthMinus 是一个基于 AviSynth+ 的音视频帧服务器分支。它通过脚本组织剪辑、滤镜处理和格式转换，并按需向编码器、播放器等应用程序提供处理后的音视频，无需先生成完整的中间文件。

项目采用独立的稳定版与主线版发布策略，让保守的修复能够及时发布，同时为新功能和较大的内部改进提供持续开发的空间。我们重视自动化测试，并以保持现有 AviSynth 脚本和插件的兼容性为目标，继续跟进和吸收上游的相关改进。

## 为什么创建这个分支

AviSynthMinus 的创建源于对开发和发布流程的不同取舍。AviSynth+ 的上游开发主要在同一条主分支上推进，修复、新功能和较大的内部变更随之累积。当正式发布间隔较长时，用户也需要等待较长时间才能通过正式版本获得修复。我们希望将稳定版维护与主线开发分开，使经过验证的修复可以独立发布，而较大的变更能够继续开发和测试。

这个分支会继续跟进 AviSynth+ 的开发，并吸收适合的修复与改进。对于上游正在开发的功能，我们倾向于在其完成后整合，尽量避免形成互不兼容的实现。同时，我们也欢迎上游采用本项目的修复和改进，并保留相应的来源与贡献者署名。

## 发布渠道

AviSynthMinus 计划维护两条发布线，以版本号 `major.minor.patch` 中的次版本号（`minor`）区分：

| 渠道 | 次版本号 | 定位 |
|---|---|---|
| 稳定版（Stable） | 偶数，例如 `0.2.x` | 以问题修复和保守改进为主，适合希望减少行为变化的用户。 |
| 主线版（Mainline） | 奇数，例如 `0.1.x`、`0.3.x` | 引入新功能和较大的内部变更，适合愿意尝试新进展并提供反馈的用户。 |

发布渠道与预发布状态分别标识：主线版不一定是预发布版，稳定版也可能先提供候选版本供测试。具体状态、变更内容和已知问题请查看对应版本的发布说明。

我们倾向于小规模、较频繁的发布，让有价值的修复尽早交付。发布不设固定周期，具体时间取决于变更的准备情况与维护者的可用时间。

## 下载与安装

请前往 [Releases](https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/releases) 选择版本，并阅读对应的变更说明、安装要求和已知问题。

Windows 用户可以根据需要选择 mod 安装器或文件包。当前 mod 安装器适用于 Windows 10 及以上的 x64 系统，需要预先安装官方 AviSynth+ 3.7.5。它会替换已安装的 x86 和／或 x64 核心，保留现有插件及其他组件，不会补装缺失的架构。

mod 安装器会备份被替换的核心，并在卸载时检查是否可以恢复。安装或卸载前，请关闭正在使用 AviSynth 的应用程序；升级或卸载官方 AviSynth+ 前，请先卸载 mod。

文件包适合手动部署或自行管理运行环境的用户。Windows、Linux 和 macOS 的具体可用架构及运行依赖，以对应版本提供的下载文件和发布说明为准。选择架构时，应与加载 AviSynth 的应用程序保持一致。

## 兼容性与平台

AviSynthMinus 以保持现有 AviSynth 脚本和插件的兼容性为目标，并尽可能维持与 AviSynth+ 的 API 和 ABI 兼容。兼容性并不意味着冻结内部实现；我们会在改进架构和行为时评估对现有用法的影响，并在发布说明中明确记录必要的不兼容变化及迁移方式。

项目面向 Windows、Linux 和 macOS，但不同操作系统与处理器架构的验证程度可能不同。自动构建成功表示代码能够在对应环境中编译，不代表所有脚本、插件和宿主应用都已通过运行验证。插件和宿主应用也有各自的平台、架构及运行环境要求。

欢迎提交兼容性问题报告。请提供 AviSynthMinus 版本、操作系统与架构、宿主应用和相关插件版本，以及能够复现问题的最小脚本；如果同一用法在 AviSynth+ 中表现不同，也请注明用于对比的版本和结果。

## 快速开始

安装完成后，新建一个名为 `version.avs` 的纯文本文件，内容如下：

```avs
Version()
```

使用支持加载 AviSynth 脚本的播放器、编辑器或编码器打开该文件。正常情况下，它会生成显示版本信息的视频，可用于确认宿主应用实际加载的核心版本。这个示例无需外部媒体文件或额外插件。

`.avs` 文件描述音视频的处理过程，由宿主应用加载并执行。处理自己的媒体文件时，需要根据输入格式选择合适的源滤镜或插件，再添加剪辑、缩放等处理步骤。

## 构建与测试

项目使用 CMake 构建，需要支持 C++17 的编译器。以下命令在仓库根目录执行，仅构建核心库，不编译仓库附带的外部插件；生成的核心库仍可正常加载兼容的插件。

**Windows（Visual Studio 2026，x64）：**

```powershell
cmake -S . -B build/core -G "Visual Studio 18 2026" -A x64 -DBUILD_SHARED_LIBS=ON -DENABLE_TESTS=OFF -DENABLE_PLUGINS=OFF
cmake --build build/core --config Release --parallel
```

Windows 下也可以使用 clang-cl 编译。在部分处理场景中，clang-cl 构建的性能可能优于 MSVC 构建；具体表现取决于编译器版本、编译选项和处理内容，建议使用实际工作负载进行比较。

**Linux / macOS（需要 Ninja 和相应的 C++ 工具链）：**

```sh
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DENABLE_TESTS=OFF -DENABLE_PLUGINS=OFF
cmake --build build/core --parallel
```

**运行测试：**

仓库提供面向 Visual Studio 2026 x64 的测试预设。测试配置使用静态核心库，首次配置时会通过 Git 下载 GoogleTest 和 xxHash，因此需要安装 Git 并能够访问依赖仓库。

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release
```

其他工具链可以在独立构建目录中使用 `-DENABLE_TESTS=ON -DBUILD_SHARED_LIBS=OFF -DENABLE_PLUGINS=OFF` 配置测试，再通过 CTest 运行。测试覆盖核心与脚本行为、音视频转换、滤镜和回归用例；具体覆盖范围随开发持续调整。

## 开发与贡献

AviSynthMinus 目前由维护者主导开发，维护者负责技术方向、变更取舍和最终发布。我们欢迎问题报告、改进建议、代码贡献和技术讨论；涉及新功能、兼容性变化或较大架构调整时，建议先通过 Issue 讨论目标与方案。

本项目使用 AI 辅助开发，包括代码实现、测试编写和代码审查。维护者提供指导、评估变更，并对最终接受和发布的内容负责。贡献应清楚说明解决的问题、实现思路和验证方式；使用 AI 参与贡献时，也请如实说明其参与情况。

报告问题请使用 [GitHub Issues](https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/issues)，并尽量提供可复现的示例。提交修复时，鼓励附上能够复现原问题并验证修复的回归测试。技术分歧可以公开讨论，项目方向和是否合并由维护者最终决定。

## 文档、致谢与许可

仓库附带的[使用文档](distrib/docs/english/source/avisynthdoc/index.rst)和[插件开发资料](distrib/docs/english/source/avisynthdoc/FilterSDK)主要继承自 AviSynth+，目前以英文为主。其中部分内容可能尚未反映本分支的变化，请结合对应版本的发布说明阅读。

AviSynthMinus 建立在 AviSynth、AviSynth+ 及其贡献者的长期工作之上。自动化测试移植自 [AviSynthPlus-UT](https://github.com/HomeOfAviSynthPlusEvolution/AviSynthPlus-UT)，并在本项目中持续扩展。感谢上游开发者、插件作者，以及参与测试、报告问题和贡献改进的用户。

感谢 [烧饼论坛](https://sb.sb) 赞助本项目开发所使用的 LLM 订阅。

许可条款请参阅仓库中的[许可说明](distrib/docs/english/source/avisynthdoc/license.rst)和 [GPL 全文](distrib/gpl.txt)。部分接口包含额外的许可例外，第三方组件也可能采用不同许可，具体以相应文件中的声明为准。
