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

## 5. 其他相关约定陷阱

### 5.1 COLOR 字节顺序 — BGRA

```c
// XNA/FNA 约定：COLOR 格式使用 BGRA 字节顺序
// 在 C 结构体中，字段应写为 b, g, r, a 顺序：
typedef struct Vertex {
    float x, y, z;
    uint8_t b, g, r, a;  // BGRA order，不是 RGBA！
} Vertex;
```

### 5.2 DXC 顶点属性 Location

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

## 6. 快速检查清单

开发新的 Effect / Test 时，检查以下几点：

- [ ] 上传矩阵用 `mat4_transpose`，**不要用** `mat4_to_colmajor`
- [ ] 传给 `FNA3D_SetEffectParamValue` 的是 `&mat.m11`，`sizeof(Mat4)`
- [ ] FEB manifest 中矩阵 default 值用列主序
- [ ] HLSL `register(cN)` 与 manifest `register` 一致
- [ ] C 端 vertex elements 顺序与 HLSL 结构体字段声明顺序一致
- [ ] COLOR 格式用 BGRA 字节序
- [ ] 变换链在 C 端计算时用行主序: `W * V * P`
