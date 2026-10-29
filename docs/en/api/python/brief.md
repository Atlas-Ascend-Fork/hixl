# Overview

<!-- md-trans-meta sourceCommit=4b20539fd7785cdac9107eef3999cf284d339c48 translatedAt=2026-10-23T03:54:09.500Z pushedAt=2026-10-27T02:46:33.557Z nl=2 -->

- The following products are supported.

  <!-- npu="910b" id1 -->
  - Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported. Only D2D is supported when the Server uses the HCCS transport protocol in this scenario.
  <!-- end id1 -->
  <!-- npu="A3" id2 -->
  - Atlas A3 products: Host memory is unsupported as a remote Cache when HCCS is used.
  <!-- end id2 -->
  <!-- npu="950" id3 -->
  - Ascend 950PR&950DT products: Use UB within a supernode and RoCE between supernodes.
  <!-- end id3 -->
- The [Ascend release package](https://www.hiascend.com/cann/download) supports only Python 3.12. Source builds support Python 3.9–3.14. For another Python version, follow [Build from Source Code](../../build.md) to build and install. For Python installation, see the [Python website](https://www.python.org/).
  <!-- npu="A3,910b" id4 -->
- Up to 50GB of Device memory can be registered. The Host memory limit is 20GB for HDK earlier than 25.5 and 1TB for HDK 25.5 or later. Registering more memory consumes more OS memory. This constraint applies to the following product forms:
  <!-- npu="910b" id5 -->
  - Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
  <!-- end id5 -->
  <!-- npu="A3" id6 -->
  - Atlas A3 products
  <!-- end id6 -->
  <!-- end id4 -->
