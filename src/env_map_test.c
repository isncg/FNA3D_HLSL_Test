// EnvironmentMapEffect test — sphere with cubemap reflection + Fresnel.
// Converts FNA EnvironmentMapEffect.fx to HLSL SM 6.0.
//
// Controls:
//   F      — toggle Fresnel
//   +/-    — adjust EnvironmentMapAmount
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

typedef struct Vertex
{
	float x, y, z;
	float nx, ny, nz;
	float u, v;
} Vertex;

/* Generate a UV sphere with latitude/longitude rings.
 * Returns vertex count. out must have room for (slices+1)*(stacks+1)*6 vertices. */
static int generate_sphere(Vertex *out, int slices, int stacks, float radius)
{
	int vi = 0;
	for (int j = 0; j < stacks; j++)
	{
		float phi0 = MATH3D_PI * (float) j      / (float) stacks;
		float phi1 = MATH3D_PI * (float)(j + 1) / (float) stacks;

		for (int i = 0; i < slices; i++)
		{
			float theta0 = 2.0f * MATH3D_PI * (float) i      / (float) slices;
			float theta1 = 2.0f * MATH3D_PI * (float)(i + 1) / (float) slices;

			/* Four corners of the quad */
			float x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
			float u0, v0, u1, v1, u2, v2, u3, v3;

			/* Spherical to Cartesian */
			float sinP0 = SDL_sinf(phi0), cosP0 = SDL_cosf(phi0);
			float sinP1 = SDL_sinf(phi1), cosP1 = SDL_cosf(phi1);
			float sinT0 = SDL_sinf(theta0), cosT0 = SDL_cosf(theta0);
			float sinT1 = SDL_sinf(theta1), cosT1 = SDL_cosf(theta1);

			x0 = radius * sinP0 * cosT0;
			y0 = radius * cosP0;
			z0 = radius * sinP0 * sinT0;
			u0 = (float) i      / (float) slices;
			v0 = (float) j      / (float) stacks;

			x1 = radius * sinP0 * cosT1;
			y1 = radius * cosP0;
			z1 = radius * sinP0 * sinT1;
			u1 = (float)(i + 1) / (float) slices;
			v1 = (float) j      / (float) stacks;

			x2 = radius * sinP1 * cosT1;
			y2 = radius * cosP1;
			z2 = radius * sinP1 * sinT1;
			u2 = (float)(i + 1) / (float) slices;
			v2 = (float)(j + 1) / (float) stacks;

			x3 = radius * sinP1 * cosT0;
			y3 = radius * cosP1;
			z3 = radius * sinP1 * sinT0;
			u3 = (float) i      / (float) slices;
			v3 = (float)(j + 1) / (float) stacks;

			/* Triangle 1: 0-1-2 */
			out[vi].x = x0; out[vi].y = y0; out[vi].z = z0;
			out[vi].nx = x0/radius; out[vi].ny = y0/radius; out[vi].nz = z0/radius;
			out[vi].u = u0; out[vi].v = v0; vi++;

			out[vi].x = x1; out[vi].y = y1; out[vi].z = z1;
			out[vi].nx = x1/radius; out[vi].ny = y1/radius; out[vi].nz = z1/radius;
			out[vi].u = u1; out[vi].v = v1; vi++;

			out[vi].x = x2; out[vi].y = y2; out[vi].z = z2;
			out[vi].nx = x2/radius; out[vi].ny = y2/radius; out[vi].nz = z2/radius;
			out[vi].u = u2; out[vi].v = v2; vi++;

			/* Triangle 2: 0-2-3 */
			out[vi].x = x0; out[vi].y = y0; out[vi].z = z0;
			out[vi].nx = x0/radius; out[vi].ny = y0/radius; out[vi].nz = z0/radius;
			out[vi].u = u0; out[vi].v = v0; vi++;

			out[vi].x = x2; out[vi].y = y2; out[vi].z = z2;
			out[vi].nx = x2/radius; out[vi].ny = y2/radius; out[vi].nz = z2/radius;
			out[vi].u = u2; out[vi].v = v2; vi++;

			out[vi].x = x3; out[vi].y = y3; out[vi].z = z3;
			out[vi].nx = x3/radius; out[vi].ny = y3/radius; out[vi].nz = z3/radius;
			out[vi].u = u3; out[vi].v = v3; vi++;
		}
	}
	return vi;
}

/* Create a colored face texture with grid lines for the cubemap */
static uint8_t *create_cubemap_face(int32_t size, uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t *data = (uint8_t *) SDL_malloc(size * size * 4);
	if (!data) return NULL;
	for (int32_t y = 0; y < size; y++)
	{
		for (int32_t x = 0; x < size; x++)
		{
			int32_t idx = (y * size + x) * 4;
			/* Grid lines every 8 pixels */
			int grid = ((x / 8) + (y / 8)) & 1;
			uint8_t dark = 40;
			data[idx+0] = grid ? r : dark;
			data[idx+1] = grid ? g : dark;
			data[idx+2] = grid ? b : dark;
			data[idx+3] = 255;
		}
	}
	return data;
}

/* Create checkerboard for base texture */
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
			data[idx+0] = c ? 240 : 80;
			data[idx+1] = c ? 240 : 80;
			data[idx+2] = c ? 240 : 80;
			data[idx+3] = 255;
		}
	}
	return data;
}

#define SPHERE_SLICES 32
#define SPHERE_STACKS 16
#define CUBEMAP_SIZE 64
#define MAX_VERTS ((SPHERE_SLICES)*(SPHERE_STACKS)*6)

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
	FNA3D_Texture *baseTex, *cubeTex;
	FNA3D_VertexElement elements[3];
	FNA3D_VertexDeclaration decl;
	FNA3D_VertexBufferBinding binding;
	uint8_t *effect_bytes, *pixels;
	uint32_t effect_len;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clearColor = {0.05f, 0.05f, 0.1f, 1.0f};
	FNA3D_EffectTechnique *technique;
	Vertex *sphereVerts;
	int vertexCount;
	float time = 0.0f;
	float envAmount = 1.0f;
	float fresnelFactor = 1.0f;
	int fresnelEnabled = 1;

	(void) argc;
	(void) argv;

	/* Generate sphere */
	sphereVerts = (Vertex *) SDL_malloc(MAX_VERTS * sizeof(Vertex));
	if (!sphereVerts) return 1;
	vertexCount = generate_sphere(sphereVerts, SPHERE_SLICES, SPHERE_STACKS, 1.0f);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		SDL_free(sphereVerts);
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
		"EnvMapEffect — F:Fresnel +/-:Amount ESC:quit",
		pp.backBufferWidth, pp.backBufferHeight,
		FNA3D_PrepareWindowAttributes());
	if (!window) { SDL_Log("Window failed"); SDL_Quit(); SDL_free(sphereVerts); return 1; }
	pp.deviceWindowHandle = window;

	device = FNA3D_CreateDevice(&pp, 1);
	if (!device) { SDL_Log("Device failed"); SDL_DestroyWindow(window); SDL_Quit(); SDL_free(sphereVerts); return 1; }
	FNA3D_SetRenderTargets(device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_D16, 1.0f);

	/* Load effect */
	effect_bytes = load_file("../assets/effects/env_map.feb", &effect_len);
	if (!effect_bytes) goto cleanup;
	if (!FNA3D_CreateEffect(device, effect_bytes, effect_len, &effect))
	{ SDL_Log("CreateEffect failed"); SDL_free(effect_bytes); goto cleanup; }
	SDL_free(effect_bytes);
	if (FNA3D_GetEffectTechniqueCount(effect) == 0)
	{ SDL_Log("No techniques"); FNA3D_AddDisposeEffect(device, effect); goto cleanup; }
	technique = FNA3D_GetEffectTechnique(effect, 0);
	FNA3D_SetEffectTechnique(device, effect, technique);

	/* Vertex buffer */
	vb = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t) (vertexCount * sizeof(Vertex)));
	FNA3D_SetVertexBufferData(device, vb, 0, sphereVerts, vertexCount,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);
	SDL_free(sphereVerts);

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

	/* Base checkerboard texture (slot 0) */
	pixels = create_checkerboard(256, 256, 16);
	if (!pixels) { FNA3D_AddDisposeVertexBuffer(device, vb); FNA3D_AddDisposeEffect(device, effect); goto cleanup; }
	baseTex = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
	FNA3D_SetTextureData2D(device, baseTex, 0, 0, 256, 256, 0, pixels, 256 * 256 * 4);
	SDL_free(pixels);

	memset(&samplerState, 0, sizeof(samplerState));
	samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
	samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_WRAP;
	samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_WRAP;
	samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
	FNA3D_VerifySampler(device, 0, baseTex, &samplerState);

	/* Cubemap texture (slot 1) */
	cubeTex = FNA3D_CreateTextureCube(device, FNA3D_SURFACEFORMAT_COLOR, CUBEMAP_SIZE, 1, 0);

	/* Six faces with distinct colors */
	struct { uint8_t r, g, b; FNA3D_CubeMapFace face; } faces[6] = {
		{255, 60,  60,  FNA3D_CUBEMAPFACE_POSITIVEX}, /* +X red */
		{60,  255, 60,  FNA3D_CUBEMAPFACE_NEGATIVEX}, /* -X green */
		{60,  60,  255, FNA3D_CUBEMAPFACE_POSITIVEY}, /* +Y blue */
		{255, 255, 60,  FNA3D_CUBEMAPFACE_NEGATIVEY}, /* -Y yellow */
		{255, 60,  255, FNA3D_CUBEMAPFACE_POSITIVEZ}, /* +Z magenta */
		{60,  255, 255, FNA3D_CUBEMAPFACE_NEGATIVEZ}, /* -Z cyan */
	};

	for (int f = 0; f < 6; f++)
	{
		pixels = create_cubemap_face(CUBEMAP_SIZE, faces[f].r, faces[f].g, faces[f].b);
		if (pixels)
		{
			FNA3D_SetTextureDataCube(device, cubeTex, 0, 0, CUBEMAP_SIZE, CUBEMAP_SIZE,
				faces[f].face, 0, pixels, CUBEMAP_SIZE * CUBEMAP_SIZE * 4);
			SDL_free(pixels);
		}
	}

	/* Sampler for cubemap */
	FNA3D_SamplerState cubeSampler;
	memset(&cubeSampler, 0, sizeof(cubeSampler));
	cubeSampler.filter = FNA3D_TEXTUREFILTER_LINEAR;
	cubeSampler.addressU = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	cubeSampler.addressV = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	cubeSampler.addressW = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	FNA3D_VerifySampler(device, 1, cubeTex, &cubeSampler);

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
			if (evt.type == SDL_EVENT_QUIT) running = 0;
			if (evt.type == SDL_EVENT_KEY_DOWN)
			{
				switch (evt.key.key)
				{
				case SDLK_ESCAPE: running = 0; break;
				case SDLK_F:
					fresnelEnabled = !fresnelEnabled;
					fresnelFactor = fresnelEnabled ? 1.0f : 0.01f;
					envAmount = fresnelEnabled ? 1.0f : 0.0f;
					SDL_Log("Fresnel: %s", fresnelEnabled ? "on" : "off");
					break;
				case SDLK_EQUALS: case SDLK_PLUS:
					envAmount += 0.1f; if (envAmount > 3.0f) envAmount = 3.0f;
					SDL_Log("EnvAmount: %.1f", envAmount);
					break;
				case SDLK_MINUS:
					envAmount -= 0.1f; if (envAmount < 0.0f) envAmount = 0.0f;
					SDL_Log("EnvAmount: %.1f", envAmount);
					break;
				default: break;
				}
			}
		}

		time += 0.016f;

		/* Camera orbit */
		float camDist = 3.5f;
		Vec3 eye = { camDist * SDL_cosf(time * 0.4f), 1.0f, camDist * SDL_sinf(time * 0.4f) };
		Vec3 target = { 0, 0, 0 };
		Vec3 up = { 0, 1, 0 };

		Mat4 view, proj, viewProj, world, worldViewProj;
		mat4_lookat_lh(&view, eye, target, up);
		mat4_perspective(&proj, MATH3D_PI * 0.25f,
			(float) pp.backBufferWidth / (float) pp.backBufferHeight, 0.1f, 20.0f);
		mat4_mul(&viewProj, &view, &proj);

		/* Sphere rotates slowly */
		Mat4 rotY;
		mat4_identity(&rotY);
		float angle = time * 0.3f;
		rotY.m11 = SDL_cosf(angle);  rotY.m13 = SDL_sinf(angle);
		rotY.m31 = -SDL_sinf(angle); rotY.m33 = SDL_cosf(angle);
		mat4_mul(&worldViewProj, &rotY, &viewProj);

		Mat4 worldT, viewProjT;
		mat4_transpose(&worldT, &rotY);
		mat4_transpose(&viewProjT, &viewProj);

		FNA3D_SetEffectParamValue(device, effect, "World",
			&worldT.m11, 0, (uint32_t) sizeof(Mat4));
		FNA3D_SetEffectParamValue(device, effect, "ViewProj",
			&viewProjT.m11, 0, (uint32_t) sizeof(Mat4));

		float eyePos[4] = { eye.x, eye.y, eye.z, 0 };
		FNA3D_SetEffectParamValue(device, effect, "EyePosition",
			eyePos, 0, (uint32_t) sizeof(eyePos));

		FNA3D_SetEffectParamValue(device, effect, "EnvironmentMapAmount",
			&envAmount, 0, (uint32_t) sizeof(envAmount));
		FNA3D_SetEffectParamValue(device, effect, "FresnelFactor",
			&fresnelFactor, 0, (uint32_t) sizeof(fresnelFactor));

		FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_DEPTHBUFFER,
			&clearColor, 1.0f, 0);
		FNA3D_ApplyEffect(device, effect, 0, NULL);
		FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
		FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, vertexCount / 3);
		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	FNA3D_AddDisposeTexture(device, cubeTex);
	FNA3D_AddDisposeTexture(device, baseTex);
	FNA3D_AddDisposeVertexBuffer(device, vb);
	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;

cleanup:
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 1;
}
