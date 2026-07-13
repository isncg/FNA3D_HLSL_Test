#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <FNA3D.h>
#include <FNA3D_ImGui.h>   /* lifecycle: FNA3D_ImGui_*EXT */
#include "dcimgui.h"       /* widget API: ImGui_Begin / ImGui_SliderFloat / ... */

#include "common.h"
#include "math3d.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Geometry vertex: Position + Normal ---- */
typedef struct Vertex
{
	float x, y, z;
	float nx, ny, nz;
} Vertex;

/* ---- Fullscreen quad vertex: NDC Position + UV ---- */
typedef struct QuadVertex
{
	float x, y, z;
	float u, v;
} QuadVertex;

/* ---- Teapot loader (identical to teapot_pbr.c) ---- */
static int load_teapot(const char *path, Vertex **out_verts, int *out_count)
{
	FILE *fp;
	int triangle_count, i, total;
	Vertex *verts;
	Vec3 *positions;
	Vec3 *face_normals;

	fp = fopen(path, "r");
	if (fp == NULL) { SDL_Log("Failed to open %s", path); return -1; }
	if (fscanf(fp, "%d", &triangle_count) != 1)
	{ SDL_Log("Failed to read triangle count"); fclose(fp); return -1; }

	total = triangle_count * 3;
	verts = (Vertex *) SDL_malloc(sizeof(Vertex) * total);
	positions = (Vec3 *) SDL_malloc(sizeof(Vec3) * total);
	face_normals = (Vec3 *) SDL_malloc(sizeof(Vec3) * triangle_count);
	if (verts == NULL || positions == NULL || face_normals == NULL)
	{ SDL_Log("Out of memory"); SDL_free(verts); SDL_free(positions); SDL_free(face_normals); fclose(fp); return -1; }

	for (i = 0; i < triangle_count; i += 1)
	{
		Vec3 p[3]; int v;
		if (fscanf(fp, "%f %f %f", &p[0].x, &p[0].y, &p[0].z) != 3 ||
			fscanf(fp, "%f %f %f", &p[1].x, &p[1].y, &p[1].z) != 3 ||
			fscanf(fp, "%f %f %f", &p[2].x, &p[2].y, &p[2].z) != 3)
		{ SDL_Log("Failed to read triangle %d", i); SDL_free(verts); SDL_free(positions); SDL_free(face_normals); fclose(fp); return -1; }
		Vec3 e0 = vec3_sub(p[1], p[0]);
		Vec3 e1 = vec3_sub(p[2], p[0]);
		face_normals[i] = vec3_normalize(vec3_cross(e0, e1));
		for (v = 0; v < 3; v += 1)
		{ positions[i * 3 + v] = p[v]; verts[i * 3 + v].x = p[v].x; verts[i * 3 + v].y = p[v].y; verts[i * 3 + v].z = p[v].z; }
	}
	fclose(fp);

	for (i = 0; i < total; i += 1)
	{
		Vec3 sum = face_normals[i / 3]; int j;
		for (j = 0; j < total; j += 1)
		{ if (j / 3 == i / 3) continue; if (vec3_equal(positions[i], positions[j])) sum = vec3_add(sum, face_normals[j / 3]); }
		Vec3 avg = vec3_normalize(sum);
		verts[i].nx = avg.x; verts[i].ny = avg.y; verts[i].nz = avg.z;
	}
	SDL_free(positions); SDL_free(face_normals);
	*out_verts = verts; *out_count = total;
	return 0;
}

/* ---- Default pipeline state helpers ---- */
static void setup_default_rasterizer(FNA3D_Device *device)
{
	FNA3D_RasterizerState rs; memset(&rs, 0, sizeof(rs));
	rs.fillMode = FNA3D_FILLMODE_SOLID; rs.cullMode = FNA3D_CULLMODE_NONE;
	FNA3D_ApplyRasterizerState(device, &rs);
}
static void setup_default_blend(FNA3D_Device *device)
{
	FNA3D_BlendState bs; memset(&bs, 0, sizeof(bs));
	bs.colorSourceBlend = FNA3D_BLEND_ONE; bs.colorDestinationBlend = FNA3D_BLEND_ZERO;
	bs.colorBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	bs.alphaSourceBlend = FNA3D_BLEND_ONE; bs.alphaDestinationBlend = FNA3D_BLEND_ZERO;
	bs.alphaBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	bs.colorWriteEnable = FNA3D_COLORWRITECHANNELS_ALL;
	bs.colorWriteEnable1 = FNA3D_COLORWRITECHANNELS_ALL;
	bs.colorWriteEnable2 = FNA3D_COLORWRITECHANNELS_ALL;
	bs.colorWriteEnable3 = FNA3D_COLORWRITECHANNELS_ALL;
	bs.multiSampleMask = -1;
	FNA3D_SetBlendState(device, &bs);
}
static void setup_default_depth(FNA3D_Device *device)
{
	FNA3D_DepthStencilState ds; memset(&ds, 0, sizeof(ds));
	ds.depthBufferEnable = 1; ds.depthBufferWriteEnable = 1;
	ds.depthBufferFunction = FNA3D_COMPAREFUNCTION_LESSEQUAL;
	FNA3D_SetDepthStencilState(device, &ds);
}

int main(int argc, char *argv[])
{
	SDL_Window *window;
	FNA3D_Device *device;
	FNA3D_PresentationParameters pp;
	FNA3D_Effect *gbufferEffect, *ssaoEffect;
	FNA3D_EffectTechnique *technique;
	FNA3D_Buffer *vbTeapot, *vbFloor, *vbQuad;
	FNA3D_Texture *rtNormal, *rtDepth;
	FNA3D_Renderbuffer *rtDepthStencil;
	FNA3D_VertexElement geomElements[2], quadElements[2];
	FNA3D_VertexDeclaration declGeom, declQuad;
	FNA3D_VertexBufferBinding bindingGeom, bindingQuad;
	FNA3D_SamplerState sampClamp, sampPoint;
	FNA3D_Viewport viewport;
	uint8_t *effectBytes;
	uint32_t effectLen;
	Vertex *teapotVerts;
	int teapotVertCount;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clearColor = {0.15f, 0.15f, 0.18f, 1.0f};
	FNA3D_Vec4 blackColor = {0.0f, 0.0f, 0.0f, 1.0f};

	/* Camera state */
	Mat4 world, proj, view, viewproj, worldviewproj, worldviewproj_t, view_t;
	Vec3 target = {0.0f, 1.5f, 0.0f};
	float radius = 12.0f, azimuth = 0.0f, altitude = 0.35f;
	int camDragging = 0;

	/* SSAO parameters */
	float ssaoRadius = 0.5f;
	float ssaoBias = 0.025f;
	float ssaoIntensity = 1.0f;

	/* Backbuffer dimensions */
	int32_t fbWidth = 1024, fbHeight = 768;

	/* ---- Floor geometry (20x20 quad at y=-0.5, normal up) ---- */
	Vertex floorVerts[6] =
	{
		{-10.0f, -0.5f, -10.0f, 0.0f, 1.0f, 0.0f},
		{ 10.0f, -0.5f, -10.0f, 0.0f, 1.0f, 0.0f},
		{ 10.0f, -0.5f,  10.0f, 0.0f, 1.0f, 0.0f},
		{-10.0f, -0.5f, -10.0f, 0.0f, 1.0f, 0.0f},
		{ 10.0f, -0.5f,  10.0f, 0.0f, 1.0f, 0.0f},
		{-10.0f, -0.5f,  10.0f, 0.0f, 1.0f, 0.0f},
	};

	/* ---- Fullscreen quad (NDC [-1,1] with UV [0,1], V=1 at top) ---- */
	QuadVertex quadVerts[6] =
	{
		{-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},  /* top-left     */
		{-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},  /* bottom-left  */
		{ 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  /* bottom-right */
		{-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},  /* top-left     */
		{ 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  /* bottom-right */
		{ 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},  /* top-right    */
	};

	(void) argc; (void) argv;

	/* ---- Init SDL and device ---- */
	if (!SDL_Init(SDL_INIT_VIDEO))
	{ SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
	SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

	if (load_teapot("../assets/models/teapot_bezier0.tris", &teapotVerts, &teapotVertCount) != 0)
	{ SDL_Quit(); return 1; }

	memset(&pp, 0, sizeof(pp));
	pp.backBufferWidth = fbWidth; pp.backBufferHeight = fbHeight;
	pp.multiSampleCount = 0; pp.isFullScreen = 0;
	pp.depthStencilFormat = FNA3D_DEPTHFORMAT_D16;
	pp.presentationInterval = FNA3D_PRESENTINTERVAL_DEFAULT;
	pp.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
	pp.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

	window = SDL_CreateWindow("FNA3D_HLSL SSAO Test",
		fbWidth, fbHeight, FNA3D_PrepareWindowAttributes());
	if (window == NULL)
	{ SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError()); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	pp.deviceWindowHandle = window;

	device = FNA3D_CreateDevice(&pp, 1);
	if (device == NULL)
	{ SDL_Log("FNA3D_CreateDevice failed"); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	FNA3D_SetRenderTargets(device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_D16, 0);

	/* ---- Init Dear ImGui overlay (drawn automatically in SwapBuffers) ---- */
	FNA3D_ImGui_InitEXT(device);

	/* ---- Load GBuffer effect ---- */
	effectBytes = load_file("../assets/effects/gbuffer.feb", &effectLen);
	if (effectBytes == NULL)
	{ FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	if (!FNA3D_CreateEffect(device, effectBytes, effectLen, &gbufferEffect))
	{ SDL_Log("FNA3D_CreateEffect(gbuffer) failed"); SDL_free(effectBytes); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	SDL_free(effectBytes);
	if (FNA3D_GetEffectTechniqueCount(gbufferEffect) == 0)
	{ SDL_Log("GBuffer effect has no techniques"); FNA3D_AddDisposeEffect(device, gbufferEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	technique = FNA3D_GetEffectTechnique(gbufferEffect, 0);
	FNA3D_SetEffectTechnique(device, gbufferEffect, technique);

	/* ---- Load SSAO effect ---- */
	effectBytes = load_file("../assets/effects/ssao.feb", &effectLen);
	if (effectBytes == NULL)
	{ FNA3D_AddDisposeEffect(device, gbufferEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	if (!FNA3D_CreateEffect(device, effectBytes, effectLen, &ssaoEffect))
	{ SDL_Log("FNA3D_CreateEffect(ssao) failed"); SDL_free(effectBytes); FNA3D_AddDisposeEffect(device, gbufferEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	SDL_free(effectBytes);
	if (FNA3D_GetEffectTechniqueCount(ssaoEffect) == 0)
	{ SDL_Log("SSAO effect has no techniques"); FNA3D_AddDisposeEffect(device, ssaoEffect); FNA3D_AddDisposeEffect(device, gbufferEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	/* SSAO effect technique is set per-frame before pass 2 */

	/* ---- Create offscreen render targets ---- */
	rtNormal = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR,
		fbWidth, fbHeight, 1, 1);
	rtDepth  = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_SINGLE,
		fbWidth, fbHeight, 1, 1);
	rtDepthStencil = FNA3D_GenDepthStencilRenderbuffer(device,
		fbWidth, fbHeight, FNA3D_DEPTHFORMAT_D16, 0);

	/* ---- Create vertex buffers ---- */
	/* Teapot */
	vbTeapot = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t)(sizeof(Vertex) * teapotVertCount));
	FNA3D_SetVertexBufferData(device, vbTeapot, 0, teapotVerts, teapotVertCount,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);
	SDL_free(teapotVerts);

	/* Floor */
	vbFloor = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t) sizeof(floorVerts));
	FNA3D_SetVertexBufferData(device, vbFloor, 0, floorVerts, 6,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);

	/* Fullscreen quad */
	vbQuad = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t) sizeof(quadVerts));
	FNA3D_SetVertexBufferData(device, vbQuad, 0, quadVerts, 6,
		sizeof(QuadVertex), sizeof(QuadVertex), FNA3D_SETDATAOPTIONS_NONE);

	/* ---- Vertex declarations ---- */
	/* Geometry (teapot + floor): Position + Normal */
	geomElements[0].offset = 0;
	geomElements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	geomElements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	geomElements[0].usageIndex = 0;
	geomElements[1].offset = sizeof(float) * 3;
	geomElements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	geomElements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_NORMAL;
	geomElements[1].usageIndex = 0;

	declGeom.vertexStride = sizeof(Vertex);
	declGeom.elementCount = 2;
	declGeom.elements = geomElements;

	memset(&bindingGeom, 0, sizeof(bindingGeom));
	bindingGeom.vertexDeclaration = declGeom;
	bindingGeom.vertexOffset = 0;
	bindingGeom.instanceFrequency = 0;

	/* Fullscreen quad: Position + TexCoord */
	quadElements[0].offset = 0;
	quadElements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	quadElements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	quadElements[0].usageIndex = 0;
	quadElements[1].offset = sizeof(float) * 3;
	quadElements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	quadElements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
	quadElements[1].usageIndex = 0;

	declQuad.vertexStride = sizeof(QuadVertex);
	declQuad.elementCount = 2;
	declQuad.elements = quadElements;

	memset(&bindingQuad, 0, sizeof(bindingQuad));
	bindingQuad.vertexDeclaration = declQuad;
	bindingQuad.vertexOffset = 0;
	bindingQuad.instanceFrequency = 0;

	/* ---- Sampler states ---- */
	memset(&sampClamp, 0, sizeof(sampClamp));
	sampClamp.filter = FNA3D_TEXTUREFILTER_LINEAR;
	sampClamp.addressU = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	sampClamp.addressV = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	sampClamp.addressW = FNA3D_TEXTUREADDRESSMODE_CLAMP;

	memset(&sampPoint, 0, sizeof(sampPoint));
	sampPoint.filter = FNA3D_TEXTUREFILTER_POINT;
	sampPoint.addressU = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	sampPoint.addressV = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	sampPoint.addressW = FNA3D_TEXTUREADDRESSMODE_CLAMP;

	/* ---- Viewport ---- */
	viewport.x = 0; viewport.y = 0;
	viewport.w = fbWidth; viewport.h = fbHeight;
	viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
	FNA3D_SetViewport(device, &viewport);

	/* ---- Default pipeline states ---- */
	setup_default_rasterizer(device);
	setup_default_blend(device);
	setup_default_depth(device);

	/* ---- Camera ---- */
	mat4_identity(&world);
	mat4_perspective(&proj, MATH3D_PI / 4.0f,
		(float) fbWidth / (float) fbHeight, 0.1f, 100.0f);

	SDL_Log("SSAO Test — Left-drag: orbit camera | Scroll: zoom");
	SDL_Log("Adjust radius/bias/intensity via the ImGui panel (or Q/A/W/S keys)");
	SDL_Log("SSAO: radius=%.2f  bias=%.3f  intensity=%.2f",
		ssaoRadius, ssaoBias, ssaoIntensity);

	/* ---- Render loop ---- */
	while (running)
	{
		Vec3 eye; float cosAlt;

		/* ---- Input ---- */
		while (SDL_PollEvent(&evt))
		{
			switch (evt.type)
			{
			case SDL_EVENT_QUIT:
				running = 0; break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				if (evt.button.button == SDL_BUTTON_LEFT &&
					!ImGui_GetIO()->WantCaptureMouse) camDragging = 1;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
				if (evt.button.button == SDL_BUTTON_LEFT) camDragging = 0;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				if (camDragging)
				{ azimuth -= evt.motion.xrel * 0.005f; altitude += evt.motion.yrel * 0.005f;
				  altitude = clampf(altitude, -MATH3D_PI/2.0f+0.01f, MATH3D_PI/2.0f-0.01f); }
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				radius -= evt.wheel.y * 1.0f;
				if (radius < 3.0f) radius = 3.0f;
				if (radius > 40.0f) radius = 40.0f;
				break;
			case SDL_EVENT_KEY_DOWN:
				switch (evt.key.key)
				{
				case SDLK_Q: ssaoRadius += 0.1f; if (ssaoRadius > 3.0f) ssaoRadius = 3.0f;
					SDL_Log("SSAO: radius=%.2f  bias=%.3f  intensity=%.2f",
						ssaoRadius, ssaoBias, ssaoIntensity); break;
				case SDLK_A: ssaoRadius -= 0.1f; if (ssaoRadius < 0.1f) ssaoRadius = 0.1f;
					SDL_Log("SSAO: radius=%.2f  bias=%.3f  intensity=%.2f",
						ssaoRadius, ssaoBias, ssaoIntensity); break;
				case SDLK_W: ssaoIntensity += 0.1f; if (ssaoIntensity > 3.0f) ssaoIntensity = 3.0f;
					SDL_Log("SSAO: radius=%.2f  bias=%.3f  intensity=%.2f",
						ssaoRadius, ssaoBias, ssaoIntensity); break;
				case SDLK_S: ssaoIntensity -= 0.1f; if (ssaoIntensity < 0.1f) ssaoIntensity = 0.1f;
					SDL_Log("SSAO: radius=%.2f  bias=%.3f  intensity=%.2f",
						ssaoRadius, ssaoBias, ssaoIntensity); break;
				default: break;
				}
				break;
			}
		}

		/* ---- ImGui panel: live-tune SSAO parameters ---- */
		FNA3D_ImGui_NewFrameEXT(device);
		ImGui_Begin("SSAO Parameters", NULL, 0);
		ImGui_SliderFloat("Radius",    &ssaoRadius,    0.1f, 3.0f);
		ImGui_SliderFloat("Bias",      &ssaoBias,      0.0f, 0.1f);
		ImGui_SliderFloat("Intensity", &ssaoIntensity, 0.0f, 3.0f);
		ImGui_End();

		/* ---- Compute matrices ---- */
		cosAlt = cosf(altitude);
		eye.x = target.x + radius * cosf(azimuth) * cosAlt;
		eye.y = target.y + radius * sinf(altitude);
		eye.z = target.z + radius * sinf(azimuth) * cosAlt;
		mat4_lookat_lh(&view, eye, target, (Vec3){0.0f, 1.0f, 0.0f});
		mat4_mul(&viewproj, &view, &proj);

		/* Transpose matrices for HLSL column-major storage */
		mat4_transpose(&worldviewproj_t, &viewproj);
		mat4_transpose(&view_t, &view);

		/* ============================================================
		 * PASS 1: G-Buffer (render teapot + floor to offscreen RTs)
		 * ============================================================ */
		{
			FNA3D_RenderTargetBinding gbufferRTs[2];

			/* RT0: View-space normal (RGBA8) */
			memset(&gbufferRTs[0], 0, sizeof(gbufferRTs[0]));
			gbufferRTs[0].type = FNA3D_RENDERTARGET_TYPE_2D;
			gbufferRTs[0].twod.width = fbWidth;
			gbufferRTs[0].twod.height = fbHeight;
			gbufferRTs[0].levelCount = 1;
			gbufferRTs[0].multiSampleCount = 0;
			gbufferRTs[0].texture = rtNormal;
			gbufferRTs[0].colorBuffer = NULL;

			/* RT1: Linear depth (R32F) */
			memset(&gbufferRTs[1], 0, sizeof(gbufferRTs[1]));
			gbufferRTs[1].type = FNA3D_RENDERTARGET_TYPE_2D;
			gbufferRTs[1].twod.width = fbWidth;
			gbufferRTs[1].twod.height = fbHeight;
			gbufferRTs[1].levelCount = 1;
			gbufferRTs[1].multiSampleCount = 0;
			gbufferRTs[1].texture = rtDepth;
			gbufferRTs[1].colorBuffer = NULL;

			FNA3D_SetRenderTargets(device, gbufferRTs, 2,
				rtDepthStencil, FNA3D_DEPTHFORMAT_D16, 0);
			FNA3D_Clear(device,
				FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_DEPTHBUFFER,
				&clearColor, 1.0f, 0);

			/* Upload GBuffer uniforms */
			FNA3D_SetEffectParamValue(device, gbufferEffect, "ViewProj",
				&worldviewproj_t.m11, 0, (uint32_t) sizeof(Mat4));
			FNA3D_SetEffectParamValue(device, gbufferEffect, "View",
				&view_t.m11, 0, (uint32_t) sizeof(Mat4));

			/* Set technique and draw geometry */
			technique = FNA3D_GetEffectTechnique(gbufferEffect, 0);
			FNA3D_SetEffectTechnique(device, gbufferEffect, technique);
			FNA3D_ApplyEffect(device, gbufferEffect, 0, NULL);

			/* Draw teapot */
			bindingGeom.vertexBuffer = vbTeapot;
			FNA3D_ApplyVertexBufferBindings(device, &bindingGeom, 1, 1, 0);
			FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST,
				0, teapotVertCount / 3);

			/* Draw floor */
			bindingGeom.vertexBuffer = vbFloor;
			FNA3D_ApplyVertexBufferBindings(device, &bindingGeom, 1, 1, 0);
			FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST,
				0, 2);
		}

		/* ============================================================
		 * PASS 2: SSAO (fullscreen quad samples G-Buffer, outputs AO)
		 * ============================================================ */
		{
			float ssaoParams[4];
			float proj_t[16];

			/* Bind backbuffer (no depth needed for SSAO pass) */
			FNA3D_SetRenderTargets(device, NULL, 0, NULL,
				FNA3D_DEPTHFORMAT_NONE, 0);
			FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET,
				&blackColor, 1.0f, 0);

			/* Bind G-Buffer textures as samplers */
			FNA3D_VerifySampler(device, 0, rtNormal, &sampPoint);
			FNA3D_VerifySampler(device, 1, rtDepth, &sampClamp);

			/* Upload SSAO uniforms (transposed projection + params) */
			mat4_transpose((Mat4 *) proj_t, &proj);
			FNA3D_SetEffectParamValue(device, ssaoEffect, "Projection",
				proj_t, 0, (uint32_t) sizeof(Mat4));

			ssaoParams[0] = ssaoRadius;
			ssaoParams[1] = ssaoBias;
			ssaoParams[2] = ssaoIntensity;
			ssaoParams[3] = 0.0f;
			FNA3D_SetEffectParamValue(device, ssaoEffect, "SSAOParams",
				ssaoParams, 0, (uint32_t) sizeof(ssaoParams));

			/* Draw fullscreen quad */
			technique = FNA3D_GetEffectTechnique(ssaoEffect, 0);
			FNA3D_SetEffectTechnique(device, ssaoEffect, technique);
			FNA3D_ApplyEffect(device, ssaoEffect, 0, NULL);

			bindingQuad.vertexBuffer = vbQuad;
			FNA3D_ApplyVertexBufferBindings(device, &bindingQuad, 1, 1, 0);
			FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST,
				0, 2);
		}

		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	/* ---- Cleanup ---- */
	FNA3D_AddDisposeTexture(device, rtNormal);
	FNA3D_AddDisposeTexture(device, rtDepth);
	FNA3D_AddDisposeRenderbuffer(device, rtDepthStencil);
	FNA3D_AddDisposeVertexBuffer(device, vbTeapot);
	FNA3D_AddDisposeVertexBuffer(device, vbFloor);
	FNA3D_AddDisposeVertexBuffer(device, vbQuad);
	FNA3D_AddDisposeEffect(device, gbufferEffect);
	FNA3D_AddDisposeEffect(device, ssaoEffect);
	FNA3D_ImGui_ShutdownEXT(device);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
