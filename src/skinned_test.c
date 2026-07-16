// SkinnedEffect test — bending cylinder with GPU bone skinning.
// Converts FNA SkinnedEffect.fx to HLSL SM 6.0.
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

/* Skinned vertex with bone indices and weights */
typedef struct SkinnedVertex
{
	float x, y, z;
	float nx, ny, nz;
	float u, v;
	float indices[4];
	float weights[4];
} SkinnedVertex;

#define CYLINDER_RINGS  16
#define CYLINDER_SLICES 32
#define CYLINDER_HEIGHT 2.0f
#define CYLINDER_RADIUS 0.5f
#define MAX_VERTS ((CYLINDER_RINGS) * (CYLINDER_SLICES) * 6)

/* Generate a cylinder mesh with bone weights.
 * Y range [0, CYLINDER_HEIGHT].
 * Bone 0: root at Y=0, Bone 1: bends at Y=CYLINDER_HEIGHT*0.5 pivot.
 * Weight transitions linearly: w0 = 1 at Y=0, w0 = 0 at Y=CYLINDER_HEIGHT.
 */
static int generate_skinned_cylinder(SkinnedVertex *out)
{
	int vi = 0;
	for (int ring = 0; ring < CYLINDER_RINGS; ring++)
	{
		float y0 = CYLINDER_HEIGHT * (float) ring      / (float) CYLINDER_RINGS;
		float y1 = CYLINDER_HEIGHT * (float)(ring + 1) / (float) CYLINDER_RINGS;

		for (int slice = 0; slice < CYLINDER_SLICES; slice++)
		{
			float a0 = 2.0f * MATH3D_PI * (float) slice      / (float) CYLINDER_SLICES;
			float a1 = 2.0f * MATH3D_PI * (float)(slice + 1) / (float) CYLINDER_SLICES;

			float cos0 = SDL_cosf(a0), sin0 = SDL_sinf(a0);
			float cos1 = SDL_cosf(a1), sin1 = SDL_sinf(a1);

			float u0 = (float) slice      / (float) CYLINDER_SLICES;
			float u1 = (float)(slice + 1) / (float) CYLINDER_SLICES;
			float v0 = (float) ring      / (float) CYLINDER_RINGS;
			float v1 = (float)(ring + 1) / (float) CYLINDER_RINGS;

			/* Bone weights: bone0 dominates at bottom, bone1 at top */
			float w0_0 = 1.0f - y0 / CYLINDER_HEIGHT;
			float w1_0 = y0 / CYLINDER_HEIGHT;
			float w0_1 = 1.0f - y1 / CYLINDER_HEIGHT;
			float w1_1 = y1 / CYLINDER_HEIGHT;

			/* Quad: (0,0), (1,0), (1,1), (0,1) -> two triangles */
			SkinnedVertex verts[4] = {
				{cos0*CYLINDER_RADIUS, y0, sin0*CYLINDER_RADIUS, cos0,0,sin0, u0,v0, {0,1,0,0}, {w0_0,w1_0,0,0} },
				{cos1*CYLINDER_RADIUS, y0, sin1*CYLINDER_RADIUS, cos1,0,sin1, u1,v0, {0,1,0,0}, {w0_0,w1_0,0,0} },
				{cos1*CYLINDER_RADIUS, y1, sin1*CYLINDER_RADIUS, cos1,0,sin1, u1,v1, {0,1,0,0}, {w0_1,w1_1,0,0} },
				{cos0*CYLINDER_RADIUS, y1, sin0*CYLINDER_RADIUS, cos0,0,sin0, u0,v1, {0,1,0,0}, {w0_1,w1_1,0,0} },
			};

			int tris[6] = {0, 1, 2, 0, 2, 3};
			for (int t = 0; t < 6; t++)
				out[vi++] = verts[tris[t]];
		}
	}
	return vi;
}

/* Create grayscale checkerboard texture */
static uint8_t *create_checkerboard(int32_t w, int32_t h, int32_t sq)
{
	uint8_t *data = (uint8_t *) SDL_malloc(w * h * 4);
	if (!data) return NULL;
	for (int32_t y = 0; y < h; y++)
	{
		for (int32_t x = 0; x < w; x++)
		{
			int c = ((x / sq) + (y / sq)) & 1 ? 220 : 60;
			int idx = (y * w + x) * 4;
			data[idx+0] = c; data[idx+1] = c; data[idx+2] = c; data[idx+3] = 255;
		}
	}
	return data;
}

/* Build bone matrix: translate to pivot, rotate, translate back
 * pivotY = Y position of the joint
 * angle = rotation around X axis
 */
static void make_bone_matrix(Mat4 *out, float pivotY, float angle)
{
	mat4_identity(out);
	float c = SDL_cosf(angle), s = SDL_sinf(angle);

	/* T(pivot) * R(X, angle) * T(-pivot)
	 * Result column-major: each column is a row of the row-major Mat4...
	 * Actually, let me think in row-major (C convention).
	 * In row-major: out = T * R * T_inv
	 * T = translate(0, pivotY, 0)
	 * R = rotation around X
	 * T_inv = translate(0, -pivotY, 0)
	 */

	/* Build directly: the matrix transforms a point by first translating
	 * pivot to origin, rotating, then translating back.
	 * For a point (x, y, z):
	 *   y' = y - pivotY
	 *   rotate: y'' = y'*c - z*s, z'' = y'*s + z*c
	 *   y''' = y'' + pivotY
	 *
	 * In matrix form (row-major layout, point * matrix convention):
	 */
	out->m11 = 1.0f; out->m12 = 0.0f;  out->m13 = 0.0f;  out->m14 = 0.0f;
	out->m21 = 0.0f; out->m22 = c;      out->m23 = s;      out->m24 = 0.0f;
	out->m31 = 0.0f; out->m32 = -s;     out->m33 = c;      out->m34 = 0.0f;
	out->m41 = 0.0f;
	out->m42 = -pivotY * c + pivotY + 0.0f; /* pivotY * (1-c) */
	out->m43 = -pivotY * s;                 /* -pivotY * s    */
	out->m44 = 1.0f;
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
	FNA3D_Texture *texture;
	FNA3D_VertexElement elements[5];
	FNA3D_VertexDeclaration decl;
	FNA3D_VertexBufferBinding binding;
	uint8_t *effect_bytes, *pixels;
	uint32_t effect_len;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clearColor = {0.15f, 0.15f, 0.2f, 1.0f};
	FNA3D_EffectTechnique *technique;
	SkinnedVertex *cylVerts;
	int vertexCount;
	float time = 0.0f;

	(void) argc;
	(void) argv;

	cylVerts = (SkinnedVertex *) SDL_malloc(MAX_VERTS * sizeof(SkinnedVertex));
	if (!cylVerts) return 1;
	vertexCount = generate_skinned_cylinder(cylVerts);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{ SDL_Log("SDL_Init: %s", SDL_GetError()); SDL_free(cylVerts); return 1; }
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

	window = SDL_CreateWindow("SkinnedEffect — bending cylinder with bone skinning",
		pp.backBufferWidth, pp.backBufferHeight, FNA3D_PrepareWindowAttributes());
	if (!window) { SDL_Quit(); SDL_free(cylVerts); return 1; }
	pp.deviceWindowHandle = window;

	device = FNA3D_CreateDevice(&pp, 1);
	if (!device) { SDL_DestroyWindow(window); SDL_Quit(); SDL_free(cylVerts); return 1; }
	FNA3D_SetRenderTargets(device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_D16, 1.0f);

	effect_bytes = load_file("../assets/effects/skinned.feb", &effect_len);
	if (!effect_bytes) goto cleanup;
	if (!FNA3D_CreateEffect(device, effect_bytes, effect_len, &effect))
	{ SDL_Log("CreateEffect failed"); SDL_free(effect_bytes); goto cleanup; }
	SDL_free(effect_bytes);
	if (FNA3D_GetEffectTechniqueCount(effect) == 0)
	{ FNA3D_AddDisposeEffect(device, effect); goto cleanup; }
	technique = FNA3D_GetEffectTechnique(effect, 0);
	FNA3D_SetEffectTechnique(device, effect, technique);

	vb = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t)(vertexCount * sizeof(SkinnedVertex)));
	FNA3D_SetVertexBufferData(device, vb, 0, cylVerts, vertexCount,
		sizeof(SkinnedVertex), sizeof(SkinnedVertex), FNA3D_SETDATAOPTIONS_NONE);
	SDL_free(cylVerts);

	/* Vertex declaration: POS, NORMAL, TEXCOORD, BLENDINDICES, BLENDWEIGHT */
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

	elements[3].offset = sizeof(float) * 8;
	elements[3].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR4;
	elements[3].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES;
	elements[3].usageIndex = 0;

	elements[4].offset = sizeof(float) * 12;
	elements[4].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR4;
	elements[4].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT;
	elements[4].usageIndex = 0;

	decl.vertexStride = sizeof(SkinnedVertex);
	decl.elementCount = 5;
	decl.elements = elements;

	memset(&binding, 0, sizeof(binding));
	binding.vertexBuffer = vb;
	binding.vertexDeclaration = decl;
	binding.vertexOffset = 0;
	binding.instanceFrequency = 0;

	/* Checkerboard texture */
	pixels = create_checkerboard(256, 256, 16);
	if (pixels)
	{
		texture = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 256, 256, 1, 0);
		FNA3D_SetTextureData2D(device, texture, 0, 0, 256, 256, 0, pixels, 256*256*4);
		SDL_free(pixels);

		memset(&samplerState, 0, sizeof(samplerState));
		samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
		samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_WRAP;
		samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_WRAP;
		samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
		FNA3D_VerifySampler(device, 0, texture, &samplerState);
	}
	else
	{
		texture = NULL;
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
			if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_ESCAPE)
				running = 0;
		}

		time += 0.016f;

		/* Camera */
		Vec3 eye = { 0, 1.0f, 2.5f };
		Vec3 target = { 0, 1.0f, 0 };
		Vec3 up = { 0, 1, 0 };
		Mat4 view, proj, viewProj, world;
		mat4_lookat_lh(&view, eye, target, up);
		mat4_perspective(&proj, MATH3D_PI * 0.25f,
			(float)pp.backBufferWidth / (float)pp.backBufferHeight, 0.1f, 20.0f);
		mat4_mul(&viewProj, &view, &proj);

		/* World: center the cylinder, slight rotation for visibility */
		Mat4 rotY;
		mat4_identity(&rotY);
		float ry = time * 0.4f;
		rotY.m11 = SDL_cosf(ry);  rotY.m13 = SDL_sinf(ry);
		rotY.m31 = -SDL_sinf(ry); rotY.m33 = SDL_cosf(ry);

		/* Translate so cylinder base is at origin, bend pivot at Y=1 */
		Mat4 worldT;
		mat4_identity(&worldT);
		worldT.m42 = -0.5f; /* center cylinder vertically */
		mat4_mul(&world, &worldT, &rotY);

		/* Bone matrices */
		Mat4 bone0, bone1;
		mat4_identity(&bone0); /* root stays fixed */

		float bendAngle = SDL_sinf(time * 1.5f) * 0.7f;
		make_bone_matrix(&bone1, 1.0f, bendAngle); /* bend at Y=1.0 pivot */

		Mat4 wT, vpT, b0T, b1T;
		mat4_transpose(&wT, &world);
		mat4_transpose(&vpT, &viewProj);
		mat4_transpose(&b0T, &bone0);
		mat4_transpose(&b1T, &bone1);

		FNA3D_SetEffectParamValue(device, effect, "World", &wT.m11, 0, (uint32_t) sizeof(Mat4));
		FNA3D_SetEffectParamValue(device, effect, "ViewProj", &vpT.m11, 0, (uint32_t) sizeof(Mat4));
		FNA3D_SetEffectParamValue(device, effect, "Bone0", &b0T.m11, 0, (uint32_t) sizeof(Mat4));
		FNA3D_SetEffectParamValue(device, effect, "Bone1", &b1T.m11, 0, (uint32_t) sizeof(Mat4));

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

cleanup:
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 1;
}
