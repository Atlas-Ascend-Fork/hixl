# Quick Start

<!-- md-trans-meta sourceCommit=1cdc56e1cc2cc477cd62ac114741ef2cd282dfed translatedAt=2026-10-23T03:54:09.559Z pushedAt=2026-10-27T02:46:33.880Z nl=2 -->

## Overall Development Process

**Overview**

For the following descriptions, inference frameworks are abstracted into these modules:

- Resource initialization module.
- KV Cache management module, including memory creation, allocation (PA scenarios), and destruction.
- Model inference module.
- Resource release module.

This section describes how to enable LLM-DataDist in an inference framework.

**Development Process**

1. Locate the framework's resource initialization module and call LLM-DataDist initialization and connection APIs during this phase.
2. Locate the inference framework's KV Cache management module and call LLM-DataDist KV Cache registration APIs to register user-allocated KV Cache memory.
3. The inference framework must separate the Prefill and Decode phases and deploy their inference scripts on different cluster nodes. Before Decode, receive Prefill output as input and use LLM-DataDist KV Cache transfer APIs to pull or push the KV Cache stored on Prefill.
4. Run the Prefill and Decode inference scripts separately.
5. Release LLM-DataDist resources in the framework's resource release module.

## Preparing the Environment

The following products are supported:

<!-- npu="910b" id1 -->
- Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported. With HCCS within the Server, only D2D is supported.
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Host memory is unsupported as a remote Cache when HCCS is used.
<!-- end id2 -->
<!-- npu="950" id3 -->
- Ascend 950PR&950DT products use UB within a supernode and RoCE between supernodes.
<!-- end id3 -->

Install the driver, firmware, and CANN following [CANN Software Installation](https://www.hiascend.com/document/detail/en/CANNCommunityEdition/920/softwareinst/instg/instg_0000.html?OS=openEuler&InstallType=netyum).

In heterogeneous scenarios, the receiver and sender must use the same MTU (maximum transmission unit) configuration. For details on querying and configuring the MTU, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference).

Use `hccn_tool` to query Device IP addresses and test inter-device connectivity. Devices across clusters must have RDMA connections to enable LLM-DataDist. For details, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference). Common commands are listed below.

|Command|Scenarios|
|--|--|
|hccn_tool [-i %d] -link -g|Queries the specified Device network port Link status. `-i` specifies the Device. Example: `hccn_tool -i 0 -link -g`.|
|hccn_tool [-i %d] -ip -g|Queries the IP address and subnet mask. `-i` specifies the Device. Example: `hccn_tool -i 0 -ip -g`.|
|hccn_tool [-i %d] -ip -inet6 -g|Queries the IPv6 address and subnet mask. `-i` specifies the Device. Example: `hccn_tool -i 0 -ip -inet6 -g`.|
|hccn_tool [-i %d] -ping -g [address %s ]|Queries ping results from a Device to a destination. `-i` specifies a Device on the current server, and `address` specifies the destination. Example: `hccn_tool -i 0 -ping -g address 192.168.2.1`.|

The following environment variables are also used. For details, see [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/9.2.0/docs/en/user_guide/README.md).

|Name|Scenarios|
|--|--|
|HCCL_RDMA_TC, HCCL_RDMA_SL|If the customer has planned the parameter-plane network and specified traffic types and priorities for various services, use these two variables to set the traffic class and priority of parameter-plane collective communication to match that plan.|
|HCCL_RDMA_RETRY_CNT, HCCL_RDMA_TIMEOUT|These specify RDMA hardware retry count and retry timeout. Excessive values make the system insensitive to network anomalies and unable to detect network faults. If the values are too small, NIC hardware may fail to mask transient network interruptions, directly disrupting the application. Choose appropriate values based on your network conditions. For example, configure these values based on the duration range of most transient interruptions. To reduce the impact of network jitter, we recommend configuring HCCL_RDMA_TIMEOUT using the following formula: HCCL_RDMA_TIMEOUT=log2(pull KV timeout * 10^6 / (HCCL_RDMA_RETRY_CNT + 1) / 4.096), rounded up. When both the pull KV timeout and HCCL_RDMA_RETRY_CNT have their default values, a value of 15 is recommended for HCCL_RDMA_TIMEOUT.|
|HCCL_INTRA_ROCE_ENABLE|Controls whether devices within a Server communicate over RoCE loopback.|
|AUTO_USE_UC_MEMORY|Controls whether operators may move data without passing through L2 Cache. If unset before using LLM-DataDist, LLM-DataDist sets it to 0, requiring all operator data movement to pass through L2 Cache.|

## Complete Example

Using the transformers LLAMA model, this example highlights script changes before and after PD disaggregation, showing how to adapt a colocated script to a PD-disaggregated script. Visit [Gitee](https://gitee.com/ascend/torchair/tree/master#version_match), download the sample package matching the version compatibility table, and obtain examples from `npu_tuned_model/llm/llama/benchmark/pd_separate`. The examples separate full and incremental models and run them on different cluster nodes.

The service layer schedules the disaggregated scripts through the inference process as follows.

1. When a user request arrives, the service layer schedules it on the Prompt cluster for full inference, then sends information needed by the incremental script to the Decode cluster node. The Prompt cluster node can then accept another request.
2. The Decode cluster receives the corresponding request information, pulls the corresponding request's KV Cache already computed on the Prompt cluster nodes, batches requests according to the incremental model's batch size, and runs incremental inference.
3. When a request on the Decode cluster completes and frees a batch slot, receive a new request from the Prompt cluster and repeat steps 2 and 3.
4. The Prompt cluster repeats step 1 and the Decode cluster repeats steps 2 and 3 until the application finishes, then both clusters exit.

For more examples, visit [GitCode](https://gitcode.com/cann/hixl), select the matching version, and obtain examples from `examples/python`.
