# FNA3D_HLSL ImGuizmo 集成开发指南

本文档介绍如何在基于 FNA3D_HLSL 的编辑器或工具中集成 3D Gizmo（平移/旋转/缩放），
使用 FNA3D_HLSL 原生内置的 ImGuizmo 库。涵盖公开 API 用法、相机拖拽共存、矩阵约定、
以及我们在开发 teapot_test 过程中踩过的每一个坑。

参考代码：`src/teapot.c` —— 以下所有模式的完整可运行示例。

---

## 1. 架构概览

```
你的 C 应用程序
  → FNA3D_ImGuizmo.h        （公开 C API，随 FNA3D 安装）
    → FNA3D_ImGuizmo.cpp     （extern "C" 封装层）
      → ImGuizmo.cpp          （thirdparty/imguizmo，内置 MIT 源码）
        → imgui.h / imgui_internal.h
        → ImDrawList          （由现有 SDL_GPU3 后端统一渲染）
```

- ImGuizmo **纯 CPU 侧运行** —— 只做数学运算 + 向 `ImDrawList` 绘制线段/三角形。
  不涉及 GPU Shader，不需要 Driver 层分发，不修改 `FNA3D_Driver.h`。
- 现有的 `FNA3D_ImGui` 后端（SDL_GPU3）已在 `FNA3D_SwapBuffers` 中完成
  `ImDrawList` 的渲染，Gizmo 输出自动呈现。
- ImGuizmo **不持有 ImGui 上下文**。你必须在 `FNA3D_CreateDevice` 之后
  调用一次 `FNA3D_ImGui_InitEXT(device)`。

---

## 2. 构建集成

ImGuizmo 是 FNA3D_HLSL 内部的**可选 CMake 组件**（`CMakeLists.txt` 第 212 行），
默认开启：

```cmake
option(FNA3D_IMGUIZMO "Build the ImGuizmo integration" ON)
```

开启后，FNA3D 动态库会自动包含 `FNA3D_ImGuizmo.cpp` 和内置的 `ImGuizmo.cpp`。
你的测试项目只需：

```cmake
target_link_libraries(your_test FNA3D)
```

无需额外源文件、无需额外 include 路径 —— `FNA3D_ImGuizmo.h` 位于
`FNA3D_HLSL/include/`，已在 PUBLIC 头文件搜索路径中。

---

## 3. 公开 API 参考

### 3.1 头文件：`<FNA3D_ImGuizmo.h>`

| 函数 | 用途 |
|---|---|
| `FNA3D_ImGuizmo_BeginFrameEXT(device)` | 开始新一帧 Gizmo。必须在 `FNA3D_ImGui_NewFrameEXT` 之后调用。 |
| `FNA3D_ImGuizmo_SetRectEXT(device, x, y, w, h)` | 设置 Gizmo 手柄点击测试的屏幕矩形区域。通常设为完整视口。 |
| `FNA3D_ImGuizmo_ManipulateEXT(device, view, proj, op, mode, matrix)` | 绘制并交互 Gizmo。返回 1 表示 `matrix` 被修改。 |
| `FNA3D_ImGuizmo_IsOverEXT(device)` | 鼠标悬停在任意 Gizmo 手柄上时返回 1。 |
| `FNA3D_ImGuizmo_IsUsingEXT(device)` | Gizmo 正在被拖拽时返回 1。 |

### 3.2 操作常量

| 常量 | 组合值 |
|---|---|
| `FNA3D_IMGUIZMO_TRANSLATE` | `TRANSLATE_X \| TRANSLATE_Y \| TRANSLATE_Z` |
| `FNA3D_IMGUIZMO_ROTATE` | `ROTATE_X \| ROTATE_Y \| ROTATE_Z \| ROTATE_SCREEN` |
| `FNA3D_IMGUIZMO_SCALE` | `SCALE_X \| SCALE_Y \| SCALE_Z` |

单轴变体（`_X`、`_Y`、`_Z`）也可直接使用。

### 3.3 模式常量

| 常量 | 含义 |
|---|---|
| `FNA3D_IMGUIZMO_LOCAL` (0) | Gizmo 坐标轴跟随对象的局部坐标系。 |
| `FNA3D_IMGUIZMO_WORLD` (1) | Gizmo 坐标轴对齐世界空间。 |

### 3.4 每帧标准调用顺序

```c
/* 每帧开始 —— 严格按此顺序： */
FNA3D_ImGui_NewFrameEXT(device);

/* 1. BeginFrame 创建内部 gizmo 窗口。                           */
FNA3D_ImGuizmo_BeginFrameEXT(device);

/* 2. SetRect 必须在 BeginFrame 之后、Manipulate 之前调用。      */
FNA3D_ImGuizmo_SetRectEXT(device, 0, 0, width, height);

/* 3. Manipulate 绘制 gizmo 并处理交互。                          */
int modified = FNA3D_ImGuizmo_ManipulateEXT(device,
    view_cm, proj_cm,
    FNA3D_IMGUIZMO_TRANSLATE,
    FNA3D_IMGUIZMO_WORLD,
    matrix_cm);

/* 4. 如果 Manipulate 返回 1，将 matrix_cm 拷贝回你的模型矩阵。  */
if (modified)
    mat4_from_colmajor(&model, matrix_cm);

/* 5. 你的 ImGui 面板放在 Gizmo 调用之后。                        */
/* ... ImGui_Begin / SliderFloat3 / Button / End ...              */

/* 6. 所有 ImGui/ImGuizmo 调用完毕后，计算最终变换、上传 Shader、 */
/*    绘制、交换缓冲区。                                            */
```

---

## 4. 矩阵约定（最重要的陷阱）

这是最容易出错的环节。必须理解你的矩阵内存布局。

### 4.1 两种约定

| 库 | 内存布局 | 16 个 float 的排列 |
|---|---|---|
| **你的 Mat4**（FNA3D） | 行主序（row-major） | `m11, m12, m13, m14, m21, …` |
| **上传到 HLSL** | 列主序（column-major） | 行主序的转置 |
| **ImGuizmo `float[16]`** | 行主序（`matrix_t` 即 `float m[4][4]`） | **与 Mat4 相同！** |

ImGuizmo 内部的 `matrix_t` 类型是 C 语言的 `float m[4][4]`，这是**行主序**的 ——
与本项目的 `Mat4` 结构体内存布局完全一致。尽管函数名叫 `mat4_to_colmajor`，
但实际转换是**逐元素直接拷贝，而不是转置**。

### 4.2 正确的转换函数

```c
/* 行主序 Mat4 → ImGuizmo 用的 float[16]（直接拷贝，不要转置！） */
void mat4_to_colmajor(float out[16], const Mat4 *m)
{
    const float *src = &m->m11;
    out[ 0] = src[ 0]; out[ 1] = src[ 1]; ... out[15] = src[15];
}

/* ImGuizmo 返回的 float[16] → 行主序 Mat4（直接拷贝，不要转置！） */
void mat4_from_colmajor(Mat4 *m, const float in[16])
{
    m->m11 = in[ 0]; m->m12 = in[ 1]; ... m->m44 = in[15];
}
```

**如果在这里做了转置，平移分量会落到错误的索引**（`[3,7,11]` 而非 `[12,13,14]`）。
Gizmo 箭头可能仍然会显示（view/projection 矩阵的错误不太明显），
但**物体会纹丝不动，拖拽完全无效**。

### 4.3 症状排查表

| 症状 | 可能原因 |
|---|---|
| Gizmo 箭头不显示 | 矩阵传入了错误的布局（例如该直接拷贝时做了转置） |
| 箭头可见但固定在原点不动 | view 或 projection 矩阵布局错误 |
| 使用 Gizmo 时物体跳到原点 | `world` 矩阵拷贝时布局错误 |
| 拖拽无任何效果 | `mat4_to_colmajor` 做了转置而非直接拷贝 |

---

## 5. 与相机轨道共存

这是第二难的问题。你的编辑器有两个消费鼠标输入的系统：
3D Gizmo（ImGuizmo）和相机控制器。两者都要响应鼠标左键拖拽。
下面的模式能干净地解决冲突。

### 5.1 相机输入的双重门控

用**两个条件同时**把守相机拖拽的入口：

```c
case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (evt.button.button == SDL_BUTTON_LEFT &&
        !ImGui_GetIO()->WantCaptureMouse &&     /* 不在 ImGui 面板上 */
        !FNA3D_ImGuizmo_IsUsingEXT(device))     /* 不在拖拽 Gizmo */
        dragging = 1;
    break;
```

**为什么需要两个条件？**

- `WantCaptureMouse`：用户与 ImGui 面板交互（滑块、按钮等）时由 ImGui 设置。
  不消费鼠标输入的 ImGui 窗口不会设置此标志。
- `IsUsingEXT`：用户正在拖拽 Gizmo 手柄时由 ImGuizmo 设置。虽然 Gizmo 绘制在
  一个 `NoInputs` 的 ImGui 窗口中（参见 §6.2），但拖拽状态在内部跟踪。

没有这双重门控的话，在 Gizmo 箭头上按下左键会**同时**移动物体和旋转相机。

### 5.2 滚轮缩放

滚轮事件通常不会产生冲突，但在 Gizmo 使用中也可以选择性地屏蔽：

```c
case SDL_EVENT_MOUSE_WHEEL:
    if (!FNA3D_ImGuizmo_IsUsingEXT(device))
    {
        radius -= evt.wheel.y * 1.0f;
    }
    break;
```

---

## 6. 开发过程中踩过的坑

### 6.1 不要自己创建 Gizmo 窗口

**错误做法（尝试后失败）：**

```c
/* 不要这样做 */
ImGui_Begin("GizmoView", NULL, NoInputsFlags);
FNA3D_ImGuizmo_BeginFrameEXT(device);  /* 只调了 SetDrawlist */
/* ... SetRect / Manipulate ... */
ImGui_End();
```

**为什么失败：** 当 `BeginFrameEXT` 只调用 `ImGuizmo::SetDrawlist()` 时，
它跳过了 `ImGuizmo::BeginFrame()` 内部两个关键步骤：

1. **创建内部 "gizmo" 窗口** —— ImGuizmo 的 `BeginFrame()` 调用
   `ImGui::Begin("gizmo", …, NoInputs)` 并保存该窗口的绘制列表。
   绘制列表的 `_OwnerName` 被设为 `"gizmo"`，`IsHoveringWindow()` 后续通过
   `FindWindowByName()` 来查找它。

2. **重置 `mbOverGizmoHotspot`** —— 此每帧标志由 `BeginFrame()` 置为 `false`。
   如果不重置，一旦鼠标在某一帧扫过 Gizmo 手柄，该标志就会卡在 `true`，
   导致后续所有帧中 `GetMoveType()` 直接返回 `MT_NONE`。
   表现为 Gizmo 正常渲染但永远无法交互。

**正确做法：**

```c
/* 直接调用公开 API —— BeginFrame() 会处理剩下的一切 */
FNA3D_ImGui_NewFrameEXT(device);
FNA3D_ImGuizmo_BeginFrameEXT(device);  /* 委托到 ImGuizmo::BeginFrame() */
```

`BeginFrameEXT` 现在调用完整的 `ImGuizmo::BeginFrame()`，它创建带 `NoInputs`
 的内部 `"gizmo"` 窗口、捕获绘制列表、并重置所有每帧状态。

### 6.2 理解 NoInputs 窗口的机制

ImGuizmo 内部窗口使用 `ImGuiWindowFlags_NoInputs`。这是有意为之：
窗口必须覆盖整个视口使 Gizmo 图形无处不在，但又不能消费鼠标点击
（否则相机轨道就废了）。

`ImGuizmo.cpp` 第 937 行的 `IsHoveringWindow()` 用四步回退处理：

```
1. 当前悬停窗口是 gizmo 窗口？          → 否（NoInputs 阻止了此情况）
2. 设置了替代窗口且正在悬停？            → 否（此处未使用）
3. 有任意其他 ImGui 窗口被悬停？         → 是则直接退出（面板有焦点）
4. 鼠标在 gizmo 窗口的屏幕矩形内？       → 是则视为悬停
```

第 4 步是关键：当没有其他 ImGui 窗口被悬停时，纯屏幕矩形测试成功。
这就是为什么**你的 ImGui 面板必须在 Gizmo 之后绘制** ——
如果面板在 Gizmo 之前绘制，而鼠标恰好在其（可能不可见的）区域上，
第 3 步就会阻断 Gizmo 的点击测试。

### 6.3 面板绘制顺序

编辑器面板放在 Gizmo 调用之后，而非之前：

```c
/* 1. 先画 Gizmo */
FNA3D_ImGuizmo_BeginFrameEXT(device);
FNA3D_ImGuizmo_SetRectEXT(device, 0, 0, w, h);
FNA3D_ImGuizmo_ManipulateEXT(device, ...);

/* 2. 后面板 */
ImGui_Begin("Inspector", NULL, 0);
ImGui_SliderFloat3("Position", pos, -10, 10);
ImGui_End();
```

### 6.4 事件轮询顺序

在调用 `NewFrame` **之前**轮询 SDL 事件。ImGui 后端在 `NewFrame` 期间
处理输入事件，因此必须先清空 SDL 事件队列：

```c
while (running)
{
    /* 1. 先轮询原始事件 */
    while (SDL_PollEvent(&evt)) { ... }

    /* 2. 再开始 ImGui/ImGuizmo 帧 */
    FNA3D_ImGui_NewFrameEXT(device);
    FNA3D_ImGuizmo_BeginFrameEXT(device);
    /* ... */
}
```

### 6.5 DXC 顶点属性 Location 分配

DXC（HLSL → SPIR-V）按照 **HLSL 结构体字段声明顺序**（0, 1, 2, …）分配
SPIR-V `Location`，而**不是**按 `usage*16+index` 的规则。
你的 `FNA3D_VertexElement` 布局必须严格匹配 HLSL 结构体的字段声明顺序。

这与 ImGuizmo 无关，但如果你为编辑器叠加层添加新的 Shader 输入，
需要留意这一点。

### 6.6 暂不支持 Uniform / Constant Buffer API

FNA3D_HLSL 尚未实现 uniform/constant buffer 更新 API。
Shader 必须是直通的（pass-through），或者依赖烘焙在 FEB 中的默认参数值。
在你的 FEB manifest 中保持 `paramCount = 0`。

---

## 7. 完整示例骨架

```c
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <FNA3D.h>
#include <FNA3D_ImGui.h>
#include "dcimgui.h"
#include <FNA3D_ImGuizmo.h>
#include "math3d.h"

int main(void)
{
    /* -------- 初始化（详见 teapot.c） -------- */
    FNA3D_ImGui_InitEXT(device);

    Mat4 view, proj, model;
    mat4_identity(&model);
    /* ... 设置 view & proj ... */

    /* -------- 主循环 -------- */
    int dragging = 0;

    while (running)
    {
        /* 1. 轮询原始 SDL 事件 */
        while (SDL_PollEvent(&evt))
        {
            switch (evt.type)
            {
            case SDL_EVENT_QUIT:
                running = 0; break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (evt.button.button == SDL_BUTTON_LEFT &&
                    !ImGui_GetIO()->WantCaptureMouse &&
                    !FNA3D_ImGuizmo_IsUsingEXT(device))
                    dragging = 1;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (evt.button.button == SDL_BUTTON_LEFT)
                    dragging = 0;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (dragging) { /* 更新相机 */ }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                /* 缩放，可选地用 !IsUsingEXT 做门控 */
                break;
            }
        }

        /* 2. 开始 ImGui 帧 */
        FNA3D_ImGui_NewFrameEXT(device);

        /* 3. Gizmo */
        {
            float view_cm[16], proj_cm[16], model_cm[16];
            mat4_to_colmajor(view_cm,  &view);
            mat4_to_colmajor(proj_cm,  &proj);
            mat4_to_colmajor(model_cm, &model);

            FNA3D_ImGuizmo_BeginFrameEXT(device);
            FNA3D_ImGuizmo_SetRectEXT(device, 0, 0, width, height);
            FNA3D_ImGuizmo_ManipulateEXT(device,
                view_cm, proj_cm,
                FNA3D_IMGUIZMO_TRANSLATE,
                FNA3D_IMGUIZMO_WORLD,
                model_cm);

            mat4_from_colmajor(&model, model_cm);
        }

        /* 4. 编辑器面板（Gizmo 之后） */
        {
            ImGui_Begin("Inspector", NULL, 0);
            ImGui_Text("物体位置: %.2f, %.2f, %.2f",
                model.m41, model.m42, model.m43);
            ImGui_End();
        }

        /* 5. 渲染 */
        mat4_mul(&mvp, &model, &viewproj);
        mat4_transpose(&mvp_t, &mvp);
        FNA3D_SetEffectParamValue(device, effect, "WorldViewProj",
            &mvp_t.m11, 0, sizeof(Mat4));
        FNA3D_Clear(device, ...);
        FNA3D_ApplyEffect(...);
        FNA3D_DrawPrimitives(...);
        FNA3D_SwapBuffers(...);
    }

    /* -------- 清理 -------- */
    FNA3D_ImGui_ShutdownEXT(device);
    FNA3D_DestroyDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
```

---

## 8. 调试技巧

### Gizmo 箭头不显示

1. 确认 `BeginFrameEXT` 在 `NewFrameEXT` **之后**调用。
2. 确认 `SetRectEXT` 在 `BeginFrameEXT` **之后**调用。
3. 打印传给 `ManipulateEXT` 之前的 `world` 矩阵值 ——
   平移分量应位于 float[16] 数组的 `[12]`、`[13]`、`[14]` 位置。
4. 如果物体离原点很远或在相机背后，Gizmo 中心点可能在屏幕外。

### Gizmo 可见但无法交互

1. 确认 `BeginFrameEXT` 委托到完整的 `ImGuizmo::BeginFrame()`（而非只调 `SetDrawlist`）。
   参见 §6.1。
2. 临时禁用相机拖拽以排除输入冲突。
3. 确认没有其他 ImGui 窗口意外覆盖视口（检查 §6.3 的面板顺序）。
4. 每帧添加 `FNA3D_ImGuizmo_IsOverEXT(device)` 的调试 printf ——
   如果始终为 0，说明 `IsHoveringWindow()` 失败。

### 相机轨道失效

1. 确认鼠标按下处理中有 `!ImGui_GetIO()->WantCaptureMouse` 门控。
2. 确认有 `!FNA3D_ImGuizmo_IsUsingEXT(device)` 门控 —— 没有它的话，
   ImGuizmo 会 `SetNextFrameWantCaptureMouse(true)`，但相机代码也同时开始拖拽，
   两者互相打架。
3. 如果你去掉了 gizmo 窗口的 `NoInputs` 标志（不要这样做），
   全视口窗口会吃掉所有鼠标事件。

---

## 9. 参考资料

- **FNA3D_HLSL ImGuizmo 封装**：`../FNA3D_HLSL/src/FNA3D_ImGuizmo.cpp`
- **公开头文件**：`../FNA3D_HLSL/include/FNA3D_ImGuizmo.h`
- **CMake 集成**：`../FNA3D_HLSL/CMakeLists.txt`（第 212 行起）
- **可运行的测试**：`src/teapot.c`
- **矩阵工具函数**：`src/math3d.c`（`mat4_to_colmajor` / `mat4_from_colmajor`）
- **ImGuizmo 上游**：<https://github.com/CedricGuillemet/ImGuizmo>（MIT 协议）
