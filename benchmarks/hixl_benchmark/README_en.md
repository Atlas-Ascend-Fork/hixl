# HIXL Python Benchmark

English | [简体中文](./README.md)

Measures point-to-point transfer bandwidth of HIXL across transfer directions and transport paths, in single-machine dual-card or dual-machine mode. It fits test scenarios that need quick link connectivity and bandwidth validation. Ready to use once the full CANN package is installed; no compilation.

- [Environment Requirements](#environment-requirements)
- [Parameters](#parameters)
- [Run Modes](#run-modes)
- [Environment Variables](#environment-variables)
- [Output](#output)
- [Data Verification](#data-verification)
- [Notes](#notes)

## Environment Requirements

- Python 3.12
- CANN >= 9.2.0
- Source the CANN environment before running:

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
```

## Parameters

| Parameter | Description | Default |
|---|---|---|
| `--device` | Device list (pass two devices separated by a comma): single-machine mode requires two cards (e.g. 0,1); in dual-machine mode the server and the client each require one card (e.g. 0) | Single-machine mode `0,1`; server and client `0` in dual-machine mode |
| `--direction` | Transfer direction: `D2rD`/`rD2D`/`D2rH`/`rH2D`/`H2rH`/`rH2H`/`H2rD`/`rD2H` | `D2rD` |
| `--transport` | Transport path: parameter shorthand `hccs`/`uboe`/`ub_rtp` (`:device` appended automatically); full parameters `hccs:device`/`roce:device`/`roce:host`/`uboe:device`/`ub_rtp:device`/`ub_ctp`/`ub_ctp:device`/`ub_ctp:host`/`ub_mem`; omitted means hixl default routing | Omitted (displayed as `default`) |
| `--block_sizes` | Block size list `4K,64K,1M` or power-of-two range `16K:2M` | `16K:2M` |
| `--transfer_size` | Total bytes per block step; also the buffer alloc/register size; must be divisible by the block size | `128M` |
| `--loops` | Timed loop count per block step; each step runs 5 warmup rounds before timed loops, excluded from statistics | `1000` |
| `--listen` | Server mode flag; port optional | Omitted; `--listen` alone defaults to port `16000` |
| `--remote` | Client mode flag; `[IP][:PORT]`, both parts optional (omitted IP defaults to 127.0.0.1, i.e. the same machine) | Omitted |

Size units support `K`/`M`/`G`/`T`, `KB`/`MB`/`GB`/`TB`, and `KiB`/`MiB`/`GiB`/`TiB`, all parsed as binary 1024. Bare `roce` is rejected (placement must be explicit, e.g. `roce:device`).

## Run Modes

### 1. Single-Machine Dual-Card (default)

Run without options to complete a bandwidth test between two local cards; devices 0 and 1 are used by default, and other cards can be selected with `--device` (e.g. `--device 2,3`). Default test scenario: D2rD direction, 16K:2M block ladder, 128M per step, hixl default routing by platform:

- The first card in the list acts as the client (initiator role): initiates transfers and reports bandwidth statistics
- The second card acts as the server (target role): receives data on write transfers and provides data on read transfers

```bash
python3 hixl_benchmark.py

# Specify block sizes and total transfer size
python3 hixl_benchmark.py --block_sizes 4K,64K,1M --transfer_size 256M
```

### 2. Dual-Machine Mode

Start the server and the client on two machines to test cross-node point-to-point bandwidth:

- Server (target role): the side whose card memory is written on write transfers and read from on read transfers; start with `--listen` (port optional, defaults to 16000); `--device` selects the card used by the server (default 0)
- Client (initiator role): the side that initiates transfers and reports bandwidth statistics; connect with `--remote <server-ip>` (port optional); `--device` selects the card used by the client (default 0)

```bash
# Basic usage
# Server host
python3 hixl_benchmark.py --listen
# Client host
python3 hixl_benchmark.py --remote 192.168.1.10

# Specify port
# Server host
python3 hixl_benchmark.py --listen 18000
# Client host
python3 hixl_benchmark.py --remote 192.168.1.10:18000

# Specify block sizes and total transfer size
# Server host
python3 hixl_benchmark.py --listen --block_sizes 4K,64K,1M --transfer_size 256M
# Client host
python3 hixl_benchmark.py --remote 192.168.1.10 --block_sizes 4K,64K,1M --transfer_size 256M

# Specify block sizes and total transfer size
# Server host
python3 hixl_benchmark.py --listen --block_sizes 16k:2M --transfer_size 256M
# Client host
python3 hixl_benchmark.py --remote 192.168.1.10 --block_sizes 16k:2M --transfer_size 256M

# Same-host dual-process debugging (IP optional; omitted IP defaults to 127.0.0.1)
python3 hixl_benchmark.py --remote :16000
python3 hixl_benchmark.py --remote
```

`--listen` and `--remote` are mutually exclusive; specifying both exits with an error.

## Environment Variables

| Variable | Purpose |
|---|---|
| `ASCEND_RT_VISIBLE_DEVICES` | Device selection when `--device` is not specified |
| `ASCEND_LOCAL_COMM_RES` | Passed through as the hixl Initialize LocalCommRes option; when its version is not `1.3`, `BufferPool=0:0` is appended; export `HCCL_INTRA_ROCE_ENABLE=1` yourself if RoCE loopback is needed |
| `ASCEND_GLOBAL_RESOURCE_CONFIG` | Used as the hixl Initialize GlobalResourceConfig option; takes priority over the `--transport` derived value |

Priority: `--device` overrides `ASCEND_RT_VISIBLE_DEVICES` (when specified, the variable is overwritten and logical ids are used internally); `ASCEND_GLOBAL_RESOURCE_CONFIG` overrides `--transport`.

```bash
# Select devices
ASCEND_RT_VISIBLE_DEVICES=2,3 python3 hixl_benchmark.py

# Set the hixl initialize option (version other than 1.3 appends BufferPool=0:0)
ASCEND_LOCAL_COMM_RES='{"version":"1.2","net_instance_id":"pod_0"}' python3 hixl_benchmark.py --transport roce:device
```

## Output

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

| Field | Meaning |
|---|---|
| Title | `HIXL Write/Read BW Test`, shown by the direction's transfer op |
| Direction | Transfer direction, one of 8 values |
| Transport type | The `--transport` value (after shorthand expansion); `default` when unspecified, i.e. hixl default routing |
| Default transport type | Only shown when `Transport type : default`; explains the default routing rules (A2/A3: hccs within a supernode, roce across; A5: ub_ctp within, roce/uboe/ub_rtp across) |
| Device ID | Cards in use; local mode shows `initiator card -> target card` |
| Local/Remote engine | Remote engine is the target engine address (backfilled after the handshake); Local engine is the initiator engine initialization string (always `127.0.0.1:0`, the actual listening address is derived inside the hixl engine) |
| block_size | Single message size; each loop splits the total into `transfer_size/block_size` messages of this size |
| loops | Timed loop count, excluding the 5 warmup rounds per step |
| transfer_size_per_loop | Total bytes per loop (equals `transfer_size`, i.e. the buffer alloc/register size) |
| BW peak/average[GB/sec] | Maximum/arithmetic mean of per-loop throughput; per-step throughput uses decimal GB/s (10⁹ bytes) |

The timing window covers only the `transfer_sync` call itself; descriptor construction, verification, and printing are excluded.

## Data Verification

- Read direction: the target pre-fills `S` and the initiator pre-fills `C`; after each block step's warmup the initiator re-fills `C`, then reads back and compares after the first timed loop
- Write direction: after all transfers, the target reads back and compares against `C`; the result is returned to the initiator and printed
- On verification failure the tool prints `Data verify : fail`, logs the first mismatched byte's offset, expected and actual values to stderr, and exits with a non-zero code

## Notes

- Relationship with `hixl_comm_bench`: a lightweight supplement, not a replacement. Complex scenarios such as CSV/JSON output remain the responsibility of the C++ benchmark; see the [Communication Benchmark](../comm_benchmark/README_en.md)
- `ub_mem` is supported on A3 only (HDK > 25.5)
- Bandwidth convention matches `hixl_comm_bench`: decimal GB/s; size units in parameters are parsed as binary 1024
