# CompleTime

**CompleTime** 是一个基于 LLVM 的静态分析框架，将**代码复杂度分析**与**常数时间（Constant-Time）安全验证**集成在同一套工具链中。它帮助开发者在理解程序结构复杂性的同时，检测潜在的时序旁路泄漏风险。

---

## 项目简介

- **ComplexityAnalysisPass**：多维度复杂度分析器，计算 McCabe 圈复杂度、Halstead 度量、循环与嵌套深度，并输出 0–100 的综合复杂度评分。
- **CT-LLVM**：流敏感、过程间的污点分析，检测通过缓存、分支、可变时序三种信道产生的泄漏。

两者结合，让你不仅能回答"这个函数是否泄漏秘密？"，还能回答"这个函数有多复杂，其复杂性是否隐藏了潜在的安全漏洞？"

---

## 仓库结构

```
CompleTime/
├── ctllvm.cpp                  # CT-LLVM：常数时间污点分析 Pass
├── ComplexityAnalysisPass.cpp  # 复杂度分析 Pass 实现
├── ComplexityAnalysisPass.h    # 复杂度分析 Pass 头文件
├── main.c                      # pass的最小测试样例
├── compile.bash                # 构建脚本：编译插件并在 main.c 上运行测试
├── extract_bitcode.py          # 工具脚本：从二进制文件中提取 LLVM 位码
├── LICENSE.txt                 # Apache 2.0 许可证
└── README.md
```

---

## 功能特性

### CT 分析模块（CT-LLVM）

- **污点传播**：基于 Def-Use 链与别名分析
- **过程间分析**：通过内联跨函数边界追踪污点，内联深度可配置
- **流敏感分析**：减少误报
- **类型系统**：区分高密级（secret）与低密级（public）值
- **Must/May 泄漏报告**：结合调试信息定位到源代码行
- 支持 LLVM 14 至 LLVM 20+

### 复杂度分析模块（ComplexityAnalysisPass）

对每个函数计算以下指标，并输出为 JSON 报告：

| 指标 | 说明 |
|---|---|
| 圈复杂度 | McCabe V(G) = E − N + 2 |
| 最大循环深度 | 基于 LoopInfo 的最深嵌套层次 |
| 最大控制流嵌套深度 | 基于 CFG BFS 的近似值 |
| Halstead 体积 | N × log₂(η) |
| Halstead 难度 | (η₁/2) × (N₂/η₂) |
| Halstead 工作量 | D × V |
| 综合评分 | 各维度加权后的 0–100 分 |
| 指令类型分布 | 每个函数 Top-10 操作码频次 |
| 函数调用图 | 直接调用关系 caller → callee |

**综合评分权重设计：**

| 分项 | 权重 |
|---|---|
| McCabe 圈复杂度 | 35% |
| 循环深度 | 20% |
| Halstead 工作量 | 20% |
| 分支/指令比率 | 15% |
| 嵌套深度 | 10% |

---

## 环境依赖

- **Clang/LLVM ≥ 14**（推荐 LLVM 20）
- 支持 C++17 的编译器

检查是否已安装：

```bash
llvm-config --version
clang --version
```

**Ubuntu 安装方式（如未安装）：**

```bash
sudo apt install llvm clang
```

Ubuntu 20.04 默认提供 LLVM 14。如需更高版本，请参考 [LLVM 官方 apt 源](https://apt.llvm.org/)。

---

## 编译构建

使用提供的脚本一键编译：

```bash
bash compile.bash
```

该脚本会：
1. 将 `ctllvm.cpp` 和 `ComplexityAnalysisPass.cpp` 编译为共享插件 `pass.so`
2. 在 `main.c` 上运行两个 Pass，验证构建是否成功

也可手动编译：

```bash
clang++ -Wno-c++17-extensions -fPIC -shared -o pass.so \
    ctllvm.cpp ComplexityAnalysisPass.cpp \
    `llvm-config --cxxflags --ldflags --system-libs --libs core passes`
```

---

## 使用方式

### 方式一：通过 Clang 直接分析源文件

```bash
clang -O0 -Xclang -disable-O0-optnone -g \
      -fno-inline-functions -fno-unroll-loops \
      -fpass-plugin=./pass.so \
      your_file.c -S -o your_file.s
```

关键编译选项说明：

| 选项 | 作用 |
|---|---|
| `-Xclang -disable-O0-optnone` | 在 `-O0` 下启用 mem2reg，生成 SSA 形式，分析必需 |
| `-fno-unroll-loops` | 保留循环结构，确保循环深度统计准确 |
| `-fno-inline-functions` | 保留过程间结构，便于调用图构建 |
| `-g` | 保留调试信息，使报告能定位到源代码行 |

### 方式二：通过 `opt` 分析 LLVM IR

```bash
opt -load-pass-plugin=./pass.so \
    -passes="ctllvm,complexity-analysis" \
    input.ll -o /dev/null
```

### 方式三：从已编译的二进制文件中提取位码分析

**第一步**：编译时嵌入位码：

```bash
clang -fembed-bitcode your_project.c -o your_binary
```

**第二步**：提取并链接位码（编辑 `extract_bitcode.py`，将 `filename` 改为你的二进制文件名）：

```bash
python3 extract_bitcode.py
# 生成 final.ll
```

**第三步**：运行 CompleTime：

```bash
opt -load-pass-plugin=./pass.so \
    -passes="ctllvm,complexity-analysis" \
    final.ll -o /dev/null
```

---

## 输出说明

### CT 分析输出

实时输出到 **stderr**，包含：

- 函数名与源文件行号（需以 `-g` 编译）
- 泄漏信道类型（Cache / Branch / Variable Timing）
- 是否为 **MUST** 或 **MAY** 泄漏
- 触发泄漏的具体指令

### 复杂度分析输出

写入当前目录下的 `<模块名>_complexity.json`，示例如下：

```json
{
  "模块": "main",
  "平均综合评分": 12.4,
  "最高综合评分": 28.7,
  "最复杂函数": "caller",
  "函数调用图": {
    "caller": ["callee"],
    "callee": []
  },
  "函数列表": [
    {
      "函数名": "example1",
      "综合评分": 9.2,
      "圈复杂度": 1,
      "指令总数": 14,
      "最大循环深度": 0,
      "Halstead工作量": 312.4
    }
  ]
}
```

---

## 参数配置

CT-LLVM 的行为可通过修改 `ctllvm.cpp` 顶部的宏定义进行调整：

| 宏 | 默认值 | 说明 |
|---|---|---|
| `TYPE_SYSTEM` | `1` | 启用基于类型系统的污点追踪 |
| `TEST_PARAMETER` | `1` | 将所有函数参数视为潜在秘密 |
| `ENABLE_MAY_LEAK` | `1` | 同时报告 may-leak（可能泄漏） |
| `SOUNDNESS_MODE` | `1` | 启用健全性模式（过近似分析） |
| `ALIAS_THRESHOLD` | `2000` | 别名分析对数上限，超出则回退 |
| `REPORT_LEAKAGES` | `1` | 是否输出泄漏报告 |
| `INLINE_THRESHOLD` | `10` | 过程间分析的最大内联深度 |
| `FILE_PATH` | `""` | 源文件路径前缀（跨目录分析时使用） |
| `DEBUG` | `0` | 启用详细 IR 及污点传播调试输出 |

---

## 测试样例说明

`main.c` 提供了六组注释完整的测试用例，覆盖 CompleTime 的核心检测场景：

| 样例 | 场景说明 |
|---|---|
| `example1` | 通过秘密衍生指针产生的缓存泄漏（FlowTracker 无法检测） |
| `example2` | 通过秘密数组下标产生的缓存泄漏（CTChecker 无法检测） |
| `example3` | 流敏感分析示例，区分误报与真报 |
| `caller` / `callee` | 跨函数边界的过程间泄漏传播 |
| `example4` | 依赖秘密的条件分支产生的分支泄漏 |
| `example5` | 地址比较（误报）与加载秘密值（真报）的对比 |

---

## 已知限制

- **间接调用与内联汇编**：遇到此类指令时，对应函数会记录错误码而非完整分析；将 `AUTO_CONTINUE` 设为 `1` 可跳过继续分析。
- **循环展开**：编译器展开循环后，循环深度统计会低于实际值，建议使用 `-fno-unroll-loops`。
- **综合评分阈值**：`ComplexityAnalysisPass.cpp` 中的各项满分阈值为经验默认值，可根据具体代码库调整。

---

## 许可证

本项目基于 [Apache License 2.0](LICENSE.txt) 开源。

CT-LLVM 最初由 Zhiyuan Zhang 开发。ComplexityAnalysisPass 及 CompleTime 集成框架在 CT-LLVM 基础上扩展构建。

---

## 引用

如在学术工作中使用 CompleTime，请引用 CT-LLVM 原始论文：

```bibtex
@inproceedings{ctllvm,
  title  = {CT-LLVM: Automatic Large-Scale Constant-Time Analysis},
  author = {Zhiyuan Zhang},
  year   = {2025}
}
```
