#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "system.h"
#include "renderer.h"
#include "xr.h"

static bool running;
static Rendering::Renderer* rendererPtr; // Stupid hack...

struct AndroidAppState {
	ANativeWindow* nativeWindow = nullptr;
	bool resumed = false;
};

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
	shaderInfo.vertexInputs = (Rendering::VertexAttribFlags)(Rendering::VERTEX_POSITION_BIT | Rendering::VERTEX_COLOR_BIT);
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

	const u32 side = 32;
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
				transform = glm::scale(transform, glm::vec3(0.05f, 0.05f, 0.05f));
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

			for (int i = 0; i < batchCount; i++) {
				renderer.DrawMeshInstanced(cubeMesh, material, Rendering::maxInstanceCountPerDraw, instanceTransforms + Rendering::maxInstanceCountPerDraw *i);
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