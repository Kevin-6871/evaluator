# MB 评测器

> **版本**：Final  
> **平台**：Windows 11 (x64)  
> **许可**：MIT  
> **最后更新**：2026-05-17  

---

## 项目简介

**MB 评测器** 是一款开箱即用的 C++ OJ 评测工具。它内置 MinGW-w64 编译器，提供 Win32 图形界面，能一键编译、运行、评测用户的 C++ 程序，并给出接近真实 OJ 环境的 **CPU 时间** 和 **内存用量**。

用户只需双击 `MB_Evaluator.exe`，选择 `.cpp` 源文件，即可自动完成编译、计时、内存统计和答案比对，最终显示 **AC (Accepted)** 或 **WA (Wrong Answer)**。

---

## 核心特性

### 1. 开箱即用
- 单一自解压程序，无需安装任何依赖。
- 双击 `MB_Evaluator.exe` 自动解压到临时目录并启动评测器。
- 内置 MinGW-w64 16.1.0 编译器，支持 C++17。

### 2. 精准计时
- 测量 **用户态 CPU 时间**（`GetProcessTimes`），与 OJ 评测机的计时方式一致。
- 排除 I/O 等待、系统调度等干扰，只统计算法逻辑的纯计算时间。
- 通过 **OJ 算力校准**（`OJTimer`）将本机耗时换算为标准 OJ 环境的预估用时。
- 支持 **CPU 核心绑定**（`CoreBinder`），消除线程迁移带来的计时抖动。

### 3. 内存统计
- 统计 **Private Bytes（专用提交内存）**，接近 OJ 判定 MLE 的标准。
- 峰值内存精确到 0.01 MB。

### 4. 自定义编译参数
- UI 提供编译参数输入框，默认 `-std=c++17 -O2 -Wall`。
- 用户可随时修改，例如 `-O0 -g` 进行调试。

### 5. 窗口自适应
- Win32 原生窗口，支持最大化、缩放。
- 编辑框和输入框随窗口大小自动调整。

### 6. 答案比对
- 自动比对程序实际输出与期望输出（`.out` 文件）。
- 支持 `freopen` 文件读写格式（`.in` / `.out`），符合 CSP/NOI 标准。

### 7. 自解压打包
- 使用 7z SFX 制作单文件安装包。
- 解压到 `%TEMP%\evaluator`，退出后不残留文件。

---

## 快速开始

### 1. 准备被评测程序
用户需要准备三个文件（文件名任意，但必须同名，扩展名不同）：

```
problem.cpp      // C++ 源文件（必须使用 freopen 读写文件）
problem.in       // 输入数据
problem.out      // 期望输出（标准答案）
```

示例 `problem.cpp`：
```cpp
#include <cstdio>
int main() {
    freopen("source.in", "r", stdin);   //目前要求必为source.in
    freopen("source.out", "w", stdout); //目前要求必为source.out
    int n;
    scanf("%d", &n);
    volatile long long sum = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            sum += i * j;
    printf("%lld\n", sum);
    return 0;
}
```

### 2. 运行评测器
- 双击 `MB_Evaluator.exe`。
- 点击 **"选择 .cpp 文件"**，选择 `problem.cpp`。
- 可在 **"编译参数"** 输入框中修改编译选项（默认 `-std=c++17 -O2 -Wall`）。
- 点击 **"开始评测"**。

### 3. 查看结果
评测器会显示：
```
========== 评测开始 ==========
文件已拷贝到 test/ 目录
编译命令: ...
编译成功

速度因子: 2.254
用户态CPU时间: 15.625 ms
标准OJ环境预估用时: 35.214 ms
峰值专用内存: 0.07 MB

========== 结果比对 ==========
答案正确 (AC)
========== 评测结束 ==========
```

---

## 评测指标说明

| 指标 | 含义 | 单位 |
|:---|:---|:---|
| **速度因子** | 本机算力与标准 OJ 机器的比值 | 无 |
| **用户态 CPU 时间** | 程序纯计算消耗的 CPU 时间 | ms |
| **标准 OJ 环境预估用时** | 换算到标准 OJ 机器的预估时间 | ms |
| **峰值专用内存 (Private Bytes)** | 程序峰值专用提交内存 | MB |
| **退出代码** | 0 为正常退出，非 0 为异常 | — |

### 判定结果
- **AC (Accepted)**：程序正常退出，输出与期望完全一致。
- **WA (Wrong Answer)**：程序正常退出，但输出与期望不符。
- **CE (Compile Error)**：编译失败（显示编译器错误信息）。

---

## 编译参数说明

| 参数 | 用途 |
|:---|:---|
| `-std=c++17` | C++17 标准 |
| `-O2` | 优化级别 2 |
| `-Wall` | 显示常见警告 |
| `-O0 -g` | 调试模式（无优化 + 调试符号） |
| `-O3 -march=native` | 极致优化（针对本机 CPU） |

---

## 技术架构

### 核心模块（`md.hpp` / `md.cpp`）
- **`CPUTimer`**：用户态 CPU 计时器（基于 `GetThreadTimes`）
- **`MemTracker`**：内存峰值统计（基于 `GetProcessMemoryInfo`）
- **`CoreBinder`**：CPU 核心绑定（RAII，自动恢复）
- **`OJTimer`**：OJ 算力模拟器（校准 + 速度因子换算）

### 评测流程
1. 用户选择 `.cpp` 文件
2. 拷贝源文件、输入、期望输出到 `test/` 目录
3. 用内置 `g++.exe` 编译 `source.cpp` → `source.exe`
4. 生成 `wrapper.cpp` 并编译为 `wrapper.exe`
5. `wrapper.exe` 通过 `CreateProcess` 启动 `source.exe`，等待结束后用 `GetProcessTimes` 读取用户态 CPU 时间
6. 读取 `wrapper.exe` 输出的 `time_result.txt`
7. 用 `OJTimer` 换算 OJ 预估时间
8. 比对实际输出与期望输出，显示 AC/WA

### 打包流程
1. 用 7-Zip 将所有文件（评测器 + MinGW）打包为 `package.7z`
2. 用 `7zSD LZMA.sfx` + `config.txt` + `package.7z` 合并生成 `MB_Evaluator.exe`
3. 用户双击后自动解压到 `%TEMP%\evaluator` 并运行评测器

---

## 系统要求

- **操作系统**：Windows 10 / 11 (64-bit)
- **内存**：至少 512 MB
- **磁盘空间**：解压后约 300 MB
- **依赖**：无需安装任何运行时库

---
## 常见问题 (FAQ)

### Q: 为什么速度因子不是 1.0？
速度因子取决于你的 CPU 性能。现代 CPU（如 Core Ultra 5 125H）比标准 OJ 机器（旧款 Xeon/i7）快 2-3 倍，所以因子通常在 2.0-2.5 之间。评测器会自动校准，换算出的 OJ 预估时间才有参考价值。

### Q: 支持多测试点吗？
当前版本仅支持单测试点（一个 `.in` + 一个 `.out`）。多测试点功能可通过修改 `evaluator.cpp` 添加循环实现。

### Q: 能评测标准 IO 程序吗？
当前仅支持 `freopen` 文件 IO 格式。如需评测标准 IO（`cin`/`cout`），需修改 `wrapper.cpp` 中的启动方式。

### Q: 如何更新编译器？
从 [winlibs.com](https://winlibs.com/) 下载新版 MinGW-w64，解压后替换 `bin/`、`lib/`、`include/` 等目录，修改 `evaluator.cpp` 中的版本号路径，重新编译评测器，重新打包。

---

## 鸣谢

- **7-Zip**：自解压模块
- **winlibs**：MinGW-w64 便携版编译器
- **Microsoft Windows API**：计时、内存统计、进程管理
- **CSP / NOI**：算法竞赛评测标准参考

---

**MB 评测器** | 让本地评测像 OJ 一样精准

**注**：本项目由deepseek协助完成
