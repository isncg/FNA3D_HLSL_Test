#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <FNA3D.h>

#include "common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vertex
{
	float x, y, z, w;
	float u, v;
	uint8_t b, g, r, a;
} Vertex;

static void set_identity_matrix(float m[16])
{
	memset(m, 0, sizeof(float) * 16);
	m[0]  = 1.0f;
	m[5]  = 1.0f;
	m[10] = 1.0f;
	m[15] = 1.0f;
}

static uint8_t *create_checkerboard(int32_t width, int32_t height, int32_t square)
{
	uint8_t *data;
	int32_t x, y;
	uint8_t c;

	data = (uint8_t *) SDL_malloc(width * height * 4);
	if (data == NULL) return NULL;

	for (y = 0; y < height; y += 1)
	{
		for (x = 0; x < width; x += 1)
		{
			c = (((x / square) + (y / square)) & 1) ? 0 : 255;
			data[(y * width + x) * 4 + 0] = c;
			data[(y * width + x) * 4 + 1] = c;
			data[(y * width + x) * 4 + 2] = c;
			data[(y * width + x) * 4 + 3] = 255;
		}
	}
	return data;
}

int main(int argc, char *argv[])
{
	SDL_Window *window;
	FNA3D_Device *device;
	FNA3D_PresentationParameters pp;
	FNA3D_Effect *effect;
	FNA3D_Buffer *vb;
	FNA3D_Texture *texture;
	FNA3D_VertexElement elements[3];
	FNA3D_VertexDeclaration decl;
	FNA3D_VertexBufferBinding binding;
	FNA3D_RasterizerState rasterizer;
	FNA3D_BlendState blend;
	FNA3D_DepthStencilState depthStencil;
	FNA3D_SamplerState samplerState;
	FNA3D_Viewport viewport;
	uint8_t *effect_bytes;
	uint8_t *checkerboard;
	uint32_t effect_len;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clearColor = {0.2f, 0.2f, 0.2f, 1.0f};
	FNA3D_EffectTechnique *technique;
	float identity[16];

	/* Quad in NDC [-0.5, 0.5] x [-0.5, 0.5], UV covering full texture */
	Vertex quad[6] =
	{
		{-0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 255, 255, 255, 255},
		{-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 255, 255, 255, 255},
		{ 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 255, 255, 255, 255},

		{-0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 255, 255, 255, 255},
		{ 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 255, 255, 255, 255},
		{ 0.5f,  0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 255, 255, 255, 255}
	};

	(void) argc;
	(void) argv;

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

	memset(&pp, 0, sizeof(pp));
	pp.backBufferWidth = 800;
	pp.backBufferHeight = 600;
	pp.multiSampleCount = 0;
	pp.isFullScreen = 0;
	pp.depthStencilFormat = FNA3D_DEPTHFORMAT_NONE;
	pp.presentationInterval = FNA3D_PRESENTINTERVAL_DEFAULT;
	pp.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
	pp.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

	window = SDL_CreateWindow("FNA3D_HLSL Textured Quad",
		pp.backBufferWidth, pp.backBufferHeight,
		FNA3D_PrepareWindowAttributes());
	if (window == NULL)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	pp.deviceWindowHandle = window;

	device = FNA3D_CreateDevice(&pp, 1);
	if (device == NULL)
	{
		SDL_Log("FNA3D_CreateDevice failed");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	FNA3D_SetRenderTargets(device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_NONE, 0);

	/* Load effect */
	effect_bytes = load_file("../assets/effects/sprite.feb", &effect_len);
	if (effect_bytes == NULL)
	{
		FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	if (!FNA3D_CreateEffect(device, effect_bytes, effect_len, &effect))
	{
		SDL_Log("FNA3D_CreateEffect failed");
		SDL_free(effect_bytes);
		FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	SDL_free(effect_bytes);

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
	FNA3D_SetEffectTechnique(device, effect, technique);

	/* Set MatrixTransform to identity */
	set_identity_matrix(identity);
	FNA3D_SetEffectParamValue(device, effect, "MatrixTransform",
		identity, 0, (uint32_t) sizeof(identity));

	/* Vertex buffer */
	vb = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t) sizeof(quad));
	FNA3D_SetVertexBufferData(device, vb, 0, quad, 6,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);

	/* Vertex declaration */
	elements[0].offset = 0;
	elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR4;
	elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	elements[0].usageIndex = 0;

	elements[1].offset = sizeof(float) * 4;
	elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
	elements[1].usageIndex = 0;

	elements[2].offset = sizeof(float) * 6;
	elements[2].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_COLOR;
	elements[2].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_COLOR;
	elements[2].usageIndex = 0;

	decl.vertexStride = sizeof(Vertex);
	decl.elementCount = 3;
	decl.elements = elements;

	memset(&binding, 0, sizeof(binding));
	binding.vertexBuffer = vb;
	binding.vertexDeclaration = decl;
	binding.vertexOffset = 0;
	binding.instanceFrequency = 0;

	/* Checkerboard texture */
	checkerboard = create_checkerboard(256, 256, 32);
	if (checkerboard == NULL)
	{
		SDL_Log("Failed to create checkerboard texture");
		FNA3D_AddDisposeVertexBuffer(device, vb);
		FNA3D_AddDisposeEffect(device, effect);
		FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	texture = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, texture, 0, 0, 256, 256, 0, checkerboard, 256 * 256 * 4);
	SDL_free(checkerboard);

	memset(&samplerState, 0, sizeof(samplerState));
	samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
	samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_WRAP;
	samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_WRAP;
	samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
	FNA3D_VerifySampler(device, 0, texture, &samplerState);

	viewport.x = 0; viewport.y = 0;
	viewport.w = pp.backBufferWidth; viewport.h = pp.backBufferHeight;
	viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
	FNA3D_SetViewport(device, &viewport);

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
	FNA3D_SetDepthStencilState(device, &depthStencil);

	while (running)
	{
		while (SDL_PollEvent(&evt))
			if (evt.type == SDL_EVENT_QUIT) running = 0;

		FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET, &clearColor, 1.0f, 0);
		FNA3D_ApplyEffect(device, effect, 0, NULL);
		FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
		FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 2);
		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	FNA3D_AddDisposeTexture(device, texture);
	FNA3D_AddDisposeVertexBuffer(device, vb);
	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
