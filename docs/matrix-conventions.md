# Matrix Convention Guide — Row-Major vs Column-Major

本文档梳理 FNA3D_HLSL 项目中所有涉及矩阵行主序（row-major）与列主序（column-major）的地方，帮助开发者避免踩坑。

---

## 1. 核心概念

| 概念 | Row-Major（行主序） | Column-Major（列主序） |
|------|---------------------|------------------------|
| 内存布局 | 一行接一行: `[r1, r2, r3, r4]` | 一列接一列: `[c1, c2, c3, c4]` |
| 向量约定 | 行向量: `p' = p * M` | 列向量: `p' = M * p` |
| 变换顺序 | `p * A * B` — 先 A 后 B | `B * A * p` — 先 A 后 B |
| 本项目中谁用 | C 代码 (`Mat4` 结构体) | HLSL (`float4x4`), SPIR-V, FEB |

**关键事实：** 行主序矩阵的**转置**就是列主序矩阵（字节布局互为转置）。同一个变换在两种约定下互为转置关系。

**例子 — 同一个矩阵的两种内存布局：**

```
C Mat4 (row-major):            HLSL float4x4 (column-major):
m11 m12 m13 m14   row 1        m11 m21 m31 m41   col 1
m21 m22 m23 m24   row 2        m12 m22 m32 m42   col 2
m31 m32 m33 m34   row 3        m13 m23 m33 m43   col 3
m41 m42 m43 m44   row 4        m14 m24 m34 m44   col 4
```

可以看出：**列主序就是行主序的转置**。

---

## 2. 项目中各层的矩阵约定

### 2.1 C 代码 — `Mat4` 是 Row-Major

文件: `src/math3d.h`, `src/math3d.c`

```c
typedef struct Mat4 {
    float m11, m12, m13, m14;  // row 1
    float m21, m22, m23, m24;  // row 2
    float m31, m32, m33, m34;  // row 3
    float m41, m42, m43, m44;  // row 4
} Mat4;
```

- `Mat4` 是行主序，行向量右乘: `p' = p * M`
- `mat4_mul(&out, &a, &b)` 计算 `out = a * b`（行向量先 a 后 b）
- `mat4_lookat_lh`, `mat4_perspective` 等均按行主序构建
- 变换链写法: `mat4_mul(&worldViewProj, &world, &viewProj)` 表示 `W * V * P`

### 2.2 HLSL 着色器 — `float4x4` 是 Column-Major

文件: `assets/shaders/*.hlsl`

```hlsl
float4x4 World : register(c0);
output.Position = mul(input.Position, World);  // 行向量右乘
```

- HLSL 默认列主序存储，即 `register(c0)` 接收的数据是列的排列
- `mul(vec, mat)` 将 `vec` 视为行向量，等价于 `v * M`
- **HLSL 和 C 代码用相同的数学公式** `p * World * View * Proj`，但 HLSL 期望传入的矩阵数据是列主序（即 C 端行主序矩阵的转置）

### 2.3 FEB 二进制 & Manifest — Column-Major

文件: `tools/feb_builder.py`, `assets/effects/*.feb.json`

```json
{
  "name": "WorldViewProj",
  "type": "MATRIX",
  "register": 0,
  "default": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
}
```

- FEB 中矩阵 default 值按 **列主序**（即 HLSL `float4x4` 的字节顺序）存放
- 16 个 float 的顺序: `col0.xyzw, col1.xyzw, col2.xyzw, col3.xyzw`
- 单位矩阵在两种约定下相同（对称矩阵），所以 default identity 怎么写都对
- **但非对称矩阵必须按列主序填写 default 值！**

### 2.4 SPIR-V / Vulkan — Column-Major

DXC 编译 `-spirv` 后，uniform buffer 中的矩阵数据在 SPIR-V 中以列主序解释。这与 HLSL 的列主序约定一致，没有额外的转置层。

---

## 3. ⚠️ 核心陷阱：`mat4_to_colmajor` 名字是假的！

文件: `src/math3d.c:135-145`

```c
void mat4_to_colmajor(float out[16], const Mat4 *m)
{
    /* ImGuizmo's matrix_t uses C float m[4][4] which is row-major — identical
     * to our Mat4 byte layout. Direct memcpy, no transpose needed.
     * (Despite the name "colmajor", this is just a float[16] copy for ImGuizmo.) */
    const float *src = &m->m11;
    out[ 0] = src[ 0]; out[ 1] = src[ 1]; ...  // 逐字节拷贝，没有转置！
}
```

| 函数 | 实际行为 | 用途 |
|------|---------|------|
| `mat4_transpose(&out, &in)` | **真正转置** row→column | ✅ 上传矩阵到 HLSL |
| `mat4_to_colmajor(f16, &m)` | 纯 memcpy，**不转置** | ❌ 仅用于 ImGuizmo |
| `mat4_from_colmajor(&m, f16)` | 纯 memcpy，**不转置** | ❌ 仅用于 ImGuizmo |

**ImGuizmo 的 `float m[4][4]` 是 `float[4][4]`，与 `Mat4` 结构体内存布局完全一致，所以直接拷贝即可。** 但这个名字极具误导性，导致误以为它做了转置。

---

## 4. ✅ 正确做法

### 4.1 上传矩阵到 HLSL Shader

```c
// ❌ 错误：用了不转置的 mat4_to_colmajor
float worldCM[16];
mat4_to_colmajor(worldCM, &world);  // 这只是 memcpy！
FNA3D_SetEffectParamValue(device, effect, "World", worldCM, 0, sizeof(worldCM));

// ✅ 正确：先转置再上传
Mat4 worldT;
mat4_transpose(&worldT, &world);
FNA3D_SetEffectParamValue(device, effect, "World", &worldT.m11, 0, sizeof(Mat4));
```

**参考实现：** `src/teapot_light.c:245-247`

### 4.2 在 FEB Manifest 中写非单位矩阵

如果需要在 manifest 中写非对称矩阵的 default 值，必须用列主序：

```json
// C 端 row-major 的平移矩阵 (translate 1,2,3):
// [1 0 0 0]
// [0 1 0 0]
// [0 0 1 0]
// [1 2 3 1]

// Manifest 中必须是 column-major (即 row-major 的转置):
// col0=(1,0,0,1) col1=(0,1,0,2) col2=(0,0,1,3) col3=(0,0,0,1)
"default": [1,0,0,1,  0,1,0,2,  0,0,1,3,  0,0,0,1]
//          ^col0^     ^col1^     ^col2^     ^col3^
```

### 4.3 在 Shader 中声明矩阵

`register(cN)` 直接对应 FEB manifest 中的 `"register": N`，不存在偏移或转换：

```hlsl
// Manifest: "register": 0 → HLSL: register(c0)
// Manifest: "register": 4 → HLSL: register(c4)
float4x4 World    : register(c0);  // 占用 c0-c3（4 个 float4）
float4x4 ViewProj : register(c4);  // 占用 c4-c7
float4   Color    : register(c8);  // 占用 c8（1 个 float4）
```

---

## 5. 深入原理：GPU 端的矩阵存储与计算

本节解释"列主序"约定背后的机制——它写在哪里、GPU 如何执行矩阵乘法、为什么 HLSL 默认选择列主序。理解这些有助于判断哪些环节可以改、哪些不能改。

### 5.1 显存本身没有"主序"——主序是读取约定

VRAM 中的 uniform/constant buffer 只是一段裸字节，驱动把上传的数据原封不动放进去。GPU 硬件也没有原生的"矩阵类型"——所谓 `float4x4`，在 buffer 里就是连续的 4 个 float4。

行主序还是列主序，取决于**编译器生成的着色器代码用什么规则解释这些字节**（即 `m[i][j]` 映射到哪个字节偏移）。这个规则在编译期固化：

| 路线 | 约定写在哪里 |
|------|-------------|
| D3D (DXBC/DXIL) | cbuffer 打包规则，默认 `column_major`，可用 `row_major` 关键字或 `-Zpr` 翻转 |
| Vulkan (SPIR-V) | 矩阵成员的 `RowMajor`/`ColMajor` 装饰 + `MatrixStride 16`，驱动按装饰生成取址代码 |

所以"上传前转置"改变的不是矩阵本身，而是让 C 端字节布局符合着色器端固化的读取约定。

### 5.2 cbuffer 打包规则

HLSL constant buffer 以 16 字节（一个 float4 寄存器）为打包单位：

- 每个 `register(cN)` 槽位 = 一个 float4（16 字节）
- 任何变量不得跨越 float4 边界，跨越则整体推到下一个寄存器
- `float4x4` 占 4 个连续寄存器；默认列主序下**每个寄存器存一列**
- `float4x3`（列主序）占 3 个寄存器；`float3x3` 每列不足 16 字节但仍各占一个完整寄存器（3 个寄存器，每个浪费 4 字节）
- 数组元素各自对齐到 16 字节边界，即使元素是 `float`

```hlsl
float4x4 World    : register(c0);  // c0=col0, c1=col1, c2=col2, c3=col3
float4x4 ViewProj : register(c4);  // c4-c7
float4   Color    : register(c8);  // c8
```

这也是 FEB manifest 中 `"register": N` 与矩阵占 4 个槽位的由来。

### 5.3 mul(v, M) 编译成什么指令

GPU 没有"矩阵乘法指令"（张量核心与顶点变换无关），`mul(v, M)` 被展开为向量点乘/乘加序列。存储主序决定展开形态：

**列主序存储（HLSL 默认）** — 每个寄存器是一列，输出分量 = v 点乘一列：

```
p'.x = dp4(v, c0)   // dot(v, col0)
p'.y = dp4(v, c1)
p'.z = dp4(v, c2)
p'.w = dp4(v, c3)
// 4 条 dp4，互相独立，无依赖链
```

**行主序存储** — 每个寄存器是一行，需要按分量展开成乘加链：

```
tmp  = v.xxxx * r0          // mul
tmp += v.yyyy * r1          // mad
tmp += v.zzzz * r2          // mad
p'   = tmp + v.wwww * r3    // mad
// 1 mul + 3 mad，串行依赖链
```

两种形态都是 4 条指令，但在早期 vec4 向量 ISA（SM 1.x-3.x 时代的 VLIW/向量硬件）上，dp4 版本无依赖链、指令级并行更好——这是 HLSL 默认列主序打包的历史原因。**现代 GPU 是标量 SIMT 架构**，两种形态最终都编译成约 16 条标量 FMA，实际性能差异基本消失，但默认约定沿袭了下来。

### 5.4 DXC → SPIR-V：装饰名是"反的"

用 `spirv-dis` 反汇编 DXC 输出时会看到一个反直觉现象：

| HLSL 声明 | SPIR-V 装饰 |
|-----------|------------|
| `column_major`（默认 / `-Zpc`） | `RowMajor` |
| `row_major`（`-Zpr`） | `ColMajor` |

这是**已知且正确**的行为：HLSL 与 SPIR-V/GLSL 的矩阵逻辑约定互为转置（HLSL 按行索引、向量在左 `mul(v, M)`；SPIR-V 按列向量组织、向量在右 `M * v`），DXC 通过翻转装饰名来保持**字节布局不变**。装饰名反了，buffer 中的字节顺序仍然是 HLSL 语义下的列主序——这就是 2.4 节"没有额外转置层"的原因。排查 SPIR-V 反汇编时不要被装饰名误导。

参考资料：
- [glslang: column_major 限定符在 SPIR-V 中显示为 RowMajor 装饰](https://chromium.googlesource.com/external/github.com/google/glslang/+/6a264bed88afbd8a452956915fc582ff2aae3a56)
- [ShaderConductor #41 — packMatricesInRowMajor 语义相反](https://github.com/microsoft/ShaderConductor/issues/41)
- [Vulkan-Guide PR #244 — HLSL/GLSL 着色器语言映射](https://github.com/KhronosGroup/Vulkan-Guide/pull/244)

---

## 6. 其他相关约定陷阱

### 6.1 COLOR 字节顺序 — BGRA

```c
// XNA/FNA 约定：COLOR 格式使用 BGRA 字节顺序
// 在 C 结构体中，字段应写为 b, g, r, a 顺序：
typedef struct Vertex {
    float x, y, z;
    uint8_t b, g, r, a;  // BGRA order，不是 RGBA！
} Vertex;
```

### 6.2 DXC 顶点属性 Location

DXC 按 HLSL 结构体中**字段声明顺序**分配 `location` (0, 1, 2...)，**不是** `usage*16+index` 公式。

C 端的 `FNA3D_VertexElement` 数组顺序必须与 HLSL 结构体字段顺序完全一致：

```hlsl
// HLSL — 声明顺序决定 location
struct VS_INPUT {
    float4 Position : POSITION0;    // location 0
    float3 Normal   : NORMAL0;      // location 1
    float2 TexCoord : TEXCOORD0;    // location 2
};
```

```c
// C — 元素数组顺序必须与 HLSL 一致
elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;       // location 0
elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_NORMAL;         // location 1
elements[2].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE; // location 2
```

---

## 7. 快速检查清单

开发新的 Effect / Test 时，检查以下几点：

- [ ] 上传矩阵用 `mat4_transpose`，**不要用** `mat4_to_colmajor`
- [ ] 传给 `FNA3D_SetEffectParamValue` 的是 `&mat.m11`，`sizeof(Mat4)`
- [ ] FEB manifest 中矩阵 default 值用列主序
- [ ] HLSL `register(cN)` 与 manifest `register` 一致
- [ ] C 端 vertex elements 顺序与 HLSL 结构体字段声明顺序一致
- [ ] COLOR 格式用 BGRA 字节序
- [ ] 变换链在 C 端计算时用行主序: `W * V * P`
