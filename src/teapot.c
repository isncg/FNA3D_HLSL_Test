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
} Vertex;

/* ---- Teapot model loader ---- */

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
	{
		SDL_Log("Failed to read triangle count");
		fclose(fp); return -1;
	}

	total = triangle_count * 3;
	verts = (Vertex *) SDL_malloc(sizeof(Vertex) * total);
	positions = (Vec3 *) SDL_malloc(sizeof(Vec3) * total);
	face_normals = (Vec3 *) SDL_malloc(sizeof(Vec3) * triangle_count);
	if (verts == NULL || positions == NULL || face_normals == NULL)
	{
		SDL_Log("Out of memory");
		SDL_free(verts); SDL_free(positions); SDL_free(face_normals);
		fclose(fp); return -1;
	}

	for (i = 0; i < triangle_count; i += 1)
	{
		Vec3 p[3];
		int v;
		if (fscanf(fp, "%f %f %f", &p[0].x, &p[0].y, &p[0].z) != 3 ||
			fscanf(fp, "%f %f %f", &p[1].x, &p[1].y, &p[1].z) != 3 ||
			fscanf(fp, "%f %f %f", &p[2].x, &p[2].y, &p[2].z) != 3)
		{
			SDL_Log("Failed to read triangle %d", i);
			SDL_free(verts); SDL_free(positions); SDL_free(face_normals);
			fclose(fp); return -1;
		}
		Vec3 e0 = vec3_sub(p[1], p[0]);
		Vec3 e1 = vec3_sub(p[2], p[0]);
		face_normals[i] = vec3_normalize(vec3_cross(e0, e1));
		for (v = 0; v < 3; v += 1)
		{
			positions[i * 3 + v] = p[v];
			verts[i * 3 + v].x = p[v].x;
			verts[i * 3 + v].y = p[v].y;
			verts[i * 3 + v].z = p[v].z;
		}
	}
	fclose(fp);

	for (i = 0; i < total; i += 1)
	{
		Vec3 sum = face_normals[i / 3];
		int j;
		for (j = 0; j < total; j += 1)
		{
			if (j / 3 == i / 3) continue;
			if (vec3_equal(positions[i], positions[j]))
				sum = vec3_add(sum, face_normals[j / 3]);
		}
		Vec3 avg = vec3_normalize(sum);
		verts[i].nx = avg.x; verts[i].ny = avg.y; verts[i].nz = avg.z;
	}

	SDL_free(positions); SDL_free(face_normals);
	*out_verts = verts; *out_count = total;
	return 0;
}

/* ---- Pipeline state helpers ---- */

static void setup_default_rasterizer(FNA3D_Device *device)
{
	FNA3D_RasterizerState rs;
	memset(&rs, 0, sizeof(rs));
	rs.fillMode = FNA3D_FILLMODE_SOLID;
	rs.cullMode = FNA3D_CULLMODE_NONE;
	FNA3D_ApplyRasterizerState(device, &rs);
}

static void setup_default_blend(FNA3D_Device *device)
{
	FNA3D_BlendState bs;
	memset(&bs, 0, sizeof(bs));
	bs.colorSourceBlend = FNA3D_BLEND_ONE;
	bs.colorDestinationBlend = FNA3D_BLEND_ZERO;
	bs.colorBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	bs.alphaSourceBlend = FNA3D_BLEND_ONE;
	bs.alphaDestinationBlend = FNA3D_BLEND_ZERO;
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
	FNA3D_DepthStencilState ds;
	memset(&ds, 0, sizeof(ds));
	ds.depthBufferEnable = 1;
	ds.depthBufferWriteEnable = 1;
	ds.depthBufferFunction = FNA3D_COMPAREFUNCTION_LESSEQUAL;
	FNA3D_SetDepthStencilState(device, &ds);
}

int main(int argc, char *argv[])
{
	SDL_Window *window;
	FNA3D_Device *device;
	FNA3D_PresentationParameters pp;
	FNA3D_Buffer *vb;
	FNA3D_Effect *effect;
	FNA3D_VertexElement elements[2];
	FNA3D_VertexDeclaration decl;
	FNA3D_VertexBufferBinding binding;
	FNA3D_Viewport viewport;
	uint8_t *effect_bytes;
	uint32_t effect_len;
	Vertex *teapot_verts;
	int teapot_vert_count;
	uint8_t running = 1;
	SDL_Event evt;
	FNA3D_Vec4 clear_color = {0.2f, 0.2f, 0.25f, 1.0f};
	FNA3D_EffectTechnique *technique;

	Mat4 world, view, proj, viewproj, worldviewproj, worldviewproj_t;
	Vec3 target = {0.0f, 1.5f, 0.0f};
	float radius = 12.0f;
	float azimuth = 0.0f, altitude = 0.35f;
	int dragging = 0;

	(void) argc; (void) argv;

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

	if (load_teapot("../assets/models/teapot_bezier0.tris",
		&teapot_verts, &teapot_vert_count) != 0)
	{
		SDL_Quit();
		return 1;
	}

	memset(&pp, 0, sizeof(pp));
	pp.backBufferWidth = 800;
	pp.backBufferHeight = 600;
	pp.multiSampleCount = 0;
	pp.isFullScreen = 0;
	pp.depthStencilFormat = FNA3D_DEPTHFORMAT_D16;
	pp.presentationInterval = FNA3D_PRESENTINTERVAL_DEFAULT;
	pp.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
	pp.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

	window = SDL_CreateWindow("FNA3D_HLSL Utah Teapot (NormalEffect)",
		pp.backBufferWidth, pp.backBufferHeight,
		FNA3D_PrepareWindowAttributes());
	if (window == NULL)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		SDL_free(teapot_verts); SDL_Quit(); return 1;
	}
	pp.deviceWindowHandle = window;

	device = FNA3D_CreateDevice(&pp, 1);
	if (device == NULL)
	{
		SDL_Log("FNA3D_CreateDevice failed");
		SDL_DestroyWindow(window); SDL_free(teapot_verts); SDL_Quit(); return 1;
	}
	FNA3D_SetRenderTargets(device, NULL, 0, NULL, FNA3D_DEPTHFORMAT_D16, 0);

	/* Load effect */
	effect_bytes = load_file("../assets/effects/normal.feb", &effect_len);
	if (effect_bytes == NULL)
	{
		FNA3D_DestroyDevice(device); SDL_DestroyWindow(window);
		SDL_free(teapot_verts); SDL_Quit(); return 1;
	}
	if (!FNA3D_CreateEffect(device, effect_bytes, effect_len, &effect))
	{
		SDL_Log("FNA3D_CreateEffect failed");
		SDL_free(effect_bytes); FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window); SDL_free(teapot_verts); SDL_Quit(); return 1;
	}
	SDL_free(effect_bytes);

	if (FNA3D_GetEffectTechniqueCount(effect) == 0)
	{
		SDL_Log("Effect has no techniques");
		FNA3D_AddDisposeEffect(device, effect); FNA3D_DestroyDevice(device);
		SDL_DestroyWindow(window); SDL_free(teapot_verts); SDL_Quit(); return 1;
	}
	technique = FNA3D_GetEffectTechnique(effect, 0);
	FNA3D_SetEffectTechnique(device, effect, technique);

	/* Vertex buffer */
	vb = FNA3D_GenVertexBuffer(device, 0, FNA3D_BUFFERUSAGE_WRITEONLY,
		(int32_t)(sizeof(Vertex) * teapot_vert_count));
	FNA3D_SetVertexBufferData(device, vb, 0, teapot_verts, teapot_vert_count,
		sizeof(Vertex), sizeof(Vertex), FNA3D_SETDATAOPTIONS_NONE);
	SDL_free(teapot_verts);

	elements[0].offset = 0;
	elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	elements[0].usageIndex = 0;

	elements[1].offset = sizeof(float) * 3;
	elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_NORMAL;
	elements[1].usageIndex = 0;

	decl.vertexStride = sizeof(Vertex);
	decl.elementCount = 2;
	decl.elements = elements;

	memset(&binding, 0, sizeof(binding));
	binding.vertexBuffer = vb;
	binding.vertexDeclaration = decl;
	binding.vertexOffset = 0;
	binding.instanceFrequency = 0;

	viewport.x = 0; viewport.y = 0;
	viewport.w = pp.backBufferWidth; viewport.h = pp.backBufferHeight;
	viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
	FNA3D_SetViewport(device, &viewport);

	setup_default_rasterizer(device);
	setup_default_blend(device);
	setup_default_depth(device);

	mat4_identity(&world);
	mat4_perspective(&proj, MATH3D_PI / 4.0f,
		(float) pp.backBufferWidth / (float) pp.backBufferHeight, 0.1f, 100.0f);

	SDL_Log("Left-drag: orbit camera | Scroll: zoom");

	while (running)
	{
		Vec3 eye;
		float cos_alt;

		while (SDL_PollEvent(&evt))
		{
			switch (evt.type)
			{
			case SDL_EVENT_QUIT:
				running = 0; break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				if (evt.button.button == SDL_BUTTON_LEFT) dragging = 1;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
				if (evt.button.button == SDL_BUTTON_LEFT) dragging = 0;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				if (dragging)
				{
					azimuth -= evt.motion.xrel * 0.005f;
					altitude += evt.motion.yrel * 0.005f;
					altitude = clampf(altitude,
						-MATH3D_PI / 2.0f + 0.01f,
						MATH3D_PI / 2.0f - 0.01f);
				}
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				radius -= evt.wheel.y * 1.0f;
				if (radius < 3.0f) radius = 3.0f;
				if (radius > 40.0f) radius = 40.0f;
				break;
			}
		}

		cos_alt = cosf(altitude);
		eye.x = target.x + radius * cosf(azimuth) * cos_alt;
		eye.y = target.y + radius * sinf(altitude);
		eye.z = target.z + radius * sinf(azimuth) * cos_alt;

		mat4_lookat_lh(&view, eye, target, (Vec3){0.0f, 1.0f, 0.0f});
		mat4_mul(&viewproj, &view, &proj);
		mat4_mul(&worldviewproj, &world, &viewproj);
		mat4_transpose(&worldviewproj_t, &worldviewproj);

		FNA3D_SetEffectParamValue(device, effect, "WorldViewProj",
			&worldviewproj_t.m11, 0, (uint32_t) sizeof(Mat4));

		FNA3D_Clear(device,
			FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_DEPTHBUFFER,
			&clear_color, 1.0f, 0);
		FNA3D_ApplyEffect(device, effect, 0, NULL);
		FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);
		FNA3D_DrawPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST,
			0, teapot_vert_count / 3);
		FNA3D_SwapBuffers(device, NULL, NULL, window);
	}

	FNA3D_AddDisposeVertexBuffer(device, vb);
	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
