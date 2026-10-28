# HIXL Python Benchmark

简体中文 | [English](./README_en.md)

测量 HIXL 在不同传输方向与传输路径下的点对点传输带宽，支持单机双卡与双机两种模式，适用于快速验证链路连通性与带宽的测试场景。安装 CANN 整包即可使用，无需编译。

- [环境要求](#环境要求)
- [参数说明](#参数说明)
- [运行模式](#运行模式)
- [环境变量](#环境变量)
- [输出说明](#输出说明)
- [精度校验](#精度校验)
- [备注](#备注)

## 环境要求

- Python 3.12
- CANN >= 9.2.0
- 运行前 source CANN 环境变量：

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
```

## 参数说明

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--device` | device 列表（传两个使用逗号分割）：单机模式需要两卡（如 0,1），双机模式的服务端与客户端各需要一卡（如 0） | 单机模式为 `0,1`；双机模式的服务端、客户端为 `0` |
| `--direction` | 传输方向：`D2rD`/`rD2D`/`D2rH`/`rH2D`/`H2rH`/`rH2H`/`H2rD`/`rD2H` | `D2rD` |
| `--transport` | 传输路径：参数简写 `hccs`/`uboe`/`ub_rtp`（后面会自动补齐 `:device`）；完整参数 `hccs:device`/`roce:device`/`roce:host`/`uboe:device`/`ub_rtp:device`/`ub_ctp`/`ub_ctp:device`/`ub_ctp:host`/`ub_mem`；不指定则由 hixl 默认选路 | 不指定（显示为 `default`） |
| `--block_sizes` | block 大小列表 `4K,64K,1M` 或 2 的幂范围 `16K:2M` | `16K:2M` |
| `--transfer_size` | 每个 block 步的传输总量，同时作为 buffer 分配/注册大小；需被 block 大小整除 | `128M` |
| `--loops` | 每个 block 步正式计时的循环轮数；正式计时前每步另有 5 轮预热，不计入统计 | `1000` |
| `--listen` | 服务端模式标志，端口可省略 | 不指定；仅 `--listen` 时端口默认 `16000` |
| `--remote` | 客户端模式标志，`[IP][:PORT]` 均可省略（省略 IP 时默认 127.0.0.1，即同机） | 不指定 |

尺寸单位支持 `K`/`M`/`G`/`T`、`KB`/`MB`/`GB`/`TB`、`KiB`/`MiB`/`GiB`/`TiB`，均按二进制 1024 解析。裸 `roce` 会报错（需显式指定 placement，如 `roce:device`）。

## 运行模式

### 1. 单机双卡（默认）

无参数直接运行即可完成本机两张卡之间的带宽测试；默认使用 0、1 两张卡，可用 `--device` 指定其他卡（如 `--device 2,3`）。默认测试场景：D2rD 方向、16K:2M block 阶梯、每档 128M 传输量、由 hixl 按平台默认选路：

- 卡列表第一张为客户端（initiator 角色）：发起传输并输出带宽统计
- 卡列表第二张为服务端（target 角色）：write 方向接收数据、read 方向提供数据

```bash
python3 hixl_benchmark.py

# 指定 block大小与总传输量
python3 hixl_benchmark.py --block_sizes 4K,64K,1M --transfer_size 256M
```

### 2. 双机模式

两台机器分别启动服务端与客户端，测试跨节点点对点带宽：

- 服务端（target 角色）：write 方向接收数据、read 方向提供数据的一方；`--listen` 启动（端口可省略，默认 16000），`--device` 指定服务端使用的卡（默认 0）
- 客户端（initiator 角色）：发起传输并输出带宽统计的一方；`--remote <服务端IP>` 连接（端口可省略），`--device` 指定客户端使用的卡（默认 0）

```bash
# 基本用法
# 服务端机
python3 hixl_benchmark.py --listen
# 客户端机
python3 hixl_benchmark.py --remote 192.168.1.10

# 指定端口
# 服务端机
python3 hixl_benchmark.py --listen 18000
# 客户端机
python3 hixl_benchmark.py --remote 192.168.1.10:18000

# 指定 block大小与总传输量
# 服务端机
python3 hixl_benchmark.py --listen --block_sizes 4K,64K,1M --transfer_size 256M
# 客户端机
python3 hixl_benchmark.py --remote 192.168.1.10 --block_sizes 4K,64K,1M --transfer_size 256M

# 指定 block大小与总传输量
# 服务端机
python3 hixl_benchmark.py --listen --block_sizes 16k:2M --transfer_size 256M
# 客户端机
python3 hixl_benchmark.py --remote 192.168.1.10 --block_sizes 16k:2M --transfer_size 256M

# 同机双进程调试（IP 可省略，省略时默认 127.0.0.1）
python3 hixl_benchmark.py --remote :16000
python3 hixl_benchmark.py --remote
```

`--listen` 与 `--remote` 互斥，同时指定会报错退出。

## 环境变量

| 环境变量 | 用途 |
|---|---|
| `ASCEND_RT_VISIBLE_DEVICES` | `--device` 未指定时的 device 选择 |
| `ASCEND_LOCAL_COMM_RES` | 原样透传为 hixl Initialize 的 LocalCommRes option；当其 version 不为 `1.3` 时自动附加 `BufferPool=0:0`；如需 RoCE 环路请自行 `export HCCL_INTRA_ROCE_ENABLE=1` |
| `ASCEND_GLOBAL_RESOURCE_CONFIG` | 作为 hixl Initialize 的 GlobalResourceConfig option，优先于 `--transport` 推导值 |

优先级：`--device` 高于 `ASCEND_RT_VISIBLE_DEVICES`（指定时会覆盖该环境变量并在内部使用逻辑编号）；`ASCEND_GLOBAL_RESOURCE_CONFIG` 高于 `--transport`。

```bash
# 指定使用的卡
ASCEND_RT_VISIBLE_DEVICES=2,3 python3 hixl_benchmark.py

# 指定 hixl 初始化 option（version 不为 1.3 时自动附加 BufferPool=0:0）
ASCEND_LOCAL_COMM_RES='{"version":"1.2","net_instance_id":"pod_0"}' python3 hixl_benchmark.py --transport roce:device
```

## 输出说明

```text
---------------------------------------------------------------------------------------
                                   HIXL Write BW Test
 Direction       : D2rD        Transport type : default
 Device ID       : 0 -> 1
 Local engine    : 127.0.0.1:0             Remote engine : 127.0.0.1:16000
 ---------------------------------------------------------------------------------------
 block_size  loops   transfer_size_per_loop  BW peak[GB/sec]   BW average[GB/sec]
 16K         1000    128M                    52.310            51.982
 32K         1000    128M                    54.102            53.871
 ---------------------------------------------------------------------------------------
 Data verify    : pass
 Default transport type  : A2/A3: hccs within a supernode, roce across supernodes
                           A5: ub_ctp within a supernode, roce/uboe/ub_rtp across supernodes
```

| 字段 | 含义 |
|---|---|
| 标题行 | `HIXL Write/Read BW Test`，按 direction 对应的传输操作显示 |
| Direction | 传输方向，8 个取值之一 |
| Transport type | `--transport` 值（简写补齐后）；未指定显示 `default`，即 hixl 默认选路 |
| Default transport type | 仅在 `Transport type : default` 时出现，说明默认选路规则（A2/A3 超节点内 hccs、超节点间 roce；A5 超节点内 ub_ctp、超节点间 roce/uboe/ub_rtp） |
| Device ID | 使用的卡；本机模式显示为 `initiator 卡 -> target 卡` |
| Local/Remote engine | Remote engine 为 target 端引擎地址（握手后回填）；Local engine 为 initiator 引擎的初始化地址串（恒为 `127.0.0.1:0`，实际监听地址由 hixl 引擎内部推导） |
| block_size | 单条传输消息大小；每个 loop 将传输总量拆成 `transfer_size/block_size` 条该大小的消息 |
| loops | 正式计时的循环轮数，不含每步前的 5 轮预热 |
| transfer_size_per_loop | 每个 loop 传输的总字节数（等于 `transfer_size`，即 buffer 分配/注册大小） |
| BW peak/average[GB/sec] | 各 loop 吞吐的最大值/算术平均；单步吞吐按十进制 GB/s（10⁹ 字节）计算 |

计时区间只含 `transfer_sync` 调用本身，描述符构建、精度校验与打印不计入。

## 精度校验

- read 方向：target 预填 `S`、initiator 预填 `C`；每步预热结束后重新填充 `C`，首个计时轮传输完成后由 initiator 回读比对
- write 方向：全部传输结束后由 target 回读比对 `C`，结果回传 initiator 打印
- 校验失败时输出 `Data verify : fail`，stderr 打印首个不匹配字节的 offset、期望值与实际值，并以非零码退出

## 备注

- 与 `hixl_comm_bench` 的关系：轻量补充，不是替代。CSV/JSON 结果输出等复杂场景仍由 C++ benchmark 承担，参见[通信 Benchmark](../comm_benchmark/README.md)
- `ub_mem` 仅 A3 平台支持（HDK > 25.5）
- 带宽口径与 `hixl_comm_bench` 一致：十进制 GB/s；参数中的尺寸单位按二进制 1024 解析
