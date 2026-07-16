// DualTextureEffect test — blends two textures multiplicatively.
// Converts FNA DualTextureEffect.fx to HLSL SM 6.0.
//
// Controls:
//   ESC — quit

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <FNA3D.h>

#include "common.h"
#include "math3d.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct Vertex
{
	float x, y, z;
	float u1, v1;
	float u2, v2;
} Vertex;

static void set_identity_matrix(float m[16])
{
	memset(m, 0, sizeof(float) * 16);
	m[0]  = 1.0f;
	m[5]  = 1.0f;
	m[10] = 1.0f;
	m[15] = 1.0f;
}

/* Base texture: colored checkerboard (red/cyan alternating squares) */
static uint8_t *create_base_texture(int32_t width, int32_t height, int32_t square)
{
	uint8_t *data = (uint8_t *) SDL_malloc(width * height * 4);
	if (data == NULL) return NULL;

	for (int32_t y = 0; y < height; y++)
	{
		for (int32_t x = 0; x < width; x++)
		{
			int32_t check = ((x / square) + (y / square)) & 1;
			int32_t idx = (y * width + x) * 4;
			data[idx + 0] = check ? 255 : 0;    /* R: red or black */
			data[idx + 1] = check ? 0 : 255;    /* G: black or cyan */
			data[idx + 2] = check ? 0 : 255;    /* B: black or cyan */
			data[idx + 3] = 255;
		}
	}
	return data;
}

/* Overlay texture: grayscale radial gradient */
static uint8_t *create_overlay_texture(int32_t width, int32_t height)
{
	uint8_t *data = (uint8_t *) SDL_malloc(width * height * 4);
	if (data == NULL) return NULL;

	float cx = (float) width * 0.5f;
	float cy = (float) height * 0.5f;
	float maxDist = sqrtf(cx * cx + cy * cy);

	for (int32_t y = 0; y < height; y++)
	{
		for (int32_t x = 0; x < width; x++)
		{
			float dx = (float) x - cx;
			float dy = (float) y - cy;
			float dist = sqrtf(dx * dx + dy * dy);
			uint8_t v = (uint8_t) ((1.0f - dist / maxDist) * 255.0f);
			int32_t idx = (y * width + x) * 4;
			data[idx + 0] = v;
			data[idx + 1] = v;
			data[idx + 2] = v;
			data[idx + 3] = 255;
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
	FNA3D_Texture *texBase, *texOverlay;
	FNA3D_VertexElement elements[3];
	FNA3D_VertexDeclaration decl;
	FNA3D_VertexBufferBinding binding;
	FNA3D_RasterizerState rasterizer;
	FNA3D_BlendState blend;
	FNA3D_DepthStencilState depthStencil;
	FNA3D_SamplerState samplerState;
	FNA3D_Viewport viewport;
	uint8_t *effect_bytes;
	uint8_t *pixels;
	uint32_t effect_len;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clearColor = {0.1f, 0.1f, 0.15f, 1.0f};
	FNA3D_EffectTechnique *technique;
	float identity[16];
	float time = 0.0f;

	/* Quad with two texcoord sets */
	Vertex quad[6] =
	{
		{-0.8f,  0.8f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
		{-0.8f, -0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
		{ 0.8f, -0.8f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},

		{-0.8f,  0.8f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
		{ 0.8f, -0.8f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
		{ 0.8f,  0.8f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
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

	window = SDL_CreateWindow("DualTextureEffect — two textures blended",
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
	effect_bytes = load_file("../assets/effects/dual_texture.feb", &effect_len);
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

	/* Set identity WorldViewProj */
	set_identity_matrix(identity);
	FNA3D_SetEffectParamValue(device, effect, "WorldViewProj",
		identity, 0, (uint32_t) sizeof(identity));

	/* Vertex buffer */
	vb = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t) sizeof(quad));
	FNA3D_SetVertexBufferData(device, vb, 0, quad, 6,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);

	/* Vertex declaration: POSITION, TEXCOORD0, TEXCOORD1 */
	elements[0].offset = 0;
	elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	elements[0].usageIndex = 0;

	elements[1].offset = sizeof(float) * 3;
	elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
	elements[1].usageIndex = 0;

	elements[2].offset = sizeof(float) * 5;
	elements[2].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	elements[2].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
	elements[2].usageIndex = 1;

	decl.vertexStride = sizeof(Vertex);
	decl.elementCount = 3;
	decl.elements = elements;

	memset(&binding, 0, sizeof(binding));
	binding.vertexBuffer = vb;
	binding.vertexDeclaration = decl;
	binding.vertexOffset = 0;
	binding.instanceFrequency = 0;

	/* Base texture: colored checkerboard */
	pixels = create_base_texture(256, 256, 32);
	if (pixels == NULL) goto cleanup_no_tex;
	texBase = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, texBase, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
	SDL_free(pixels);

	/* Overlay texture: radial gradient */
	pixels = create_overlay_texture(256, 256);
	if (pixels == NULL) { FNA3D_AddDisposeTexture(device, texBase); goto cleanup_no_tex; }
	texOverlay = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, texOverlay, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
	SDL_free(pixels);

	/* Bind textures to slots 0 and 1 */
	memset(&samplerState, 0, sizeof(samplerState));
	samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
	samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_WRAP;
	samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_WRAP;
	samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
	FNA3D_VerifySampler(device, 0, texBase, &samplerState);
	FNA3D_VerifySampler(device, 1, texOverlay, &samplerState);

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
		{
			if (evt.type == SDL_EVENT_QUIT)
				running = 0;
			if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_ESCAPE)
				running = 0;
		}

		time += 0.016f;

		/* Animate diffuse color with a subtle pulsing hue */
		float diffuse[4];
		diffuse[0] = 0.5f + 0.5f * SDL_sinf(time * 0.7f);
		diffuse[1] = 0.5f + 0.5f * SDL_sinf(time * 0.7f + 2.0f);
		diffuse[2] = 0.5f + 0.5f * SDL_sinf(time * 0.7f + 4.0f);
		diffuse[3] = 1.0f;
		FNA3D_SetEffectParamValue(device, effect, "DiffuseColor",
			diffuse, 0, (uint32_t) sizeof(diffuse));

		FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET, &clearColor, 1.0f, 0);
		FNA3D_ApplyEffect(device, effect, 0, NULL);
		FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
		FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 2);
		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	FNA3D_AddDisposeTexture(device, texOverlay);
	FNA3D_AddDisposeTexture(device, texBase);
	FNA3D_AddDisposeVertexBuffer(device, vb);
	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;

cleanup_no_tex:
	FNA3D_AddDisposeVertexBuffer(device, vb);
	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 1;
}
