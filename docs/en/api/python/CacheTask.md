# CacheTask

<!-- md-trans-meta sourceCommit=4b20539fd7785cdac9107eef3999cf284d339c48 translatedAt=2026-10-23T03:54:09.507Z pushedAt=2026-10-27T02:46:33.137Z nl=2 -->

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

<!-- npu="910b" id4 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id4 -->

## CacheTask Constructor

Constructs **CacheTask**, returned by **CacheManager.transfer**, representing an asynchronous layer-by-layer transfer task.

## synchronize

**Description**

Waits for all layers to finish transferring and obtains the overall result.

**Prototype**

```python
synchronize(timeout_in_millis: Optional[int] = None) -> LLMStatusCode
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| timeout_in_millis | Optional[int] | Wait timeout, in milliseconds. Default: **None** (no timeout). |

**Example**

```python
ret = cache_task.synchronize()
```

**Return Value**

Normally returns an **LLMStatusCode** value.

Raises **TypeError** or **ValueError** for an incorrect input data type.

**Constraints**

None

## get\_results

**Description**

Waits for all layers to finish transferring and obtains the execution result for each **TransferConfig**.

**Prototype**

```python
get_results(timeout_in_millis: Optional[int] = None) -> List[LLMStatusCode]
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| timeout_in_millis | Optional[int] | Wait timeout, in milliseconds. Default: **None** (no timeout). |

**Example**

```python
rets = cache_task.get_results()
```

**Return Value**

Normally returns a list of **LLMStatusCode** values for the corresponding **TransferConfig** transfers.

If no transfer has started for a layer corresponding to a **TransferConfig**, its result is **None**.

Raises **TypeError** or **ValueError** for an incorrect input data type.

**Constraints**

None
