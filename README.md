# FNA3D_HLSL_Test

针对 [FNA3D_HLSL](../FNA3D_HLSL/) 的渲染管线测试项目。FNA3D_HLSL 是 FNA3D 的 HLSL-only 分支，移除了 MojoShader，改用 DXC 将 HLSL 编译为 SPIR-V，打包为自定义 FEB 二进制格式，运行时通过 SDL_shadercross 交给 SDL_GPU（Vulkan/Metal/D3D12）。

## 前置条件

| 依赖 | 说明 |
|------|------|
| SDL3 (≥3.2.0) | 提供 SDL_GPU 抽象层 |
| DXC | DirectX Shader Compiler，HLSL → SPIR-V |
| CMake (≥3.16) | 构建系统 |
| Python 3 | FEB builder 工具 |
| Vulkan 驱动 | 当前默认使用 Vulkan 后端 |

## 快速开始

```bash
# 构建 FEB 着色器资源 + CMake 配置 + 编译
./build_linux.sh

# 运行测试
./out_linux/triangle_test
```

## 项目结构

```
FNA3D_HLSL_Test/
├── CMakeLists.txt              # 构建配置
├── build_linux.sh              # 一键构建脚本
├── tools/
│   └── feb_builder.py          # FEB 打包工具（HLSL→DXC→SPIRV→.feb）
├── assets/
│   ├── effects/
│   │   └── *.feb.json          # FEB manifest（描述 effect 结构）
│   └── shaders/
│       ├── *_vs.hlsl           # 顶点着色器
│       └── *_ps.hlsl           # 像素着色器
└── src/
    └── *_test.c                # 测试程序（自包含，每个一个可执行文件）
```

## 着色器管线

FNA3D_HLSL 使用 **FEB (FNA3D Effect Binary)** 格式，管线如下：

```
HLSL 源码 (.hlsl)
  → DXC -spirv -T vs_6_0 / -T ps_6_0
    → SPIR-V 二进制
      → FEB Builder (tools/feb_builder.py)
        → .feb 文件
          → FNA3D_CreateEffect() 加载
            → SDL_GPU 创建 Shader → 绑定 Pipeline → 绘制
```

**关键限制**：当前 FNA3D_HLSL 尚未实现 uniform/常量缓冲区更新 API。着色器只能使用：
- 无 uniform 的直穿着色器（NDC 空间顶点 + 顶点颜色）
- 或使用 FEB 中烘焙的默认参数值（后续 uniform API 就绪后可动态更新）

## 添加新测试的步骤

### 1. 编写 HLSL 着色器

```
assets/shaders/my_vs.hlsl    顶点着色器（入口如 VSMain）
assets/shaders/my_ps.hlsl    像素着色器（入口如 PSMain）
```

着色器模板（无 uniform，直传 NDC 位置 + 颜色）：

```hlsl
// my_vs.hlsl
struct VS_INPUT {
    float4 Position : POSITION0;
    float4 Color    : COLOR0;
};
struct VS_OUTPUT {
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};
VS_OUTPUT VSMain(VS_INPUT input) {
    VS_OUTPUT output;
    output.Position = input.Position;
    output.Color = input.Color;
    return output;
}

// my_ps.hlsl
float4 PSMain(float4 color : COLOR0) : SV_TARGET0 {
    return color;
}
```

### 2. 编写 FEB Manifest

文件：`assets/effects/my_effect.feb.json`

```json
{
  "techniques": [{
    "name": "MainTechnique",
    "passes": [{
      "name": "P0",
      "vertexShader": {"source": "../shaders/my_vs.hlsl", "entry": "VSMain"},
      "pixelShader": {"source": "../shaders/my_ps.hlsl", "entry": "PSMain"},
      "renderStates": [],
      "samplerStates": []
    }]
  }],
  "parameters": []
}
```

**注意**：`source` 路径相对于 manifest 文件所在目录。

### 3. 编写测试程序

文件：`src/my_test.c`

参考 `src/triangle_test.c`，基本结构：

```c
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <FNA3D.h>

int main(int argc, char *argv[]) {
    // 1. SDL 初始化
    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

    // 2. 创建窗口
    FNA3D_PresentationParameters pp = {0};
    pp.backBufferWidth = 800;
    pp.backBufferHeight = 600;
    pp.depthStencilFormat = FNA3D_DEPTHFORMAT_NONE;
    // ... 其他 pp 字段
    SDL_Window *window = SDL_CreateWindow("Title", 800, 600,
        FNA3D_PrepareWindowAttributes());
    pp.deviceWindowHandle = window;

    // 3. 创建设备
    FNA3D_Device *device = FNA3D_CreateDevice(&pp, 1);
    FNA3D_SetRenderTargets(device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_NONE, 0);

    // 4. 加载 FEB effect
    //    使用 SDL_LoadFile / SDL_IOFromFile 读取 .feb 文件
    //    调用 FNA3D_CreateEffect(device, bytes, len, &effect)
    //    获取 technique: FNA3D_GetEffectTechnique(effect, 0)
    //    设置 technique: FNA3D_SetEffectTechnique(device, effect, tech)

    // 5. 创建顶点/索引缓冲
    //    FNA3D_GenVertexBuffer → FNA3D_SetVertexBufferData

    // 6. 设置顶点声明
    //    FNA3D_VertexElement elements[] = {...}
    //    FNA3D_VertexDeclaration decl = {stride, count, elements}
    //    FNA3D_VertexBufferBinding binding = {vb, decl, ...}

    // 7. 设置管线状态（光栅化、混合、深度、视口）
    //    FNA3D_ApplyRasterizerState / FNA3D_SetBlendState /
    //    FNA3D_SetDepthStencilState / FNA3D_SetViewport

    // 8. 渲染循环
    while (running) {
        SDL_Event evt;
        while (SDL_PollEvent(&evt))
            if (evt.type == SDL_EVENT_QUIT) running = 0;

        FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET, &clearColor, 0, 0);
        FNA3D_ApplyEffect(device, effect, 0, NULL);
        FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
        FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 1);
        FNA3D_SwapBuffers(device, NULL, NULL, window);
    }

    // 9. 清理
    FNA3D_AddDisposeVertexBuffer(device, vb);
    FNA3D_AddDisposeEffect(device, effect);
    FNA3D_DestroyDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
```

### 4. 注册到 CMakeLists.txt

```cmake
add_executable(my_test src/my_test.c)
target_include_directories(my_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include")
target_link_libraries(my_test SDL3::SDL3 FNA3D m)
```

### 5. 构建和运行

```bash
# 先构建 FEB
python3 tools/feb_builder.py assets/effects/my_effect.feb.json

# 再编译
make -C build_linux -j$(nproc)

# 运行
./out_linux/my_test
```

## 着色器编写注意事项

1. **使用 SM 6.0 profile**：`vs_6_0` / `ps_6_0`
2. **入口函数名与 manifest 中的 entry 一致**
3. **顶点输入语义必须在 FNA3D_VertexElement 中声明**：
   - POSITION → `FNA3D_VERTEXELEMENTUSAGE_POSITION`，格式通常为 `VECTOR3`
   - COLOR → `FNA3D_VERTEXELEMENTUSAGE_COLOR`，格式为 `COLOR`
   - TEXCOORD → `FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE`，格式为 `VECTOR2`
4. **像素着色器输出**：使用 `SV_TARGET0`
5. **DXC 按声明顺序分配 SPIR-V Location**（0, 1, 2...），与 HLSL 输入参数顺序一致
6. **COLOR 语义的字节顺序**：`COLOR` 格式在内存中为 BGRA 顺序（XNA 约定），struct 中应写为 `b, g, r, a`

## 已知 FNA3D_HLSL Bug（已修复）

在开发过程中发现并修复了以下 bug，编写新测试时可参考：

| # | 文件 | 问题 | 症状 |
|---|------|------|------|
| 1 | `FNA3D_Driver_SDL.c` | `SDLGPU_CreateEffect` 返回 `SDLGPU_Effect*` 包装指针，但公开 API 直接读取 `FNA3D_Effect` 字段 | 技术名乱码、pass 数显示为 0 |
| 2 | `FNA3D_Driver_SDL.c` | 顶点 shader 声明 `num_samplers = MAX_TEXTURE_SAMPLERS(16)`，但只绑定了 `MAX_VERTEXTEXTURE_SAMPLERS(4)` | SDL_GPU 断言 "Missing vertex sampler binding" |
| 3 | `FNA3D_Driver_SDL.c` | 顶点属性 Location 公式 `usage*16+index` 与 DXC 按序分配不一致 | 三角形黑色（颜色数据未传到像素着色器） |
| 4 | `FNA3D_Effect.c` | 参数条目 stride 为 32 字节，但实际条目 ≥84 字节（含 64 字节默认值） | 多参数 effect 解析错误（未触发，paramCount=0 绕过） |

## FEB 二进制格式要点

编写 `feb_builder.py` 或手动构造 FEB 文件时需注意：

- Header 64 字节（16 × uint32 LE）
- 各 section 的 stride：Technique 16 字节、Pass 24 字节、Shader **24 字节**（非 20）
- 字符串表追加在 header 之后
- Parameter 条目目前有 stride bug（见上表 #4），建议 `paramCount = 0`
- `totalSize` 必须与文件实际大小一致

## 构建脚本

`build_linux.sh` 执行两步：
1. `python3 tools/feb_builder.py <manifest>` — 编译 HLSL → FEB
2. `cmake + make` — 编译 C 源码，链接 FNA3D_HLSL

输出目录：`out_linux/`
