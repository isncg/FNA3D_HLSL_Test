# SDL_GPU Uniform Buffer 描述符集绑定：从 HLSL 到 GPU 的完整链路

## 背景说明

本文档来自 [FNA3D_HLSL](https://github.com/FNA-XNA/FNA3D) 项目的开发实践。FNA3D 是 XNA 4.0 图形 API 的重新实现，FNA3D_HLSL 是其分支，旨在用 DirectX Shader Compiler (DXC) 替代 MojoShader，将 HLSL 着色器编译为 SPIR-V 并在 SDL_GPU 上运行。

在集成过程中，所有使用了 uniform 变量（如 `MatrixTransform`、`WorldViewProj` 等变换矩阵和材质参数）的着色器均无法正常渲染。经过多轮调试，最终定位到 **DXC 生成的 SPIR-V 中 `$Globals` 常量缓冲区的描述符集编号与 SDL_GPU 预期的固定布局不匹配**。

本文既是对这一技术问题的深度剖析，也是整个调试过程的复盘总结。读者无需了解 FNA3D 项目的全部细节，只需要具备 HLSL 着色器编程和基本的现代图形 API（Vulkan/D3D12/Metal）概念即可阅读。

---

## 内容大纲

1. **基础知识**
   - 1.1 HLSL：`register(cN)` 与隐式 `$Globals` 常量缓冲区
   - 1.2 SPIR-V：DXC 如何将 `$Globals` 编译为 SPIR-V
   - 1.3 Vulkan 描述符集（Descriptor Set）基本概念
   - 1.4 SDL_GPU 的固定描述符集布局约定
   - 1.5 绑定冲突的本质

2. **FEB 管线集成**
   - 2.1 FNA3D_HLSL 的着色器管线全景
   - 2.2 `samplers`/`uniforms` 字段的传递链路
   - 2.3 DXC 描述符集修复方案
   - 2.4 `num_samplers >= 1` 的强制要求

3. **调试过程复盘**
   - 3.1 时间线总览
   - 3.2 各阶段详解（共八个阶段）
   - 3.3 有效方法总结
   - 3.4 走过的弯路
   - 3.5 调试模式提炼
   - 3.6 GPU 编程的通用经验

4. **附录：关键文件索引**

5. **延伸阅读**

---

## 1. 基础知识

### 1.1 HLSL：`register(cN)` 与隐式 `$Globals` 常量缓冲区

在 HLSL 中，着色器中的 uniform 变量使用 `register(cN)` 语法声明：

```hlsl
float4x4 MatrixTransform : register(c0);
float3   LightDirection  : register(c4);
float4   DiffuseColor    : register(c5);
```

这种写法源自 Direct3D 9/10 时代的常量寄存器模型：每个 `float4` 占用一个 `c` 寄存器。一个 `float4x4` 矩阵消耗四个连续寄存器（c0–c3），`float3` 消耗一个寄存器，但下一个变量会跳过剩余空间从下一个 `float4` 边界开始。

当 DXC 将这些代码编译到现代 GPU 目标（SPIR-V、DXIL）时，它会将所有 `register(cN)` 变量收集到一个**隐式常量缓冲区**中，命名为 `$Globals`。这个 cbuffer 在原始 HLSL 源代码中没有名字，由编译器自动合成。

等价于显式声明：

```hlsl
cbuffer $Globals : register(b0) {
    float4x4 MatrixTransform;  // offset 0,  matrix stride 16
    float3   LightDirection;   // offset 64
    float4   DiffuseColor;     // offset 80
};
```

`$Globals` 始终被放置在 `register(b0)`（常量缓冲区槽位 0）。这是 DXC 的默认行为，无法通过 HLSL 源码更改，只能通过命令行标志覆盖。

### 1.2 SPIR-V：DXC 如何将 `$Globals` 编译为 SPIR-V

当 DXC 使用 `-spirv` 标志将 HLSL 编译为 SPIR-V 时，生成的模块包含以下关键指令：

```spirv
; 类型声明
OpName %type_Globals "type.$Globals"
OpMemberName %type_Globals 0 "MatrixTransform"
OpDecorate %type_Globals Block                         ; 标记为 UBO
OpMemberDecorate %type_Globals 0 Offset 0
OpMemberDecorate %type_Globals 0 MatrixStride 16       ; 每行 16 字节
OpMemberDecorate %type_Globals 0 RowMajor              ; 行主序

; 变量实例
OpName %_Globals "$Globals"
OpDecorate %_Globals DescriptorSet 0                   ; ← 默认：Set 0
OpDecorate %_Globals Binding 0                         ; ← 默认：Binding 0

%type_Globals = OpTypeStruct %mat4v4float
%_ptr_Uniform_type_Globals = OpTypePointer Uniform %type_Globals
%_Globals = OpVariable %_ptr_Uniform_type_Globals Uniform
```

关键装饰（Decoration）：

- **`Block`**：标记该结构体为 Uniform Buffer Object (UBO)。着色器通过描述符（而非 push constant）读取其数据。
- **`DescriptorSet 0`**、**`Binding 0`**：DXC 的默认值。`$Globals` 的所有成员都位于描述符集 0、绑定点 0。
- **`RowMajor`**、**`MatrixStride 16`**：DXC 以行主序输出矩阵，每行间隔 16 字节（4 个 float）。

DXC 提供了 `-fvk-bind-globals <binding> <set>` 标志来覆盖这些装饰：

```
-fvk-bind-globals 0 3    →  DescriptorSet 3, Binding 0
-fvk-bind-globals 0 1    →  DescriptorSet 1, Binding 0
```

这个标志的存在本身就说明了 DXC 的默认值（Set 0）与分层 GPU API 的布局约定不兼容。不同的 GPU 抽象层会将不同类型的资源分配到不同的描述符集中。

### 1.3 Vulkan 描述符集（Descriptor Set）基本概念

Vulkan 将着色器资源组织为**描述符集**——绑定点按组打包，整组一次性绑定：

```
DescriptorSet 0:
  Binding 0: Combined Image Sampler（纹理采样器）
  Binding 1: Combined Image Sampler（另一个纹理）

DescriptorSet 1:
  Binding 0: Uniform Buffer（UBO，常量缓冲区）
```

每个 `Binding` 都有特定的**描述符类型**（采样器、uniform buffer、storage buffer 等）。不同类型的绑定点可以共存于同一个集内。同一个集内不同绑定点必须使用不同的索引号。

Vulkan 的管线布局（Pipeline Layout）定义了哪些描述符集存在。在实际绘制前，应用程序必须将 `VkDescriptorSet` 绑定到每个需要的集槽位上。如果着色器引用了一个从未被绑定的描述符集，验证层会报告：

> Shader referenced a descriptor set N that was not bound.

如果着色器引用了一个在当前集合布局中不存在的绑定点，验证层会报告：

> Shader referenced a bind M in descriptor set N that does not exist.

### 1.4 SDL_GPU 的固定描述符集布局约定

SDL_GPU 是 SDL3 中抽象 Vulkan、Metal、D3D12 的跨平台 GPU API。它在 Vulkan 后端上对 SPIR-V 着色器**强制使用一套固定的描述符集布局**。这套布局定义在 SDL_shadercross 的源代码（`src/SDL_shadercross.c`，第 1133–1156 行）中，在交叉编译到 Metal 时会被显式校验：

| 资源类型            | 顶点着色器 (VS) | 片元着色器 (PS) | 计算着色器 (CS) |
|--------------------|----------------|----------------|----------------|
| 采样纹理 (Sampled Textures)   | Set 0 | Set 2 | Set 0 |
| 存储纹理 (Storage Textures)   | Set 0 | Set 2 | Set 0 |
| 存储缓冲区 (Storage Buffers)  | Set 0 | Set 2 | Set 0 |
| **Uniform Buffers (UBO)**    | **Set 1** | **Set 3** | **Set 2** |

这套布局是**固定的**。当使用 `SDL_GPU_SHADERFORMAT_SPIRV`（原生 SPIR-V 路径）时，SDL_GPU 将字节码直接传递给 Vulkan 驱动，**不会重新映射绑定点**。SPIR-V 必须已经在代码生成层面符合这套布局。

`SDL_GPUShaderCreateInfo` 结构体中的 `num_samplers`、`num_uniform_buffers` 等字段告诉 SDL_GPU 在每个描述符集中分配多少个绑定点：

```c
createInfo.num_samplers = 1;         // VS: Set 0 有 1 个采样器 / PS: Set 2 有 1 个采样器
createInfo.num_uniform_buffers = 1;  // VS: Set 1 有 1 个 UBO    / PS: Set 3 有 1 个 UBO
```

SDL_GPU 使用这些计数值来：
1. 创建 `VkDescriptorSetLayout` 绑定点——每种资源类型在其对应集合中从绑定点 0 开始连续分配。UBO 绑定点在 UBO 集合中从 binding 0 开始，与采样器绑定点位于不同的描述符集。
2. 为 uniform 数据上传分配内部环形缓冲区空间。

**`SDL_PushGPUVertexUniformData(cmd, slot, data, size)`** 将 `data` 写入顶点 UBO 集合（Set 1）中 `slot` 对应的 uniform buffer。**`SDL_PushGPUFragmentUniformData`** 对片元 UBO 集合（Set 3）执行同样的操作。一次 push 的数据在同一命令缓冲区的后续所有绘制调用中持续有效，直到被新的 push 覆盖或命令缓冲区结束。

### 1.5 绑定冲突的本质

```
DXC 生成：  $Globals → DescriptorSet 0, Binding 0  （"放在 Set 0"）
SDL_GPU 要求：                                  （根据着色器阶段使用不同的集合）
  - 顶点 UBO：DescriptorSet 1
  - 片元 UBO：DescriptorSet 3
```

DXC 的默认行为将 `$Globals` 放在 Set 0——这恰好是顶点着色器的**采样器集合**，或是片元着色器的一个**非 UBO 集合**。实际效果如下表所示：

| 场景 | SPIR-V 中 UBO 位置 | SDL_GPU 实际情况 | 结果 |
|------|--------------------|-----------------|------|
| 无 DXC 标志 | Set 0, Binding 0 | Set 0 = 采样器集合 | 着色器将采样器数据当作 UBO 读取 → 垃圾值 |
| `-fvk-bind-globals 1 0` | Set 0, Binding 1 | Set 0 只有 1 个绑定点（binding 0） | "bind 1 in set 0 不存在" |
| `-fvk-bind-globals 0 1`（片元着色器）| Set 1, Binding 0 | 片元着色器 UBO 应在 Set 3，而非 Set 1 | "descriptor set 1 未被绑定" |

唯一的正确配置：

```spirv
; 片元着色器 (PS)
OpDecorate %_Globals DescriptorSet 3   ; Set 3 = 片元 UBO 集合
OpDecorate %_Globals Binding 0

; 顶点着色器 (VS)
OpDecorate %_Globals DescriptorSet 1   ; Set 1 = 顶点 UBO 集合
OpDecorate %_Globals Binding 0
```

---

## 2. FEB 管线集成

### 2.1 FNA3D_HLSL 的着色器管线全景

```
HLSL 源码 (.hlsl)
  ↓ DXC -spirv -T vs_6_0 / -T ps_6_0  (-fvk-bind-globals 0 {1|3})
SPIR-V 二进制
  ↓ feb_builder.py（读取 .feb.json 清单）
.feb 二进制（Header + StringTable + Parameters + Techniques + Passes + Shaders + SPIR-V）
  ↓ FNA3D_LoadEffect() 解析 → FNA3D_Effect 结构体
  ↓ SDLGPU_CreateEffect() 创建 SDL_GPUShader 对象
  ↓ SDLGPU_ApplyEffect() 每帧推送 uniform 数据
  ↓ DrawIndexedPrimitives → GPU
```

FEB（FNA3D Effect Binary）是本项目的自定义着色器打包格式，64 字节文件头，包含字符串表、参数定义、Technique/Pass 层级结构、着色器元数据和 SPIR-V 二进制数据。详细格式见 `tools/feb_builder.py` 中的实现。

### 2.2 `samplers`/`uniforms` 字段的传递链路

`.feb.json` 清单文件中的每个着色器条目都会声明资源数量：

```json
{
  "vertexShader": {
    "source": "../shaders/sprite_vs.hlsl",
    "entry": "VSMain",
    "samplers": 0,
    "uniforms": 1
  },
  "pixelShader": {
    "source": "../shaders/sprite_ps.hlsl",
    "entry": "PSMain",
    "samplers": 1,
    "uniforms": 0
  }
}
```

这些计数沿整条链路传递：

1. **`feb_builder.py`**：将 `samplers` 和 `uniforms` 作为每个着色器条目的 2×uint32 写入 FEB 二进制文件
2. **`FNA3D_Effect.c`**（解析器）：读入 `FNA3D_EffectShader.samplerCount` 和 `.uniformBufferCount`
3. **`FNA3D_Driver_SDL.c`**（`SDLGPU_CreateEffect`）：在 `SDL_GPUShaderCreateInfo` 中使用这些值：
   ```c
   createInfo.num_samplers = SDL_max(vs->samplerCount, 1);
   createInfo.num_uniform_buffers = vs->uniformBufferCount;
   ```
4. **`SDLGPU_ApplyEffect`**：仅向 `uniformBufferCount > 0` 的阶段有条件地推送 uniform 数据

这种显式声明的方式参考了 MojoShader 的 `samplerSlots` 方案，未来可以进一步优化为从 SPIR-V 自动反射获取。

### 2.3 DXC 描述符集修复方案

在 `tools/feb_builder.py` 的 `compile_hlsl_to_spirv()` 函数中，根据着色器阶段选择正确的描述符集：

```python
# SDL_GPU 固定描述符集布局：
#   顶点着色器：UBO 在 Set 1（采样器在 Set 0）
#   片元着色器：UBO 在 Set 3（采样器在 Set 2）
if stage == "vertex":
    fvk_globals_set = "1"
else:  # pixel
    fvk_globals_set = "3"

cmd = [
    "dxc", "-spirv",
    "-fvk-bind-globals", "0", fvk_globals_set,
    ...
]
```

此修复确保无论是顶点着色器中使用的 uniform（如 `WorldViewProj` 矩阵）还是片元着色器中使用的 uniform（如 matviz 的 `MatrixTransform`）都被 DXC 输出到正确的描述符集中。

### 2.4 `num_samplers >= 1` 的强制要求

即使着色器完全不需要纹理，SDL_GPU 也要求至少有一个采样器槽位（`num_samplers >= 1`），以便正确创建和绑定描述符集。这一点与 MojoShader 参考实现一致——MojoShader 将 `maxSamplerIndex` 初始化为 `0`（而非 `-1`），从而保证 `samplerSlots = maxSamplerIndex + 1 >= 1`。

以下代码确保了这个最小值：

```c
createInfo.num_samplers = SDL_max(vs->samplerCount, 1);
```

如果不设置这个最小值，当 SDL_GPU 未发现任何资源声明时，可能完全跳过描述符集的创建，即便另一个着色器阶段声明了 UBO，也会导致 "descriptor set N 未被绑定" 的错误。

---

## 3. 调试过程复盘

### 3.1 时间线总览

| 阶段 | 假设 | 关键方法 | 结果 |
|------|------|---------|------|
| 1 | 矩阵变换有问题 | 逐个移除变量 | 确认 uniform 是问题所在 |
| 2 | 需要可视化 GPU 端数据 | 编写 matviz 可视化工具 | 初步看到白色对角线，后来回归 |
| 3 | 需要观察 GPU 状态 | RenderDoc 截帧 | 发现描述符集错误和 SPIR-V 反汇编 |
| 4 | 采样器数量不对 | SPIR-V 扫描 → MAX 值 | 错误消失，但屏幕全黑 |
| 5 | 驱动与 MojoShader 有差异 | 逐行对比参考实现 | 发现三个关键差异点 |
| 6 | 绑定位需要偏移 | DXC 标志二分搜索 | 通过错误信息摸清了约束边界 |
| 7 | 描述符集编号有固定规则 | 阅读 SDL_shadercross 源码 | **突破**：找到了权威答案 |
| 8 | 按阶段设置正确的 Set 编号 | 实现最终修复 | UBO 在 RenderDoc 中正确显示 |

### 3.2 各阶段详解

#### 阶段 1：怀疑矩阵变换

**假设**：`MatrixTransform` uniform 没有正确到达顶点着色器。
**测试**：在 C 代码中硬编码单位矩阵，然后完全绕过 `mul(input.Position, MatrixTransform)`。
**结果**：移除矩阵乘法后矩形可见。确认问题是 uniform 相关，而非顶点输入相关。
**方法**：**特性隔离**——逐个排除变量。

#### 阶段 2：Matviz 可视化调试器

**假设**：需要精确看到 GPU 着色器实际接收到的 `MatrixTransform` 数值，且不能依赖顶点坐标变换。
**设计**：全屏四边形 + 纯透传 VS + 将矩阵值渲染为灰度格的 PS（4×4 网格，UV → 矩阵行列索引）。
**结果**：初版成功显示白色对角线，但后续修改导致回归成全黑或闪烁。
**方法**：**可视化调试工具**——将不可见的 GPU 状态转换为可见像素。matviz 工具在整个调试过程中都是必不可少的验证手段。

#### 阶段 3：描述符集错误

**假设**：SDL_GPU 的渲染状态管理有问题。
**测试**：RenderDoc 截帧分析。
**结果**：发现关键错误——"Shader referenced a descriptor set 0 that was not bound"。PS 的 Uniform Buffers 列表为空。Pipeline State → FS 显示了 SPIR-V 反汇编，`$Globals` 位于 DescriptorSet 0, Binding 0。
**方法**：**RenderDoc**——GPU 调试的必备工具。可以查看 SPIR-V 反汇编、描述符集状态、管线布局和所有验证错误。

#### 阶段 4：SPIR-V 扫描与 MAX 采样器值

**假设**：SPIR-V 自动扫描得到的 `num_samplers=0`（对于无纹理着色器）导致 SDL_GPU 跳过描述符集的创建。
**测试**：将 PS 的 `num_samplers` 设为 `MAX_TEXTURE_SAMPLERS`（16）。
**结果**：断言失败——"Missing vertex sampler binding"（VS 的 `MAX_VERTEXTEXTURE_SAMPLERS` 仅为 4，两者的最大值不匹配）。调整 VS 使用 4、PS 使用 16 后，描述符集错误消失，但屏幕全黑。
**方法**：**暴力参数搜索**——过度分配资源暴露了依赖关系，但副作用说明问题更深。

#### 阶段 5：MojoShader 参考实现对比

**假设**：FNA3D_HLSL 驱动的 `num_samplers`/`num_uniform_buffers` 处理与经过验证的 MojoShader 实现存在关键差异。
**测试**：逐行对比 `mojoshader_sdlgpu.c`。
**发现**：
1. MojoShader 将 `maxSamplerIndex` 初始化为 `0` → `samplerSlots` 始终 ≥ 1，以此为最低保障
2. MojoShader 对 VS 始终设置 `num_uniform_buffers = 1`，对 PS 不设置（默认为 0）
3. MojoShader 在推送 uniform 数据前检查每个阶段的 `uniformBufferSize > 0`
**方法**：**参考实现对比**——效率最高的单一步骤。MojoShader 在 SDL_GPU 上经过了长期验证；与其不一致的地方都值得怀疑。

#### 阶段 6：DXC 标志参数空间搜索

**假设 A**：将 `$Globals` 移到 Set 0 内的另一个绑定位可以避开采样器冲突。
**测试**：`-fvk-bind-globals 1 0`（Set 0, Binding 1）。
**结果**："bind 1 in descriptor set 0 不存在"——Set 0 只有 1 个绑定位（binding 0 是采样器）。

**假设 B**：UBO 与采样器位于不同的描述符集。
**测试**：`-fvk-bind-globals 0 1`（Set 1, Binding 0）。
**结果**："descriptor set 1 未被绑定"——SDL_GPU 只绑定了 Set 0。片元着色器的 UBO 应该在 Set 3，而非 Set 1。
**方法**：**错误驱动的参数空间探索**——每条验证错误都刻画了解决空间的一个边界。

#### 阶段 7：突破——SDL_shadercross 源码

**假设**：SDL_GPU 的描述符集编号在源码中有明确记录。
**发现**：SDL_shadercross 的源代码（`src/SDL_shadercross.c`）在将 SPIR-V 交叉编译到 MSL 时包含显式的描述符集验证代码：

```c
if (!(descriptor_set_index == 1 || descriptor_set_index == 3)) {
    SDL_SetError(
        "Descriptor set index for graphics uniform buffer must be 1 or 3!");
    return NULL;
}
```

这揭示了**完整的布局**：顶点着色器 UBO 在 Set 1，片元着色器 UBO 在 Set 3。进一步分析还确认了采样器集合分别是 Set 0（VS）和 Set 2（PS）。
**方法**：**源码考古**——权威答案在实现中，不在文档中。

#### 阶段 8：最终修复

针对 VS 和 PS 使用不同的 `-fvk-bind-globals` 集合编号后，RenderDoc 的 Pipeline State → FS → Uniform Buffers 中正确显示了 `MatrixTransform` 及其单位矩阵的值。

### 3.3 有效方法总结

| 方法 | 有效性 | 原因 |
|------|--------|------|
| **RenderDoc 截帧** | ★★★★★ | 直接显示 SPIR-V 反汇编、描述符集状态、验证错误——是观察 GPU 内部状态的窗口 |
| **参考实现对比（MojoShader）** | ★★★★★ | 消除了 SDL_GPU API 使用方式上的猜测 |
| **SDL_shadercross 源码分析** | ★★★★★ | 权威答案：描述符集布局硬编码在此 |
| **Matviz 可视化调试器** | ★★★★☆ | 将不可见的 GPU 状态转换为可见像素，且是最小可复现用例 |
| **CPU 端日志** | ★★★☆☆ | 确认 CPU 端数据正确，将搜索范围缩小到 GPU 端 |
| **特性隔离** | ★★★☆☆ | 移除矩阵乘法直接证明了 uniform 是问题所在 |

### 3.4 走过的弯路

| 尝试 | 失败原因 |
|------|---------|
| HLSL 中使用 `[[vk::push_constant]]` | SDL_GPU 的 uniform 推送 API 操作的是 UBO，而非 push constant——数据写入了 UBO，但着色器从 push constant 读取（空值）|
| `-fvk-bind-globals 100 0` | 将 UBO 移到 binding 100，但 SDL_GPU 仍向 binding 0 推送数据 → GPU 页错误（GCVM_L2_PROTECTION_FAULT），系统崩溃 |
| SPIR-V 扫描得到 `num_samplers=0` | SDL_GPU 要求 `num_samplers >= 1` 才能创建描述符集；0 个采样器意味着完全不创建描述符集 |
| 使用 MAX 采样器数量 | 过度分配勉强绕过了描述符集问题，但掩盖了真正的绑定冲突——屏幕仍然全黑 |
| 对片元着色器使用 `-fvk-bind-globals 0 1` | Set 1 是**顶点**着色器的 UBO 集合；片元着色器需要使用 Set 3 |

### 3.5 调试模式提炼

整个调试过程遵循了一条逐层深入的路径，这在 GPU 编程问题中具有相当的普适性：

```
表面症状（黑屏，无渲染输出）
  → 特性隔离（移除矩阵乘法 → 矩形出现 → uniform 是问题）
    → 可视化调试（matviz：将 uniform 值渲染为颜色）
      → GPU 调试器（RenderDoc：描述符集错误、SPIR-V 反汇编）
        → API 使用方式审计（对比 MojoShader 参考实现）
          → 参数空间探索（DXC 标志二分搜索）
            → 实现代码考古（SDL_shadercross 源码）
              → 根本原因（描述符集不匹配）
                → 修复
```

每一层调试都为问题空间提供了更清晰的约束，直到根因被唯一确定。关键是：**不要停留在症状层面修修补补，而要沿着数据路径逐层深入，直到找到合同（shader ↔ API 层之间的约定）破裂的位置**。

### 3.6 GPU 编程的通用经验

1. **描述符集布局是 API 特定的，通常文档不全。**当分层 API（SDL_GPU）封装原生 API（Vulkan）时，绑定约定是着色器编译器与 API 层之间的隐性合同。DXC 的默认值不匹配任何特定的分层 API——必须显式覆盖。

2. **验证层错误是地图，不是噪音。**每条错误消息（"Set N 未被绑定"、"Binding M 不存在"）都刻画了有效参数空间的一个边界。系统性理解这些边界就能定位问题。

3. **参考实现是唯一的事实来源。**MojoShader 已经与 SDL_GPU 配合工作多年。与其不一致的地方都需要强有力的理由。凭空猜测 API 的行为方式几乎不会成功。

4. **可视化调试工具投入产出比极高。**matviz 测试只花了几分钟编写，但通过将不可见的状态问题变为可见的图像问题，大幅缩短了调试周期。

5. **源码优于文档。**仅凭 SDL_gpu.h 头文件无法得知描述符集布局。权威答案在 SDL_shadercross 的 MSL 交叉编译验证代码中。

---

## 附录：关键文件索引

| 文件 | 作用 |
|------|------|
| `tools/feb_builder.py` | 通过 DXC 将 HLSL 编译为 SPIR-V，组装 FEB 二进制文件。包含 `-fvk-bind-globals` 修复。 |
| `FNA3D_HLSL/src/FNA3D_Driver_SDL.c` | SDL_GPU 后端。`SDLGPU_CreateEffect` 使用正确的 `num_samplers`/`num_uniform_buffers` 创建着色器。`SDLGPU_ApplyEffect` 按阶段推送 uniform 数据。 |
| `FNA3D_HLSL/src/FNA3D_Effect.h` | `FNA3D_EffectShader` 结构体，包含 `samplerCount` 和 `uniformBufferCount` 字段。 |
| `FNA3D_HLSL/src/FNA3D_Effect.c` | FEB 解析器——从二进制格式中读取着色器资源计数。 |
| `FNA3D/MojoShader/mojoshader_sdlgpu.c` | 参考实现——经过验证的 SDL_GPU effect 集成方式。 |
| `SDL_shadercross/src/SDL_shadercross.c` | SDL_GPU 的 SPIR-V 交叉编译器——包含权威的描述符集布局验证代码。 |

---

## 延伸阅读

以下资源适合希望补全相关基础知识的读者。

### HLSL 与 DXC

- [DirectX Shader Compiler (DXC) 官方仓库](https://github.com/microsoft/DirectXShaderCompiler) —— DXC 的源码、Release 和 Wiki 文档
- [HLSL for Vulkan: SPIR-V Code Generation in DXC](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst) —— DXC 官方文档，说明 HLSL 到 SPIR-V 的映射规则，包括 `-fvk-bind-globals` 等 Vulkan 专用标志
- [Microsoft HLSL 语言参考](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-reference) —— HLSL 语法和内建函数的完整参考

### SPIR-V

- [SPIR-V 规范 (Khronos)](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html) —— SPIR-V 二进制格式和指令集的正式规范
- [SPIR-V 指南 (Google/ARM)](https://github.com/KhronosGroup/SPIRV-Guide) —— SPIR-V 编程的实用指南，包含着色器资源绑定等主题
- [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) —— `spirv-dis`（反汇编）、`spirv-val`（验证）和 `spirv-opt`（优化）工具集

### Vulkan 描述符集与管线布局

- [Vulkan 规范：描述符集](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#descriptorsets) —— 描述符集布局、绑定点、管线布局的正式规范
- [Vulkan 教程：描述符集布局与绑定](https://vulkan-tutorial.com/Uniform_buffers/Descriptor_layout_and_buffer) —— 从零开始讲解 uniform buffer 的描述符集配置
- [Vulkan Validation Layers](https://github.com/KhronosGroup/Vulkan-ValidationLayers) —— 验证层的源码和文档，本文中多次出现的 "Shader referenced..." 错误即由此产生

### SDL_GPU 与 SDL3

- [SDL3 GPU API 参考](https://wiki.libsdl.org/SDL3/CategoryGPU) —— SDL_GPU 的官方文档
- [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) —— SDL_GPU 的 SPIR-V 交叉编译器，处理 SPIR-V 到 MSL/DXIL/DXBC 的转换
- [SDL3 官方仓库](https://github.com/libsdl-org/SDL) —— SDL3 源码

### GPU 调试

- [RenderDoc](https://renderdoc.org/) —— 跨平台图形调试器，支持 Vulkan/D3D12/Metal/OpenGL
- [RenderDoc 文档](https://renderdoc.org/docs/) —— 使用指南和 API 参考

### FNA 与 XNA

- [FNA3D 仓库](https://github.com/FNA-XNA/FNA3D) —— FNA 的 3D 图形抽象层
- [MojoShader](https://github.com/icculus/mojoshader) —— 原本为 FNA 生成着色器字节码的 HLSL 编译器（FNA3D_HLSL 项目正是为了替代它）

