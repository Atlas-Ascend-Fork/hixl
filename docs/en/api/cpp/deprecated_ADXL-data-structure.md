# ADXL Data Structures (To Be Deprecated)

<!-- md-trans-meta sourceCommit=6d1c2b68a462afd7a7adffaf765096e79dfcd92f translatedAt=2026-10-23T03:54:09.455Z pushedAt=2026-10-27T02:46:32.889Z nl=2 -->

**MemType**

Memory type.

```cpp
enum MemType {
  MEM_DEVICE,
  MEM_HOST
};
```

**TransferOp**

Transfer operation type.

```cpp
enum TransferOp {
  READ,
  WRITE
};
```

**MemDesc**

Memory description.

```cpp
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
};
```

**TransferOpDesc**

Transfer operation description.

```cpp
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
};
```

**MemHandle**

Handle of registered memory.

```cpp
using MemHandle = void *;
```

**ShareableHandle**

Fabric shareable handle type for sharing the same physical memory across processes.

```cpp
using ShareableHandle = aclrtMemFabricHandle;
```

**NotifyDesc**

Notify description.

```cpp
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
};
```

**FeatureType**

Library capability type queried through **GetCapability**. Enum values must be assigned explicitly. New capabilities may be appended only at the end.

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  CLIENT_SERVER_COMM = 1,
};
```

|Enum Value|Description|
|--|--|
|AUTO_CONNECT|Auto Connect mode, corresponding to the **OPTION_AUTO_CONNECT** option in **Initialize**.|
|CLIENT_SERVER_COMM|Client/Server communication mode, where the Server listens on a port and the Client initiates a connection.|

**FEATURE_SUPPORTED / FEATURE_NOT_SUPPORTED**

Constants for the **value** output parameter of **GetCapability**.

```cpp
constexpr int32_t FEATURE_SUPPORTED = 1;
constexpr int32_t FEATURE_NOT_SUPPORTED = 0;
```
