#include "xr.h"
#include "system.h"
#include <string.h>
#include "renderer.h"
#include <gtc/quaternion.hpp>

namespace XR {
	XRInstance::XRInstance(android_app *app) {

		PFN_xrInitializeLoaderKHR xrInitializeLoaderKhr = nullptr;
		xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKhr);

		XrLoaderInitInfoAndroidKHR androidInfo{};
		androidInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
		androidInfo.applicationVM = app->activity->vm;
		androidInfo.applicationContext = app->activity->clazz;
		xrInitializeLoaderKhr((XrLoaderInitInfoBaseHeaderKHR*)&androidInfo);

		XrApplicationInfo appInfo{};
		strcpy(appInfo.applicationName, "Test");
		appInfo.applicationVersion = XR_MAKE_VERSION(0, 0, 0);
		strcpy(appInfo.applicationName, "Nekro Engine");
		appInfo.engineVersion = XR_MAKE_VERSION(0, 0, 0);
		appInfo.apiVersion = XR_CURRENT_API_VERSION;

		const char* validationLayerName = "XR_APILAYER_LUNARG_core_validation";

		u32 availableApiLayerCount;
		xrEnumerateApiLayerProperties(0, &availableApiLayerCount, nullptr);
		std::vector<XrApiLayerProperties> availableApiLayers(availableApiLayerCount, { .type = XR_TYPE_API_LAYER_PROPERTIES });
		xrEnumerateApiLayerProperties(availableApiLayerCount, &availableApiLayerCount, availableApiLayers.data());

		for (auto& layer : availableApiLayers) {
			DEBUG_LOG("Found OpenXR api layer %s: %s", layer.layerName, layer.description);
		}

		u32 availableExtensionCount;
		xrEnumerateInstanceExtensionProperties(nullptr, 0, &availableExtensionCount, nullptr);
		std::vector<XrExtensionProperties> availableExtensions(availableExtensionCount, { .type = XR_TYPE_EXTENSION_PROPERTIES });
		xrEnumerateInstanceExtensionProperties(nullptr, availableExtensionCount, &availableExtensionCount, availableExtensions.data());

		for (auto& extension : availableExtensions) {
			DEBUG_LOG("Found OpenXR extension %s", extension.extensionName);
		}

		XrInstanceCreateInfo createInfo{};
		createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
		createInfo.next = nullptr;
		createInfo.createFlags = 0;
		createInfo.applicationInfo = appInfo;
		// TODO: Fix validation layers
		// createInfo.enabledApiLayerCount = 1;
		// createInfo.enabledApiLayerNames = &validationLayerName;

		// TODO: Actually check if these are in the list of supported extensions
		const char* const extensionNames[] = {
		"XR_KHR_vulkan_enable",
		"XR_KHR_vulkan_enable2",
#ifdef NEKRO_DEBUG
		"XR_EXT_debug_utils"
		};
		createInfo.enabledExtensionCount = 3;
#else
		};
		createInfo.enabledExtensionCount = 2;
#endif
		createInfo.enabledExtensionNames = extensionNames;

		XrResult result = xrCreateInstance(&createInfo, &instance);
		if (result != XR_SUCCESS) {
			// This can fail on PC because SteamVR is not running (Error code -2)
			DEBUG_ERROR("XR instance creation failed with error code %d", result);
		}

#ifdef NEKRO_DEBUG
		SetupDebugLogging();
#endif
	}

	XRInstance::~XRInstance() {
#ifdef NEKRO_DEBUG
		CleanupDebugLogging();
#endif

		xrDestroyInstance(instance);
	}

	bool XRInstance::InitializeHMD() {
		XrInstanceProperties instanceProperties{};
		instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;
		XrResult result = xrGetInstanceProperties(instance, &instanceProperties);
		DEBUG_LOG("OpenXR runtime: %s, version %d", instanceProperties.runtimeName, instanceProperties.runtimeVersion);

		System system{};

		// Try to get HMD system
		XrSystemGetInfo systemGetInfo{};
		systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
		systemGetInfo.next = nullptr;
		systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

		result = xrGetSystem(instance, &systemGetInfo, &system.id);

		if (result != XR_SUCCESS) {
			if (result == XR_ERROR_FORM_FACTOR_UNSUPPORTED) {
				DEBUG_LOG("OpenXR runtime %s does not support HMD", instanceProperties.runtimeName);
			}
			if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
				// TODO: This should be allowed to fail, and app should inform user to connect HMD, then try this again
				DEBUG_LOG("HMD is supported, but currently unavailable");
			}
			DEBUG_LOG("Get system failed with error code %d", result);
			return false;
		}

		system.properties = XrSystemProperties{};
		system.properties.type = XR_TYPE_SYSTEM_PROPERTIES;

		xrGetSystemProperties(instance, system.id, &system.properties);

		DEBUG_LOG("Found XR system %s", system.properties.systemName);
		DEBUG_LOG("Max swapchain dimensions (%d x %d)", system.properties.graphicsProperties.maxSwapchainImageWidth, system.properties.graphicsProperties.maxSwapchainImageHeight);
		// Check that multiview is supported (This should always be supported because XR_MIN_COMPOSITION_LAYERS_SUPPORTED is 16)
		if (system.properties.graphicsProperties.maxLayerCount < 2) {
			DEBUG_LOG("HMD does not support multiview :c");
			return false;
		}

		// Check for proper tracking support
		if (!system.properties.trackingProperties.orientationTracking || !system.properties.trackingProperties.positionTracking) {
			DEBUG_LOG("HMD does not support both position and orientation tracking");
			return false;
		}

		// Get view configuration:
		// TODO: Verify that stereo is available
		u32 viewConfigurationCount;
		xrEnumerateViewConfigurations(instance, system.id, 0, &viewConfigurationCount, nullptr);
		std::vector<XrViewConfigurationType> viewConfigurations(viewConfigurationCount);
		xrEnumerateViewConfigurations(instance, system.id, viewConfigurationCount, &viewConfigurationCount, viewConfigurations.data());

		for (auto& viewConfiguration : viewConfigurations) {
			DEBUG_LOG("Found view configuration: %d", viewConfiguration);
		}

		u32 viewConfigurationViewCount;
		xrEnumerateViewConfigurationViews(instance, system.id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewConfigurationViewCount, nullptr);
		std::vector<XrViewConfigurationView> viewConfigurationViews(viewConfigurationViewCount, { .type = XR_TYPE_VIEW_CONFIGURATION_VIEW });
		xrEnumerateViewConfigurationViews(instance, system.id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewConfigurationViewCount, &viewConfigurationViewCount, viewConfigurationViews.data());

		for (auto& view : viewConfigurationViews) {
			DEBUG_LOG("Found view with recommended dimensions (%d, %d) and swapchain sample count %d", view.recommendedImageRectWidth, view.recommendedImageRectHeight, view.recommendedSwapchainSampleCount);
		}

		// There's technically two views but they should have the same configuration because it's multiview
		system.viewConfigurationView = viewConfigurationViews[0];

		hmdSystem = system;
		return true;
	}

	bool XRInstance::CreateSession(const Rendering::Renderer& renderer) {
		if (hmdSystem.id == XR_NULL_SYSTEM_ID) {
			DEBUG_LOG("HMD not initialized");
			return false;
		}

		if (session != XR_NULL_HANDLE) {
			DEBUG_LOG("A session already exists");
			return false;
		}

		const Rendering::Vulkan* vulkan = renderer.GetImplementation();
		Rendering::Vulkan::XrGraphicsBindingInfo bindingInfo = vulkan->GetXrGraphicsBindingInfo();

		XrGraphicsBindingVulkanKHR graphicsBinding{};
		graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
		graphicsBinding.next = nullptr;
		graphicsBinding.instance = bindingInfo.instance;
		graphicsBinding.physicalDevice = bindingInfo.physicalDevice;
		graphicsBinding.device = bindingInfo.device;
		graphicsBinding.queueFamilyIndex = bindingInfo.queueFamilyIndex;
		graphicsBinding.queueIndex = bindingInfo.queueIndex;

		XrSessionCreateInfo sessionInfo{};
		sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
		sessionInfo.next = &graphicsBinding;
		sessionInfo.createFlags = 0;
		sessionInfo.systemId = hmdSystem.id;

		XrResult result = xrCreateSession(instance, &sessionInfo, &session);
		if (result != XR_SUCCESS) {
			DEBUG_ERROR("Session creation failed with error code %d", result);
		}

		u32 availableSwapchainFormatCount;
		xrEnumerateSwapchainFormats(session, 0, &availableSwapchainFormatCount, nullptr);
		std::vector<s64> availableSwapchainFormats(availableSwapchainFormatCount);
		xrEnumerateSwapchainFormats(session, availableSwapchainFormatCount, &availableSwapchainFormatCount, availableSwapchainFormats.data());

		for (auto& format : availableSwapchainFormats) {
			DEBUG_LOG("Found supported swapchain format %d", format);
		}

		// TODO: Verify that the format we want to use is available
		// TODO: Verify that sample count is ideal
		XrSwapchainCreateInfo swapchainCreateInfo{};
		swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
		swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		swapchainCreateInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
		swapchainCreateInfo.width = hmdSystem.viewConfigurationView.recommendedImageRectWidth;
		swapchainCreateInfo.height = hmdSystem.viewConfigurationView.recommendedImageRectHeight;
		swapchainCreateInfo.faceCount = 1;
		swapchainCreateInfo.arraySize = 2;
		swapchainCreateInfo.mipCount = 1;

		xrCreateSwapchain(session, &swapchainCreateInfo, &swapchain);

		u32 swapchainImageCount;
		xrEnumerateSwapchainImages(swapchain, 0, &swapchainImageCount, nullptr);
		swapchainImages = std::vector<XrSwapchainImageVulkanKHR>(swapchainImageCount, { .type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
		xrEnumerateSwapchainImages(swapchain, swapchainImageCount, &swapchainImageCount, (XrSwapchainImageBaseHeader*)swapchainImages.data());

		DEBUG_LOG("XR swapchain image count = %d", swapchainImageCount);

		return true;
	}

	bool XRInstance::DestroySession() {
		if (session == XR_NULL_HANDLE) {
			DEBUG_LOG("No session to destroy");
			return false;
		}

		swapchainImages.clear();
		xrDestroySwapchain(swapchain);
		swapchain = XR_NULL_HANDLE;

		// TODO: Session seems to have some resources that need to be freed beforehand? Validation layer complains about a command pool that hasn't been destroyed
		// Maybe just need to wait for XR_SESSION_STATE_EXITING?
		xrDestroySession(session);
		session = XR_NULL_HANDLE;
		sessionState = XR_SESSION_STATE_UNKNOWN;

		return true;
	}

	void XRInstance::RequestStartSession() {
		if (requestedSessionState == true) {
			DEBUG_LOG("Session start has already been requested or session is already running");
		}
		requestedSessionState = true;
	}
	void XRInstance::RequestEndSession() {
		if (requestedSessionState == false) {
			DEBUG_LOG("Session stop has already been requested or session is already stopped");
		}
		requestedSessionState = false;
	}
	bool XRInstance::SessionRunning() const {
		return sessionState == XR_SESSION_STATE_SYNCHRONIZED ||
			sessionState == XR_SESSION_STATE_VISIBLE ||
			sessionState == XR_SESSION_STATE_FOCUSED ||
			sessionState == XR_SESSION_STATE_STOPPING;
	}

	// Update loop
	void XRInstance::Update(r32 dt) {
		XrEventDataBuffer event{};
		event.type = XR_TYPE_EVENT_DATA_BUFFER;

		while (xrPollEvent(instance, &event) == XR_SUCCESS) {

			switch (event.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			{
				XrEventDataSessionStateChanged* sessionStateChangedEvent = (XrEventDataSessionStateChanged*)&event;
				sessionState = sessionStateChangedEvent->state;

				switch (sessionState)
				{
				case XR_SESSION_STATE_READY:
				{
					if (requestedSessionState == true) {
						StartSession();
					}
					break;
				}
				case XR_SESSION_STATE_STOPPING:
				{
					if (requestedSessionState == false) {
						EndSession();
					}
					break;
				}
				default:
					break;
				}

				break;
			}
			default:
				break;
			}

			// Reset event to poll again
			event.type = XR_TYPE_EVENT_DATA_BUFFER;
		}
	}

	bool XRInstance::BeginFrame(s64& outPredictedDisplayTime) {
		if (session == XR_NULL_HANDLE) {
			DEBUG_LOG("No session to begin frame");
			return false;
		}

		// Prepare for rendering
		XrFrameWaitInfo frameWaitInfo{};
		frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;

		XrFrameState frameState{};
		frameState.type = XR_TYPE_FRAME_STATE;

		xrWaitFrame(session, &frameWaitInfo, &frameState);

		outPredictedDisplayTime = frameState.predictedDisplayTime;

		XrFrameBeginInfo beginFrameInfo{};
		beginFrameInfo.type = XR_TYPE_FRAME_BEGIN_INFO;

		xrBeginFrame(session, &beginFrameInfo);

		return frameState.shouldRender;
	}

	void XRInstance::GetViewData(const XrView& view, r32 nearClip, r32 farClip, glm::mat4& outView, glm::mat4& outProj, glm::vec3& outPos) {
		const glm::vec3 pos = glm::vec3(view.pose.position.x, view.pose.position.y, view.pose.position.z);
		const glm::quat rot = glm::quat(view.pose.orientation.w, view.pose.orientation.x, view.pose.orientation.y, view.pose.orientation.z);

		const glm::mat4x4 transform = glm::translate(glm::mat4x4(1.0f), pos) * glm::mat4_cast(rot);
		outView = glm::inverse(transform);
		outPos = pos;

		const r32 tLeft = tan(view.fov.angleLeft);
		const r32 tRight = tan(view.fov.angleRight);
		const r32 tUp = tan(view.fov.angleUp);
		const r32 tDown = tan(view.fov.angleDown);

		const r32 depth = farClip - nearClip;

		// I stole this off the internet
		outProj = glm::mat4(0.0f);
		outProj[0][0] = 2.0f / (tRight - tLeft);
		outProj[2][0] = (tRight + tLeft) / (tRight - tLeft);
		outProj[1][1] = 2.0f / (tDown - tUp);
		outProj[2][1] = (tDown + tUp) / (tDown - tUp);
		outProj[2][2] = -farClip / depth;
		outProj[3][2] = -(farClip * nearClip) / depth;
		outProj[2][3] = -1.0f;
	}

	bool XRInstance::GetCameraData(s64 displayTime, r32 nearClip, r32 farClip, Rendering::CameraData& outData) {
		if (session == XR_NULL_HANDLE) {
			DEBUG_LOG("No session to get camera matrices");
			return false;
		}

		XrViewLocateInfo viewLocateInfo{};
		viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
		viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		viewLocateInfo.displayTime = displayTime;
		viewLocateInfo.space = space;

		XrViewState viewState{};
		viewState.type = XR_TYPE_VIEW_STATE;

		u32 viewCount = 2;
		XrView views[2] = {
			{ XR_TYPE_VIEW }, { XR_TYPE_VIEW }
		};
		xrLocateViews(session, &viewLocateInfo, &viewState, viewCount, &viewCount, views);

		GetViewData(views[0], nearClip, farClip, outData.view[0], outData.proj[0], outData.pos[0]);
		GetViewData(views[1], nearClip, farClip, outData.view[1], outData.proj[1], outData.pos[1]);

		return true;
	}

	bool XRInstance::EndFrame(s64 displayTime) {
		if (session == XR_NULL_HANDLE) {
			DEBUG_LOG("No session to end frame");
			return false;
		}

		XrViewLocateInfo viewLocateInfo{};
		viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
		viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		viewLocateInfo.displayTime = displayTime;
		viewLocateInfo.space = space;

		XrViewState viewState{};
		viewState.type = XR_TYPE_VIEW_STATE;

		u32 viewCount = 2;
		XrView views[2] = {
			{ XR_TYPE_VIEW }, { XR_TYPE_VIEW }
		};
		xrLocateViews(session, &viewLocateInfo, &viewState, viewCount, &viewCount, views);

		XrCompositionLayerProjectionView projectedViews[2]{};

		for (int i = 0; i < viewCount; i++)
		{
			projectedViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projectedViews[i].pose = views[i].pose;
			projectedViews[i].fov = views[i].fov;
			projectedViews[i].subImage.swapchain = swapchain;
			projectedViews[i].subImage.imageRect.offset = { 0, 0 };
			projectedViews[i].subImage.imageRect.extent = { (s32)hmdSystem.viewConfigurationView.recommendedImageRectWidth, (s32)hmdSystem.viewConfigurationView.recommendedImageRectHeight };
			projectedViews[i].subImage.imageArrayIndex = i;
		}

		XrCompositionLayerProjection layer{};
		layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		layer.space = space;
		layer.viewCount = viewCount;
		layer.views = projectedViews;

		auto pLayer = (const XrCompositionLayerBaseHeader*)&layer;

		XrFrameEndInfo endFrameInfo{};
		endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
		endFrameInfo.displayTime = displayTime;
		endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		endFrameInfo.layerCount = 1;
		endFrameInfo.layers = &pLayer;

		xrEndFrame(session, &endFrameInfo);

		return true;
	}

	// Vulkan implementation
	bool XRInstance::GetVulkanInstanceRequirements(VulkanInstanceRequirements& outRequirements) const {
		if (hmdSystem.id == XR_NULL_SYSTEM_ID) {
			DEBUG_LOG("HMD has not been initialized");
			return false;
		}

		PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR;
		xrGetInstanceProcAddr(instance, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction*)&xrGetVulkanGraphicsRequirements2KHR);

		XrGraphicsRequirementsVulkan2KHR graphicsRequirements{};
		graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
		xrGetVulkanGraphicsRequirements2KHR(instance, hmdSystem.id, &graphicsRequirements);
		u32 minPatch = graphicsRequirements.minApiVersionSupported & 0xffffffff;
		u32 minMinor = (graphicsRequirements.minApiVersionSupported >> 32) & 0xffff;
		u32 minMajor = (graphicsRequirements.minApiVersionSupported >> 48) & 0xffff;

		DEBUG_LOG("Minimum vulkan API version: %d.%d.%d", minMajor, minMinor, minPatch);

		u32 maxPatch = graphicsRequirements.maxApiVersionSupported & 0xffffffff;
		u32 maxMinor = (graphicsRequirements.maxApiVersionSupported >> 32) & 0xffff;
		u32 maxMajor = (graphicsRequirements.maxApiVersionSupported >> 48) & 0xffff;

		DEBUG_LOG("Maximum vulkan API version: %d.%d.%d", maxMajor, maxMinor, maxPatch);

		PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR;
		xrGetInstanceProcAddr(instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction*)&xrGetVulkanInstanceExtensionsKHR);

		memset(outRequirements.requiredExtensions, 0, REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE);
		u32 requiredVulkanExtensionCount;
		xrGetVulkanInstanceExtensionsKHR(instance, hmdSystem.id, REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE, &requiredVulkanExtensionCount, outRequirements.requiredExtensions);

		DEBUG_LOG("Required Vulkan instance extensions: %s", outRequirements.requiredExtensions);

		outRequirements.minApiVersion = VK_MAKE_VERSION(minMajor, minMinor, minPatch);
		outRequirements.maxApiVersion = VK_MAKE_VERSION(maxMajor, maxMinor, maxPatch);

		return true;
	}

	VkInstance XRInstance::GetVulkanInstance(VkInstanceCreateInfo* vulkanCreateInfo) const {
		if (hmdSystem.id == XR_NULL_SYSTEM_ID) {
			DEBUG_LOG("HMD has not been initialized");
			return VK_NULL_HANDLE;
		}

		PFN_xrCreateVulkanInstanceKHR xrCreateVulkanInstanceKHR;
		XrResult err = xrGetInstanceProcAddr(instance, "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction*)&xrCreateVulkanInstanceKHR);
		if (err != XR_SUCCESS) {
			DEBUG_ERROR("Failed to get xrCreateVulkanInstanceKHR proc address with code %d", err);
		}

		XrVulkanInstanceCreateInfoKHR createInfo{};
		createInfo.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
		createInfo.next = nullptr;
		createInfo.systemId = hmdSystem.id;
		createInfo.createFlags = 0;
		createInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
		createInfo.vulkanCreateInfo = vulkanCreateInfo;
		createInfo.vulkanAllocator = nullptr;

		VkInstance result;
		VkResult vkErr;
		err = xrCreateVulkanInstanceKHR(instance, &createInfo, &result, &vkErr);
		if (err != XR_SUCCESS) {
			DEBUG_ERROR("Vulkan instance creation failed with XR error code %d", err);
		} else if (vkErr != VK_SUCCESS) {
			DEBUG_ERROR("Vulkan instance creation failed with Vulkan error code %d", err);
		}

		return result;
	}

	VkPhysicalDevice XRInstance::GetVulkanPhysicalDevice(const VkInstance vkInstance) const {
		if (hmdSystem.id == XR_NULL_SYSTEM_ID) {
			DEBUG_LOG("HMD has not been initialized");
			return VK_NULL_HANDLE;
		}

		PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR;
		xrGetInstanceProcAddr(instance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction*)&xrGetVulkanGraphicsDeviceKHR);

		VkPhysicalDevice result;
		XrResult err = xrGetVulkanGraphicsDeviceKHR(instance, hmdSystem.id, vkInstance, &result);
			if (err != XR_SUCCESS) {
				DEBUG_ERROR("Failed to get physical device with error code %d", err);
			}

		return result;
	}

	VkDevice XRInstance::GetVulkanLogicalDevice(VkPhysicalDevice vulkanPhysicalDevice, VkDeviceCreateInfo* vulkanCreateInfo) const {
		PFN_xrCreateVulkanDeviceKHR xrCreateVulkanDeviceKHR;
		xrGetInstanceProcAddr(instance, "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction*)&xrCreateVulkanDeviceKHR);

		XrVulkanDeviceCreateInfoKHR createInfo{};
		createInfo.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
		createInfo.next = nullptr;
		createInfo.systemId = hmdSystem.id;
		createInfo.createFlags = 0;
		createInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
		createInfo.vulkanPhysicalDevice = vulkanPhysicalDevice;
		createInfo.vulkanCreateInfo = vulkanCreateInfo;
		createInfo.vulkanAllocator = nullptr;

		VkDevice result;
		VkResult vkErr;
		XrResult err = xrCreateVulkanDeviceKHR(instance, &createInfo, &result, &vkErr);
		if (err != XR_SUCCESS) {
			DEBUG_ERROR("Vulkan logical device creation failed with XR error code %d", err);
		} else if (vkErr != VK_SUCCESS) {
			DEBUG_ERROR("Vulkan logical device creation failed with Vulkan error code %d", err);
		}

		return result;
	}

	bool XRInstance::GetVulkanDeviceRequirements(VulkanDeviceRequirements& outRequirements) const {
		if (hmdSystem.id == XR_NULL_SYSTEM_ID) {
			DEBUG_LOG("HMD has not been initialized");
			return false;
		}

		PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR;
		xrGetInstanceProcAddr(instance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction*)&xrGetVulkanDeviceExtensionsKHR);

		u32 requiredExtensionCount;
		xrGetVulkanDeviceExtensionsKHR(instance, hmdSystem.id, REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE, &requiredExtensionCount, outRequirements.requiredExtensions);

		DEBUG_LOG("%d required Vulkan device extensions: %s", requiredExtensionCount, outRequirements.requiredExtensions);

		return true;
	}

	bool XRInstance::GetVulkanSwapchainImages(VkImage* outImages, u32& outImageCount) const {
		if (session == XR_NULL_HANDLE) {
			DEBUG_LOG("No session exists, cannot get swapchain images");
			return false;
		}

		outImageCount = swapchainImages.size();

		if (outImages != nullptr) {
			for (int i = 0; i < outImageCount; i++) {
				outImages[i] = swapchainImages[i].image;
			}
		}

		return true;
	}

	bool XRInstance::GetSwapchainDimensions(u32& outWidth, u32& outHeight) const {
		if (hmdSystem.id == XR_NULL_SYSTEM_ID) {
			DEBUG_LOG("HMD has not been initialized");
			return false;
		}

		outWidth = hmdSystem.viewConfigurationView.recommendedImageRectWidth;
		outHeight = hmdSystem.viewConfigurationView.recommendedImageRectHeight;

		return true;
	}
	bool XRInstance::GetNextSwapchainImage(u32& outIndex) const {
		if (swapchain == XR_NULL_HANDLE) {
			//DEBUG_LOG("Swapchain has not been created");
			return false;
		}

		XrSwapchainImageAcquireInfo acquireImageInfo{};
		acquireImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

		xrAcquireSwapchainImage(swapchain, &acquireImageInfo, &outIndex);

		XrSwapchainImageWaitInfo waitImageInfo{};
		waitImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
		waitImageInfo.timeout = XR_INFINITE_DURATION;

		xrWaitSwapchainImage(swapchain, &waitImageInfo);

		return true;
	}

	bool XRInstance::ReleaseSwapchainImage() const {
		if (swapchain == XR_NULL_HANDLE) {
			return false;
		}

		XrSwapchainImageReleaseInfo releaseImageInfo{};
		releaseImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;

		XrResult result = xrReleaseSwapchainImage(swapchain, &releaseImageInfo);

		return true;
	}

	// Private
	void XRInstance::StartSession() {
		if (session == XR_NULL_HANDLE) {
			DEBUG_ERROR("Session has not been created");
			return;
		}

		XrSessionBeginInfo sessionBeginInfo{};
		sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
		sessionBeginInfo.next = nullptr;
		sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

		XrResult result = xrBeginSession(session, &sessionBeginInfo);

		if (result != XR_SUCCESS) {
			DEBUG_ERROR("Starting session failed with error code %d", result);
		}

		XrReferenceSpaceCreateInfo spaceCreateInfo{};
		spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
		spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		spaceCreateInfo.poseInReferenceSpace = { { 0, 0, 0, 1 }, { 0, 0, 0 } };

		result = xrCreateReferenceSpace(session, &spaceCreateInfo, &space);
	}
	void XRInstance::EndSession() {
		if (session == XR_NULL_HANDLE) {
			DEBUG_ERROR("Session has not been created");
			return;
		}

		XrResult result = xrDestroySpace(space);
		space = XR_NULL_HANDLE;

		result = xrEndSession(session);

		if (result != XR_SUCCESS) {
			DEBUG_ERROR("Ending session failed with error code %d", result);
		}
	}

	// DEBUG
#ifdef NEKRO_DEBUG
	void XRInstance::SetupDebugLogging() {
		XrDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
		createInfo.userCallback = DebugCallback;
		createInfo.userData = nullptr;

		PFN_xrCreateDebugUtilsMessengerEXT xrCreateDebugUtilsMessengerEXT;
		xrGetInstanceProcAddr(instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)&xrCreateDebugUtilsMessengerEXT);

		XrResult result = xrCreateDebugUtilsMessengerEXT(instance, &createInfo, &debugMessenger);

		if (result != XR_SUCCESS) {
			DEBUG_ERROR("Failed to create debug messenger with error code %d", result);
		}
	}
	XrBool32 XRInstance::DebugCallback(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type, const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {
		const char* typeStr;
		const char* severityStr;

		switch (type)
		{
		case XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
			typeStr = "general";
			break;
		case XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
			typeStr = "validation";
			break;
		case XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
			typeStr = "performance";
			break;
		case XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT:
			typeStr = "conformance";
			break;
		default:
			typeStr = "unknown";
			break;
		}

		switch (severity)
		{
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			severityStr = "verbose";
			break;
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			severityStr = "info";
			break;
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			severityStr = "warning";
			break;
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			severityStr = "error";
			break;
		default:
			severityStr = "unknown";
			break;
		}

		if (severity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			DEBUG_ERROR("OpenXr %s (%s): %s", typeStr, severityStr, callbackData->message);
		}
		else {
			DEBUG_LOG("OpenXr %s (%s): %s", typeStr, severityStr, callbackData->message);
		}

		return XR_FALSE;
	}
	void XRInstance::CleanupDebugLogging() {
		PFN_xrDestroyDebugUtilsMessengerEXT xrDestroyDebugUtilsMessengerEXT;
		xrGetInstanceProcAddr(instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)&xrDestroyDebugUtilsMessengerEXT);

		xrDestroyDebugUtilsMessengerEXT(debugMessenger);
	}
#endif
}