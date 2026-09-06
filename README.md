# MB 评测器 (OJ Evaluator)

一个基于 Qt6 的本地 C++ OJ 评测器：对单个 C++ 源文件做**多组数据评测**，逐组输出 CPU/OJ 用时、峰值内存与 AC/WA/TLE/MLE 判定，并以自适应表格汇总、给出综合评价。

## 特性

- **多组评测**：自动探测同目录下 `aaa-1.in / aaa-1.out`、`aaa_2.in / aaa_2.out`（支持 `-` 与 `_` 分隔符混用，最多 100 组）逐组运行并汇总；无编号的单样例 `aaa.in / aaa.out` 亦可作为 1 组。
- **源码文件流自动改写**：`aaa.cpp` 内 `freopen("source.in", ...)` / `freopen("source.out", ...)` 会被自动改写为与源文件同名（`aaa.in` / `aaa.out`）。
- **零开销双时限**：基于 Windows Job Object —— `JOB_OBJECT_LIMIT_PROCESS_TIME` 做 CPU 软限即杀，`WaitForSingleObject` 单次阻塞等待做墙钟硬限（防休眠/IO 卡死），**无轮询开销**。
- **内存动态限制 + 超限杀死**：`JOB_OBJECT_LIMIT_JOB_MEMORY` 内存硬线，超限分配失败/进程被杀；峰值内存由 `PeakJobMemoryUsed` 一次性测量，超过软限判 MLE。
- **参考机因子**：单一因子 `refFactor = IPC × GHz`（默认 3）定义参考算力，`refIPS = refFactor × 1e8`；经 4 域（整型/浮点/内存/分支）指令吞吐校准，把本地用户态 CPU 时间折算为 OJ 预估用时。
- **自适应宽度表格**：评测结果以表格输出，列宽随内容自适应（CJK 按双列宽计）。
- **设置子窗口 + 持久化**：参考机因子、TLE/MEM 硬线比例、热键等配置持久化到 `./mboj_config.json`（当前目录不可写时回退 `%AppData%`，双写镜像、按 `savedAt` 取较新者同步）。
- **Mica 界面**：主窗口使用 Windows 11 Mica 背景 + 半透卡片。
- **独立 selftest 工具**：`agent-use/selftest.exe`（静态链接、内嵌用例）可脱离 GUI 回归评测链路。

## 构建

要求：Qt 6 (Widgets)、C++17、Windows。

```powershell
# 方式一: 一键脚本 (MSVC/MinGW + windeployqt)
powershell -ExecutionPolicy Bypass -File rele-build.ps1 -VerboseLog

# 方式二: qmake + make
qmake QT.pro CONFIG+=release
make          # 或 nmake / jom / mingw32-make
```

## 使用

1. 启动 `QT.exe`。
2. 选择 `.cpp` 源文件（如 `aaa.cpp`）。
3. 设置 **OJ 时限(ms)** 与 **内存软限(MB)**（绑核与编译参数可调）。
4. 点击「开始评测」，查看逐组表格与综合评价。

测试数据放在源文件同目录，命名如下：

```
aaa.cpp
aaa-1.in   aaa-1.out
aaa-2.in   aaa_2.out      # 支持 - 与 _ 混用
...
aaa-100.in aaa_100.out    # 最多 100 组
```

## 设置

主窗口「设置」打开设置子窗口（非模态、仅关闭按钮）：

- **参考机因子** (IPC×GHz，默认 3，参考指令数 = 因子 × 1e8)
- **TLE 硬线比例**（墙钟硬时限 = CPU 软限 × 比例，默认 1.5）
- **MEM 硬线比例**（内存硬杀线 = 软限 × 比例，默认 1.5）
- **热键**：开始评测 (默认 F5) / 浏览文件 (默认 Ctrl+O) / 打开设置 (默认 Ctrl+,)

配置写入 `./mboj_config.json`；若当前目录不可写则回退 `%AppData%`，两处双写、取较新者同步。

## 独立 selftest 工具

静态链接、内嵌测试用例的 `selftest.exe`（位于 `agent-use/selftest-package/`），可在任意 Win10+ 设备直接运行并生成结果 log：

```powershell
selftest.exe --self-contained            # 用内嵌用例, 当前目录生成 selftest_result.txt
selftest.exe <用例目录>                  # 或指定目录
```

退出码 0 表示全部通过；目标设备需可用的 g++（`toolchain/bin/g++.exe` 或 PATH）。

## 目录结构

```
main.cpp               GUI（Mica 主窗 + 设置子窗 + 输入框样式）
evaluator.cpp/.hpp     编译/运行/判题核心（Job Object 零开销限制、多组探测）
md.cpp/.hpp            OJTimer 速度因子校准、CPU/内存计时
selftest.cpp/.hpp      无头自测
testcases/             用例（AplusB / Gcd / TimeLimit / SleepTest / MemLimit / MemTest ...）
agent-use/             开发期辅助文件（git 忽略）
```

## 判定模型

- **AC / WA**：结束后比对程序输出 `aaa.out` 与标准答案 `aaa.ans`。
- **TLE-CPU**：进程用户态 CPU 时间 ≥ OJ 时限折算的本地软限（Job Object 到点即杀）。
- **TLE-WALL**：墙钟超过「CPU 软限 × TLE 硬线比例」（捕获休眠/IO 卡死）。
- **MLE**：峰值提交内存（`PeakJobMemoryUsed`）> 内存软限；进程还会被「软限 × MEM 硬线比例」的硬线限制动态约束。

> 说明：参考机因子默认 3 意味着本机若快于参考机，本地时限会相应缩短（速度因子由 4 域校准得到）。
