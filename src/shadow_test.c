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

#define SHADOW_MAP_SIZE 1024

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

/* ---- Teapot loader (identical to ssao_test.c) ---- */
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
	FNA3D_Effect *depthEffect, *sceneEffect, *vizEffect;
	FNA3D_EffectTechnique *technique;
	FNA3D_Buffer *vbTeapot, *vbFloor, *vbQuad;
	FNA3D_Texture *rtShadow, *rtShadowViz;
	FNA3D_Renderbuffer *rbShadowDepth;
	FNA3D_VertexElement geomElements[2], quadElements[2];
	FNA3D_VertexDeclaration declGeom, declQuad;
	FNA3D_VertexBufferBinding bindingGeom, bindingQuad;
	FNA3D_SamplerState sampPoint;
	FNA3D_Viewport viewport;
	uint8_t *effectBytes;
	uint32_t effectLen;
	Vertex *teapotVerts;
	int teapotVertCount;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clearColor = {0.15f, 0.15f, 0.18f, 1.0f};
	FNA3D_Vec4 whiteColor = {1.0f, 1.0f, 1.0f, 1.0f};
	FNA3D_Vec4 blackColor = {0.0f, 0.0f, 0.0f, 1.0f};

	/* Camera state */
	Mat4 proj, view, viewproj, viewproj_t;
	Mat4 lightView, lightProj, lightViewProj, lightViewProj_t;
	Vec3 target = {0.0f, 1.5f, 0.0f};
	float radius = 12.0f, azimuth = 0.0f, altitude = 0.35f;
	int camDragging = 0;

	/* Directional light state (right-drag to rotate) */
	float lightAzimuth = -0.8f, lightAltitude = 0.6f;
	float lightDist = 20.0f;
	int lightDragging = 0;

	/* Shadow parameters (live-tuned via ImGui) */
	float depthBias = 0.002f;
	float pcfRadius = 1.0f;
	float shadowStrength = 0.85f;
	float orthoSize = 25.0f;

	/* Backbuffer dimensions */
	int32_t fbWidth = 1024, fbHeight = 768;

	/* ---- Floor geometry (20x20 quad at y=0, normal up) ---- */
	Vertex floorVerts[6] =
	{
		{-10.0f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f},
		{ 10.0f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f},
		{ 10.0f, 0.0f,  10.0f, 0.0f, 1.0f, 0.0f},
		{-10.0f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f},
		{ 10.0f, 0.0f,  10.0f, 0.0f, 1.0f, 0.0f},
		{-10.0f, 0.0f,  10.0f, 0.0f, 1.0f, 0.0f},
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

	window = SDL_CreateWindow("FNA3D_HLSL Shadow Mapping Test",
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

	/* ---- Load shadow depth effect ---- */
	effectBytes = load_file("../assets/effects/shadow_depth.feb", &effectLen);
	if (effectBytes == NULL)
	{ FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	if (!FNA3D_CreateEffect(device, effectBytes, effectLen, &depthEffect))
	{ SDL_Log("FNA3D_CreateEffect(shadow_depth) failed"); SDL_free(effectBytes); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	SDL_free(effectBytes);
	if (FNA3D_GetEffectTechniqueCount(depthEffect) == 0)
	{ SDL_Log("shadow_depth effect has no techniques"); FNA3D_AddDisposeEffect(device, depthEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }

	/* ---- Load shadow scene effect ---- */
	effectBytes = load_file("../assets/effects/shadow_scene.feb", &effectLen);
	if (effectBytes == NULL)
	{ FNA3D_AddDisposeEffect(device, depthEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	if (!FNA3D_CreateEffect(device, effectBytes, effectLen, &sceneEffect))
	{ SDL_Log("FNA3D_CreateEffect(shadow_scene) failed"); SDL_free(effectBytes); FNA3D_AddDisposeEffect(device, depthEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	SDL_free(effectBytes);
	if (FNA3D_GetEffectTechniqueCount(sceneEffect) == 0)
	{ SDL_Log("shadow_scene effect has no techniques"); FNA3D_AddDisposeEffect(device, sceneEffect); FNA3D_AddDisposeEffect(device, depthEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }

	/* ---- Load shadow viz effect ---- */
	effectBytes = load_file("../assets/effects/shadow_viz.feb", &effectLen);
	if (effectBytes == NULL)
	{ FNA3D_AddDisposeEffect(device, sceneEffect); FNA3D_AddDisposeEffect(device, depthEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	if (!FNA3D_CreateEffect(device, effectBytes, effectLen, &vizEffect))
	{ SDL_Log("FNA3D_CreateEffect(shadow_viz) failed"); SDL_free(effectBytes); FNA3D_AddDisposeEffect(device, sceneEffect); FNA3D_AddDisposeEffect(device, depthEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }
	SDL_free(effectBytes);
	if (FNA3D_GetEffectTechniqueCount(vizEffect) == 0)
	{ SDL_Log("shadow_viz effect has no techniques"); FNA3D_AddDisposeEffect(device, vizEffect); FNA3D_AddDisposeEffect(device, sceneEffect); FNA3D_AddDisposeEffect(device, depthEffect); FNA3D_DestroyDevice(device); SDL_DestroyWindow(window); SDL_free(teapotVerts); SDL_Quit(); return 1; }

	/* ---- Create shadow map render targets ----
	 * Depth renderbuffers cannot be sampled (no SAMPLER usage in the SDL_GPU
	 * driver), so the shadow map is an R32F color RT written by the pixel
	 * shader; the D16 renderbuffer only provides depth testing for pass 1.
	 */
	rtShadow = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_SINGLE,
		SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1, 1);
	rtShadowViz = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR,
		SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1, 1);
	rbShadowDepth = FNA3D_GenDepthStencilRenderbuffer(device,
		SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, FNA3D_DEPTHFORMAT_D16, 0);

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

	/* ---- Sampler state ----
	 * POINT + CLAMP for every shadow map read: linear filtering of R32F is
	 * optional in Vulkan, and averaging depths before the compare is wrong
	 * anyway (PCF averages compare *results*).
	 */
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
	mat4_perspective(&proj, MATH3D_PI / 4.0f,
		(float) fbWidth / (float) fbHeight, 0.1f, 100.0f);

	SDL_Log("Shadow Mapping Test — Left-drag: orbit camera | Right-drag: rotate light | Scroll: zoom");
	SDL_Log("Adjust bias/PCF/strength/ortho size via the ImGui panel");

	/* ---- Render loop ---- */
	while (running)
	{
		Vec3 eye; float cosAlt;
		Vec3 dirToLight, lightEye;
		float lightZFar;

		/* ---- Input ---- */
		while (SDL_PollEvent(&evt))
		{
			switch (evt.type)
			{
			case SDL_EVENT_QUIT:
				running = 0; break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				if (ImGui_GetIO()->WantCaptureMouse) break;
				if (evt.button.button == SDL_BUTTON_LEFT) camDragging = 1;
				else if (evt.button.button == SDL_BUTTON_RIGHT) lightDragging = 1;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
				if (evt.button.button == SDL_BUTTON_LEFT) camDragging = 0;
				else if (evt.button.button == SDL_BUTTON_RIGHT) lightDragging = 0;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				if (camDragging)
				{ azimuth -= evt.motion.xrel * 0.005f; altitude += evt.motion.yrel * 0.005f;
				  altitude = clampf(altitude, -MATH3D_PI/2.0f+0.01f, MATH3D_PI/2.0f-0.01f); }
				if (lightDragging)
				{ lightAzimuth -= evt.motion.xrel * 0.005f; lightAltitude += evt.motion.yrel * 0.005f;
				  /* Keep the light above the floor and the lookat well-conditioned */
				  lightAltitude = clampf(lightAltitude, 0.05f, MATH3D_PI/2.0f-0.01f); }
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				radius -= evt.wheel.y * 1.0f;
				if (radius < 3.0f) radius = 3.0f;
				if (radius > 40.0f) radius = 40.0f;
				break;
			}
		}

		/* ---- ImGui panel: shadow parameters + light depth buffer ---- */
		FNA3D_ImGui_NewFrameEXT(device);
		ImGui_Begin("Shadow Mapping", NULL, 0);
		ImGui_SliderFloat("Depth Bias",      &depthBias,      0.0f, 0.02f);
		ImGui_SliderFloat("PCF Radius",      &pcfRadius,      0.0f, 4.0f);
		ImGui_SliderFloat("Shadow Strength", &shadowStrength, 0.0f, 1.0f);
		ImGui_SliderFloat("Ortho Size",      &orthoSize,      5.0f, 40.0f);
		ImGui_SliderFloat("Light Distance",  &lightDist,      5.0f, 60.0f);
		ImGui_Text("Light: az=%.2f alt=%.2f (right-drag to rotate)",
			lightAzimuth, lightAltitude);
		ImGui_Text("Shadow map %dx%d (light depth):",
			SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
		{
			/* FNA3D_Texture* is SDLGPU_TextureHandle*; its first member is
			 * the SDL_GPUTexture* (FNA3D_Driver_SDL.c "Cast from
			 * FNA3D_Texture*"). imgui_impl_sdlgpu3 uses SDL_GPUTexture* as
			 * ImTextureID, so the grayscale viz RT can be shown directly. */
			ImTextureRef texRef;
			memset(&texRef, 0, sizeof(texRef));
			texRef._TexID = (ImTextureID)(uintptr_t)(*(void **) rtShadowViz);
			ImGui_Image(texRef, (ImVec2){256.0f, 256.0f});
		}
		ImGui_End();

		/* ---- Camera matrices ---- */
		cosAlt = cosf(altitude);
		eye.x = target.x + radius * cosf(azimuth) * cosAlt;
		eye.y = target.y + radius * sinf(altitude);
		eye.z = target.z + radius * sinf(azimuth) * cosAlt;
		mat4_lookat_lh(&view, eye, target, (Vec3){0.0f, 1.0f, 0.0f});
		mat4_mul(&viewproj, &view, &proj);
		mat4_transpose(&viewproj_t, &viewproj);

		/* ---- Light matrices (directional, orthographic) ---- */
		cosAlt = cosf(lightAltitude);
		dirToLight.x = cosAlt * cosf(lightAzimuth);
		dirToLight.y = sinf(lightAltitude);
		dirToLight.z = cosAlt * sinf(lightAzimuth);
		lightEye.x = target.x + dirToLight.x * lightDist;
		lightEye.y = target.y + dirToLight.y * lightDist;
		lightEye.z = target.z + dirToLight.z * lightDist;
		lightZFar = lightDist + 40.0f;
		mat4_lookat_lh(&lightView, lightEye, target, (Vec3){0.0f, 1.0f, 0.0f});
		mat4_ortho_lh(&lightProj, orthoSize, orthoSize, 1.0f, lightZFar);
		mat4_mul(&lightViewProj, &lightView, &lightProj);
		mat4_transpose(&lightViewProj_t, &lightViewProj);

		/* ============================================================
		 * PASS 1: Shadow depth (scene from the light's view -> R32F)
		 * ============================================================ */
		{
			FNA3D_RenderTargetBinding shadowRT;

			memset(&shadowRT, 0, sizeof(shadowRT));
			shadowRT.type = FNA3D_RENDERTARGET_TYPE_2D;
			shadowRT.twod.width = SHADOW_MAP_SIZE;
			shadowRT.twod.height = SHADOW_MAP_SIZE;
			shadowRT.levelCount = 1;
			shadowRT.multiSampleCount = 0;
			shadowRT.texture = rtShadow;
			shadowRT.colorBuffer = NULL;

			FNA3D_SetRenderTargets(device, &shadowRT, 1,
				rbShadowDepth, FNA3D_DEPTHFORMAT_D16, 0);

			/* SetRenderTargets does not touch the viewport — set it. */
			viewport.w = SHADOW_MAP_SIZE; viewport.h = SHADOW_MAP_SIZE;
			FNA3D_SetViewport(device, &viewport);

			/* Clear to 1.0 = far plane, so empty texels never shadow */
			FNA3D_Clear(device,
				FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_DEPTHBUFFER,
				&whiteColor, 1.0f, 0);

			FNA3D_SetEffectParamValue(device, depthEffect, "LightViewProj",
				&lightViewProj_t.m11, 0, (uint32_t) sizeof(Mat4));

			technique = FNA3D_GetEffectTechnique(depthEffect, 0);
			FNA3D_SetEffectTechnique(device, depthEffect, technique);
			FNA3D_ApplyEffect(device, depthEffect, 0, NULL);

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
		 * PASS 2: Depth viz (R32F -> grayscale RGBA8 for ImGui_Image)
		 * ============================================================ */
		{
			FNA3D_RenderTargetBinding vizRT;

			memset(&vizRT, 0, sizeof(vizRT));
			vizRT.type = FNA3D_RENDERTARGET_TYPE_2D;
			vizRT.twod.width = SHADOW_MAP_SIZE;
			vizRT.twod.height = SHADOW_MAP_SIZE;
			vizRT.levelCount = 1;
			vizRT.multiSampleCount = 0;
			vizRT.texture = rtShadowViz;
			vizRT.colorBuffer = NULL;

			FNA3D_SetRenderTargets(device, &vizRT, 1, NULL,
				FNA3D_DEPTHFORMAT_NONE, 0);

			viewport.w = SHADOW_MAP_SIZE; viewport.h = SHADOW_MAP_SIZE;
			FNA3D_SetViewport(device, &viewport);

			FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET,
				&blackColor, 1.0f, 0);

			FNA3D_VerifySampler(device, 0, rtShadow, &sampPoint);

			technique = FNA3D_GetEffectTechnique(vizEffect, 0);
			FNA3D_SetEffectTechnique(device, vizEffect, technique);
			FNA3D_ApplyEffect(device, vizEffect, 0, NULL);

			bindingQuad.vertexBuffer = vbQuad;
			FNA3D_ApplyVertexBufferBindings(device, &bindingQuad, 1, 1, 0);
			FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST,
				0, 2);
		}

		/* ============================================================
		 * PASS 3: Scene (backbuffer, NdotL diffuse + PCF shadows)
		 * ============================================================ */
		{
			float shadowParams[4];
			float lightDir3[3];

			FNA3D_SetRenderTargets(device, NULL, 0, NULL,
				FNA3D_DEPTHFORMAT_D16, 0);

			/* Restore window viewport */
			viewport.w = fbWidth; viewport.h = fbHeight;
			FNA3D_SetViewport(device, &viewport);

			FNA3D_Clear(device,
				FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_DEPTHBUFFER,
				&clearColor, 1.0f, 0);

			FNA3D_VerifySampler(device, 0, rtShadow, &sampPoint);

			FNA3D_SetEffectParamValue(device, sceneEffect, "ViewProj",
				&viewproj_t.m11, 0, (uint32_t) sizeof(Mat4));
			FNA3D_SetEffectParamValue(device, sceneEffect, "LightViewProj",
				&lightViewProj_t.m11, 0, (uint32_t) sizeof(Mat4));

			lightDir3[0] = dirToLight.x;
			lightDir3[1] = dirToLight.y;
			lightDir3[2] = dirToLight.z;
			FNA3D_SetEffectParamValue(device, sceneEffect, "DirToLight",
				lightDir3, 0, (uint32_t) sizeof(lightDir3));

			shadowParams[0] = depthBias;
			shadowParams[1] = 1.0f / (float) SHADOW_MAP_SIZE;
			shadowParams[2] = pcfRadius;
			shadowParams[3] = shadowStrength;
			FNA3D_SetEffectParamValue(device, sceneEffect, "ShadowParams",
				shadowParams, 0, (uint32_t) sizeof(shadowParams));

			technique = FNA3D_GetEffectTechnique(sceneEffect, 0);
			FNA3D_SetEffectTechnique(device, sceneEffect, technique);
			FNA3D_ApplyEffect(device, sceneEffect, 0, NULL);

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

		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	/* ---- Cleanup ---- */
	FNA3D_AddDisposeTexture(device, rtShadow);
	FNA3D_AddDisposeTexture(device, rtShadowViz);
	FNA3D_AddDisposeRenderbuffer(device, rbShadowDepth);
	FNA3D_AddDisposeVertexBuffer(device, vbTeapot);
	FNA3D_AddDisposeVertexBuffer(device, vbFloor);
	FNA3D_AddDisposeVertexBuffer(device, vbQuad);
	FNA3D_AddDisposeEffect(device, depthEffect);
	FNA3D_AddDisposeEffect(device, sceneEffect);
	FNA3D_AddDisposeEffect(device, vizEffect);
	FNA3D_ImGui_ShutdownEXT(device);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
