// YUV→RGBA effect test — fullscreen quad with YUV→RGB color conversion.
// Converts FNA YUVToRGBAEffect.fx and YUVToRGBAEffectR.fx to HLSL SM 6.0.
//
// Controls:
//   C      — toggle channel mode (alpha / red)
//   S      — toggle color space (BT.709 / BT.601)
//   R/F    — adjust RescaleFactor (red-channel mode only)
//   1/2/3  — select which RescaleFactor component to adjust (+Shift=decrease)
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

/* Create a texture with data in all RGBA channels.
 * Width x height, each pixel gets value in all 4 channels.
 */
static uint8_t *create_single_value_texture(
	int32_t width, int32_t height,
	uint8_t (*valueFunc)(int32_t x, int32_t y, int32_t w, int32_t h))
{
	uint8_t *data = (uint8_t *) SDL_malloc(width * height * 4);
	if (data == NULL) return NULL;

	for (int32_t y = 0; y < height; y++)
	{
		for (int32_t x = 0; x < width; x++)
		{
			uint8_t v = valueFunc(x, y, width, height);
			int32_t idx = (y * width + x) * 4;
			data[idx + 0] = v;
			data[idx + 1] = v;
			data[idx + 2] = v;
			data[idx + 3] = v;
		}
	}
	return data;
}

/* Luma ramp (Y): horizontal gradient 16→235 in 8-bit space */
static uint8_t y_ramp(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void) y; (void) h;
	return (uint8_t) (16 + (x * (235 - 16)) / (w - 1));
}

/* U chroma: constant mid-gray (128 = no chroma shift after -0.5 offset) */
static uint8_t u_mid(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void) x; (void) y; (void) w; (void) h;
	return 128;
}

/* V chroma: constant mid-gray */
static uint8_t v_mid(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void) x; (void) y; (void) w; (void) h;
	return 128;
}

/* Create a color-bar style texture for U: vertical gradient 16→240 */
static uint8_t u_bars(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void) x; (void) w;
	int32_t stripe = (y * 6) / h;
	switch (stripe)
	{
		case 0: return 16;
		case 1: return 60;
		case 2: return 110;
		case 3: return 150;
		case 4: return 200;
		default: return 240;
	}
}

/* Create color-bar style for V: horizontal stripes */
static uint8_t v_bars(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void) y; (void) h;
	int32_t stripe = (x * 6) / w;
	switch (stripe)
	{
		case 0: return 16;
		case 1: return 60;
		case 2: return 110;
		case 3: return 150;
		case 4: return 200;
		default: return 240;
	}
}

int main(int argc, char *argv[])
{
	SDL_Window *window;
	FNA3D_Device *device;
	FNA3D_PresentationParameters pp;
	FNA3D_Effect *effect;
	FNA3D_Buffer *vb;
	FNA3D_Texture *texY, *texU, *texV;
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
	int channelMode = 0;     /* 0=alpha, 1=red */
	int colorSpace = 0;      /* 0=BT.709, 1=BT.601 */
	float rescale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	int rescaleComponent = 0; /* 0=x, 1=y, 2=z */

	/* Full-screen quad */
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

	window = SDL_CreateWindow(
		"YUV→RGBA Effect — C:channel S:colorspace 1/2/3:component R/F:adjust ESC:quit",
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
	effect_bytes = load_file("../assets/effects/yuv_to_rgba.feb", &effect_len);
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

	/* Create Y texture (luma ramp) */
	pixels = create_single_value_texture(256, 256, y_ramp);
	if (!pixels) goto cleanup_no_tex;
	texY = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, texY, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
	SDL_free(pixels);

	/* Create U texture (color bars) */
	pixels = create_single_value_texture(256, 256, u_bars);
	if (!pixels) { FNA3D_AddDisposeTexture(device, texY); goto cleanup_no_tex; }
	texU = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, texU, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
	SDL_free(pixels);

	/* Create V texture (color bars) */
	pixels = create_single_value_texture(256, 256, v_bars);
	if (!pixels) { FNA3D_AddDisposeTexture(device, texY); FNA3D_AddDisposeTexture(device, texU); goto cleanup_no_tex; }
	texV = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, texV, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
	SDL_free(pixels);

	/* Bind textures to sampler slots 0 (Y), 1 (U), 2 (V) */
	memset(&samplerState, 0, sizeof(samplerState));
	samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
	samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_CLAMP;

	FNA3D_VerifySampler(device, 0, texY, &samplerState);
	FNA3D_VerifySampler(device, 1, texU, &samplerState);
	FNA3D_VerifySampler(device, 2, texV, &samplerState);

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
				float *comp = &rescale[rescaleComponent];
				switch (evt.key.key)
				{
				case SDLK_ESCAPE:
					running = 0;
					break;
				case SDLK_C:
					channelMode = !channelMode;
					SDL_Log("ChannelMode: %d (%s)", channelMode,
						channelMode ? "red" : "alpha");
					FNA3D_SetEffectParamValue(device, effect, "ChannelMode",
						&channelMode, 0, sizeof(int));
					break;
				case SDLK_S:
					colorSpace = !colorSpace;
					SDL_Log("ColorSpace: %d (%s)", colorSpace,
						colorSpace ? "BT.601" : "BT.709");
					FNA3D_SetEffectParamValue(device, effect, "ColorSpace",
						&colorSpace, 0, sizeof(int));
					break;
				case SDLK_1: rescaleComponent = 0; break;
				case SDLK_2: rescaleComponent = 1; break;
				case SDLK_3: rescaleComponent = 2; break;
				case SDLK_R:
					*comp += (evt.key.mod & SDL_KMOD_SHIFT) ? -0.1f : 0.1f;
					if (*comp < 0.0f) *comp = 0.0f;
					if (*comp > 3.0f) *comp = 3.0f;
					SDL_Log("RescaleFactor[%d] = %.1f", rescaleComponent, *comp);
					FNA3D_SetEffectParamValue(device, effect, "RescaleFactor",
						rescale, 0, (uint32_t) sizeof(rescale));
					break;
				case SDLK_F:
					*comp += (evt.key.mod & SDL_KMOD_SHIFT) ? -0.1f : 0.1f;
					if (*comp < 0.0f) *comp = 0.0f;
					if (*comp > 3.0f) *comp = 3.0f;
					SDL_Log("RescaleFactor[%d] = %.1f", rescaleComponent, *comp);
					FNA3D_SetEffectParamValue(device, effect, "RescaleFactor",
						rescale, 0, (uint32_t) sizeof(rescale));
					break;
				default:
					break;
				}
			}
		}

		FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET, &clearColor, 1.0f, 0);
		FNA3D_ApplyEffect(device, effect, 0, NULL);
		FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
		FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 2);
		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	FNA3D_AddDisposeTexture(device, texV);
	FNA3D_AddDisposeTexture(device, texU);
	FNA3D_AddDisposeTexture(device, texY);
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
