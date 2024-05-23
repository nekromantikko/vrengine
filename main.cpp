#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "system.h"
#include "renderer.h"
#include "xr.h"

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

	// Load gltf
	u32 duckFileSize;
	char* buf = AllocFileBytes("models/Duck.gltf", duckFileSize, app->activity->assetManager);
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
	Rendering::MeshHandle handle;
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

				handle = renderer.CreateMesh("duck", meshInfo);

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

	Rendering::MeshCreateInfo cubeInfo{};
	glm::vec3 cubeVerts[] = {
		{-1,-1,-1},
		{1,-1,-1},
		{1,-1,1},
		{-1,-1,1},
		{-1,1,-1},
		{1,1,-1},
		{1,1,1},
		{-1,1,1}
	};
	Rendering::Color cubeColors[] = {
		{0,0,0,1},
		{1,0,0,1},
		{1,0,1,1},
		{0,0,1,1},
		{0,1,0,1},
		{1,1,0,1},
		{1,1,1,1},
		{0,1,1,1}
	};
	cubeInfo.vertexCount = 8;
	cubeInfo.position = cubeVerts;
	cubeInfo.color = cubeColors;
	Rendering::Triangle tris[] = {
		{0,1,2},
		{0,2,3},
		{3,6,7},
		{3,2,6},
		{6,2,5},
		{2,1,5},
		{5,1,0},
		{5,0,4},
		{4,0,7},
		{7,0,3},
		{7,6,4},
		{4,6,5}
	};
	cubeInfo.triangleCount = 12;
	cubeInfo.triangles = tris;

	Rendering::MeshHandle cubeMesh = renderer.CreateMesh("Cube", cubeInfo);

	Rendering::ShaderDataLayout shaderLayout{};
	shaderLayout.dataSize = 0;
	shaderLayout.propertyCount = 0;

	Rendering::ShaderCreateInfo shaderInfo{};
	shaderInfo.metadata.layer = Rendering::RENDER_LAYER_OPAQUE;
	shaderInfo.metadata.dataLayout = shaderLayout;
	shaderInfo.vertexInputs = (Rendering::VertexAttribFlags)(Rendering::VERTEX_POSITION_BIT | Rendering::VERTEX_TEXCOORD_0_BIT);
	shaderInfo.samplerCount = 0;
	shaderInfo.vertShader = AllocFileBytes("shaders/vert.spv", shaderInfo.vertShaderLength, app->activity->assetManager);
	shaderInfo.fragShader = AllocFileBytes("shaders/test_frag.spv", shaderInfo.fragShaderLength, app->activity->assetManager);

	Rendering::ShaderHandle shader = renderer.CreateShader("TestShader", shaderInfo);
	free(shaderInfo.vertShader);
	free(shaderInfo.fragShader);

	Rendering::MaterialCreateInfo matInfo{};
	matInfo.metadata.shader = shader;
	matInfo.metadata.castShadows = true;

	Rendering::MaterialHandle material = renderer.CreateMaterial("TestMat", matInfo);

	const u32 side = 6;
	const u32 instanceCount = side * side * side;
	if (instanceCount > Rendering::maxInstanceCount) {
		DEBUG_ERROR("too many instances");
	}
	const u32 batchCount = (u32)ceil(instanceCount / (r32)Rendering::maxInstanceCountPerDraw);
	glm::mat4x4* instanceTransforms = (glm::mat4x4*)calloc(instanceCount, sizeof(glm::mat4x4));
	u32 i = 0;
	for (int x = 0; x < side; x++) {
		for (int y = 0; y < side; y++) {
			for (int z = 0; z < side; z++) {
				glm::mat4x4& transform = instanceTransforms[i++];
				transform = glm::translate(glm::mat4x4(1.0f), glm::vec3(0.25f * x, 0.25f * y, 0.25f * z));
				transform = glm::scale(transform, glm::vec3(0.0005f, 0.0005f, 0.0005f));
			}
		}
	}

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

			u32 count = batchCount == 1 ? instanceCount : Rendering::maxInstanceCountPerDraw;
			u32 offset = 0;
			for (int i = 0; i < batchCount; i++) {
				if (i == batchCount - 1) {
					count = instanceCount % Rendering::maxInstanceCountPerDraw;
				}
				renderer.DrawMeshInstanced(handle, material, count, instanceTransforms + offset);
				offset += count;
			}
			renderer.Render(xrSwapchainImageIndex);

			xrInstance.ReleaseSwapchainImage();
		}
		xrInstance.EndFrame(xrDisplayTime);
	}

	xrInstance.DestroySession();

	ANativeActivity_finish(app->activity);
	app->activity->vm->DetachCurrentThread();
}