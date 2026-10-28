# HIXL Benchmarks

简体中文 | [English](./README_en.md)

本目录包含 HIXL 的基准测试工具集，覆盖通信 Benchmark、KV Cache Benchmark 与 HIXL Python Benchmark，用于测量不同配置下的传输性能。

| 工具 | 说明 |
| --- | --- |
| [通信 Benchmark（hixl_comm_bench）](comm_benchmark/README.md) | C++ 通信基准：8 方向 × 多传输类型 block 带宽测量，支持单机/双机 |
| [KV Benchmark（hixl_kv_bench）](kv_benchmark/README.md) | C++ KV Cache 基准：按模型形状（deepseek-r1/glm5/deepseek-v4）与 key 数量测试 put/get 性能 |
| [HIXL Python Benchmark（hixl_benchmark）](hixl_benchmark/README.md) | Python 带宽基准：安装 CANN 整包即可使用，无需编译 |

- [快速开始](#快速开始)
- [目录结构](#目录结构)
- [性能数据](performance.md)

环境准备与 Device 连通性检查参见[通信 Benchmark 环境要求](comm_benchmark/README.md#环境要求)。

## 快速开始

请在**仓库根目录**执行下列命令（相对路径与默认 `output_dir` 均依赖 cwd）。

### 入口说明

| 入口 | 用途 |
| --- | --- |
| `bash benchmarks/run_all_bench.sh` | 单机一键跑全量通信 + KV，生成 `benchmarks/perf.md` |
| `python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py` | 通信基准启动器（单机 / 双机编排） |
| `build/benchmarks/comm_benchmark/hixl_comm_bench` | 直接跑 C++ binary（需自行起 target / initiator） |
| `python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py` | KV Cache 基准 |
| `python3 benchmarks/hixl_benchmark/hixl_benchmark.py` | HIXL Python Benchmark（免编译） |

### 1. 编译

C++ 基准需要编译；HIXL Python Benchmark 无需编译：

```bash
bash build.sh --examples
```

编译后的可执行文件在 `build/benchmarks/` 下：

- `comm_benchmark/hixl_comm_bench` — 通信性能测试
- `kv_benchmark/hixl_kv_bench` — KV Cache 场景测试

### 2. 一键运行全部测试

```bash
bash benchmarks/run_all_bench.sh
```

这个脚本会自动：

- 检查并 source CANN 环境
- 通过 `npu-smi info` 检测芯片型号（A2 / A3 / A5；设备名含 Ascend910B→A2、Ascend910→A3、Ascend950→A5）
- 依次跑完当前平台支持的通信方向 × 传输类型组合
- 运行 KV Cache 基准测试（transport 随平台：A2=`roce`，A3=`fabric_mem`，A5=`ub`）
- 生成 `benchmarks/perf.md` 和折线统计图
- 在终端打印性能结果

跑完后打开 `benchmarks/perf.md` 即可看到结果。

> `run_all_bench.sh` **仅支持单机**。双机请用 `run_comm_benchmark.py --role=target|initiator`（见[通信 Benchmark 文档](comm_benchmark/README.md)）。`--target-host` 已废弃，传入非 `127.0.0.1` 会直接报错退出。

### 3. 自定义参数

```bash
# 自定义通信基准重复轮数和设备
bash benchmarks/run_all_bench.sh --loops 10 --device-ids 0,1,2,3,4,5,6,7

# 跳过 npu-smi，强制指定平台
bash benchmarks/run_all_bench.sh --platform a3

# 指定 perf.md 输出路径
bash benchmarks/run_all_bench.sh --output /tmp/perf.md

# 只跑KV测试
bash benchmarks/run_all_bench.sh --skip-comm

# 只跑通信测试
bash benchmarks/run_all_bench.sh --skip-kv

# 向通信基准传入 HIXL Initialize() 选项（与 hixl_comm_bench 的 -H=KEY=VALUE 一致，可重复）
bash benchmarks/run_all_bench.sh --hixl-option 'LocalCommRes={"version":"1.3"}'
```

### 4. 性能数据汇总

- **`perf.md`**：由 `run_all_bench.sh` 在单平台（A2 / A3 / A5）上自动生成，包含当前平台的性能表格和折线图。
- **`performance.md`**：多平台汇总文档，由开发者手动维护，内容来源于各平台跑出来的 `perf.md`。

## 目录结构

```sh
benchmarks/
├── README.md / README_en.md
├── run_all_bench.sh                        # 一键运行全部测试（入口）
├── run_all_benchmarks.py                   # Python 编排脚本
├── platform_detect.py                      # npu-smi 平台检测
├── kv_defaults.py                          # KV 按平台默认参数
├── benchmark_log.py                        # 日志配置
├── performance.md / performance_en.md      # 多平台汇总（手动维护）
├── performance/
│   └── render_perf_md.py                   # CSV → perf.md 渲染 + 图表
├── comm_benchmark/
│   ├── README.md / README_en.md            # 通信 Benchmark 使用文档
│   ├── hixl_comm_bench.cpp                 # 通信测试主程序
│   ├── common/
│   │   ├── benchmark_config.h/cpp          # 参数配置
│   │   ├── tcp_client_server.h/cpp         # peer TCP 协调
│   │   ├── client_runner.cc                # Initiator 逻辑
│   │   └── server_runner.cc                # Target 逻辑
│   ├── scripts/
│   │   ├── run_comm_benchmark.py           # 启动脚本
│   │   └── plot_comm_benchmark.py          # 画图脚本
│   └── output/                             # 测试输出 (CSV，运行后生成)
├── kv_benchmark/
│   ├── README.md / README_en.md            # KV Benchmark 使用文档
│   ├── hixl_kv_bench.cpp                   # KV 测试主程序
│   ├── kv_transfer_executor.h/cpp          # 传输执行
│   ├── kvstore/
│   │   ├── kvstore.h/cpp                   # KV 存储模拟
│   │   ├── model_config.h/cpp              # 模型配置加载
│   │   ├── segment_manager.h/cpp           # 内存段管理
│   │   └── kv_slice_layout.h/cpp           # slice 布局
│   ├── config/
│   │   └── models.json                     # 模型参数配置
│   ├── scripts/
│   │   ├── run_kv_benchmark.py             # 启动脚本
│   │   └── plot_kv_benchmark.py            # 画图脚本
│   └── output/                             # 测试输出（运行后生成）
└── hixl_benchmark/
    ├── README.md / README_en.md            # HIXL Python Benchmark 使用文档
    ├── bench_lib.py                        # 选项解析 / 协调通道 / 报表
    ├── acl_compat.py                       # ACL 访问层
    └── hixl_benchmark.py                   # 基准主程序（免编译）
```
