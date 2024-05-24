#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "system.h"
#include "renderer.h"
#include "xr.h"
#include "astc.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

static bool running;
static Rendering::Renderer* rendererPtr; // Stupid hack...

struct AndroidAppState {
	ANativeWindow* nativeWindow = nullptr;
	bool resumed = false;
};

size_t ProcessVertexBuffer(cgltf_accessor* accessor, u8** pOutBuffer, size_t outElementSize, u8* bufferData) {
	cgltf_buffer_view* bufView = accessor->buffer_view;
	cgltf_buffer* buffer = bufView->buffer;

	size_t componentCount = 0;
	switch (accessor->type) {
		case cgltf_type_vec2:
			componentCount = 2;
			break;
		case cgltf_type_vec3:
			componentCount = 3;
			break;
		case cgltf_type_vec4:
			componentCount = 4;
			break;
		case cgltf_type_mat2:
			componentCount = 4;
			break;
		case cgltf_type_mat3:
			componentCount = 9;
			break;
		case cgltf_type_mat4:
			componentCount = 16;
			break;
		case cgltf_type_scalar:
			componentCount = 1;
			break;
		default:
			DEBUG_ERROR("Unknown accessor type %d", accessor->type);
			break;
	}

	size_t componentSize = 0;
	switch (accessor->component_type) {
		case cgltf_component_type_r_8:
			componentSize = sizeof(s8);
			break;
		case cgltf_component_type_r_8u:
			componentSize = sizeof(u8);
			break;
		case cgltf_component_type_r_16:
			componentSize = sizeof(s16);
			break;
		case cgltf_component_type_r_16u:
			componentSize = sizeof(u16);
			break;
		case cgltf_component_type_r_32u:
			componentSize = sizeof(u32);
			break;
		case cgltf_component_type_r_32f:
			componentSize = sizeof(r32);
			break;
		default:
			DEBUG_ERROR("Unknown accessor component type %d", accessor->component_type);
			break;
	}

	size_t elementSize = componentCount * componentSize;
	*pOutBuffer = (u8*)calloc(accessor->count, outElementSize);

	size_t srcStride = bufView->stride == 0 ? elementSize : bufView->stride;
	size_t copySize = std::min(elementSize, outElementSize);
	if (elementSize > outElementSize) {
		DEBUG_LOG("Out element size %d is smaller than %d, clamping will occur", outElementSize, elementSize);
	}

	const u8* pSrc = bufferData + bufView->offset + accessor->offset;
	for (int i = 0; i < accessor->count; i++) {
		int dstOffset = outElementSize * i;
		int srcOffset = srcStride * i;
		memcpy(*pOutBuffer + dstOffset, pSrc + srcOffset, copySize);
	}

	return accessor->count;
}

// TODO: Validate that element size matches source...
void ProcessVertexAttributeData(Rendering::MeshCreateInfo& meshInfo, cgltf_attribute* attr, u8* bufferData) {
	u8** pOutBuffer = nullptr;
	size_t outElementSize = 0;

	if (attr->type == cgltf_attribute_type_position) {
		outElementSize = sizeof(glm::vec3);
		pOutBuffer = (u8**)&meshInfo.position;
	}
	else if (attr->type == cgltf_attribute_type_texcoord && attr->index == 0) {
		outElementSize = sizeof(glm::vec2);
		pOutBuffer = (u8**)&meshInfo.texcoord0;
	}
	else if (attr->type == cgltf_attribute_type_normal) {
		outElementSize = sizeof(glm::vec3);
		pOutBuffer = (u8**)&meshInfo.normal;
	}
	else if (attr->type == cgltf_attribute_type_tangent) {
		outElementSize = sizeof(glm::vec4);
		pOutBuffer = (u8**)&meshInfo.tangent;
	}
	else if (attr->type == cgltf_attribute_type_color && attr->index == 0) {
		outElementSize = sizeof(Rendering::Color);
		pOutBuffer = (u8**)&meshInfo.color;
	}
	else {
		DEBUG_LOG("Unsupported buffer \"%d\", ignoring!", attr->type);
		return;
	}

	meshInfo.vertexCount = ProcessVertexBuffer(attr->data, pOutBuffer, outElementSize, bufferData);
}

/**
 * Process the next main command.
 */
static void app_handle_cmd(struct android_app* app, int32_t cmd) {
	AndroidAppState* appState = (AndroidAppState*)app->userData;

	switch (cmd) {
		// There is no APP_CMD_CREATE. The ANativeActivity creates the
		// application thread from onCreate(). The application thread
		// then calls android_main().
		case APP_CMD_START: {
			DEBUG_LOG("onStart()");
			DEBUG_LOG("    APP_CMD_START");
			break;
		}
		case APP_CMD_RESUME: {
			DEBUG_LOG("onResume()");
			DEBUG_LOG("    APP_CMD_RESUME");
			appState->resumed = true;
			break;
		}
		case APP_CMD_PAUSE: {
			DEBUG_LOG("onPause()");
			DEBUG_LOG("    APP_CMD_PAUSE");
			appState->resumed = false;
			break;
		}
		case APP_CMD_STOP: {
			DEBUG_LOG("onStop()");
			DEBUG_LOG("    APP_CMD_STOP");
			break;
		}
		case APP_CMD_DESTROY: {
			DEBUG_LOG("onDestroy()");
			DEBUG_LOG("    APP_CMD_DESTROY");
			appState->nativeWindow = NULL;
			break;
		}
		case APP_CMD_INIT_WINDOW: {
			DEBUG_LOG("surfaceCreated()");
			DEBUG_LOG("    APP_CMD_INIT_WINDOW");
			appState->nativeWindow = app->window;
			break;
		}
		case APP_CMD_TERM_WINDOW: {
			DEBUG_LOG("surfaceDestroyed()");
			DEBUG_LOG("    APP_CMD_TERM_WINDOW");
			appState->nativeWindow = NULL;
			break;
		}
	}
}

extern "C" void android_main(struct android_app *app) {
	JNIEnv *env;
	app->activity->vm->AttachCurrentThread(&env, nullptr);

	AndroidAppState appState = {};

	app->userData = &appState;
	app->onAppCmd = app_handle_cmd;

	// Setup XR
	XR::XRInstance xrInstance(app);
	if (!xrInstance.InitializeHMD()) {
		DEBUG_ERROR("Failed to initialize HMD!");
	}

	// Setup rendering
	Rendering::Renderer renderer(&xrInstance);
	rendererPtr = &renderer;

	xrInstance.CreateSession(renderer);
	renderer.CreateXRSwapchain(&xrInstance);
	xrInstance.RequestStartSession();

	// Test render stuff
	u32 devTexSize;
	u8* devTexBuffer = (u8*)AllocFileBytes("textures/dev.astc", devTexSize, app->activity->assetManager);
	Rendering::TextureCreateInfo texInfo{};
	texInfo.type = Rendering::TEXTURE_2D;
	texInfo.generateMips = false;
	texInfo.space = Rendering::COLORSPACE_SRGB;
	texInfo.filter = Rendering::TEXFILTER_LINEAR;
	GetAstcInfo(devTexBuffer, texInfo.width, texInfo.height, texInfo.compression);
	GetAstcPayload(devTexBuffer, &texInfo.pixels);

	Rendering::TextureHandle devTexHandle = renderer.CreateTexture("dev", texInfo);
	free(devTexBuffer);

	devTexBuffer = (u8*)AllocFileBytes("textures/tv_albedo.astc", devTexSize, app->activity->assetManager);
	GetAstcInfo(devTexBuffer, texInfo.width, texInfo.height, texInfo.compression);
	GetAstcPayload(devTexBuffer, &texInfo.pixels);

	Rendering::TextureHandle tvAlbedoHandle = renderer.CreateTexture("tv_albedo", texInfo);
	free(devTexBuffer);

	r32 playAreaWidth, playAreaDepth;
	xrInstance.GetSpaceDimensions(playAreaWidth, playAreaDepth);
	DEBUG_LOG("Roomscale dimensions = (%f, %f)", playAreaWidth, playAreaDepth);

	// Add padding around play area
	const u32 roomWidth = playAreaWidth + 2.0f;
	const u32 roomDepth = playAreaDepth + 2.0f;

	r32 roomHalfWidth = roomWidth / 2.0f;
	r32 roomHalfDepth = roomDepth / 2.0f;

	r32 roomHeight = 2.5f;

	Rendering::MeshCreateInfo cubeInfo{};
	glm::vec3 cubeVerts[] = {
		// Floor
		{-roomHalfWidth,0,roomHalfDepth},
		{roomHalfWidth,0,roomHalfDepth},
		{roomHalfWidth,0,-roomHalfDepth},
		{-roomHalfWidth,0,-roomHalfDepth},

		// Ceiling
		{roomHalfWidth,roomHeight,roomHalfDepth},
		{-roomHalfWidth,roomHeight,roomHalfDepth},
		{-roomHalfWidth,roomHeight,-roomHalfDepth},
		{roomHalfWidth,roomHeight,-roomHalfDepth},

		// Front wall
		{-roomHalfWidth,0,roomHalfDepth},
		{-roomHalfWidth,roomHeight,roomHalfDepth},
	{roomHalfWidth,roomHeight,roomHalfDepth},
		{roomHalfWidth,0,roomHalfDepth},

		// Back wall
		{roomHalfWidth,0,-roomHalfDepth},
		{roomHalfWidth,roomHeight,-roomHalfDepth},
		{-roomHalfWidth,roomHeight,-roomHalfDepth},
		{-roomHalfWidth,0,-roomHalfDepth},

		// Right wall
		{roomHalfWidth, 0, roomHalfDepth},
		{roomHalfWidth, roomHeight, roomHalfDepth},
		{roomHalfWidth, roomHeight, -roomHalfDepth},
		{roomHalfWidth, 0, -roomHalfDepth},

		// Left wall
		{-roomHalfWidth, 0, -roomHalfDepth},
		{-roomHalfWidth, roomHeight, -roomHalfDepth},
		{-roomHalfWidth, roomHeight, roomHalfDepth},
		{-roomHalfWidth, 0, roomHalfDepth},
	};
	Rendering::VertexUV cubeUV[] = {
		// Floor
		{roomHalfWidth, -roomHalfDepth},
		{-roomHalfWidth,-roomHalfDepth},
		{-roomHalfWidth,roomHalfDepth},
		{roomHalfWidth,roomHalfDepth},

		// Ceiling
		{roomHalfWidth, -roomHalfDepth},
		{-roomHalfWidth,-roomHalfDepth},
		{-roomHalfWidth,roomHalfDepth},
		{roomHalfWidth,roomHalfDepth},

		// Front wall
		{roomHalfWidth, 0},
		{roomHalfWidth,-roomHeight},
		{-roomHalfWidth,-roomHeight},
		{-roomHalfWidth,0},

		// Back wall
		{roomHalfWidth, 0},
		{roomHalfWidth,-roomHeight},
		{-roomHalfWidth,-roomHeight},
		{-roomHalfWidth,0},

		// Right wall
		{roomHalfDepth, 0},
		{roomHalfDepth,-roomHeight},
		{-roomHalfDepth,-roomHeight},
		{-roomHalfDepth,0},

			// Left wall
		{roomHalfDepth, 0},
		{roomHalfDepth,-roomHeight},
		{-roomHalfDepth,-roomHeight},
		{-roomHalfDepth,0},
	};
	cubeInfo.vertexCount = 24;
	cubeInfo.position = cubeVerts;
	cubeInfo.texcoord0 = cubeUV;
	Rendering::Triangle tris[] = {
		{0,1,2},
		{0,2,3},
		{4,5,6},
		{4,6,7},
		{8,9,10},
		{8,10,11},
		{12,13,14},
		{12,14,15},
		{16,17,18},
		{16,18,19},
		{20,21,22},
		{20,22,23}
	};
	cubeInfo.triangleCount = 12;
	cubeInfo.triangles = tris;

	Rendering::MeshHandle cubeMesh = renderer.CreateMesh("Cube", cubeInfo);

	// Load gltf
	u32 duckFileSize;
	char* buf = AllocFileBytes("models/tv.gltf", duckFileSize, app->activity->assetManager);
	cgltf_options options{};
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse(&options, buf, duckFileSize, &data);
	if (result != cgltf_result_success) {
		DEBUG_ERROR("Failed to load gltf :c");
	}

	u8 *buffer = nullptr;
	if (data->buffers_count > 0) {
		cgltf_buffer &gltfBuffer = data->buffers[0];
		u32 bufferSize;
		char fname[1024];
		sprintf(fname, "models/%s", gltfBuffer.uri);
		buffer = (u8*)AllocFileBytes(fname, bufferSize, app->activity->assetManager);
	}

	// Load first mesh
	Rendering::MeshHandle gltfMesh;
	if (data->meshes_count > 0 && buffer != nullptr) {
		cgltf_mesh &mesh = data->meshes[0];
		DEBUG_LOG("Loading mesh %s", mesh.name);
		if (mesh.primitives_count > 0) {
			cgltf_primitive &prim = mesh.primitives[0];
			if (prim.type == cgltf_primitive_type_triangles) {
				Rendering::MeshCreateInfo meshInfo{};
				for (int i = 0; i < prim.attributes_count; i++) {
					cgltf_attribute &attr = prim.attributes[i];
					ProcessVertexAttributeData(meshInfo, &attr, buffer);
				}

				size_t indexCount = ProcessVertexBuffer(prim.indices, (u8**)&meshInfo.triangles, sizeof(u32), buffer);
				meshInfo.triangleCount = indexCount / 3;
				DEBUG_LOG("Mesh triangle count = %d", meshInfo.triangleCount);

				gltfMesh = renderer.CreateMesh("tv", meshInfo);

				if (meshInfo.position != nullptr) {
					free(meshInfo.position);
				}
				if (meshInfo.texcoord0 != nullptr) {
					free(meshInfo.texcoord0);
				}
				if (meshInfo.normal != nullptr) {
					free(meshInfo.normal);
				}
				if (meshInfo.tangent != nullptr) {
					free(meshInfo.tangent);
				}
				if (meshInfo.color != nullptr) {
					free(meshInfo.color);
				}
				free(meshInfo.triangles);
			}
		}
	}

	cgltf_free(data);

	Rendering::ShaderDataLayout shaderLayout{};
	shaderLayout.dataSize = 0;
	shaderLayout.propertyCount = 0;

	Rendering::ShaderCreateInfo shaderInfo{};
	shaderInfo.metadata.layer = Rendering::RENDER_LAYER_OPAQUE;
	shaderInfo.metadata.dataLayout = shaderLayout;
	shaderInfo.vertexInputs = (Rendering::VertexAttribFlags)(Rendering::VERTEX_POSITION_BIT | Rendering::VERTEX_TEXCOORD_0_BIT);
	shaderInfo.samplerCount = 1;
	shaderInfo.vertShader = AllocFileBytes("shaders/vert.spv", shaderInfo.vertShaderLength, app->activity->assetManager);
	shaderInfo.fragShader = AllocFileBytes("shaders/test_frag.spv", shaderInfo.fragShaderLength, app->activity->assetManager);

	Rendering::ShaderHandle shader = renderer.CreateShader("TestShader", shaderInfo);
	free(shaderInfo.vertShader);
	free(shaderInfo.fragShader);

	Rendering::MaterialCreateInfo matInfo{};
	matInfo.metadata.shader = shader;
	matInfo.metadata.castShadows = true;
	matInfo.data.textures[0] = devTexHandle;

	Rendering::MaterialHandle material = renderer.CreateMaterial("TestMat", matInfo);

	matInfo.data.textures[0] = tvAlbedoHandle;
	Rendering::MaterialHandle tvMaterial = renderer.CreateMaterial("TVMat", matInfo);

	//u64 time = GetTickCount64();

	while (app->destroyRequested == 0) {
		// Read all pending events.
		for (;;) {
			int events;
			struct android_poll_source* source;
			// If the timeout is zero, returns immediately without blocking.
			// If the timeout is negative, waits indefinitely until an event appears.
			const int timeoutMilliseconds = (!appState.resumed && !xrInstance.SessionRunning() && app->destroyRequested == 0) ? -1 : 0;
			if (ALooper_pollAll(timeoutMilliseconds, nullptr, &events, (void**)&source) < 0) {
				break;
			}

			// Process this event.
			if (source != nullptr) {
				source->process(app, source);
			}
		}

		/*u64 newTime = GetTickCount64();
		u64 deltaTime = newTime - time;
		r32 deltaTimeSeconds = deltaTime / 1000.0f;
		time = newTime;*/

		xrInstance.Update(0.0f); //deltaTimeSeconds

		static s64 xrDisplayTime;
		if (xrInstance.BeginFrame(xrDisplayTime)) {
			static u32 xrSwapchainImageIndex;
			xrInstance.GetNextSwapchainImage(xrSwapchainImageIndex);

			Rendering::CameraData camData{};
			xrInstance.GetCameraData(xrDisplayTime, 0.01f, 100.0f, camData);
			renderer.UpdateCameraRaw(camData);

			// Drwa room
			renderer.DrawMesh(cubeMesh, material, glm::mat4(1.0f));

			// Draw TV
			glm::mat4 tvTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0, 1.0f, -roomHalfDepth + 0.5f));
			renderer.DrawMesh(gltfMesh, tvMaterial, tvTransform);

			renderer.Render(xrSwapchainImageIndex);

			xrInstance.ReleaseSwapchainImage();
		}
		xrInstance.EndFrame(xrDisplayTime);
	}

	xrInstance.DestroySession();

	ANativeActivity_finish(app->activity);
	app->activity->vm->DetachCurrentThread();
}