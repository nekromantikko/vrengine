#pragma once
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "typedef.h"
#include <vector>
#include "rendering.h"

namespace Rendering {
	class Renderer;
}

namespace XR {

#define REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE 1024
	struct VulkanInstanceRequirements {
		u32 minApiVersion;
		u32 maxApiVersion;

		// Space separated list of required instance extension names
		char requiredExtensions[REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE];
	};

	struct VulkanDeviceRequirements {
		// Space separated list of required device extension names
		char requiredExtensions[REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE];
	};

	enum HandIndex {
		VR_HAND_LEFT = 0,
		VR_HAND_RIGHT = 1
	};

	class XRInstance {
	public:
		XRInstance(android_app *app);
		~XRInstance();

		bool InitializeHMD();

		bool CreateSession(const Rendering::Renderer& renderer);
		bool DestroySession();
		void RequestStartSession();
		void RequestEndSession();
		bool SessionRunning() const;

		bool GetControllerGeometry(HandIndex hand, u8** outBuffer, u32& outSize);
		void Update(r32 dt);
		bool BeginFrame(s64& outPredictedDisplayTime);
		bool GetCameraData(s64 displayTime, r32 nearClip, r32 farClip, Rendering::CameraData& outData);
		bool GetHandTransform(s64 displayTime, HandIndex hand, glm::mat4& outTransform);
		bool GetSpaceDimensions(r32& outWidth, r32& outHeight);
		bool EndFrame(s64 displayTime);

		// Vulkan implementation
		bool GetVulkanInstanceRequirements(VulkanInstanceRequirements& outRequirements) const;
		VkInstance GetVulkanInstance(VkInstanceCreateInfo* vulkanCreateInfo) const;
		VkPhysicalDevice GetVulkanPhysicalDevice(const VkInstance instance) const;
		VkDevice GetVulkanLogicalDevice(VkPhysicalDevice vulkanPhysicalDevice, VkDeviceCreateInfo* vulkanCreateInfo) const;
		bool GetVulkanDeviceRequirements(VulkanDeviceRequirements& outRequirements) const;
		bool GetVulkanSwapchainImages(VkImage* outImages, u32& outImageCount) const;

		// Generic swapchain thingz
		bool GetSwapchainDimensions(u32& outWidth, u32& outHeight) const;
		bool GetNextSwapchainImage(u32& outIndex) const;
		bool ReleaseSwapchainImage() const;
	private:
		struct System {
			XrSystemId id = XR_NULL_SYSTEM_ID;
			XrSystemProperties properties;
			XrViewConfigurationView viewConfigurationView;
		};

		void StartSession();
		void EndSession();

		void GetViewData(const XrView& view, r32 nearClip, r32 farClip, glm::mat4& outView, glm::mat4& outProj, glm::vec3& outPos);

		XrInstance instance;
		System hmdSystem;

		XrSession session = XR_NULL_HANDLE;
		XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
		// True = running, false = stopped
		b32 requestedSessionState = false;

		XrSpace space = XR_NULL_HANDLE;

		XrActionSet actionSet = XR_NULL_HANDLE;
		XrSpace leftHandSpace = XR_NULL_HANDLE;
		XrAction leftHandAction = XR_NULL_HANDLE;
		XrSpace rightHandSpace = XR_NULL_HANDLE;
		XrAction rightHandAction = XR_NULL_HANDLE;

		XrSwapchain swapchain = XR_NULL_HANDLE;
		std::vector<XrSwapchainImageVulkanKHR> swapchainImages;

#ifdef NEKRO_DEBUG
		void SetupDebugLogging();
		static XrBool32 DebugCallback(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type, const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData);
		void CleanupDebugLogging();
		XrDebugUtilsMessengerEXT debugMessenger;
#endif
	};
}