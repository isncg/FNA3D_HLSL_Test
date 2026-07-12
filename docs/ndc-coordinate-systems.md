# NDC 坐标系统与跨图形 API 开发陷阱

## 为什么要写这篇文档

本项目使用 **HLSL 编写着色器**，通过 **DXC 编译为 SPIR-V**，最终运行在 **Vulkan** 后端。表面上看你写的是标准的 D3D 风格 HLSL，但实际执行环境是 Vulkan——两者在坐标系统、纹理方向、深度范围等方面存在微妙但关键的差异。

如果你过去主要接触 D3D 或 OpenGL，按照过往经验写出的全屏 Quad、深度采样、屏幕空间重建等代码，在 Vulkan 上很可能是**上下颠倒的**。更隐蔽的是，这种颠倒不一定会导致程序崩溃或产生明显错误——它会产生"看起来差不多对"但实际**完全错误**的结果（比如 SSAO 亮度随相机角度变化这类诡异 bug）。

本文档从最基础的概念讲起，逐步深入到本项目的实际案例，帮助后续开发者理解并避免这类问题。

---

## 第一章：渲染管线中的坐标空间

### 1.1 渲染管线坐标空间总览

一个顶点从模型文件到最终呈现在屏幕上，需要经过多个坐标空间：

```
模型空间 (Model Space)
    ↓  × Model Matrix
世界空间 (World Space)
    ↓  × View Matrix
观察空间 (View Space) / 相机空间 (Camera Space)
    ↓  × Projection Matrix
裁剪空间 (Clip Space)
    ↓  ÷ w (透视除法 Perspective Divide)
NDC (Normalized Device Coordinates)
    ↓  视口变换 (Viewport Transform)
屏幕空间 (Screen Space) / 帧缓冲坐标 (Framebuffer Coordinates)
```

**NDC（标准化设备坐标）** 是透视除法之后、视口变换之前那个统一的立方体空间。它是 GPU 管线中的一个"枢纽"——无论你的场景多大、相机在哪、投影矩阵怎么设，到了 NDC 这一步，所有可见的几何体都被映射到一个固定范围的立方体内。

### 1.2 NDC 的定义

NDC 是一个三维立方体，各轴的范围由图形 API 规定：

| 轴 | 范围（因 API 而异） |
|----|---------------------|
| X  | `[-1, 1]` —— 所有主流 API 一致 |
| Y  | `[-1, 1]` —— 范围一致，但**方向**有差异（见下文） |
| Z  | `[-1, 1]`（OpenGL）或 `[0, 1]`（D3D / Vulkan / Metal） |

**X 轴**在所有 API 中都没有歧义：`-1` 是左，`+1` 是右。

**Y 轴**在范围上一致（都是 `[-1, 1]`），但"哪个方向是 +Y" 不同：

- **D3D** 和 **OpenGL**：NDC 的 **+Y 指向上方**（Y-up）。即屏幕底部是 `y = -1`，屏幕顶部是 `y = +1`。
- **Vulkan**：NDC 的 **+Y 指向下方**（Y-down）。即屏幕顶部是 `y = -1`，屏幕底部是 `y = +1`。

**Z 轴**：
- **OpenGL**：深度范围为 `[-1, 1]`，近平面为 `-1`，远平面为 `+1`。
- **D3D / Vulkan / Metal**：深度范围为 `[0, 1]`，近平面为 `0`，远平面为 `1`。

### 1.3 从裁剪空间到 NDC：透视除法

顶点着色器输出的是**裁剪空间坐标**（HLSL 中的 `SV_POSITION` 语义）。GPU 硬件自动执行透视除法：

```
ndc.x = clip.x / clip.w
ndc.y = clip.y / clip.w
ndc.z = clip.z / clip.w
```

注意：**你的 shader 代码不需要做这个除法**，它是 GPU 固定功能管线的一部分。但当你需要从 NDC 反推 view-space 位置时（后文会讲），你就需要手动做逆运算。

### 1.4 从 NDC 到屏幕：视口变换

视口变换将 NDC 映射到帧缓冲的像素坐标。以 Vulkan 为例（假设 viewport 覆盖整个窗口）：

```
pixelX = (ndc.x * 0.5 + 0.5) * viewportWidth  + viewportX
pixelY = (ndc.y * 0.5 + 0.5) * viewportHeight + viewportY   // ← 方向取决于 API
pixelZ = ndc.z * (maxDepth - minDepth) + minDepth
```

其中 `viewportX/Y` 是视口左上角在帧缓冲中的位置。在大多数情况下视口覆盖整个窗口，即 `viewportX=0, viewportY=0, viewportWidth=fbWidth, viewportHeight=fbHeight`。

简化后：
```
pixelX = (ndc.x * 0.5 + 0.5) * fbWidth
pixelY = (ndc.y * 0.5 + 0.5) * fbHeight
```

---

## 第二章：各图形 API 的 Y 轴方向详解

### 2.1 为什么 Y 轴方向不一致

这主要是历史原因。

**OpenGL** 诞生于 1992 年，它遵循数学惯例：笛卡尔坐标系 Y 轴向上。OpenGL 将帧缓冲的原点放在**左下角**，(0,0) 在左下，(width, height) 在右上。这与数学中"原点在左下，X 向右，Y 向上"一致。因此 OpenGL NDC 的 Y-up 与帧缓冲的 Y-up 是**自然对齐**的。

**Direct3D** 诞生于 1995 年，当时 Windows 的 GDI 和窗口系统使用**左上角**作为原点。为了与 Windows 窗口系统保持一致，D3D 将帧缓冲原点放在左上角。但 D3D 选择了保持 NDC Y-up（与 OpenGL 一致），然后在视口变换中进行翻转——这要求投影矩阵在 Y 分量上取反。

**Vulkan**（2016 年）决定"少一层间接"，让 NDC 的 Y 轴直接与帧缓冲的 Y 轴对齐——帧缓冲原点在左上角，所以 NDC 的 +Y 指向下方。这意味着**不需要在视口变换或投影矩阵中做任何 Y 翻转**。

### 2.2 各 API 对比表

| 特性 | OpenGL | Direct3D 11/12 | Vulkan | Metal |
|------|--------|---------------|--------|-------|
| NDC Y 方向 | **Y-up**（+Y 朝上） | **Y-up**（+Y 朝上） | **Y-down**（+Y 朝下） | **Y-up**（+Y 朝上） |
| NDC Z 范围 | `[-1, 1]` | `[0, 1]` | `[0, 1]` | `[0, 1]` |
| 帧缓冲原点 | 左下角 | 左上角 | 左上角 | 左上角 |
| 纹理坐标原点 | (0,0) 左下 | (0,0) 左上 | (0,0) 左上 | (0,0) 左上 |
| 投影矩阵需翻转 Y | 否 | **是** | 否 | 否 |
| 视口变换需翻转 Y | 否 | **是** | 否 | 否 |

一张图胜过千言万语：

```
        OpenGL / D3D NDC              Vulkan NDC
             ┌─────┐                    ┌─────┐
       y=+1  │  ▲  │              y=-1  │  ▲  │
             │  │  │                    │  │  │
       y=0   │  │  │              y=0   │  │  │
             │  │  │                    │  │  │
       y=-1  │  ●  │              y=+1  │  ●  │
             └─────┘                    └─────┘
         x=-1 → x=+1               x=-1 → x=+1
```

在 OpenGL/D3D 的 NDC 中，**y=+1 是屏幕顶部**。在 Vulkan NDC 中，**y=+1 是屏幕底部**。

### 2.3 纹理坐标系

除了 NDC，纹理坐标系也有差异：

| API | UV (0,0) 位置 | V 轴方向 |
|-----|--------------|---------|
| OpenGL | **左下** | V 向上 |
| D3D 10/11/12 | **左上** | V 向下 |
| Vulkan | **左上** | V 向下 |
| Metal | **左上** | V 向下 |

这意味着：**在 Vulkan 中，NDC 的 +Y 方向（下）和纹理 V 轴方向（下）是一致的**。帧缓冲的顶部对应 NDC y=-1，也对应纹理 V=0。

---

## 第三章：本项目面临的特殊情况

### 3.1 项目技术栈

```
HLSL 源码 (.hlsl)
  → DXC -spirv -T vs_6_0 / -T ps_6_0
    → SPIR-V 二进制
      → feb_builder.py 打包进 .feb
        → FNA3D_CreateEffect() 加载
          → SDL3 GPU (Vulkan 后端) 执行
```

这个链路的几个关键点：
- 你编写的是 **D3D 风格 HLSL**（`SV_POSITION`、`register(c0)`、`Texture2D` 等）
- **DXC** 是微软的 HLSL 编译器，原生支持 SPIR-V 输出。它**按照 D3D 的语义**来理解你的代码
- Vulkan 的 **NDC 是 Y-down**，这与 D3D 的 Y-up 不同

### 3.2 SDL3 GPU 的"透明转换"

SDL3 GPU 的文档称它会自动处理坐标系统差异（"You do not need to flip Y in shaders"）。具体来说，SDL3 GPU 在 Vulkan 后端会自动设置一个**翻转 Y 的视口变换**，使得：

- 你的 shader 中 `SV_POSITION.y` 的 **+1 依然是屏幕顶部**（就像 D3D/OpenGL 一样）
- 你的顶点数据、投影矩阵都**不需要手动翻转 Y**

> SDL_GPU.h 原文：
> "NDC: Left-handed, lower-left = (-1,-1), upper-right = (1,1). Z is [0,1] (near=0)."
> "Viewport: Top-left = (0,0), +Y is down."
> "SDL automatically converts the coordinate system when the backend differs."

SDL3 的描述说 NDC 是 Y-up 的（lower-left = (-1,-1)），这意味着 SDL3 在 Vulkan 之上提供了一个 Y-up NDC 的抽象层。它通过调整视口变换来实现这一点。

### 3.3 关键陷阱：SDL3 只帮你翻转了 NDC，没帮你翻转纹理！

这是最重要的认知：**SDL3 GPU 的自动坐标转换只影响顶点着色器输出的 `SV_POSITION` 到帧缓冲像素的映射。它不会自动翻转你手动计算的 UV 坐标。**

当你写一个全屏 Quad 的顶点着色器时：

```hlsl
// ssao_vs.hlsl
output.Position = float4(input.Position, 1.0);  // 直传 NDC 位置
output.UV = input.TexCoord;                      // 直传 UV 坐标
```

`SV_POSITION` 会被 SDL3 正确映射到屏幕像素。但是 `UV` 值是**你自己定义的**，SDL3 不会碰它。如果你的 UV 与帧缓冲的纹理坐标约定不一致，采样就会出错。

---

## 第四章：本项目的 Bug 详细分析

### 4.1 项目设置

本项目的 SSAO（屏幕空间环境光遮蔽）渲染分为两步：

1. **G-Buffer Pass**：将场景几何（茶壶 + 地板）渲染到两张离屏纹理：
   - `rtNormal` (RGBA8)：存储 view-space 法线
   - `rtDepth` (R32F)：存储 view-space 线性深度

2. **SSAO Pass**：用全屏 Quad 画一个覆盖屏幕的矩形，像素着色器从 G-Buffer 采样法线和深度，计算每个像素的环境光遮蔽值。

全屏 Quad 的顶点数据定义在 `src/ssao_test.c`（修复后）：

```c
QuadVertex quadVerts[6] = {
    {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},  // 左上  NDC(-1, 1)  UV(0,0)
    {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},  // 左下  NDC(-1,-1)  UV(0,1)
    { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  // 右下  NDC( 1,-1)  UV(1,1)
    {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},  // 左上  NDC(-1, 1)  UV(0,0)
    { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  // 右下  NDC( 1,-1)  UV(1,1)
    { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},  // 右上  NDC( 1, 1)  UV(1,0)
};
```

### 4.2 错误版本（修复前）

修复前的顶点数据是：

```c
QuadVertex quadVerts[6] = {
    {-1.0f,  1.0f, 0.0f, 0.0f, 1.0f},  // 左上  NDC(-1, 1)  UV(0,1) ← 错误！
    {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},  // 左下  NDC(-1,-1)  UV(0,0)
    { 1.0f, -1.0f, 0.0f, 1.0f, 0.0f},  // 右下  NDC( 1,-1)  UV(1,0)
    ...
};
```

**注意 UV 的 Y 分量**：屏幕顶部的 UV.y = 1.0，屏幕底部的 UV.y = 0.0。

这在 D3D 中是**正确的**——因为 D3D 纹理坐标的 (0,0) 在左上角，（0,1）在左下角。而 D3D NDC 中 y=1 在顶部，对应 UV.y=1 也在顶部——这样的设置意味着"NDC 顶部采样纹理底部"，看起来上下颠倒，但实际上 D3D 中纹理 V 轴是向下的，所以 UV(0,1) 在纹理中是底部，正好对应屏幕顶部。

参考下图理解（D3D 中的正确映射）：

```
D3D 中的 NDC vs 纹理坐标：
    NDC 坐标                UV 坐标              纹理内容
  ┌──────────┐           ┌──────────┐         ┌──────────┐
  │(-1,1)    │ (1,1)     │(0,0)     │ (1,0)   │  顶部    │
  │  顶部    │           │  顶部    │         │          │
  │          │           │          │         │          │
  │(-1,-1)   │ (1,-1)    │(0,1)     │ (1,1)   │  底部    │
  │  底部    │           │  底部    │         │          │
  └──────────┘           └──────────┘         └──────────┘
  
  映射关系：NDC 顶部(y=1) → UV(0,0) → 纹理顶部
           NDC 底部(y=-1) → UV(0,1) → 纹理底部
```

**但问题在于，本项目运行在 Vulkan 上。** 在 Vulkan 中：

```
Vulkan 中的 NDC（SDL3 模拟 Y-up 后）vs Vulkan 纹理坐标：
    NDC 坐标                UV 坐标              纹理内容
  ┌──────────┐           ┌──────────┐         ┌──────────┐
  │(-1,1)    │ (1,1)     │(0,0)     │ (1,0)   │  顶部    │
  │  顶部    │           │  顶部    │         │          │
  │          │           │          │         │          │
  │(-1,-1)   │ (1,-1)    │(0,1)     │ (1,1)   │  底部    │
  │  底部    │           │  底部    │         │          │
  └──────────┘           └──────────┘         └──────────┘
  
  Vulkan 纹理：UV(0,0) 在纹理顶部，UV(0,1) 在纹理底部
```

Vulkan 的纹理坐标系是 (0,0) 在**左上角**，(0,1) 在**左下角**。经过 SDL3 的 Y-up 模拟后，NDC y=1 在屏幕顶部。那么屏幕顶部应该采样纹理顶部的数据，即 UV 应该是 (u, 0) 而不是 (u, 1)。

**修复前的代码把 UV.y=1 放在了 NDC y=1（屏幕顶部）**。这意味着：
- 屏幕顶部的像素 → 采样 UV (u, 1.0) → **采样到纹理底部**
- 屏幕底部的像素 → 采样 UV (u, 0.0) → **采样到纹理顶部**

**整个画面上下颠倒了。**

### 4.3 为什么上下颠倒会导致"地板颜色随相机俯仰角变化"

这是一个更隐蔽的后果。SSAO 的像素着色器需要用 UV 坐标来重建 view-space 位置：

```hlsl
// 从 UV 反推 NDC
float2 ndc = float2(input.UV.x * 2.0 - 1.0, input.UV.y * 2.0 - 1.0);  // 错误版本
// 从 NDC + 深度重建 view-space 位置
viewPos.x = ndc.x * viewZ / Projection._11;
viewPos.y = ndc.y * viewZ / Projection._22;
viewPos.z = viewZ;
```

问题在于：**每个像素的 UV 对应的是错误的屏幕位置**。屏幕顶部的像素拿到了屏幕底部的深度和法线数据。`viewPos` 重建出来的 Y 坐标是**符号错误**的。

这导致 SSAO 计算中的 `geomHeight = dot(sampleViewPos - viewPos, N)` 不再是 0（对于平坦的地板平面本应为 0）。而且这个误差的大小取决于 `viewPos.y` 和 `N.y` —— 而这两者都随相机俯仰角变化。这就解释了为什么地板亮度会随相机俯仰角改变。

### 4.4 修复方法

修复分两步：

**第一步：翻转 Quad 的 UV.y**（`src/ssao_test.c`）

```c
// 修复后：UV.y=0 在屏幕顶部
{-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},  // 左上  UV(0,0)
{-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},  // 左下  UV(0,1)
{ 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  // 右下  UV(1,1)
{ 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},  // 右上  UV(1,0)
```

**第二步：调整 shader 中的 NDC ↔ UV 转换**（`ssao_ps.hlsl`）

有三处需要修改，它们必须使用**同一种 UV ↔ NDC 映射**：

```hlsl
// 1. 当前像素：UV → NDC（用于重建 viewPos）
//    UV.y=0 在顶部，ndc.y=+1 在顶部 → ndc = 1 - 2*uv
float2 ndc = float2(input.UV.x * 2.0 - 1.0, 1.0 - input.UV.y * 2.0);

// 2. 采样点投影：NDC → UV（用于确定采样 UV）
//    与上面的映射一致：uv = (1 - ndc) / 2
sampleUV.y = (1.0 - sampleClip.y / sampleClip.w) * 0.5;

// 3. 采样 UV → 采样 NDC（用于重建采样点的 viewPos）
//    与 #1 使用相同的映射
float2 sampleNDC = float2(sampleUV.x * 2.0 - 1.0, 1.0 - sampleUV.y * 2.0);
```

**这三处必须使用完全一致的 UV ↔ NDC 映射公式。** 任何一处不一致，都会导致 `viewPos` 和 `sampleViewPos` 的坐标系不同，从而产生错误的 SSAO 计算结果。

---

## 第五章：全屏 Quad 的正确写法

### 5.1 什么时候需要全屏 Quad

以下场景通常需要用全屏 Quad 或全屏三角形来做屏幕空间后处理：
- SSAO / SSR / 延迟渲染的光照 pass
- Bloom / Tone Mapping
- 任何需要逐像素处理 G-Buffer 或场景颜色的效果

### 5.2 推荐方案：使用 `SV_VertexID` 生成全屏三角形

现代渲染中，更推荐的做法是不使用顶点缓冲，而是用 `SV_VertexID` 直接生成三个覆盖整个屏幕的顶点：

```hlsl
// 全屏三角形——无需顶点缓冲
struct VS_OUTPUT {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

VS_OUTPUT VSMain(uint id : SV_VertexID) {
    VS_OUTPUT output;
    // 三个顶点覆盖整个 NDC：(-1,-1), (3,-1), (-1,3)
    output.UV = float2((id << 1) & 2, id & 2);
    output.Position = float4(output.UV * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
```

注意其中的 `-2.0` 和 `+1.0` 已经内置了 Y 翻转，这使得 UV(0,0) 在屏幕顶部，符合 Vulkan 约定。

### 5.3 如果必须用顶点缓冲（本项目的做法）

参考第四章修复后的代码。核心原则：**确保 UV.y=0 对应纹理顶部（帧缓冲顶部），UV.y=1 对应纹理底部（帧缓冲底部）。**

对于不同 API 和不同的中间层封装，最保险的做法是：
1. 确认你的帧缓冲原点在哪里（通常是左上角）
2. 确认你的纹理坐标系（Vulkan/D3D12 是左上角为原点）
3. 让 UV 的 (0,0) 对应屏幕左上角，(1,1) 对应屏幕右下角

### 5.4 调试技巧：输出 UV 作为颜色

当你怀疑全屏 Quad 的 UV 有问题时，最快的诊断方法是将 UV 直接输出为颜色：

```hlsl
return float4(input.UV.x, input.UV.y, 0.0, 1.0);
// 预期：
//   左上角 = 深绿色 (0, 0, 0)   ← UV (0,0)
//   右上角 = 黄色   (1, 0, 0)   ← UV (1,0)
//   左下角 = 蓝绿色 (0, 1, 0)   ← UV (0,1)
//   右下角 = 白色   (1, 1, 0)   ← UV (1,1)
```

如果左上角是蓝绿色（G=1）而左下角是深绿色（G=0），说明 UV.y 是反的。

---

## 第六章：其他常见 NDC 相关陷阱

### 6.1 投影矩阵的 Z 范围

D3D 风格 HLSL 的投影矩阵通常假设深度范围为 `[0, 1]`（D3D 约定）。DXC 编译到 SPIR-V 时，默认也使用 `[0, 1]`。本项目的 `mat4_perspective` 使用的就是 `[0, 1]` 深度范围。

如果你使用 OpenGL 风格的投影矩阵（假设深度 `[-1, 1]`），在 Vulkan 上会导致深度值整体偏移。

### 6.2 矩阵的行主序 vs 列主序

这是一个独立但同样常见的陷阱。C/C++ 中通常使用行主序（row-major）矩阵，而 HLSL 默认使用列主序（column-major）。本项目的做法是在上传前做一次 transpose：

```c
mat4_transpose(&proj_t, &proj);
FNA3D_SetEffectParamValue(device, ssaoEffect, "Projection", proj_t, ...);
```

如果你不 transpose，矩阵在 shader 中就是转置的，所有变换都会出错。

### 6.3 DXC 的 Y 翻转选项

DXC 编译 Vulkan SPIR-V 时有 `-fvk-invert-y` 选项，它会在顶点着色器中自动翻转 `SV_POSITION.y`。**但本项目不使用这个选项**，因为 SDL3 GPU 已经在视口变换层面处理了 Y 翻转。

这带来一个微妙之处：如果你同时使用 `-fvk-invert-y` 和 SDL3（或任何已经翻转了视口的框架），Y 轴会被翻转两次，结果就反了。

### 6.4 从屏幕 UV 重建世界/观察空间位置

这类重建在延迟渲染中非常常见。本项目的 SSAO shader 中就有这样的代码：

```hlsl
viewPos.x = ndc.x * viewZ / Projection._11;
viewPos.y = ndc.y * viewZ / Projection._22;
viewPos.z = viewZ;
```

这是从**透视投影矩阵**的逆变换推导出来的。公式假设投影矩阵的形式为：

```
[ w,  0,  0,  0 ]
[ 0,  h,  0,  0 ]
[ 0,  0,  Q,  1 ]
[ 0,  0, -Q*zn, 0 ]
```

其中 `w = 1/(tan(fovY/2) * aspect)`, `h = 1/tan(fovY/2)`。

如果你的投影矩阵结构不同（例如翻转了 Y），公式也需要相应调整。

---

## 第七章：总结与最佳实践

### 核心原则

1. **弄清楚你的 NDC Y 方向**：你的代码（和中间框架）假设的是 Y-up 还是 Y-down？
2. **弄清楚你的纹理坐标系**：(0,0) 在左上角还是左下角？
3. **保持 UV ↔ NDC 映射的一致性**：所有手动转换（`ndc = f(uv)` 和 `uv = f⁻¹(ndc)`）必须使用完全相同的公式
4. **不要依赖框架的"自动转换"**：框架只能帮你处理 `SV_POSITION` 到像素的映射，你手动写的 UV 和 NDC 计算需要你自己保证正确

### 本项目的正确设置（以 Vulkan 为后端 + SDL3 GPU）

| 项目 | 约定 |
|------|------|
| NDC Y 方向 | Y-up（SDL3 模拟） |
| NDC Z 范围 | [0, 1] |
| 帧缓冲原点 | 左上角 |
| 纹理坐标系 | (0,0) = 左上角 |
| 全屏 Quad UV | UV.y=0 在屏幕顶部 |
| UV → NDC 映射 | `ndc.y = 1.0 - uv.y * 2.0` |
| NDC → UV 映射 | `uv.y = (1.0 - ndc.y) * 0.5` |

### 如果换了后端（例如 Metal / D3D12）

如果你将来在其他后端上运行，需要重新检查上述约定。SDL3 GPU 的目标是提供一致的抽象，但全屏 Quad 的 UV 设置和 shader 中的手动转换仍然需要针对具体后端进行调整。

---

## 参考资料

- [Vulkan Specification - Coordinate Systems](https://registry.khronisk.org/vulkan/specs/1.3-extensions/html/vkspec.html#vertexpostproc-coords)
- [D3D12 Coordinate Systems](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.1%20Rasterizer)
- [SDL3 GPU API Documentation](https://wiki.libsdl.org/SDL3/CategoryGPU)
- [DXC SPIR-V CodeGen](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst)
