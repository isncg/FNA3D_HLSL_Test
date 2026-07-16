// AlphaTestEffect test — textured quad with alpha-based pixel discard.
// Converts FNA AlphaTestEffect.fx to HLSL SM 6.0.
//
// Controls:
//   F/f    — cycle alpha test function (lt/gt/eq/ne)
//   Up/Down — adjust reference alpha
//   R      — toggle auto-oscillation of alpha reference
//   ESC    — quit

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
	float x, y, z;
	float u, v;
} Vertex;

static void set_identity_matrix(float m[16])
{
	memset(m, 0, sizeof(float) * 16);
	m[0]  = 1.0f;
	m[5]  = 1.0f;
	m[10] = 1.0f;
	m[15] = 1.0f;
}

/* Create a checkerboard texture with alpha gradient:
 *   left→right: alpha goes from 0.0 to 1.0
 *   checkerboard pattern in RGB channels
 */
static uint8_t *create_alpha_gradient_texture(int32_t width, int32_t height, int32_t square)
{
	int32_t x, y;
	uint8_t c, alpha;

	uint8_t *data = (uint8_t *) SDL_malloc(width * height * 4);
	if (data == NULL) return NULL;

	for (y = 0; y < height; y += 1)
	{
		for (x = 0; x < width; x += 1)
		{
			c = (((x / square) + (y / square)) & 1) ? 0 : 255;
			alpha = (uint8_t) ((float) x / (float) (width - 1) * 255.0f);
			data[(y * width + x) * 4 + 0] = c;
			data[(y * width + x) * 4 + 1] = c;
			data[(y * width + x) * 4 + 2] = c;
			data[(y * width + x) * 4 + 3] = alpha;
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
	FNA3D_VertexElement elements[2];
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
	float alphaRef = 0.5f;
	int alphaFunc = 0; /* 0=lt, 1=gt, 2=eq, 3=ne */
	int autoOscillate = 1;
	float time = 0.0f;
	float alphaTest[4];

	/* Full-screen quad in NDC [-1,1] */
	Vertex quad[6] =
	{
		{-1.0f,  1.0f, 0.0f, 0.0f, 1.0f},
		{-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
		{ 1.0f, -1.0f, 0.0f, 1.0f, 0.0f},

		{-1.0f,  1.0f, 0.0f, 0.0f, 1.0f},
		{ 1.0f, -1.0f, 0.0f, 1.0f, 0.0f},
		{ 1.0f,  1.0f, 0.0f, 1.0f, 1.0f},
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

	window = SDL_CreateWindow("AlphaTestEffect — Left/Right arrows: func, Up/Down: ref, R: oscillate",
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
	effect_bytes = load_file("../assets/effects/alpha_test.feb", &effect_len);
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

	/* Vertex declaration */
	elements[0].offset = 0;
	elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	elements[0].usageIndex = 0;

	elements[1].offset = sizeof(float) * 3;
	elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
	elements[1].usageIndex = 0;

	decl.vertexStride = sizeof(Vertex);
	decl.elementCount = 2;
	decl.elements = elements;

	memset(&binding, 0, sizeof(binding));
	binding.vertexBuffer = vb;
	binding.vertexDeclaration = decl;
	binding.vertexOffset = 0;
	binding.instanceFrequency = 0;

	/* Texture with alpha gradient */
	pixels = create_alpha_gradient_texture(256, 256, 32);
	if (pixels == NULL)
	{
		SDL_Log("Failed to create texture");
		FNA3D_AddDisposeVertexBuffer(device, vb);
		FNA3D_AddDisposeEffect(device, effect);
		FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	texture = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, texture, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
	SDL_free(pixels);

	memset(&samplerState, 0, sizeof(samplerState));
	samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
	samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_CLAMP;
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
		{
			if (evt.type == SDL_EVENT_QUIT)
				running = 0;

			if (evt.type == SDL_EVENT_KEY_DOWN)
			{
				switch (evt.key.key)
				{
				case SDLK_ESCAPE:
					running = 0;
					break;
				case SDLK_F:
					alphaFunc = (alphaFunc + 1) & 3;
					SDL_Log("AlphaFunc: %d (0=lt, 1=gt, 2=eq, 3=ne)", alphaFunc);
					break;
				case SDLK_UP:
					alphaRef += 0.05f;
					if (alphaRef > 1.0f) alphaRef = 1.0f;
					break;
				case SDLK_DOWN:
					alphaRef -= 0.05f;
					if (alphaRef < 0.0f) alphaRef = 0.0f;
					break;
				case SDLK_R:
					autoOscillate = !autoOscillate;
					SDL_Log("Auto-oscillate: %s", autoOscillate ? "on" : "off");
					break;
				default:
					break;
				}
			}
		}

		time += 0.016f;
		if (autoOscillate)
			alphaRef = (SDL_sinf(time * 2.0f) + 1.0f) * 0.5f;

		/* Update AlphaTest uniform */
		alphaTest[0] = alphaRef;
		alphaTest[1] = 0.01f;          /* tolerance for eq/ne */
		alphaTest[2] = (float) alphaFunc;
		alphaTest[3] = -1.0f;          /* clip value (force discard) */
		FNA3D_SetEffectParamValue(device, effect, "AlphaTest",
			alphaTest, 0, (uint32_t) sizeof(alphaTest));

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
