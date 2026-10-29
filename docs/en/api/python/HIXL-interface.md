# HIXL APIs

<!-- md-trans-meta sourceCommit=520b50468db2bc267d2190104cf5771bc77b50c9 translatedAt=2026-10-23T04:26:16.647Z pushedAt=2026-10-27T02:46:33.214Z nl=2 -->

## Applicable Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Supported
<!-- end id3 -->

<!-- npu="910b" id40 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id40 -->

## Hixl Constructor

**Description**

Creates a **Hixl** object.

**Prototype**

```python
__init__()
```

**Parameters**

None

**Example**

```python
import hixl
engine = hixl.Hixl()
```

**Return Value**

Returns a **Hixl** instance on success.

**Constraints**

- If **finalize** has not been called before a **Hixl** object is destroyed, its destructor calls **finalize** automatically. Explicitly calling **finalize** is still recommended to ensure resources are released as expected.

## initialize

**Description**

Initializes **HIXL**. Call this API before calling other APIs.

**Prototype**

```python
initialize(local_engine: str, options: Dict[str, str] = {}) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| local_engine | str | HIXL identifier, unique among all instances participating in connection establishment. IPv4 format: **host\_ip:host\_port** or **host\_ip**. IPv6 format: **[host\_ip]:host\_port** or **[host\_ip]**. A loopback IP address is not recommended because it may conflict when multiple HIXL instances interact.<br>If **host\_port** is set and **host\_port>0**, the current HIXL acts as a Server and listens on the configured port. If omitted or **host\_port<=0**, it acts as a Client and does not listen. |
| options | Dict[str, str] | Initialization parameter values. See the following table. |

<!-- npu="A3,910b" id5 -->
**Table 1**  options (Atlas A2 products/Atlas A3 products)

|Parameter|Optional/Required|Description|
| --- | --- | --- |
| OPTION_ENABLE_USE_FABRIC_MEM | Optional | String value **"EnableUseFabricMem"**.<br>- 0: Disables Fabric Mem mode.<br>- 1: Enables Fabric Mem mode.<br><br>Applies to D2RH and RH2D transfers over HCCS.<br><br>Note: Configure the same value on all nodes in the cluster. Cannot be configured together with **"OPTION_BUFFER_POOL"**. Supported only on Atlas A3 products. |
| OPTION_BUFFER_POOL | Optional | String value **"BufferPool"**.<br>Configures the staging memory pool for transfers requiring staging buffers:<br>- When RDMA registration of Host memory is size-limited.<br>- When staging improves performance for multiple small memory blocks (for example, 128K).<br>The value format is **"${BUFFER_NUM}:${BUFFER_SIZE}"**. The system default is **"4:8"** (in MB). Set it to **"0:0"** to disable the staging pool. For concurrent transfers, increasing **${BUFFER_NUM}** is recommended. Configure the same value wherever this option is used. Cannot be configured together with **"OPTION_ENABLE_USE_FABRIC_MEM"**. |
| OPTION_RDMA_TRAFFIC_CLASS | Optional | String value **"RdmaTrafficClass"**.<br>Configures the RDMA NIC traffic class and has the same function as **HCCL_RDMA_TC**. If both are configured, this option takes precedence; otherwise, the configured value applies.<br>Range: [0,255], in multiples of 4; default: 132. |
| OPTION_RDMA_SERVICE_LEVEL | Optional | String value **"RdmaServiceLevel"**.<br>Configures the RDMA NIC service level and has the same function as **HCCL_RDMA_SL**. If both are configured, this option takes precedence; otherwise, the configured value applies.<br>Range: [0, 7]; default: 4. |
| OPTION_GLOBAL_RESOURCE_CONFIG | Optional | String value **"GlobalResourceConfig"**. Enables and configures global resources. See the examples and constraints below the table. |
| OPTION_AUTO_CONNECT | Optional | String value **"AutoConnect"**.<br>- 0: Disables Auto Connect mode.<br>- 1: Enables Auto Connect mode.<br><br>Notes:<br>- When enabled, transfers can be performed without explicitly establishing connections.<br>- When enabled, abnormal connections are automatically cleaned up after a transfer error or peer destruction. Peer destruction is detected by heartbeats, with a default interval of 10s. |
| OPTION_LOCAL_COMM_RES | Optional | Local communication resources as a JSON string.<br>- Omitted or empty: Resources are generated automatically, and connections are established using collective communication communicators. Because Device Stream resources are limited and connections consume memory, no more than 512 connections per card are recommended. Alternatively, set **local_comm_res_path** in **OPTION_GLOBAL_RESOURCE_CONFIG** to a local communication resource JSON file for HIXL to read. If both are configured and this option is nonempty, this option takes precedence.<br>Note: If **OPTION_BUFFER_POOL** (or **adxl.BufferPool**) is **"0:0"** (staging pool disabled) and hcomm/toolkit is 9.1.0 or later, HixlCS is used without a connection limit.<br>- Ranktable format with **version** **"1.0"** or **"1.2"**: Connections are established using collective communication communicators. Because Device Stream resources are limited and connections consume memory, no more than 512 connections per card are recommended. Configure only the Device information used by the current llm datadist; **server_count** and **rank_id** are unnecessary.<br>- **version** **"1.3"** (recommended; requires HDK 25.5.0 or later and toolkit 9.1.0 or later): HixlCS is used without a connection limit. See [Communication Resource Configuration Fields](#communication-resource-configuration-fields) for the format. Only **version** is required; other fields are generated automatically. |

For the environment variables and ranktable in the preceding table, see [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/9.2.0/docs/en/user_guide/README.md).

<!-- npu="910b" id41 -->
Atlas A2 products: Only D2D is supported when **OPTION_BUFFER_POOL** is not configured and the Server uses HCCS.
<!-- end id41 -->

<!-- end id5 -->
<!-- npu="A3,910b" id7 -->
The following examples configure **OPTION_LOCAL_COMM_RES** with **version** **"1.3"**:

- Minimal configuration (only **version** is specified; other fields are generated automatically):

```json
{
    "version": "1.3"
}
```

- Full configuration example (communication resources specified manually):

```json
{
    "version": "1.3",
    "net_instance_id": "superpod1_1",
    "endpoint_list": [
        {
            "protocol": "roce",
            "comm_id": "10.10.10.1",
            "placement": "device"
        }
    ]
}
```

> **Note:** The minimal configuration is recommended. The system generates local communication resources automatically. For manual configuration, see [Communication Resource Configuration Fields](#communication-resource-configuration-fields) for the meanings of the fields in **endpoint_list**.

Configuration examples and constraints for **OPTION_GLOBAL_RESOURCE_CONFIG** are as follows:

<!-- npu="A3" id6 -->
For Fabric Mem mode (supported only on Atlas A3 products), configure this parameter as follows:

```sh
{
    "fabric_memory.max_capacity": "128", //Virtual memory pool size, an integer in (0, 1024], in TB. Default: 32. The underlying component determines the usable range.
    "fabric_memory.start_address": "40", //Virtual memory pool starting address, an integer in [0, 1024], in TB. Default: 40.
    "fabric_memory.task_stream_num": "1", //Streams used by a task, in [1, 8]. Default: 1. Only 1 is supported when enable_aicpu_unfold is true.
    "fabric_memory.enable_aicpu_unfold": true // Whether AICPU unfolds FabricMem. Boolean; default: true.
}
```
<!-- end id6 -->

For asynchronous connection establishment/disconnection, configure this parameter as follows:

```sh
{
    "connect_pool.thread_num": "2", //Connection thread pool size, an integer in [1, 64]. Default: 2.
    "connect_pool.task_queue_capacity": "256" //Connection task queue capacity, an integer in [1, 65535]. Default: 128.
}
```

The Device NIC listens on port 16666 by default. If multiple processes use the same NIC, configure the following:

```sh
{
    "comm_resource_config.listen_port": "26666", // Optional. Integer in [1, 65535]. If omitted, the generated ranktable does not contain device_port.
    "comm_resource_config.max_active_channels": "128" // Optional. Maximum number of simultaneously active Device transfer channels in CS scenarios. Range: [1, 8192]; default: 128. Each active channel consumes 2 Streams.
}
```

To configure local communication resources through a file path, use the following configuration:

```json
{
    "local_comm_res_path": "/path/to/local_comm_res.json"
}
```

`local_comm_res_path` accepts absolute and relative paths. Relative paths are resolved against the process's current working directory. The target must be a regular file with a size in [1 byte, 1MiB], using the same content format as **OPTION_LOCAL_COMM_RES**. If both are configured and **OPTION_LOCAL_COMM_RES** is nonempty, **OPTION_LOCAL_COMM_RES** takes precedence.

For the connection pool, configure this parameter as follows:

```sh
{
    "channel_pool.max_channel": "10", //Maximum connections, an integer in (0, 512]. Default: 512.
    "channel_pool.high_waterline": "0.3", //High watermark for connection reclamation, a decimal in (0, 1). Configure together with channel_pool.low_waterline.
    "channel_pool.low_waterline": "0.1" //Low watermark, the proportion of connections retained after reclamation. A decimal in (0, 1), lower than the high watermark and configured together with it.
}
```

When the connection count reaches the high watermark, the pool selects (current connection count - connection count corresponding to the low watermark) connections for destruction. Connections transferring data are not destroyed. Parameters are calculated as follows:

```sh
Connection count at the high watermark = max(1, floor(channel_pool.max_channel × channel_pool.high_waterline))
Connection count at the low watermark = max(1, floor(channel_pool.max_channel × channel_pool.low_waterline))
```

In this example, the high watermark corresponds to 3 connections and the low watermark to 1 connection. Before establishing a connection, the pool checks whether the current count has reached 3. If so, it selects (current connection count - 1) connections to destroy. Connections with ongoing transfers are not destroyed.

Observe the following when enabling the connection pool:

- Configure **OPTION_GLOBAL_RESOURCE_CONFIG** for all Hixl Engines in the cluster.
- When **transfer\_sync** or **transfer_async** is called without an available connection, the pool establishes one automatically.
- The pool introduces additional transfer and connection establishment overhead, which may degrade performance.
<!-- end id7 -->

<!-- npu="950" id4 -->
**Table 2**  options (Ascend 950PR&950DT products)

| Parameter | Optional/Required | Description |
| --- | --- | --- |
| OPTION_LOCAL_COMM_RES | Optional | Local communication resources as a JSON string. See [Communication Resource Configuration Fields](#communication-resource-configuration-fields) for the format. An empty configuration does not generate resources automatically. Alternatively, set **local_comm_res_path** in **OPTION_GLOBAL_RESOURCE_CONFIG** to a local communication resource JSON file for HIXL to read. At least one must be configured: a nonempty **OPTION_LOCAL_COMM_RES** or a valid **local_comm_res_path** in **OPTION_GLOBAL_RESOURCE_CONFIG**. If both are configured and this option is nonempty, this option takes precedence. See [Configuration Examples](#configuration-examples) below.<br/>**Important:<br/>1. Values in the examples illustrate the format only. Replace them with actual communication resource information queried from the current environment. Copying example values directly causes communication failures.<br/>2. Automatic localcommres generation requires calling HIXL APIs with root privileges and LCNE UBM_2.0.0.B011 or later. Run dis startup on the 1213 frontend to check the LCNE version. HDK 25.1.RC1.B108 or later is required; run npu-smi info to check the HDK version.<br/>3. Currently, only UB scenarios support automatic generation of net_instance_id and endpoint_list. To configure localcommres manually, use the tool to help generate localcommres information for a specified NPU. See [scripts/tools/lcrgen/README.md](../../../../scripts/tools/lcrgen/README.md) for usage.<br/>4. In UB scenarios, configuring only UB endpoints with placement device in endpoint_list supports registration and transfer of Device addresses only. Configuring only UB endpoints with placement host supports Host addresses only. To use both Device and Host addresses, configure UB endpoints for both placements.** |
| OPTION_GLOBAL_RESOURCE_CONFIG | Optional | String value **"GlobalResourceConfig"**. Enables and configures global resources as a JSON string. See [Global Resource Configuration Fields](#global-resource-configuration-fields) for field descriptions. |
| OPTION_AUTO_CONNECT | Optional | String value **"AutoConnect"**.<br>- 0: Disables Auto Connect mode.<br>- 1: Enables Auto Connect mode.<br><br>Notes:<br>- When enabled, transfers can be performed without explicitly establishing connections.<br>- When enabled, abnormal connections are automatically cleaned up after a transfer error or peer destruction. Peer destruction is detected by heartbeats, with a default interval of 10s. |
| OPTION_RDMA_TRAFFIC_CLASS | Optional | String value **"RdmaTrafficClass"**.<br>Configures the RDMA NIC traffic class and has the same function as **HCCL_RDMA_TC**. If both are configured, this option takes precedence; otherwise, the configured value applies.<br>Range: [0,255], in multiples of 4; default: 132.<br>Note: Applies to RoCE scenarios on Ascend 950PR&950DT products. |
| OPTION_RDMA_SERVICE_LEVEL | Optional | String value **"RdmaServiceLevel"**.<br>Configures the RDMA NIC service level and has the same function as **HCCL_RDMA_SL**. If both are configured, this option takes precedence; otherwise, the configured value applies.<br>Range: [0, 7]; default: 4.<br>Note: Applies to RoCE scenarios on Ascend 950PR&950DT products. |
<!-- end id4 -->

<a id="configuration-examples"></a>**Configuration Examples**

UB: Minimal configuration (only **version** is specified; other fields are generated automatically)

```json
{
    "version": "1.3"
}
```

UB: Full configuration

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "server_id": "server_0",
  "endpoint_list": [
    {
      "protocol": "ub_ctp",
      "comm_id": "00000000007f020000100000df149001",
      "placement": "host",
      "dst_eid": "00000000007f030000100000df141c01"
    }
  ]
}
```

ROCE

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "endpoint_list": [
    {
      "protocol": "roce",
      "comm_id": "192.168.100.100",
      "placement": "host"
    }
  ]
}
```

UBOE

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "endpoint_list": [
    {
      "protocol": "uboe",
      "comm_id": "192.168.100.123",
      "placement": "device"
    }
  ]
}
```

UB_RTP

```json
{
  "version": "1.3",
  "net_instance_id": "superpod_1",
  "endpoint_list": [
    {
      "protocol": "ub_rtp",
      "comm_id": "0000000000ff0a80000000000a140200",
      "placement": "device"
    }
  ]
}
```

<a id="communication-resource-configuration-fields"></a>**Communication Resource Configuration Fields**

| Field | Data Type | Required/Optional | Description | Supported Values/Rules |
| --- | --- | --- | --- | --- |
| version | String | Required | Version number | **"1.3"**. Requires HDK 25.5.0 or later and toolkit 9.1.0 or later. |
| net_instance_id | String | Required | Unique identifier of the current supernode | Must be unique for each supernode. |
| server_id | String | Optional | Current server identifier | Used only to check for same-OS H2rH loopback when **protocol** is **ub_ctp** and **placement** is **host**. The check is disabled if this field is empty or differs between the two ends. |
| endpoint_list | Array | Required | Available communication devices | - |
| endpoint_list[].protocol | String | Required | Communication protocol | **"roce"**/**"ub_ctp"**/**"uboe"**/**"ub_rtp"** |
| endpoint_list[].comm_id | String | Required | Communication identifier | For **ub_ctp**/**ub_rtp**, use `${eid}`; for **roce**, use the NIC IPv4/IPv6 address; for **uboe**, use the Device UBOE NIC IP address. |
| endpoint_list[].placement | String | Required | Communication device location | **"host"**/**"device"** |
| endpoint_list[].plane | String | Optional | Communication device plane | For **ub_ctp**, specify this field if the device distinguishes planes. Use a unique value for each plane, such as **"plane-a"**/**"plane-b"**. |
| endpoint_list[].dst_eid | String | Optional | `${eid}` of the peer communication device connected to the current device | For **ub_ctp**, specify the peer `${eid}` if a full-mesh direct peer connection exists. |

<a id="global-resource-configuration-fields"></a>**Global Resource Configuration Fields**

| Field | Data Type | Required/Optional | Description | Supported Values/Rules |
| --- | --- | --- | --- | --- |
| comm_resource_config.protocol_desc | String or string array | Optional | Available communication protocols and device placements. Use `ub_ctp` for pure URMA mode with UB CTP; otherwise, use `${protocol}:${placement}`. | Supports **"ub_ctp"**/**"roce:device"**/**"hccs:device"**/**"ub_ctp:device"**/**"ub_ctp:host"**/**"uboe:device"**/**"ub_rtp:device"**/**"roce:host"**. Filters both explicitly configured and automatically generated **endpoint_list** entries in **OPTION_LOCAL_COMM_RES**. On Ascend 950PR&950DT products, omitting this field or specifying only **"ub_ctp:device"** generates Device UB resources; Host memory is mapped to Device addresses through UBMEM and transferred over Device UB links. Specifying both **"ub_ctp:device"** and **"ub_ctp:host"** generates Device and Host UB resources and uses the pure URMA path. **"ub_ctp"** is equivalent to this combination. Specifying only **"ub_ctp:host"** retains Host UB CTP endpoints only. For manually configured LocalCommRes, **"ub_ctp"** requires both Device and Host UB CTP endpoints. Explicit **OPTION_LOCAL_COMM_RES** entries are not additionally filtered if this field is omitted. |
| comm_resource_config.listen_port | JSON number or numeric string | Optional | Device NIC listening port | Range: [1, 65535]. |
| comm_resource_config.qos | Number | Optional | Communication protocol QoS | Only [0-7] is supported. Defaults to 0 when not configured. |
| comm_resource_config.max_active_channels | Number | Optional | Number of simultaneously active Device transfer channels in CS scenarios | Range: [1, 8192]; defaults to 128 when not configured. Each active channel consumes 2 Streams. Allow headroom based on the Stream limit of the current card type and the number of Streams already created by the application. For Stream limits by card type, see CANN Runtime API [aclrtCreateStream](https://www.hiascend.com/document/detail/en/canncommercial/latest/API/runtimeapi/aclcppdevg_03_0066.html). Values outside [1, 8192] cause **initialize** to return a parameter error. |
| transfer_config.max_transfer_count_per_batch | Number or decimal numeric string | Optional | Maximum buffer count per internal transfer batch. Larger transfers are automatically split into batches in the original order. | Default: 1920; global range: [1, 32766]; HCCS/FabricMem range: [1, 1920]. Hcomm validates specific RoCE/URMA queue depth limits against protocol and hardware capabilities. Both FabricMem modes validate [1, 1920]. With `fabric_memory.enable_aicpu_unfold=true`, this value controls logical AICPU unfolding batches and Notify boundaries. With `false`, the Host submits `aclrtMemcpyAsync` operations consecutively, without batching or synchronizing at boundaries defined by this value. The RoCE/URMA Client SQ/SCQ depth is `max(64, nextPowerOfTwo(configured value + 2))`. Supported in CANN 9.2.0 and later. |
| local_comm_res_path | String | Optional | Local communication resource JSON file path. The content format is the same as **OPTION_LOCAL_COMM_RES**. | Absolute or relative path. Relative paths are resolved against the process's current working directory. The target must be a regular file with a size in [1 byte, 1MiB]. If both are configured and **OPTION_LOCAL_COMM_RES** is nonempty, **OPTION_LOCAL_COMM_RES** takes precedence. |

Usage notes for **comm_resource_config.listen_port** are as follows:

<!-- npu="910b" id37 -->
- Atlas A2 products: If this option is omitted, port `16666` is used.
<!-- end id37 -->
<!-- npu="A3" id38 -->
- Atlas A3 products: If this option is omitted, port `16666` is used.
<!-- end id38 -->
<!-- npu="950" id39 -->
- Ascend 950PR&950DT products: If omitted, the underlying communication component selects an available port, and HIXL queries the actual listening port automatically.
<!-- end id39 -->

**Example**

See [Running the Example](../../../../examples/python/hixl/hixl_d2rd_multiproc_sample.py).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- Use this API together with **finalize**. After successful initialization, call **finalize** before any exit to release resources. Otherwise, resources may be released in an unexpected order, causing errors.
- Call **aclrtSetDevice** before initialization.
- Repeated **initialize** calls return **SUCCESS** and are otherwise ignored.

## finalize

**Description**

Cleans up HIXL resources.

**Prototype**

```python
finalize() -> None
```

**Parameters**

None

**Example**

```python
import hixl
engine = hixl.Hixl()
engine.initialize("127.0.0.1:16000")
# ... Application logic ...
engine.finalize()
```

**Return Value**

None

**Constraints**

- Use this API together with **initialize**.
- It is recommended that you disconnect connections and deregister memory before calling **finalize**.
- The Server must wait for all Clients to disconnect before calling this API. If the Server exits earlier, errors occur during Client disconnection and data transfer.
- If a Client reads or writes remote Server addresses, the Server must wait until the Client finishes these operations before calling this API. Otherwise, the operations fail.
- This API cannot be called concurrently with other APIs.

## register\_mem

**Description**

Registers memory addresses for use as local and remote addresses in **transfer\_sync** calls. The addresses specified by **transfer\_sync** may be subsets of the registered regions. Local memory addresses must be registered with the current HIXL, and remote memory addresses must be registered with the remote HIXL. Repeated **register\_mem** calls for the same region (same **addr** and **len**) return **SUCCESS** and the original **mem\_handle**, without creating new underlying resources.

**Prototype**

```python
register_mem(mem_desc: MemDesc, mem_type: MemType) -> Tuple[int, int]
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| mem_desc | MemDesc | Description of the memory to register. |
| mem_type | MemType | Type of the memory to register. |

**Example**

```python
import hixl
engine = hixl.Hixl()
engine.initialize("127.0.0.1:16000")
mem_desc = hixl.MemDesc(addr=dev_addr, len=buf_size)
ret, handle = engine.register_mem(mem_desc, hixl.MemType.MEM_DEVICE)
```

**Return Value**

Returns a tuple **(ret, mem\_handle)**:

- **ret**: Status code. **SUCCESS** indicates success, **PARAM\_INVALID** indicates an invalid parameter, and other values indicate failure.
- **mem\_handle**: Memory handle (**int**) returned on successful registration, used for deregistration.

**Constraints**

- Register all local memory before calling **connect** to establish a connection with the peer.
- It is recommended that you register no more than 4K memory regions per **Hixl** instance. Excessive registrations may cause device OOM and increase connection establishment time, potentially causing connection timeouts. Control the number and size of registered regions based on your application.
<!-- npu="A3,910b" id8 -->
- Up to 50GB of Device memory can be registered. The Host memory limit is 20GB for HDK earlier than 25.5 and 1TB for HDK 25.5 or later. Registering more memory consumes more OS memory. This constraint applies to the following products:
  <!-- npu="910b" id9 -->
  - Atlas A2 products
  <!-- end id9 -->
  <!-- npu="A3" id10 -->
  - Atlas A3 products
  <!-- end id10 -->
<!-- end id8 -->
  <!-- npu="A3,910b" id11 -->
- Allocate Host memory to register using **aclrtMallocHost**, which automatically aligns addresses. This constraint applies to the following models:
  <!-- npu="910b" id12 -->
  - Atlas A2 products
  <!-- end id12 -->
  <!-- npu="A3" id13 -->
  - Atlas A3 products
  <!-- end id13 -->
<!-- end id11 -->
<!-- npu="A3" id35 -->
- Host memory in FabricMem has these constraints: HDK 25.5 does not support `aclrtMemRetainAllocationHandle`, so allocate with ADXL `AdxlEngine::MallocMem` and free with `AdxlEngine::FreeMem`. HDK 26.0 and later support direct management through ACL APIs. This constraint applies to the following products:
  <!-- npu="A3" id36 -->
  - Atlas A3 products
  <!-- end id36 -->
<!-- end id35 -->
- Device memory to be registered must be allocated using **aclrtMalloc**. For HCCS transfers, set the allocation policy to **ACL\_MEM\_MALLOC\_HUGE\_ONLY**.
<!-- npu="950" id14 -->
- On Ascend 950PR&950DT products, Host RoCE NICs currently do not support registering memory allocated by **aclrtMallocHost**. Use **malloc** or another method.
<!-- end id14 -->

## deregister\_mem

**Description**

Deregisters memory. Repeated **deregister\_mem** calls with the same **mem\_handle** release resources on the first call; later calls return **SUCCESS** without action. A nonzero handle not obtained through **register\_mem** also returns **SUCCESS** without action. **0** returns **PARAM\_INVALID**.

**Prototype**

```python
deregister_mem(mem_handle: int) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| mem_handle | int | Memory handle returned by **register\_mem** during registration. |

**Example**

```python
ret = engine.deregister_mem(handle)
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- Before calling this API, call **disconnect** to disconnect all connections and ensure that all memory is no longer in use.

## connect

**Description**

Establishes a connection with a remote HIXL.

**Prototype**

```python
connect(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote HIXL. |
| timeout_in_millis | int | Connection establishment timeout, in ms. Default: 1000. |

**Example**

```python
ret = engine.connect("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **TIMEOUT**: Connection establishment timed out.
- **ALREADY\_CONNECTED**: Already connected.
- Other values: Failure.

**Constraints**

- Call this API after **initialize** completes on both Client and Server.
  <!-- npu="A3,910b" id15 -->
- If **OPTION\_LOCAL\_COMM\_RES** is empty or **version** is **"1.0"** or **"1.2"**, connections use collective communication communicators, with a maximum of 512 communications. Excessive connections may cause memory OOM and degrade KV Cache transfer performance. This constraint applies to the following models:
  <!-- npu="910b" id16 -->
  - Atlas A2 products
  <!-- end id16 -->
  <!-- npu="A3" id17 -->
  - Atlas A3 products
  <!-- end id17 -->
  <!-- end id15 -->
- With **OPTION\_LOCAL\_COMM\_RES** version **"1.3"** (recommended; requires HDK 25.5.0 or later and toolkit 9.1.0 or later), connections use HixlCS without a connection limit.
- A timeout of at least 200ms is recommended.
- Register all local and remote memory before calling this API. Memory registered after connection establishment does not support remote access.
  <!-- npu="A3,910b" id18 -->
- In containers, map **/etc/hccn.conf** into the container or ensure that **hccn\_tool** exists under **/usr/local/Ascend/driver/tools**. Otherwise, add its directory to **PATH**. In the example below, **hccn\_tool\_install\_path** is that directory. This constraint applies to the following models:
  <!-- npu="910b" id19 -->
  - Atlas A2 products
  <!-- end id19 -->
  <!-- npu="A3" id20 -->
  - Atlas A3 products
  <!-- end id20 -->

  ```sh
  export PATH=$PATH:${hccn_tool_install_path}
  ```
  <!-- end id18 -->

<!-- npu="A3,910b" id42 -->
- For Device RoCE, use consistent Device RoCE address configurations throughout the communication cluster. Mixing IPv6-only nodes with IPv4/IPv6 dual-stack nodes is unsupported. This constraint applies to the following products:
  <!-- npu="910b" id43 -->
  - Atlas A2 products
  <!-- end id43 -->
  <!-- npu="A3" id44 -->
  - Atlas A3 products
  <!-- end id44 -->

<!-- end id42 -->
## disconnect

**Description**

Disconnects from a remote HIXL.

**Prototype**

```python
disconnect(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote HIXL. |
| timeout_in_millis | int | Disconnection timeout, in ms. Default: 1000. |

**Example**

```python
ret = engine.disconnect("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **NOT\_CONNECTED**: No connection to the peer.
- Other values: Failure.

**Constraints**

- Call **initialize** to complete initialization before calling this API.

## connect\_async

**Description**

Establishes a connection with a remote HIXL asynchronously.

**Prototype**

```python
connect_async(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote HIXL. |
| timeout_in_millis | int | Connection establishment timeout, in ms. Default: 1000. |

**Example**

```python
ret = engine.connect_async("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter (**timeout\_in\_millis <= 0**).
- **RESOURCE\_EXHAUSTED**: Task queue full.
- Other values: Failure.

**Constraints**

- This API inherits all constraints of the **connect** API.
- Configure the thread pool size and task queue length through the **initialize** API of **Hixl**.
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- Do not mix **connect\_async/disconnect\_async** with **connect/disconnect**.
- Tasks for the same **remote\_engine** execute in submission order; tasks for different **remote\_engine** values may execute concurrently. The queried status is that of the most recently submitted task.

## disconnect\_async

**Description**

Disconnects from a remote HIXL asynchronously.

**Prototype**

```python
disconnect_async(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote HIXL. |
| timeout_in_millis | int | Disconnection timeout, in ms. Default: 1000. |

**Example**

```python
ret = engine.disconnect_async("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter (**timeout\_in\_millis <= 0**).
- **RESOURCE\_EXHAUSTED**: Task queue full.
- Other values: Failure.

**Constraints**

- This API inherits all constraints of the **disconnect** API.
- Configure the thread pool size and task queue length through the **initialize** API of **Hixl**.
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- Do not mix **connect\_async/disconnect\_async** with **connect/disconnect**.
- Tasks for the same **remote\_engine** execute in submission order; tasks for different **remote\_engine** values may execute concurrently. The queried status is that of the most recently submitted task.

## get\_async\_connect\_status

**Description**

Queries a specific asynchronous connection status.

**Prototype**

```python
get_async_connect_status(remote_engine: str) -> Tuple[int, AsyncConnectStatus]
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote HIXL. |

**Example**

```python
ret, status = engine.get_async_connect_status("127.0.0.1:16001")
if status == hixl.AsyncConnectStatus.CONNECTED:
    print("Connected")
```

**Return Value**

Returns a tuple **(ret, status)**:

- **ret**: Status code. **SUCCESS** indicates success; other values indicate failure.
- **status**: Asynchronous connection status. See [AsyncConnectStatus](HIXL-data-structure.md#asyncconnectstatus).

**Constraints**

- Call **initialize** to complete initialization before calling this API.
- The return value indicates only whether the API call succeeded. The output parameter indicates the asynchronous connection/disconnection task status.

## get\_all\_async\_connect\_status

**Description**

Queries all asynchronous connection statuses.

**Prototype**

```python
get_all_async_connect_status() -> Tuple[int, Dict[str, AsyncConnectStatus]]
```

**Parameters**

None

**Example**

```python
ret, statuses = engine.get_all_async_connect_status()
for remote, status in statuses.items():
    print(f"{remote}: {status}")
```

**Return Value**

Returns a tuple **(ret, statuses)**:

- **ret**: Status code. **SUCCESS** indicates success; other values indicate failure.
- **statuses**: Dictionary whose keys are remote HIXL identifiers (**str**) and values are asynchronous connection statuses (**AsyncConnectStatus**).

**Constraints**

- Call **initialize** to complete initialization before calling this API.
- The return value indicates only whether the API call succeeded. The output parameter indicates the asynchronous connection/disconnection task status.

## transfer\_sync

**Description**

Transfers memory with a remote HIXL.

**Prototype**

```python
transfer_sync(remote_engine: str, op: TransferOp, op_descs: List[TransferOpDesc], timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote HIXL. |
| op | TransferOp | Reads remote memory into local memory or writes local memory to remote memory. |
| op_descs | List[TransferOpDesc] | Local and remote addresses for batch operations. |
| timeout_in_millis | int | Transfer timeout, in ms. Default: 1000. |

**Example**

```python
op_descs = [hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=size)]
ret = engine.transfer_sync("127.0.0.1:16001", hixl.TransferOp.READ, op_descs, timeout_in_millis=30000)
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **NOT\_CONNECTED**: No connection to the peer.
- **TIMEOUT**: Transfer timed out.
- **RESOURCE\_EXHAUSTED**: Resources exhausted.
- Other values: Failure.

**Constraints**

  <!-- npu="A3,910b" id21 -->
- Before calling this API, call **connect** to establish a connection or enable the connection pool during HIXL initialization through **OPTION\_GLOBAL\_RESOURCE\_CONFIG** in **options**. This constraint applies to the following models:
  <!-- npu="910b" id22 -->
  - Atlas A2 products
  <!-- end id22 -->
  <!-- npu="A3" id23 -->
  - Atlas A3 products
  <!-- end id23 -->
  <!-- end id21 -->
  <!-- npu="A3,910b" id24 -->
- The staging memory pool is enabled by default. With it enabled, a staging transfer is used if either local or remote memory in **op\_desc** is unregistered. Unregistered memory is treated as Host memory; ensure that its address is valid. This constraint applies to the following products:
  <!-- npu="910b" id25 -->
  - Atlas A2 products
  <!-- end id25 -->
  <!-- npu="A3" id26 -->
  - Atlas A3 products
  <!-- end id26 -->
  <!-- end id24 -->
  <!-- npu="A3,910b" id27 -->
- In staging mode, all **op\_desc** entries must use the same transfer type, for example, writing local Host memory to remote Host memory. This constraint applies to the following products:
  <!-- npu="910b" id28 -->
  - Atlas A2 products
  <!-- end id28 -->
  <!-- npu="A3" id29 -->
  - Atlas A3 products
  <!-- end id29 -->
  <!-- end id27 -->
  <!-- npu="A3" id30 -->
- In Fabric Mem mode, all **op\_descs** must use the same transfer type. The system determines direction from the first **op\_desc** memory type. This constraint applies to the following models:
  - Atlas A3 products
  <!-- end id30 -->

## transfer\_async

**Description**

Performs batch asynchronous memory transfers with a remote HIXL.

**Prototype**

```python
transfer_async(remote_engine: str, op: TransferOp, op_descs: List[TransferOpDesc], args: TransferArgs = TransferArgs()) -> Tuple[int, int]
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote HIXL. |
| op | TransferOp | Reads remote memory into local memory or writes local memory to remote memory. |
| op_descs | List[TransferOpDesc] | Local and remote addresses for batch operations. |
| args | TransferArgs | Optional parameter (reserved). |

**Example**

```python
op_descs = [hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=size)]
ret, req_id = engine.transfer_async("127.0.0.1:16001", hixl.TransferOp.WRITE, op_descs)
```

**Return Value**

Returns a tuple **(ret, req\_id)**:

- **ret**: Status code. **SUCCESS** indicates success; **NOT\_CONNECTED**, no peer connection; **RESOURCE\_EXHAUSTED**, exhausted resources; other values, failure.
- **req\_id**: Request handle (**int**) used to query transfer request status.

**Constraints**

- The following constraints apply before calling this API:
  - Call **connect** to establish a connection with the peer first.
  <!-- npu="A3,910b" id31 -->
  - Alternatively, enable the connection pool during HIXL initialization through **OPTION\_GLOBAL\_RESOURCE\_CONFIG** in **options**. This constraint applies to the following models:
    <!-- npu="910b" id32 -->
    - Atlas A2 products
    <!-- end id32 -->
    <!-- npu="A3" id33 -->
    - Atlas A3 products
    <!-- end id33 -->
  <!-- end id31 -->
- Currently, asynchronous transfers support only direct transfers. Staging transfers are unsupported. Direct transfer is the default.
  <!-- npu="A3" id34 -->
- In Fabric Mem mode, all **op\_descs** must use the same transfer type. The system determines direction from the first **op\_desc** memory type. This constraint applies to the following models:
  - Atlas A3 products
  <!-- end id34 -->

## get\_transfer\_status

**Description**

Queries the status of an asynchronous memory transfer.

**Prototype**

```python
get_transfer_status(req_id: int) -> Tuple[int, TransferStatus]
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| req_id | int | Request handle generated by **transfer\_async**. |

**Example**

```python
ret, status = engine.get_transfer_status(req_id)
if status == hixl.TransferStatus.COMPLETED:
    print("Transfer completed")
```

**Return Value**

Returns a tuple **(ret, status)**:

- **ret**: Status code. **SUCCESS** indicates success; **PARAM\_INVALID**, an invalid parameter; **NOT\_CONNECTED**, no peer connection; other values, failure.
- **status**: Transfer status. See [TransferStatus](HIXL-data-structure.md#transferstatus).

**Constraints**

- Call **connect** to establish a connection with the peer before this API.
- After calling **transfer\_async** for asynchronous transfer, use this API to query the request status. If the queried status is **COMPLETED** or **FAILED**, related resources are released and the request cannot be queried again.
- For asynchronous transfers, determine timeouts in your application. If a task times out, call **disconnect** to destroy the connection and clean up resources.

## get\_all\_transfer\_status

**Description**

Queries the status of all asynchronous memory transfers.

**Prototype**

```python
get_all_transfer_status(args: GetTransferStatusArgs = GetTransferStatusArgs()) -> Tuple[int, List[TransferResult]]
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| args | GetTransferStatusArgs | Parameters for querying all asynchronous transfer request statuses. |

**Example**

```python
args = hixl.GetTransferStatusArgs(max_query_count=4, skip_waiting=True)
ret, results = engine.get_all_transfer_status(args)
for result in results:
    print(f"req={result.req}, status={result.status}")
```

**Return Value**

Returns a tuple **(ret, results)**:

- **ret**: Status code. **SUCCESS** indicates success. **UNSUPPORTED** indicates that the **options** for Hixl initialization neither set **LocalCommRes.version** to **1.3** nor include **uboe:device** or **ub\_rtp:device** in **GlobalResourceConfig.comm\_resource\_config.protocol\_desc**, in which case this query is unsupported. Other values indicate failure.
- **results**: **TransferResult** list, each containing transfer request status information.

**Constraints**

- Call **connect** to establish a connection with the peer before this API.
- After calling **transfer\_async** for asynchronous transfer, use this API to query all request statuses. A **COMPLETED** or **FAILED** request releases its resources and is omitted from subsequent queries.
- For asynchronous transfers, determine timeouts in your application. If a task times out, it is recommended that you call **disconnect** to destroy the connection and clean up resources.

## send\_notify

**Description**

Sends a Notify message to a remote engine.

**Prototype**

```python
send_notify(remote_engine: str, notify: NotifyDesc, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_engine | str | Unique identifier of the remote **Hixl**. |
| notify | NotifyDesc | Notify content to send. Both **notify\_msg** and **name** are limited to 1024 characters. |
| timeout_in_millis | int | Send timeout, in ms. Default: 1000. |

**Example**

```python
notify = hixl.NotifyDesc(name="cache_ready", notify_msg="block_0")
ret = engine.send_notify("127.0.0.1:16001", notify, timeout_in_millis=1000)
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter (**timeout\_in\_millis <= 0**, or **notify.name/notify\_msg** exceeds 1024 bytes).
- Other values: Failure.

**Constraints**

- Call **connect** to establish a connection with the peer before this API.
- Each connection supports up to 4096 Notify messages. Ensure that the remote **Hixl** promptly calls **get\_notifies** to consume them, preventing send failures caused by reaching the limit.

## get\_notifies

**Description**

Obtains Notify messages received by all Servers in the current **Hixl** and clears the received messages.

**Prototype**

```python
get_notifies() -> Tuple[int, List[NotifyDesc]]
```

**Parameters**

None

**Example**

```python
ret, notifies = engine.get_notifies()
for n in notifies:
    print(f"name={n.name}, msg={n.notify_msg}")
```

**Return Value**

Returns a tuple **(ret, notifies)**:

- **ret**: Status code. **SUCCESS** indicates success; other values indicate failure.
- **notifies**: **NotifyDesc** list.

**Constraints**

None

## get\_capability

**Description**

Queries library capabilities. Upper-layer applications can call this API before **initialize** to detect whether the current **Hixl** library supports capabilities such as Auto Connect and Client/Server communication, avoiding hard-coded defaults or incompatibility with older **.so** files.

**Prototype**

```python
hixl.get_capability(feature_type: FeatureType) -> Tuple[int, int]
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| feature_type | FeatureType | Capability type. See [FeatureType](HIXL-data-structure.md#featuretype). |

**Example**

```python
ret, value = hixl.get_capability(hixl.FeatureType.AUTO_CONNECT)
supports_auto_connect = (value == hixl.FEATURE_SUPPORTED)
```

**Return Value**

Returns a tuple **(ret, value)**:

- **ret**: Status code. **SUCCESS** indicates success. Unknown or unsupported capability types set **value** to **FEATURE\_NOT\_SUPPORTED**. **PARAM\_INVALID** indicates an invalid parameter (**feature\_type** is negative).
- **value**: Capability support status. **1** means supported (**FEATURE\_SUPPORTED**); **0** means unsupported (**FEATURE\_NOT\_SUPPORTED**).

**Constraints**

- Module-level function independent of a **Hixl** instance.
