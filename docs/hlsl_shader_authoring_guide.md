# FNA3D_HLSL 着色器编写指南

本文档面向编写自定义 HLSL 着色器的 FNA3D_HLSL 用户，解释 DXC（DirectX Shader Compiler）将 HLSL 编译为 SPIR-V 时需要特别注意的两个关键问题，以及如何在 `.feb.json` 清单文件中正确配置。

---

## 目录

1. [背景：HLSL → SPIR-V → SDL_GPU 的完整链路](#1-背景hlsl--spir-v--sdl_gpu-的完整链路)
2. [问题一：描述符集（Descriptor Set）不匹配](#2-问题一描述符集descriptor-set不匹配)
3. [问题二：DXC 按声明顺序分配 Location](#3-问题二dxc-按声明顺序分配-location)
4. [清单文件配置速查表](#4-清单文件配置速查表)
5. [完整示例：带纹理的着色器](#5-完整示例带纹理的着色器)

---

## 1. 背景：HLSL → SPIR-V → SDL_GPU 的完整链路

FNA3D_HLSL 的着色器管线流程如下：

```
HLSL 源码 (.hlsl)
  → DXC（编译为 SPIR-V）
    → feb_builder.py（读取 .feb.json 清单，打包为 FEB 二进制）
      → FNA3D_CreateEffect()（运行时加载）
        → SDL_GPU（创建着色器对象，绑定资源，执行绘制）
```

DXC 是微软的 HLSL 编译器，可以将 HLSL 编译为 SPIR-V（Vulkan 使用的着色器中间表示）。SDL_GPU 是 SDL3 提供的跨平台 GPU API，底层封装了 Vulkan、Metal、D3D12。

### SDL_GPU 的固定描述符集布局

SDL_GPU 对不同类型着色器资源的绑定位置有一套**硬编码的约定**。这套布局定义在 SDL_shadercross 的源码中，所有通过 `SDL_GPU_SHADERFORMAT_SPIRV` 加载的 SPIR-V 着色器都必须遵循：

| 资源类型 | 顶点着色器 (VS) | 像素着色器 (PS) |
|---------|:---:|:---:|
| 采样器（纹理+采样器） | **Set 0** | **Set 2** |
| Uniform Buffer（常量缓冲区） | **Set 1** | **Set 3** |

DXC 的默认行为是将所有资源放在 **Set 0**，因此需要通过命令行标志显式调整。

---

## 2. 问题一：描述符集不匹配

### 2.1 现象

着色器使用了 uniform 变量（如变换矩阵）或纹理采样器，但渲染结果全黑，或者出现 "descriptor set N not bound" 的 Vulkan 验证层错误。

### 2.2 原因

DXC 在生成 SPIR-V 时，会将 HLSL 中的全局变量分配到一个隐式的常量缓冲区 `$Globals` 中，并将其放在 **DescriptorSet 0, Binding 0**。纹理和采样器默认也放在 **Set 0**。

但 SDL_GPU 要求 UBO 放在 Set 1（VS）或 Set 3（PS），采样器放在 Set 0（VS）或 Set 2（PS）。如果不通过 DXC 标志修正，着色器引用的描述符集会与实际绑定的不匹配，导致读取到垃圾数据或零值。

### 2.3 解决方案：DXC 命令行标志

`feb_builder.py` 在编译着色器时会自动添加必要的标志，用户只需在 `.feb.json` 清单文件中正确声明 `uniforms` 和 `samplers` 的数量。

#### 标志的含义

```bash
# 将 $Globals（隐式常量缓冲区）移到正确的描述符集
-fvk-bind-globals 0 <set>

# 将指定的 HLSL 寄存器映射到目标描述符集和绑定点
# 语法：-fvk-bind-register <类型-编号> <space> <binding> <set>
-fvk-bind-register t0 0 0 2   # 纹理寄存器 t0 → Set 2, Binding 0
-fvk-bind-register s0 0 0 2   # 采样器寄存器 s0 → Set 2, Binding 0
```

`feb_builder.py` 按照以下规则自动追加这些标志：

| 着色器阶段 | `$Globals` 目标集 | 采样器目标集 |
|-----------|:---:|:---:|
| 顶点着色器 (VS) | Set 1 | Set 0（DXC 默认，无需覆盖） |
| 像素着色器 (PS) | Set 3 | Set 2（必须覆盖 DXC 默认的 Set 0） |

#### 清单文件中的配置

```json
{
  "vertexShader": {
    "source": "../shaders/my_vs.hlsl",
    "entry": "VSMain",
    "samplers": 0,
    "uniforms": 1
  },
  "pixelShader": {
    "source": "../shaders/my_ps.hlsl",
    "entry": "PSMain",
    "samplers": 1,
    "uniforms": 0
  }
}
```

**关键要点：**

- `uniforms`：着色器是否有 uniform 变量（`register(cN)`）。有则填 `1`，无则填 `0`。仅当你有 uniform 参数时，`$Globals` 描述符集修正才会生效。
- `samplers`：着色器使用的纹理+采样器配对数量。一个 `Texture2D` + 一个 `SamplerState` 计为 1 对。像素着色器有纹理时**必须填对**，否则 `feb_builder.py` 无法生成正确的 `-fvk-bind-register` 标志。
- **`uniforms` 的值始终为 1**，无论你有多少个 uniform 变量（1 个 `float` 和 1 个 `float4x4` 都只占用 1 个 `$Globals` 缓冲区绑定）。它代表的是常量缓冲区的数量，而非寄存器个数。

#### 如果你在手动编译（不使用 feb_builder.py）

使用 DXC 直接编译时，需要手动添加完整标志：

**顶点着色器：**
```bash
dxc -spirv -T vs_6_0 -E VSMain my_vs.hlsl -Fo my_vs.spv \
    -fvk-bind-globals 0 1
```

**像素着色器（无纹理）：**
```bash
dxc -spirv -T ps_6_0 -E PSMain my_ps.hlsl -Fo my_ps.spv \
    -fvk-bind-globals 0 3
```

**像素着色器（有 1 个纹理+采样器）：**
```bash
dxc -spirv -T ps_6_0 -E PSMain my_ps.hlsl -Fo my_ps.spv \
    -fvk-bind-globals 0 3 \
    -fvk-bind-register t0 0 0 2 \
    -fvk-bind-register s0 0 0 2
```

**像素着色器（有多个纹理+采样器）：**
```bash
dxc -spirv -T ps_6_0 -E PSMain my_ps.hlsl -Fo my_ps.spv \
    -fvk-bind-globals 0 3 \
    -fvk-bind-register t0 0 0 2 \
    -fvk-bind-register s0 0 0 2 \
    -fvk-bind-register t1 0 1 2 \
    -fvk-bind-register s1 0 1 2
```

规律：`t0` 和 `s0` 是第一个纹理配对，`t1` 和 `s1` 是第二个，以此类推。绑定编号（`binding`）与寄存器编号保持一致。

---

## 3. 问题二：DXC 按声明顺序分配 Location

### 3.1 现象

着色器编译成功，程序运行不报错，但渲染结果的颜色不对，或者 UV 坐标看起来被当成了颜色（例如四边形的四个角分别显示黑色、绿色、红色、黄色）。

### 3.2 原因

在 HLSL 中，顶点着色器的输出（VS_OUTPUT）和像素着色器的输入（PS 参数）通过**语义（Semantic）**进行匹配。例如 VS 输出 `TEXCOORD0` 应该与 PS 输入的 `TEXCOORD0` 匹配。

**然而，DXC 在生成 SPIR-V 时，不是按语义名称匹配**，而是按 HLSL 源码中的**声明顺序**为每个变量分配一个 `Location`（位置编号），从 0 开始递增。

```
顶点着色器输出结构体：
  struct VS_OUTPUT {
      float4 Position : SV_POSITION;  // 系统值，不占 Location
      float2 TexCoord : TEXCOORD0;    // → Location 0
      float4 Color    : COLOR0;       // → Location 1
  };

像素着色器入口参数：
  PSMain(float4 color : COLOR0,       // → Location 0 ← 错！
         float2 texCoord : TEXCOORD0) // → Location 1 ← 错！

实际效果：
  VS Location 0 (TexCoord) → PS Location 0 (color)  → UV 被当作颜色
  VS Location 1 (Color)    → PS Location 1 (texCoord) → 颜色被当作 UV
```

SPIR-V 的 `Location` 装饰是纯数字的，没有语义名称。GPU 硬件按 Location 编号进行插值和传递，所以**声明顺序必须完全对齐**。

### 3.3 解决方案：保持声明顺序一致

**方法：**让像素着色器入口参数的声明顺序与顶点着色器输出结构体的字段顺序完全一致（忽略 `SV_POSITION`，因为它是系统值，不占用 Location）。

**正确的写法：**

```hlsl
// 顶点着色器输出
struct VS_OUTPUT {
    float4 Position : SV_POSITION;  // 系统值
    float2 TexCoord : TEXCOORD0;    // Location 0
    float4 Color    : COLOR0;       // Location 1
};

// 像素着色器入口 — 参数顺序与 VS_OUTPUT 一致
float4 PSMain(float2 texCoord : TEXCOORD0,   // Location 0 ✓
              float4 color    : COLOR0)      // Location 1 ✓
              : SV_TARGET0
{
    return texColor * color;
}
```

**错误的写法（参数顺序与 VS 输出不一致）：**

```hlsl
// ❌ 声明顺序不一致！
float4 PSMain(float4 color    : COLOR0,      // Location 0 — VS 输出的是 TexCoord
              float2 texCoord : TEXCOORD0)   // Location 1 — VS 输出的是 Color
              : SV_TARGET0
```

### 3.4 同样适用于顶点着色器输入

DXC 对顶点着色器的**输入**也按声明顺序分配 Location。因此 C 代码中的 `FNA3D_VertexElement` 数组必须与 HLSL 顶点输入结构体的字段顺序一致：

```hlsl
// HLSL 顶点输入
struct VS_INPUT {
    float4 Position : POSITION0;   // Location 0
    float2 TexCoord : TEXCOORD0;   // Location 1
    float4 Color    : COLOR0;      // Location 2
};
```

```c
// C 代码顶点声明 — 偏移量和顺序必须与 HLSL 一致
FNA3D_VertexElement elements[3];
elements[0].offset = 0;                                    // Location 0: POSITION
elements[1].offset = sizeof(float) * 4;                    // Location 1: TEXCOORD
elements[2].offset = sizeof(float) * 6;                    // Location 2: COLOR
```

### 3.5 Location 对齐速查

```
     顶点缓冲区布局      顶点着色器输入       顶点着色器输出       像素着色器输入
     ──────────────     ──────────────      ──────────────      ─────────────
Loc0: POSITION     →    POSITION0      →   (SV_POSITION)   →
Loc1: TEXCOORD     →    TEXCOORD0      →    TEXCOORD0      →    TEXCOORD0
Loc2: COLOR        →    COLOR0         →    COLOR0         →    COLOR0
```

**核心原则：每一列中的字段声明顺序必须一致。** 从上到下、从顶点输入到像素输出，相同编号的 Location 始终承载相同的语义。

---

## 4. 清单文件配置速查表

### `uniforms` 字段

| 场景 | `uniforms` 值 | 说明 |
|------|:---:|------|
| 着色器不含任何 `register(cN)` 变量 | `0` | 无常量缓冲区 |
| 着色器包含至少一个 `register(cN)` 变量 | `1` | 所有 uniform 共享一个 `$Globals` 缓冲区 |

### `samplers` 字段

| 场景 | `samplers` 值 | 说明 |
|------|:---:|------|
| 着色器不使用纹理 | `0` | 无采样器 |
| 1 个 `Texture2D` + 1 个 `SamplerState` | `1` | 1 对纹理+采样器 |
| N 个 `Texture2D` + N 个 `SamplerState` | `N` | N 对纹理+采样器 |

**重要：** `samplers` 的值决定了 `feb_builder.py` 会生成多少个 `-fvk-bind-register` 标志。每个纹理+采样器对都会在对应的描述符集中创建一个绑定点。如果你的着色器有纹理但 `samplers` 填了 0，纹理将无法访问。

### `register` 规范

| HLSL 声明 | 寄存器约定 |
|-----------|----------|
| `Texture2D tex0 : register(t0)` | 第一个纹理 → `t0` |
| `SamplerState samp0 : register(s0)` | 第一个采样器 → `s0` |
| `Texture2D tex1 : register(t1)` | 第二个纹理 → `t1` |
| `SamplerState samp1 : register(s1)` | 第二个采样器 → `s1` |
| `float4x4 mat : register(c0)` | Uniform 变量 → `c0`, `c1`, ... |

**约定：** 请从 `t0` / `s0` 开始连续编号，不要跳号。

---

## 5. 完整示例：带纹理的着色器

以下是一个带纹理和 uniform 矩阵的完整 HLSL 着色器示例（就是项目中的 `sprite_ps.hlsl` / `sprite_vs.hlsl`）。

### 顶点着色器 (`sprite_vs.hlsl`)

```hlsl
float4x4 MatrixTransform : register(c0);

struct VS_INPUT
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = mul(input.Position, MatrixTransform);
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}
```

### 像素着色器 (`sprite_ps.hlsl`)

```hlsl
Texture2D<float4> Texture : register(t0);
SamplerState TextureSampler : register(s0);

float4 PSMain(float2 texCoord : TEXCOORD0, float4 color : COLOR0) : SV_TARGET0
{
    return Texture.Sample(TextureSampler, texCoord) * color;
}
```

注意像素着色器参数顺序与顶点着色器输出结构体顺序一致（`TEXCOORD0` → `COLOR0`）。

### 清单文件 (`sprite.feb.json`)

```json
{
  "techniques": [
    {
      "name": "SpriteBatch",
      "passes": [
        {
          "name": "P0",
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
      ]
    }
  ],
  "parameters": [
    {
      "name": "MatrixTransform",
      "type": "MATRIX",
      "register": 0,
      "default": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    }
  ]
}
```

### C 代码顶点声明

```c
typedef struct Vertex
{
    float x, y, z, w;    // Location 0: POSITION
    float u, v;          // Location 1: TEXCOORD
    uint8_t b, g, r, a;  // Location 2: COLOR (BGRA byte order)
} Vertex;

FNA3D_VertexElement elements[3];
elements[0].offset = 0;
elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR4;
elements[0].vertexElementUsage   = FNA3D_VERTEXELEMENTUSAGE_POSITION;
elements[0].usageIndex = 0;

elements[1].offset = sizeof(float) * 4;
elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
elements[1].vertexElementUsage   = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
elements[1].usageIndex = 0;

elements[2].offset = sizeof(float) * 6;
elements[2].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_COLOR;
elements[2].vertexElementUsage   = FNA3D_VERTEXELEMENTUSAGE_COLOR;
elements[2].usageIndex = 0;

FNA3D_VertexDeclaration decl;
decl.vertexStride = sizeof(Vertex);
decl.elementCount = 3;
decl.elements = elements;
```

---

## 故障排查清单

遇到渲染异常时，按以下顺序排查：

1. **着色器有 uniform 变量但渲染全黑或位置错误**
   - 检查 `.feb.json` 中对应着色器的 `uniforms` 是否为 `1`
   - 检查 `parameters` 中参数名是否与 HLSL 变量名一致

2. **着色器有纹理但纹理不显示（黑色）**
   - 检查 `.feb.json` 中对应着色器的 `samplers` 值是否等于纹理数量
   - 确认像素着色器参数声明顺序与顶点着色器输出顺序一致

3. **颜色或 UV 看起来错位/交换**
   - 检查像素着色器入口参数的声明顺序是否与顶点着色器输出结构体顺序一致
   - 检查 C 代码中 `FNA3D_VertexElement` 数组的顺序是否与 HLSL 顶点输入结构体顺序一致

4. **程序崩溃 / Vulkan 验证层报错**
   - 启用 Vulkan 验证层查看详细错误信息
   - 使用 RenderDoc 截帧检查描述符集绑定和 SPIR-V 反汇编
