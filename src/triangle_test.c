#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <FNA3D.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vertex
{
	float x, y, z;
	uint8_t r, g, b, a;
} Vertex;

int main(int argc, char *argv[])
{
	SDL_Window *window;
	FNA3D_Device *device;
	FNA3D_PresentationParameters pp;
	FNA3D_Effect *effect;
	FNA3D_Buffer *vb;
	FNA3D_VertexElement elements[2];
	FNA3D_VertexDeclaration decl;
	FNA3D_VertexBufferBinding binding;
	FNA3D_RasterizerState rasterizer;
	FNA3D_BlendState blend;
	FNA3D_DepthStencilState depthStencil;
	FNA3D_Viewport viewport;
	uint8_t *febBytes;
	size_t febLen;
	uint8_t running;
	FNA3D_Vec4 clearColor;
	SDL_Event evt;
	uint32_t version;
	int32_t backbufferW, backbufferH;
	FNA3D_SurfaceFormat backbufferFormat;
	FNA3D_DepthFormat backbufferDepthFormat;
	int32_t textures, vertexTextures;
	FNA3D_EffectTechnique *technique;

	/* Three vertices in NDC, each a different color */
	Vertex vertices[3] = {
		{  0.0f,  0.5f, 0.0f, 255,   0,   0, 255 }, /* top: red */
		{ -0.5f, -0.5f, 0.0f,   0, 255,   0, 255 }, /* bottom-left: green */
		{  0.5f, -0.5f, 0.0f,   0,   0, 255, 255 }, /* bottom-right: blue */
	};

	(void) argc;
	(void) argv;

	/* --- Init SDL --- */
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}

	SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

	/* --- Create window --- */
	memset(&pp, 0, sizeof(pp));
	pp.backBufferWidth = 800;
	pp.backBufferHeight = 600;
	pp.multiSampleCount = 0;
	pp.isFullScreen = 0;
	pp.depthStencilFormat = FNA3D_DEPTHFORMAT_NONE;
	pp.presentationInterval = FNA3D_PRESENTINTERVAL_DEFAULT;
	pp.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
	pp.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

	window = SDL_CreateWindow(
		"FNA3D_HLSL Triangle Test",
		pp.backBufferWidth,
		pp.backBufferHeight,
		FNA3D_PrepareWindowAttributes()
	);
	if (window == NULL)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	pp.deviceWindowHandle = window;

	/* --- Create FNA3D device --- */
	device = FNA3D_CreateDevice(&pp, 1);
	if (device == NULL)
	{
		SDL_Log("FNA3D_CreateDevice failed");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	/* Print version and feature info */
	version = FNA3D_LinkedVersion();
	SDL_Log("FNA3D version: %u.%u.%u (ABI %u)",
		(version >> 24) & 0xFF,
		(version >> 16) & 0xFF,
		version & 0xFFFF,
		(version >> 24) & 0xFF);

	FNA3D_GetBackbufferSize(device, &backbufferW, &backbufferH);
	backbufferFormat = FNA3D_GetBackbufferSurfaceFormat(device);
	backbufferDepthFormat = FNA3D_GetBackbufferDepthFormat(device);
	SDL_Log("Backbuffer: %dx%d, format=%d, depthFormat=%d",
		backbufferW, backbufferH, backbufferFormat, backbufferDepthFormat);

	FNA3D_GetMaxTextureSlots(device, &textures, &vertexTextures);
	SDL_Log("Max texture slots: %d (vertex: %d)", textures, vertexTextures);
	SDL_Log("Features: DXT1=%d S3TC=%d BC7=%d Instancing=%d NoOverwrite=%d SRGB=%d",
		FNA3D_SupportsDXT1(device),
		FNA3D_SupportsS3TC(device),
		FNA3D_SupportsBC7(device),
		FNA3D_SupportsHardwareInstancing(device),
		FNA3D_SupportsNoOverwrite(device),
		FNA3D_SupportsSRGBRenderTargets(device));

	/* --- Set initial render target (backbuffer) --- */
	FNA3D_SetRenderTargets(
		device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_NONE, 0);

	/* --- Load FEB effect --- */
	{
		SDL_IOStream *io = SDL_IOFromFile(
			"../assets/effects/triangle.feb", "rb");
		if (io == NULL)
		{
			SDL_Log("Failed to open triangle.feb: %s", SDL_GetError());
			FNA3D_DestroyDevice(device);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 1;
		}
		febLen = (size_t) SDL_GetIOSize(io);
		febBytes = (uint8_t*) SDL_malloc(febLen);
		if (febBytes == NULL || SDL_ReadIO(io, febBytes, febLen) != febLen)
		{
			SDL_Log("Failed to read triangle.feb");
			SDL_free(febBytes);
			SDL_CloseIO(io);
			FNA3D_DestroyDevice(device);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 1;
		}
		SDL_CloseIO(io);

		if (!FNA3D_CreateEffect(device, febBytes, (uint32_t) febLen, &effect))
		{
			SDL_Log("FNA3D_CreateEffect failed");
			SDL_free(febBytes);
			FNA3D_DestroyDevice(device);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 1;
		}
		SDL_free(febBytes);
	}

	/* --- Select technique --- */
	if (FNA3D_GetEffectTechniqueCount(effect) == 0)
	{
		SDL_Log("Effect has no techniques");
		FNA3D_AddDisposeEffect(device, effect);
		FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	technique = FNA3D_GetEffectTechnique(effect, 0);
	SDL_Log("Technique: %s (%d passes)",
		FNA3D_GetTechniqueName(technique),
		FNA3D_GetTechniquePassCount(technique));
	FNA3D_SetEffectTechnique(device, effect, technique);

	/* --- Create vertex buffer --- */
	vb = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t) sizeof(vertices));
	if (vb == NULL)
	{
		SDL_Log("FNA3D_GenVertexBuffer failed");
		FNA3D_AddDisposeEffect(device, effect);
		FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	FNA3D_SetVertexBufferData(device, vb, 0, vertices, 3,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);

	/* --- Vertex declaration --- */
	elements[0].offset = 0;
	elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	elements[0].usageIndex = 0;

	elements[1].offset = 12;
	elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_COLOR;
	elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_COLOR;
	elements[1].usageIndex = 0;

	decl.vertexStride = sizeof(Vertex);
	decl.elementCount = 2;
	decl.elements = elements;

	memset(&binding, 0, sizeof(binding));
	binding.vertexBuffer = vb;
	binding.vertexDeclaration = decl;
	binding.vertexOffset = 0;
	binding.instanceFrequency = 0;

	/* --- Pipeline states --- */
	memset(&rasterizer, 0, sizeof(rasterizer));
	rasterizer.fillMode = FNA3D_FILLMODE_SOLID;
	rasterizer.cullMode = FNA3D_CULLMODE_NONE;
	FNA3D_ApplyRasterizerState(device, &rasterizer);

	memset(&blend, 0, sizeof(blend));
	blend.colorSourceBlend = FNA3D_BLEND_ONE;
	blend.colorDestinationBlend = FNA3D_BLEND_ZERO;
	blend.colorBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	blend.alphaSourceBlend = FNA3D_BLEND_ONE;
	blend.alphaDestinationBlend = FNA3D_BLEND_ZERO;
	blend.alphaBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	blend.colorWriteEnable = FNA3D_COLORWRITECHANNELS_ALL;
	blend.colorWriteEnable1 = FNA3D_COLORWRITECHANNELS_ALL;
	blend.colorWriteEnable2 = FNA3D_COLORWRITECHANNELS_ALL;
	blend.colorWriteEnable3 = FNA3D_COLORWRITECHANNELS_ALL;
	blend.multiSampleMask = -1;
	FNA3D_SetBlendState(device, &blend);

	memset(&depthStencil, 0, sizeof(depthStencil));
	depthStencil.depthBufferEnable = 0;
	FNA3D_SetDepthStencilState(device, &depthStencil);

	viewport.x = 0;
	viewport.y = 0;
	viewport.w = pp.backBufferWidth;
	viewport.h = pp.backBufferHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	FNA3D_SetViewport(device, &viewport);

	/* --- Main loop --- */
	SDL_Log("Entering render loop...");
	running = 1;
	while (running)
	{
		while (SDL_PollEvent(&evt))
		{
			if (evt.type == SDL_EVENT_QUIT)
			{
				running = 0;
			}
		}

		clearColor.x = 0.2f;
		clearColor.y = 0.2f;
		clearColor.z = 0.2f;
		clearColor.w = 1.0f;
		FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET,
			&clearColor, 0.0f, 0);

		FNA3D_ApplyEffect(device, effect, 0, NULL);
		FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
		FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 1);

		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	/* --- Cleanup --- */
	SDL_Log("Cleaning up...");
	FNA3D_AddDisposeVertexBuffer(device, vb);
	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();

	SDL_Log("Done.");

	return 0;
}
