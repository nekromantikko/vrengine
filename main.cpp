#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "system.h"
#include "renderer.h"
#include "xr.h"
#include "astc.h"
#include "gltf.h"
#include "math.h"

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

	devTexBuffer = (u8*)AllocFileBytes("textures/hands_albedo.astc", devTexSize, app->activity->assetManager);
	GetAstcInfo(devTexBuffer, texInfo.width, texInfo.height, texInfo.compression);
	GetAstcPayload(devTexBuffer, &texInfo.pixels);

	Rendering::TextureHandle handsAlbedoHandle = renderer.CreateTexture("hands_albedo", texInfo);
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
	cgltf_data* tvData = nullptr;
	if (!LoadGLTF("models", "tv.gltf", &tvData, app->activity->assetManager)) {
		DEBUG_ERROR("Failed to load TV asset!");
	}
	Rendering::MeshCreateInfo meshInfo;
	if (!GetGLTFMeshInfo(tvData, "Mesh.010", meshInfo)) {
		DEBUG_ERROR("Failed to get TV mesh!");
	}

	Rendering::MeshHandle tvMesh = renderer.CreateMesh("tv", meshInfo);

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

	FreeGLTFData(tvData);

	// Load hands
	cgltf_data* handsData = nullptr;
	if (!LoadGLTF("models", "hands.gltf", &handsData, app->activity->assetManager)) {
		DEBUG_ERROR("Failed to load hand asset!");
	}
	if (!GetGLTFMeshInfo(handsData, "hand_left", meshInfo)) {
		DEBUG_ERROR("Failed to get left hand mesh!");
	}
	Rendering::MeshHandle leftHandMesh = renderer.CreateMesh("hand_left", meshInfo);

	// TODO: Make this an util or struct destructor or something...
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

	if (!GetGLTFMeshInfo(handsData, "hand_right", meshInfo)) {
		DEBUG_ERROR("Failed to get right hand mesh!");
	}
	Rendering::MeshHandle rightHandMesh = renderer.CreateMesh("hand_right", meshInfo);

	// TODO: Make this an util or struct destructor or something...
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

	FreeGLTFData(handsData);

	// Load placeholder gamepad
	cgltf_data* gamepadData = nullptr;
	if (!LoadGLTF("models", "gamepad.gltf", &handsData, app->activity->assetManager)) {
		DEBUG_ERROR("Failed to load gamepad asset!");
	}
	if (!GetGLTFMeshInfo(handsData, "Mesh.004", meshInfo)) {
		DEBUG_ERROR("Failed to get gamepad mesh!");
	}
	Rendering::MeshHandle gamepadMesh = renderer.CreateMesh("gamepad", meshInfo);

	// TODO: Make this an util or struct destructor or something...
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

	FreeGLTFData(gamepadData);

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

	matInfo.data.textures[0] = handsAlbedoHandle;
	Rendering::MaterialHandle handsMaterial = renderer.CreateMaterial("HandsMat", matInfo);

	const glm::mat4 leftHandControllerOffset = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.03686, -0.02571, 0.06465)), -0.2443f, glm::vec3(0,0,1));
	const glm::mat4 rightHandControllerOffset = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-0.03686, -0.02571, 0.06465)), 0.2443f, glm::vec3(0,0,1));

	const glm::mat4 leftHandControllerOffsetInverse = glm::inverse(leftHandControllerOffset);
	const glm::mat4 rightHandControllerOffsetInverse = glm::inverse(rightHandControllerOffset);

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

			// Draw hands
			bool leftHandVisible = false;
			bool rightHandVisible = false;
			glm::mat4 leftHandTransform = glm::mat4(1.0);
			if (xrInstance.GetHandTransform(xrDisplayTime, XR::VR_HAND_LEFT, leftHandTransform)) {
				//renderer.DrawMesh(leftHandMesh, handsMaterial, leftHandTransform);
				//renderer.DrawMesh(gamepadMesh, material, leftHandTransform * leftHandControllerOffset);
				leftHandVisible = true;
			}
			glm::mat4 rightHandTransform = glm::mat4(1.0);
			if (xrInstance.GetHandTransform(xrDisplayTime, XR::VR_HAND_RIGHT, rightHandTransform)) {
				//renderer.DrawMesh(rightHandMesh, handsMaterial, rightHandTransform);
				//renderer.DrawMesh(gamepadMesh, material, rightHandTransform * rightHandControllerOffset);
				rightHandVisible = true;
			}

			if (leftHandVisible || rightHandVisible) {
				const glm::mat4 gamepadLeftTransform = leftHandTransform * leftHandControllerOffset;
				const glm::mat4 gamepadRightTransform = rightHandTransform * rightHandControllerOffset;

				const glm::quat gamepadLeftRot = glm::quat_cast(gamepadLeftTransform);
				const glm::quat gamepadRightRot = glm::quat_cast(gamepadRightTransform);

				glm::mat4 gamepadTransform;
				if (leftHandVisible && rightHandVisible) {
					const glm::vec3 gamepadPos = (gamepadLeftTransform[3] + gamepadRightTransform[3]) / 2.0f;
					const glm::quat gamepadRot = AverageQuaternionsLogarithm(gamepadLeftRot, gamepadRightRot);
					gamepadTransform = glm::translate(glm::mat4(1.0), gamepadPos) * glm::mat4_cast(gamepadRot);
				} else if (leftHandVisible) {
					gamepadTransform = gamepadLeftTransform;
				} else {
					gamepadTransform = gamepadRightTransform;
				}

				renderer.DrawMesh(gamepadMesh, material, gamepadTransform);
				renderer.DrawMesh(leftHandMesh, handsMaterial, gamepadTransform * leftHandControllerOffsetInverse);
				renderer.DrawMesh(rightHandMesh, handsMaterial, gamepadTransform * rightHandControllerOffsetInverse);
			}

			// Draw room
			renderer.DrawMesh(cubeMesh, material, glm::mat4(1.0f));

			// Draw TV
			glm::mat4 tvTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0, 1.0f, -roomHalfDepth + 0.5f));
			renderer.DrawMesh(tvMesh, tvMaterial, tvTransform);

			renderer.Render(xrSwapchainImageIndex);

			xrInstance.ReleaseSwapchainImage();
		}
		xrInstance.EndFrame(xrDisplayTime);
	}

	xrInstance.DestroySession();

	ANativeActivity_finish(app->activity);
	app->activity->vm->DetachCurrentThread();
}