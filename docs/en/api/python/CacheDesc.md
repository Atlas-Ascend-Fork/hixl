# CacheDesc

<!-- md-trans-meta sourceCommit=4b20539fd7785cdac9107eef3999cf284d339c48 translatedAt=2026-10-23T03:54:09.501Z pushedAt=2026-10-27T02:46:33.052Z nl=2 -->

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

## Description

Constructs **CacheDesc**, typically used as a parameter type in **CacheManager.allocate\_cache**.

## Prototype

```python
__init__(self,
                 num_tensors: int,
                 shape: Union[Tuple[int], List[int]],
                 data_type: DataType,
                 placement: Placement = Placement.DEVICE,
                 batch_dim_index: int = 0,
                 seq_len_dim_index: int = -1,
                 kv_tensor_format: str = None)
```

## Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| num_tensors | int | Number of tensors in the cache. Cache operations apply the same operation to every tensor. |
| shape | Union[Tuple[int], List[int]] | Tensor shape. |
| data_type | DataType | Tensor data type. |
| placement | Placement | Device type containing the cache. Default: **Placement.DEVICE**. |
| batch_dim_index | int | Dimension of **batch size** in **shape**. Default: 0, meaning dimension 0. |
| seq_len_dim_index | int | Dimension of **seq_len** in **shape**. Default: -1 (not configured). |
| kv_tensor_format | str | Cache format. Not configured by default. |

## Examples

```python
from llm_datadist import CacheDesc
cache_desc = CacheDesc(80, [4, 2048, 1, 128], DataType.DT_FLOAT16)
```

## Return Value

Returns a **CacheDesc** instance on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

## Constraints

None
