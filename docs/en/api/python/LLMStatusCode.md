# LLMStatusCode

<!-- md-trans-meta sourceCommit=c54b3df96df6822272b59eb9ec0a55fdd6049157 translatedAt=2026-10-23T03:54:09.543Z pushedAt=2026-10-27T02:46:33.367Z nl=2 -->

Enum corresponding to **status\_code** in **LLMException**. Values and solutions are listed below.

| Enum Value | Meaning | Recoverable | Solution |
| --- | --- | --- | --- |
| LLM_SUCCESS | Success | None | None |
| LLM_FAILED | General failure | No | Restart the machine or container.<br>Preserve the environment, obtain Host/Device logs, and back them up. |
| LLM_PARAM_INVALID | Invalid parameter | Yes | Troubleshoot the cause using the logs. |
| LLM_KV_CACHE_NOT_EXIST | KV does not exist | Yes | - Check whether the request in the Prompt-side error log has completed.<br>  - Check for duplicate pulls.<br>  - Check whether the parameters identifying the destination cache are incorrect. |
| LLM_REPEAT_REQUEST | Duplicate request | Yes | Check for duplicate calls. |
| LLM_NOT_YET_LINK | Not connected | Yes | Check the Decode-Prompt connection at the upper layer. |
| LLM_ALREADY_LINK | Already connected | Yes | Check the Decode-Prompt connection at the upper layer. |
| LLM_LINK_FAILED | Connection establishment failed | Yes | If this code appears in the second return value of **link_clusters**, check network connectivity between the corresponding clusters. |
| LLM_UNLINK_FAILED | Disconnection failed | Yes | If this code appears in the second return value of **unlink_clusters**, check network connectivity between the corresponding clusters. |
| LLM_NOTIFY_PROMPT_UNLINK_FAILED | Failed to notify the Prompt side to disconnect | Yes | 1. Check network connectivity between Decode and Prompt.<br>  2. Call **unlink_clusters** on the Prompt side to clean up remaining resources. |
| LLM_CLUSTER_NUM_EXCEED_LIMIT | Cluster count exceeds the limit. | Yes | Check **link_clusters** and **unlink_clusters** parameters. **clusters** cannot contain more than 16 entries. |
| LLM_PROCESSING_LINK | Connection establishment in progress | Yes | Connection establishment or disconnection is in progress. Retry later. |
| LLM_PREFIX_ALREADY_EXIST | Prefix already exists | Yes | Check whether a shared prefix with the same Prefix ID is already loaded. If so, release it first. |
| LLM_PREFIX_NOT_EXIST | Prefix does not exist | Yes | Check whether the Prefix ID in the Request has been loaded. |
| LLM_EXIST_LINK | Unreleased connections exist during **switch_role**. | Yes | Check whether **unlink_clusters** has been called to disconnect all connections before switching the current LLMDataDist role. |
| LLM_FEATURE_NOT_ENABLED | Feature not enabled | Yes | Check that required options were passed during LLMDataDist initialization.<br>If this exception is raised while switching the current LLMDataDist role, check whether **LLMConfig.enable_switch_role = True** was set during initialization. |
| LLM_TIMEOUT | Processing timed out | Yes | - If returned by a transfer API such as **pull_cache**, **pull_blocks**, or **transfer_cache_async**, the connection is unrecoverable and must be reestablished.<br>  - For other APIs, increase the timeout and retry. |
| LLM_LINK_BUSY | Connection busy | Yes | Check for conflicting concurrent API calls. For example, this error code is returned when the following APIs are called concurrently:<br>Concurrent **unlink** and **pull_cache**.<br>Concurrent **pull_cache** and **transfer_cache_async** on the same connection. |
| LLM_OUT_OF_MEMORY | Insufficient memory | Yes | Check that the memory pool can hold the requested KV size.<br>Check whether allocated memory has not been freed. |
| LLM_DEVICE_MEM_ERROR | Faulty virtual address from a memory UCE (an error that system hardware cannot directly recover from). | Yes | Obtain and repair the faulty virtual address from the memory UCE. For KV Cache memory, also call the cache manager's **remap_registered_memory** API to repair the KV Cache memory registered with the NIC.<br>Note: This error code is reserved and currently unsupported. |
| LLM_SUSPECT_REMOTE_ERROR | Suspected memory UCE | No | The upper-layer framework must assess this alongside other faults to determine whether it is a memory UCE or another fault. |
| LLM_UNKNOWN_ERROR | Unknown error | No | Preserve the environment, obtain the Host/Device logs, and back them up. |

Follow **torch\_npu.npu.restart\_device** in [PyTorch](https://www.hiascend.com/developer/software/ai-frameworks/pytorch) to obtain and repair faulty virtual addresses from memory UCEs.
