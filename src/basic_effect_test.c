// BasicEffect test — rotating cube with directional lighting, fog, texture.
// Converts FNA BasicEffect.fx to HLSL SM 6.0.
//
// Controls:
//   L      — cycle lighting mode (none → vertex → pixel)
//   F      — toggle fog
//   T      — toggle texture
//   ESC    — quit

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <FNA3D.h>

#include "common.h"
#include "math3d.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Vertex with position, normal, and texcoord */
typedef struct Vertex
{
	float x, y, z;
	float nx, ny, nz;
	float u, v;
} Vertex;

/* Generate a unit cube centered at origin. 36 vertices (6 faces × 2 triangles). */
static int generate_cube(Vertex *out)
{
	/* Face data: normal, tangent vectors for UV, then 4 corners in CCW order */
	struct {
		float nx, ny, nz;
		float ux, uy, uz; /* U direction */
		float vx, vy, vz; /* V direction */
	} faces[6] = {
		{ 0, 0, 1,  1,0,0,  0,1,0 },  /* front  (+Z) */
		{ 0, 0,-1, -1,0,0,  0,1,0 },  /* back   (-Z) */
		{ 1, 0, 0,  0,0,1,  0,1,0 },  /* right  (+X) */
		{-1, 0, 0,  0,0,-1, 0,1,0 },  /* left   (-X) */
		{ 0, 1, 0,  1,0,0,  0,0,-1},  /* top    (+Y) */
		{ 0,-1, 0,  1,0,0,  0,0, 1 },  /* bottom (-Y) */
	};

	int vi = 0;
	float h = 0.5f; /* half-size */
	for (int f = 0; f < 6; f++)
	{
		float nx = faces[f].nx, ny = faces[f].ny, nz = faces[f].nz;
		float ux = faces[f].ux, uy = faces[f].uy, uz = faces[f].uz;
		float vx = faces[f].vx, vy = faces[f].vy, vz = faces[f].vz;

		/* Center of face */
		float cx = nx * h, cy = ny * h, cz = nz * h;

		/* 4 corners: center ± u*half ± v*half, ordered CCW */
		float px[4], py[4], pz[4];
		for (int c = 0; c < 4; c++)
		{
			float su = (c == 0 || c == 3) ? -h : h;
			float sv = (c < 2) ? h : -h;
			px[c] = cx + ux * su + vx * sv;
			py[c] = cy + uy * su + vy * sv;
			pz[c] = cz + uz * su + vz * sv;
		}

		/* Two triangles: 0-1-2, 0-2-3 */
		int tris[6] = {0, 1, 2, 0, 2, 3};
		float uvs[4][2] = {{0,0}, {0,1}, {1,1}, {1,0}};

		for (int t = 0; t < 6; t++)
		{
			int c = tris[t];
			out[vi].x  = px[c]; out[vi].y  = py[c]; out[vi].z  = pz[c];
			out[vi].nx = nx;    out[vi].ny = ny;    out[vi].nz = nz;
			out[vi].u  = uvs[c][0]; out[vi].v  = uvs[c][1];
			vi++;
		}
	}
	return vi;
}

/* Create colored checkerboard texture */
static uint8_t *create_checkerboard(int32_t w, int32_t h, int32_t sq)
{
	uint8_t *data = (uint8_t *) SDL_malloc(w * h * 4);
	if (!data) return NULL;
	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < w; x++)
		{
			int c = ((x / sq) + (y / sq)) & 1;
			int idx = (y * w + x) * 4;
			data[idx+0] = c ? 220 : 60;
			data[idx+1] = c ? 220 : 60;
			data[idx+2] = c ? 220 : 60;
			data[idx+3] = 255;
		}
	}
	return data;
}

int main(int argc, char *argv[])
{
	SDL_Window *window;
	FNA3D_Device *device;
	FNA3D_PresentationParameters pp;
	FNA3D_RasterizerState rasterizer;
	FNA3D_BlendState blend;
	FNA3D_DepthStencilState depthStencil;
	FNA3D_SamplerState samplerState;
	FNA3D_Viewport viewport;
	FNA3D_Effect *effect;
	FNA3D_Buffer *vb;
	FNA3D_Texture *texture = NULL;
	FNA3D_VertexElement elements[3];
	FNA3D_VertexDeclaration decl;
	FNA3D_VertexBufferBinding binding;
	uint8_t *effect_bytes, *pixels;
	uint32_t effect_len;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clearColor = {0.15f, 0.15f, 0.2f, 1.0f};
	FNA3D_EffectTechnique *technique;
	Vertex cubeVerts[36];
	int vertexCount;
	float time = 0.0f;
	int lightingMode = 2; /* 0=none, 1=vertex, 2=pixel */
	int fogEnable = 0;
	int textureEnable = 1;

	(void) argc;
	(void) argv;

	/* Generate cube */
	vertexCount = generate_cube(cubeVerts);

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
	pp.depthStencilFormat = FNA3D_DEPTHFORMAT_D16;
	pp.presentationInterval = FNA3D_PRESENTINTERVAL_DEFAULT;
	pp.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
	pp.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

	window = SDL_CreateWindow(
		"BasicEffect — L:lighting F:fog T:texture ESC:quit",
		pp.backBufferWidth, pp.backBufferHeight,
		FNA3D_PrepareWindowAttributes());
	if (!window) { SDL_Log("Window failed"); SDL_Quit(); return 1; }
	pp.deviceWindowHandle = window;

	device = FNA3D_CreateDevice(&pp, 1);
	if (!device) { SDL_Log("Device failed"); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
	FNA3D_SetRenderTargets(device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_D16, 1.0f);

	/* Load effect */
	effect_bytes = load_file("../assets/effects/basic_effect.feb", &effect_len);
	if (!effect_bytes) { FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
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
		SDL_Log("No techniques");
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
		(int32_t) (vertexCount * sizeof(Vertex)));
	FNA3D_SetVertexBufferData(device, vb, 0, cubeVerts, vertexCount,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);

	/* Vertex declaration: POSITION, NORMAL, TEXCOORD */
	elements[0].offset = 0;
	elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	elements[0].usageIndex = 0;

	elements[1].offset = sizeof(float) * 3;
	elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_NORMAL;
	elements[1].usageIndex = 0;

	elements[2].offset = sizeof(float) * 6;
	elements[2].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	elements[2].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
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
	pixels = create_checkerboard(256, 256, 32);
	if (pixels)
	{
		texture = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
		FNA3D_SetTextureData2D(device, texture, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
		SDL_free(pixels);

		memset(&samplerState, 0, sizeof(samplerState));
		samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
		samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_WRAP;
		samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_WRAP;
		samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
		FNA3D_VerifySampler(device, 0, texture, &samplerState);
	}

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
	depthStencil.depthBufferEnable = 1;
	depthStencil.depthBufferWriteEnable = 1;
	depthStencil.depthBufferFunction = FNA3D_COMPAREFUNCTION_LESSEQUAL;
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
				case SDLK_ESCAPE: running = 0; break;
				case SDLK_L:
					lightingMode = (lightingMode + 1) % 3;
					SDL_Log("LightingMode: %d", lightingMode);
					break;
				case SDLK_F:
					fogEnable = !fogEnable;
					SDL_Log("Fog: %s", fogEnable ? "on" : "off");
					break;
				case SDLK_T:
					textureEnable = !textureEnable;
					SDL_Log("Texture: %s", textureEnable ? "on" : "off");
					break;
				default: break;
				}
			}
		}

		time += 0.016f;

		/* Camera orbit */
		float camDist = 4.0f;
		float camAngle = time * 0.5f;
		float camY = SDL_sinf(time * 0.3f) * 1.5f;
		Vec3 eye = { camDist * SDL_cosf(camAngle), camY, camDist * SDL_sinf(camAngle) };
		Vec3 target = { 0, 0, 0 };
		Vec3 up = { 0, 1, 0 };

		Mat4 view, proj, viewProj, world, worldViewProj;
		mat4_lookat_lh(&view, eye, target, up);
		mat4_perspective(&proj, MATH3D_PI * 0.25f,
			(float) pp.backBufferWidth / (float) pp.backBufferHeight, 0.1f, 20.0f);
		mat4_mul(&viewProj, &view, &proj);

		/* Rotating cube */
		Mat4 rotY, rotX;
		float angleY = time * 0.7f;
		float angleX = time * 0.3f;
		mat4_identity(&rotY);
		rotY.m11 = SDL_cosf(angleY);  rotY.m13 = SDL_sinf(angleY);
		rotY.m31 = -SDL_sinf(angleY); rotY.m33 = SDL_cosf(angleY);
		mat4_identity(&rotX);
		rotX.m22 = SDL_cosf(angleX);  rotX.m23 = -SDL_sinf(angleX);
		rotX.m32 = SDL_sinf(angleX);  rotX.m33 = SDL_cosf(angleX);
		mat4_mul(&world, &rotX, &rotY);
		mat4_mul(&worldViewProj, &world, &viewProj);

			/* Transpose matrices for HLSL (column-major) */
			Mat4 worldT, viewProjT;
			mat4_transpose(&worldT, &world);
			mat4_transpose(&viewProjT, &viewProj);

			FNA3D_SetEffectParamValue(device, effect, "World",
				&worldT.m11, 0, (uint32_t) sizeof(Mat4));
			FNA3D_SetEffectParamValue(device, effect, "ViewProj",
				&viewProjT.m11, 0, (uint32_t) sizeof(Mat4));

		/* Eye position */
		float eyePos[4] = { eye.x, eye.y, eye.z, 0 };
		FNA3D_SetEffectParamValue(device, effect, "EyePosition",
			eyePos, 0, (uint32_t) sizeof(eyePos));

		/* Lights — rotate light 0 with the cube for dynamic effect */
		float lightDir0[4] = { SDL_cosf(time), -1.0f, SDL_sinf(time), 0 };
		FNA3D_SetEffectParamValue(device, effect, "LightDir0",
			lightDir0, 0, (uint32_t) sizeof(lightDir0));

		/* Modes */
		float modes[4] = { (float) lightingMode, (float) fogEnable, (float) textureEnable, 0 };
		FNA3D_SetEffectParamValue(device, effect, "Modes",
			modes, 0, (uint32_t) sizeof(modes));

		FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_DEPTHBUFFER,
			&clearColor, 1.0f, 0);
		FNA3D_ApplyEffect(device, effect, 0, NULL);
		FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
		FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, vertexCount / 3);
		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	if (texture) FNA3D_AddDisposeTexture(device, texture);
	FNA3D_AddDisposeVertexBuffer(device, vb);
	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
