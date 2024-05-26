#include "vulkan.h"
#include "system.h"
#include "math.h"
#include <cstring>

namespace Rendering {
	Vulkan::Vulkan(const XR::XRInstance* const xrInstance) {
		DEBUG_LOG("Initializing vulkan...");

		XR::VulkanInstanceRequirements instanceXrRequirements{};
		xrInstance->GetVulkanInstanceRequirements(instanceXrRequirements);
		if (instanceXrRequirements.minApiVersion > VK_API_VERSION_1_1 || instanceXrRequirements.maxApiVersion < VK_API_VERSION_1_1) {
			DEBUG_ERROR("Vulkan 1.1 not supported by XR requirements!");
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Test";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
		appInfo.pEngineName = "Nekro Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_1;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledLayerCount = 0;
		createInfo.enabledExtensionCount = 0;

		u32 availableLayerCount;
		vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
		std::vector<VkLayerProperties> availableLayerProperties(availableLayerCount);
		vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayerProperties.data());

		for (auto layer : availableLayerProperties) {
				DEBUG_LOG("Available layer: %s", layer.layerName);
		}

		// Enable validation layers for debug
#ifdef NEKRO_DEBUG
		const char* validationLayer = "VK_LAYER_KHRONOS_validation";
		createInfo.enabledLayerCount = 1;
		createInfo.ppEnabledLayerNames = &validationLayer;
#endif 

		u32 availableExtensionCount;
		vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensionProperties(availableExtensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, availableExtensionProperties.data());

		for (auto extension : availableExtensionProperties) {
			DEBUG_LOG("Available instance extension: %s", extension.extensionName);
		}

		// TODO: Check that extensions are actually available
		std::vector<const char*> extensionNames;
		// Parse required extensions
		char nullSeparatedExtensionNames[REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE];
		strcpy(nullSeparatedExtensionNames, instanceXrRequirements.requiredExtensions);
		char delimiter[2] = { 0x20, 0 };
		char* token = strtok(nullSeparatedExtensionNames, delimiter);
		for (int i = 0; token != nullptr; i++, token = strtok(nullptr, delimiter)) {
			extensionNames.push_back(token);
		}

		createInfo.enabledExtensionCount = extensionNames.size();
		createInfo.ppEnabledExtensionNames = (char**)extensionNames.data();

		vkInstance = xrInstance->GetVulkanInstance(&createInfo);
		/*VkResult result = vkCreateInstance(&createInfo, nullptr, &vkInstance);
		if (result != VK_SUCCESS) {
				DEBUG_ERROR("Instance creation failed with error code %d", result);
		}*/

		// Get proc addresses
		vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(vkInstance, "vkGetPhysicalDeviceProperties2");
		vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(vkInstance, "vkGetPhysicalDeviceFeatures2");
		vkGetImageMemoryRequirements2 = (PFN_vkGetImageMemoryRequirements2)vkGetInstanceProcAddr(vkInstance, "vkGetImageMemoryRequirements2");
		vkGetBufferMemoryRequirements2 = (PFN_vkGetBufferMemoryRequirements2)vkGetInstanceProcAddr(vkInstance, "vkGetBufferMemoryRequirements2");

		VkPhysicalDevice physicalDeviceCandidate = xrInstance->GetVulkanPhysicalDevice(vkInstance);
		u32 queueFamilyIndex;
		if (!IsPhysicalDeviceSuitable(physicalDeviceCandidate, queueFamilyIndex)) {
			DEBUG_ERROR("Physical device picked by OpenXR not suitable");
		}
		physicalDevice = physicalDeviceCandidate;
		primaryQueueFamilyIndex = queueFamilyIndex;

		vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceInfo.properties);
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceInfo.memProperties);
		DEBUG_LOG("Using physical device %s", physicalDeviceInfo.properties.deviceName);
		DEBUG_LOG("MAX UNIFORM BUFFER RANGE = %d", physicalDeviceInfo.properties.limits.maxUniformBufferRange);
		DEBUG_LOG("MAX STORAGE BUFFER RANGE = %d", physicalDeviceInfo.properties.limits.maxStorageBufferRange);
		for (int i = 0; i < physicalDeviceInfo.memProperties.memoryTypeCount; i++) {
			DEBUG_LOG("Device supports memory type %d", physicalDeviceInfo.memProperties.memoryTypes[i]);
		}

		CreateLogicalDevice(xrInstance);
		vkGetDeviceQueue(device, primaryQueueFamilyIndex, 0, &primaryQueue);

		xrInstance->GetSwapchainDimensions(xrEyeImageWidth, xrEyeImageHeight);

		CreateRenderPasses();

		// Likely overkill pool sizes
		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo{};
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorPoolInfo.poolSizeCount = 11;
		descriptorPoolInfo.pPoolSizes = poolSizes;
		descriptorPoolInfo.maxSets = 1000;

		vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool);

		CreateUniformBuffers();
		CreatePerInstanceBuffers();
		CreateFrameData();

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = 0;
		poolInfo.queueFamilyIndex = primaryQueueFamilyIndex;

		if (vkCreateCommandPool(device, &poolInfo, nullptr, &tempCommandPool) != VK_SUCCESS) {
			DEBUG_ERROR("failed to create general command pool!");
		}

		CreateFramebuffer();
	}
	Vulkan::~Vulkan() {
		// Wait for all commands to execute first
		WaitForAllCommands();

		vkDestroyCommandPool(device, tempCommandPool, nullptr);

		// Free all user-created resources
		PoolHandle<Texture> texHandle;
		while (textures.GetHandle(0, texHandle)) {
			FreeTexture((TextureHandle)texHandle.Raw());
		}

		PoolHandle<Mesh> meshHandle;
		while (meshes.GetHandle(0, meshHandle)) {
			FreeMesh((MeshHandle)meshHandle.Raw());
		}

		PoolHandle<Shader> shaderHandle;
		while (shaders.GetHandle(0, shaderHandle)) {
			FreeShader((ShaderHandle)shaderHandle.Raw());
		}

		PoolHandle<Material> matHandle;
		while (materials.GetHandle(0, matHandle)) {
			FreeMaterial((MaterialHandle)matHandle.Raw());
		}

		FreeFramebuffer();

		FreeFrameData();
		FreePerInstanceBuffers();
		FreeUniformBuffers();

		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		FreeRenderPasses();
		vkDestroyDevice(device, nullptr);
		vkDestroyInstance(vkInstance, nullptr);
	}

	void Vulkan::CreateXRSwapchain(const XR::XRInstance* const xrInstance) {
		if (xrInstance == nullptr) {
			DEBUG_LOG("XR instance is null, ignoring");
			return;
		}

		if (!xrSwapchainImages.empty()) {
			DEBUG_ERROR("XR swapchain already exists");
		}

		u32 xrSwapchainImageCount;
		if (!xrInstance->GetVulkanSwapchainImages(nullptr, xrSwapchainImageCount)) {
			DEBUG_LOG("XR swapchain not available, ignoring");
			return;
		}
		std::vector<VkImage> images(xrSwapchainImageCount);
		xrInstance->GetVulkanSwapchainImages(images.data(), xrSwapchainImageCount);

		xrSwapchainImages = std::vector<SwapchainImage>(xrSwapchainImageCount);
		// Create image views and framebuffers
		for (u32 i = 0; i < xrSwapchainImageCount; i++) {
			SwapchainImage& swap = xrSwapchainImages[i];
			swap.image = images[i];

			VkImageViewCreateInfo imageViewCreateInfo{};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.image = swap.image;
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
			imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = 1;
			imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
			imageViewCreateInfo.subresourceRange.layerCount = 2;

			VkResult result = vkCreateImageView(device, &imageViewCreateInfo, nullptr, &swap.view);
			if (result != VK_SUCCESS) {
				DEBUG_ERROR("Failed to create image view!");
			}
		}
	}

	void Vulkan::WaitForAllCommands() {
		vkQueueWaitIdle(primaryQueue);
	}

	bool Vulkan::IsPhysicalDeviceSuitable(VkPhysicalDevice physicalDevice, u32& outQueueFamilyIndex) {
		// Check if device has correct properties
		VkPhysicalDeviceMultiviewProperties multiviewProps{};
		multiviewProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;

		VkPhysicalDeviceProperties2 deviceProperties{};
		deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		deviceProperties.pNext = &multiviewProps;

		vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);

		// Check if device has all the features needed
		VkPhysicalDeviceMultiviewFeatures multiview{};
		multiview.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
		multiview.pNext = nullptr;

		VkPhysicalDeviceFeatures2 deviceFeatures{};
		deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		deviceFeatures.pNext = &multiview;

		vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures);

		// TODO: Check multiview max instance count
		bool hasMultiviewSupport = multiview.multiview && multiviewProps.maxMultiviewViewCount >= 2;

		if (!hasMultiviewSupport) {
			return false;
		}

		u32 extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

		bool hasSwapchainSupport = false;
		bool hasFormatListSupport = false;
		bool hasImagelessSupport = false;
		for (const auto& extension : availableExtensions) {
			if (!hasSwapchainSupport && strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
				hasSwapchainSupport = true;
			}
			else if (!hasImagelessSupport && strcmp(extension.extensionName, "VK_KHR_imageless_framebuffer") == 0) {
				hasImagelessSupport = true;
			}
			else if (!hasFormatListSupport && strcmp(extension.extensionName, "VK_KHR_image_format_list") == 0) {
				hasFormatListSupport = true;
			}
		}

		if (!hasSwapchainSupport || !hasImagelessSupport || !hasFormatListSupport) {
			return false;
		}

		u32 queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

		for (u32 i = 0; i < queueFamilyCount; i++) {
			const VkQueueFamilyProperties& queueFamily = queueFamilies.at(i);

			if (queueFamily.queueCount == 0) {
				continue;
			}

			// For now, to keep things simple I want to use one queue for everything, so the family needs to support all of these:
			if ((queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))) {
				outQueueFamilyIndex = i;
				return true;
			}
		}

		return false;
	}

	void Vulkan::CreateLogicalDevice(const XR::XRInstance* const xrInstance) {
		float queuePriority = 1.0f;

		VkDeviceQueueCreateInfo queueCreateInfo;
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.pNext = nullptr;
		queueCreateInfo.flags = 0;
		queueCreateInfo.queueFamilyIndex = primaryQueueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceImagelessFramebufferFeatures imagelessFeatures{};
		imagelessFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES;
		imagelessFeatures.pNext = nullptr;
		imagelessFeatures.imagelessFramebuffer = true;

		VkPhysicalDeviceMultiviewFeatures multiview{};
		multiview.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
		multiview.pNext = &imagelessFeatures;
		multiview.multiview = true;
		multiview.multiviewGeometryShader = false;
		multiview.multiviewTessellationShader = false;

		VkPhysicalDeviceFeatures2 deviceFeatures{};
		deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		deviceFeatures.pNext = &multiview;
		deviceFeatures.features = VkPhysicalDeviceFeatures{};

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext = &deviceFeatures;
		createInfo.flags = 0;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.pEnabledFeatures = nullptr;
		createInfo.enabledExtensionCount = 0;

		// TODO: Check that extensions are actually available
		std::vector<const char*> extensionNames;
		if (xrInstance != nullptr) {
			XR::VulkanDeviceRequirements deviceXrRequirements{};
			xrInstance->GetVulkanDeviceRequirements(deviceXrRequirements);

			// Parse required extensions
			char nullSeparatedExtensionNames[REQUIRED_VULKAN_EXTENSION_NAME_BUFFER_SIZE];
			strcpy(nullSeparatedExtensionNames, deviceXrRequirements.requiredExtensions);
			char delimiter[2] = { 0x20, 0 };
			char* token = strtok(nullSeparatedExtensionNames, delimiter);
			for (int i = 0; token != nullptr; i++, token = strtok(nullptr, delimiter)) {
				extensionNames.push_back(token);
			}

			createInfo.enabledExtensionCount = extensionNames.size();
		}
		
		extensionNames.push_back("VK_KHR_image_format_list");
		extensionNames.push_back("VK_KHR_imageless_framebuffer");
		createInfo.enabledExtensionCount += 2;

		createInfo.ppEnabledExtensionNames = (char**)extensionNames.data();

		device = xrInstance->GetVulkanLogicalDevice(physicalDevice, &createInfo);
		/*VkResult err = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to create logical device!");
		}*/
	}

	void Vulkan::CreateForwardRenderPass() {
		// Multipass
		const u32 viewMask = 0b00000011;
		const u32 correlationMask = 0b00000011;

		VkRenderPassMultiviewCreateInfo multiviewInfo{};
		multiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
		multiviewInfo.pNext = nullptr;
		multiviewInfo.subpassCount = 1;
		multiviewInfo.pViewMasks = &viewMask;
		multiviewInfo.dependencyCount = 0;
		multiviewInfo.pViewOffsets = nullptr;
		multiviewInfo.correlationMaskCount = 1;
		multiviewInfo.pCorrelationMasks = &correlationMask;

		VkRenderPassCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		createInfo.pNext = &multiviewInfo;
		createInfo.attachmentCount = 3;

		VkAttachmentDescription attachmentDescription{};
		attachmentDescription.flags = 0;
		attachmentDescription.format = VK_FORMAT_R8G8B8A8_SRGB;
		attachmentDescription.samples = VK_SAMPLE_COUNT_4_BIT;
		attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; //temporary: clear before drawing. Change later to VK_ATTACHMENT_LOAD_OP_LOAD
		attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		//attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		//attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; //this is also temporary for testing purposes
		attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription depthDescription{};
		depthDescription.flags = 0;
		depthDescription.format = VK_FORMAT_D32_SFLOAT;
		depthDescription.samples = VK_SAMPLE_COUNT_4_BIT;
		depthDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthDescription.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription colorResolveDescription{};
		colorResolveDescription.flags = 0;
		colorResolveDescription.format = VK_FORMAT_R8G8B8A8_SRGB;
		colorResolveDescription.format = VK_FORMAT_R8G8B8A8_SRGB;
		colorResolveDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		colorResolveDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // guy on youtube told me to clear these resolve attachments
		colorResolveDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorResolveDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorResolveDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorResolveDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorResolveDescription.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription attachments[3] = { attachmentDescription, depthDescription, colorResolveDescription };

		createInfo.pAttachments = attachments;
		createInfo.subpassCount = 1;

		VkAttachmentReference colorAttachmentReference{};
		colorAttachmentReference.attachment = 0;
		colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentReference{};
		depthAttachmentReference.attachment = 1;
		depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorResolveDescriptionRef{};
		colorResolveDescriptionRef.attachment = 2;
		colorResolveDescriptionRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpassDescription{};
		subpassDescription.flags = 0;
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.inputAttachmentCount = 0;
		subpassDescription.pInputAttachments = nullptr;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentReference;
		subpassDescription.pResolveAttachments = &colorResolveDescriptionRef;
		subpassDescription.pDepthStencilAttachment = &depthAttachmentReference;
		subpassDescription.preserveAttachmentCount = 0;
		subpassDescription.pPreserveAttachments = nullptr;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		createInfo.pSubpasses = &subpassDescription;
		createInfo.dependencyCount = 1;
		createInfo.pDependencies = &dependency;

		VkResult err = vkCreateRenderPass(device, &createInfo, nullptr, &forwardRenderPass);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("failed to create render pass!");
		}
	}

	void Vulkan::CreateRenderPasses() {
		CreateForwardRenderPass();
	}

	void Vulkan::FreeRenderPasses()
	{
		vkDestroyRenderPass(device, finalBlitRenderPass, nullptr);
		vkDestroyRenderPass(device, forwardRenderPass, nullptr);
	}

	void Vulkan::CreateFramebufferAttachments() {
		// Color attachment (multisampled)
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.flags = 0;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { xrEyeImageWidth, xrEyeImageHeight, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 2;
		imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_4_BIT;

		vkCreateImage(device, &imageInfo, nullptr, &colorAttachment.image);

		AllocateImage(colorAttachment.image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorAttachment.memory); // Use VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT if available

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = colorAttachment.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 2;

		vkCreateImageView(device, &viewInfo, nullptr, &colorAttachment.view);

		// Depth attachment (multisampled)
		imageInfo.format = VK_FORMAT_D32_SFLOAT;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_4_BIT;
		vkCreateImage(device, &imageInfo, nullptr, &depthAttachment.image);
		AllocateImage(depthAttachment.image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthAttachment.memory); // Use VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT if available
		viewInfo.format = VK_FORMAT_D32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.image = depthAttachment.image;
		vkCreateImageView(device, &viewInfo, nullptr, &depthAttachment.view);
	}
	void Vulkan::FreeFramebufferAttachments() {
		vkDestroyImageView(device, colorAttachment.view, nullptr);
		vkDestroyImage(device, colorAttachment.image, nullptr);
		vkFreeMemory(device, colorAttachment.memory, nullptr);

		vkDestroyImageView(device, depthAttachment.view, nullptr);
		vkDestroyImage(device, depthAttachment.image, nullptr);
		vkFreeMemory(device, depthAttachment.memory, nullptr);
	}
	void Vulkan::CreateFramebuffer() {
		CreateFramebufferAttachments();

		VkFormat colorAttachmentFormat = VK_FORMAT_R8G8B8A8_SRGB;
		VkFormat depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

		VkFramebufferAttachmentImageInfo attachmentInfo[3]{};

		attachmentInfo[0].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
		attachmentInfo[0].pNext = nullptr;
		attachmentInfo[0].flags = 0;
		attachmentInfo[0].usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		attachmentInfo[0].width = xrEyeImageWidth;
		attachmentInfo[0].height = xrEyeImageHeight;
		attachmentInfo[0].layerCount = 2;
		attachmentInfo[0].viewFormatCount = 1;
		attachmentInfo[0].pViewFormats = &colorAttachmentFormat;

		attachmentInfo[1].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
		attachmentInfo[1].pNext = nullptr;
		attachmentInfo[1].flags = 0;
		attachmentInfo[1].usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		attachmentInfo[1].width = xrEyeImageWidth;
		attachmentInfo[1].height = xrEyeImageHeight;
		attachmentInfo[1].layerCount = 2;
		attachmentInfo[1].viewFormatCount = 1;
		attachmentInfo[1].pViewFormats = &depthAttachmentFormat;

		attachmentInfo[2].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
		attachmentInfo[2].pNext = nullptr;
		attachmentInfo[2].flags = 0;
		attachmentInfo[2].usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		attachmentInfo[2].width = xrEyeImageWidth;
		attachmentInfo[2].height = xrEyeImageHeight;
		attachmentInfo[2].layerCount = 2;
		attachmentInfo[2].viewFormatCount = 1;
		attachmentInfo[2].pViewFormats = &colorAttachmentFormat;

		VkFramebufferAttachmentsCreateInfo attachmentsCreateInfo{};
		attachmentsCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
		attachmentsCreateInfo.pNext = nullptr;
		attachmentsCreateInfo.attachmentImageInfoCount = 3;
		attachmentsCreateInfo.pAttachmentImageInfos = attachmentInfo;

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.pNext = &attachmentsCreateInfo;
		framebufferInfo.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
		framebufferInfo.renderPass = forwardRenderPass;
		framebufferInfo.attachmentCount = 3;
		framebufferInfo.pAttachments = nullptr;
		framebufferInfo.width = xrEyeImageWidth;
		framebufferInfo.height = xrEyeImageHeight;
		framebufferInfo.layers = 1;

		VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer);
		if (result != VK_SUCCESS) {
			DEBUG_ERROR("Failed to create framebuffer!");
		}
	}
	void Vulkan::FreeFramebuffer() {
		FreeFramebufferAttachments();
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	}
	void Vulkan::CreateFrameData() {
		for (int i = 0; i < maxFramesInFlight; i++) {
			FrameData& frame = frames[i];

			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = primaryQueueFamilyIndex;

			if (vkCreateCommandPool(device, &poolInfo, nullptr, &frame.cmdPool) != VK_SUCCESS) {
				DEBUG_ERROR("failed to create primary command pool!");
			}

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = frame.cmdPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			if (vkAllocateCommandBuffers(device, &allocInfo, &frame.cmdBuffer) != VK_SUCCESS) {
				DEBUG_ERROR("failed to allocate render command buffe!");
			}

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			vkCreateFence(device, &fenceInfo, nullptr, &frame.cmdFence);
		}
	}
	void Vulkan::FreeFrameData() {
		for (int i = 0; i < maxFramesInFlight; i++) {
			const FrameData& frame = frames[i];

			vkDestroyFence(device, frame.cmdFence, nullptr);
			vkFreeCommandBuffers(device, frame.cmdPool, 1, &frame.cmdBuffer);

			vkDestroyCommandPool(device, frame.cmdPool, nullptr);
		}
	}
	VkDeviceSize PadUniformBufferSize(VkDeviceSize originalSize, const VkDeviceSize minAlignment) {
		VkDeviceSize result = originalSize;
		if (minAlignment > 0) {
			result = (originalSize + minAlignment - 1) & ~(minAlignment - 1);
		}
		return result;
	}

	void Vulkan::CreateUniformBuffers() {
		const VkDeviceSize minUniformBufferOffsetAlignment = physicalDeviceInfo.properties.limits.minUniformBufferOffsetAlignment;

		cameraDataSize = PadUniformBufferSize(sizeof(CameraData), minUniformBufferOffsetAlignment);
		lightingDataSize = PadUniformBufferSize(sizeof(LightingData), minUniformBufferOffsetAlignment);
		shaderDataElementSize = PadUniformBufferSize(maxShaderDataBlockSize, minUniformBufferOffsetAlignment);
		shaderDataSize = shaderDataElementSize * maxMaterialCount;

		uniformDataSize = cameraDataSize + lightingDataSize + shaderDataSize;

		AllocateBuffer(uniformDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, uniformHostBuffer); // If this doesn't work, use VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		AllocateBuffer(uniformDataSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, uniformDeviceBuffer);

		cameraDataOffset = 0;
		lightingDataOffset = cameraDataOffset + cameraDataSize;
		shaderDataOffset = lightingDataOffset + lightingDataSize;

		vkMapMemory(device, uniformHostBuffer.memory, 0, uniformDataSize, 0, (void**)&pHostVisibleUniformData);
	}
	void Vulkan::FreeUniformBuffers() {
		vkUnmapMemory(device, uniformHostBuffer.memory);

		FreeBuffer(uniformDeviceBuffer);
		FreeBuffer(uniformHostBuffer);
	}
	void Vulkan::CreatePerInstanceBuffers() {
		const VkDeviceSize minUniformBufferOffsetAlignment = physicalDeviceInfo.properties.limits.minUniformBufferOffsetAlignment;

		instanceDataElementSize = PadUniformBufferSize(sizeof(PerInstanceData), minUniformBufferOffsetAlignment);
		instanceDataSize = instanceDataElementSize * maxInstanceCount;
		DEBUG_LOG("PER INSTANCE ELEMENT SIZE: %d, FULL SIZE: %d", instanceDataElementSize, instanceDataSize);

		AllocateBuffer(instanceDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, instanceHostBuffer); // If this doesn't work, use VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		AllocateBuffer(instanceDataSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, instanceDeviceBuffer);

		vkMapMemory(device, instanceHostBuffer.memory, 0, instanceDataSize, 0, (void**)&pHostVisibleInstanceData);
	}
	void Vulkan::FreePerInstanceBuffers() {

	}

	s32 Vulkan::GetDeviceMemoryTypeIndex(u32 typeFilter, VkMemoryPropertyFlags propertyFlags) {
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
			if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags) {
				return (s32)i;
			}
		}

		return -1;
	}

	VkCommandBuffer Vulkan::GetTemporaryCommandBuffer() {
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = tempCommandPool;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

		return commandBuffer;
	}

	void Vulkan::AllocateMemory(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& outMemory) {
		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = requirements.size;
		memAllocInfo.memoryTypeIndex = GetDeviceMemoryTypeIndex(requirements.memoryTypeBits, properties);

		VkResult err = vkAllocateMemory(device, &memAllocInfo, nullptr, &outMemory);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to allocate memory with error code %d", err);
		}
	}

    VkDeviceSize Vulkan::AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, Buffer& outBuffer) {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.pNext = nullptr;
		bufferInfo.flags = 0;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult err = vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer.buffer);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to create buffer!\n");
		}

		VkBufferMemoryRequirementsInfo2 requirementsInfo{};
		requirementsInfo.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
		requirementsInfo.pNext = nullptr;
		requirementsInfo.buffer = outBuffer.buffer;

		VkMemoryDedicatedRequirements dedicated{};
		dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
		dedicated.pNext = nullptr;

		VkMemoryRequirements2 memRequirements{};
		memRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		memRequirements.pNext = &dedicated;

		vkGetBufferMemoryRequirements2(device, &requirementsInfo, &memRequirements);
		DEBUG_LOG("Buffer memory required: %d\n", memRequirements.memoryRequirements.size);
		DEBUG_LOG("Requires dedicated allocation: %s", dedicated.requiresDedicatedAllocation ? "true" : "false");
		DEBUG_LOG("Prefers dedicated allocation: %s", dedicated.requiresDedicatedAllocation ? "true" : "false");

		AllocateMemory(memRequirements.memoryRequirements, memProps, outBuffer.memory);

		vkBindBufferMemory(device, outBuffer.buffer, outBuffer.memory, 0);

  return memRequirements.memoryRequirements.size;
	}

  VkDeviceSize Vulkan::AllocateImage(VkImage image, VkMemoryPropertyFlags memProps, VkDeviceMemory& outMemory) {
		VkImageMemoryRequirementsInfo2 requirementsInfo{};
		requirementsInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
		requirementsInfo.pNext = nullptr;
		requirementsInfo.image = image;

		VkMemoryDedicatedRequirements dedicated{};
		dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
		dedicated.pNext = nullptr;

		VkMemoryRequirements2 memRequirements{};
		memRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		memRequirements.pNext = &dedicated;

		vkGetImageMemoryRequirements2(device, &requirementsInfo, &memRequirements);
		DEBUG_LOG("Image memory required: %d\n", memRequirements.memoryRequirements.size);
		DEBUG_LOG("Requires dedicated allocation: %s", dedicated.requiresDedicatedAllocation ? "true" : "false");
		DEBUG_LOG("Prefers dedicated allocation: %s", dedicated.requiresDedicatedAllocation ? "true" : "false");

		AllocateMemory(memRequirements.memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outMemory);
		vkBindImageMemory(device, image, outMemory, 0);

        return memRequirements.memoryRequirements.size;
	}

	void Vulkan::CopyBuffer(const VkBuffer& src, const VkBuffer& dst, VkDeviceSize size) {
		VkCommandBuffer temp = GetTemporaryCommandBuffer();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(temp, &beginInfo);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0; // Optional
		copyRegion.dstOffset = 0; // Optional
		copyRegion.size = size;
		vkCmdCopyBuffer(temp, src, dst, 1, &copyRegion);

		vkEndCommandBuffer(temp);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &temp;

		vkQueueSubmit(primaryQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(primaryQueue); // Bad?

		vkFreeCommandBuffers(device, tempCommandPool, 1, &temp);
	}

	void Vulkan::CopyRawDataToBuffer(void* src, const VkBuffer& dst, VkDeviceSize size) {
		Buffer stagingBuffer{};
		AllocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer);

		void* data;
		vkMapMemory(device, stagingBuffer.memory, 0, size, 0, &data);
		memcpy(data, src, size);
		vkUnmapMemory(device, stagingBuffer.memory);

		CopyBuffer(stagingBuffer.buffer, dst, size);

		FreeBuffer(stagingBuffer);
	}

	void Vulkan::FreeBuffer(const Buffer& buffer) {
		vkDestroyBuffer(device, buffer.buffer, nullptr);
		vkFreeMemory(device, buffer.memory, nullptr);
	}

	VkShaderModule Vulkan::CreateShaderModule(const char* code, const u32 size) {
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = size;
		createInfo.pCode = (const u32*)code;

		DEBUG_LOG("Creating shader module with size %d", size);

		VkShaderModule module;
		VkResult err = vkCreateShaderModule(device, &createInfo, nullptr, &module);
		if (err != VK_SUCCESS) {
			DEBUG_ERROR("Failed to create shader module!");
		}
		return module;
	}

	void Vulkan::CreateDescriptorSetLayout(VkDescriptorSetLayout& layout, const DescriptorSetLayoutInfo& info) {
		if (info.samplerCount > maxSamplerCount) {
			DEBUG_ERROR("Max sampler count exceeded");
		}

		std::vector<VkDescriptorSetLayoutBinding> bindings(info.bindingCount);

		u32 bindingIndex = 0;

		// Camera data
		if ((info.flags & DSF_CAMERADATA) == DSF_CAMERADATA)
		{
			bindings[bindingIndex].binding = cameraDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Lighting data
		if ((info.flags & DSF_LIGHTINGDATA) == DSF_LIGHTINGDATA)
		{
			bindings[bindingIndex].binding = lightingDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Instance data
		if ((info.flags & DSF_INSTANCEDATA) == DSF_INSTANCEDATA)
		{
			bindings[bindingIndex].binding = perInstanceDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Material data
		if ((info.flags & DSF_SHADERDATA) == DSF_SHADERDATA)
		{
			bindings[bindingIndex].binding = shaderDataBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		for (u32 i = 0; i < info.samplerCount; i++)
		{
			bindings[bindingIndex].binding = samplerBinding + i;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		// Env cubemap
		if ((info.flags & DSF_CUBEMAP) == DSF_CUBEMAP)
		{
			bindings[bindingIndex].binding = envMapBinding;
			bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[bindingIndex].descriptorCount = 1;
			bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[bindingIndex].pImmutableSamplers = nullptr;

			bindingIndex++;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo;
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.pNext = nullptr;
		layoutInfo.flags = 0;
		layoutInfo.bindingCount = info.bindingCount;
		layoutInfo.pBindings = bindings.data();

		vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);
	}

	void Vulkan::CreateShaderRenderPipeline(VkPipelineLayout& outLayout, VkPipeline& outPipeline, const VkDescriptorSetLayout &descSetLayout, VertexAttribFlags vertexInputs, const char* vert, u32 vertSize, const char* frag, u32 fragSize) {
		// Create shader modules
		VkShaderModule vertShader = CreateShaderModule(vert, vertSize);

		VkPipelineShaderStageCreateInfo vertShaderStageInfo;
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.pNext = nullptr;
		vertShaderStageInfo.flags = 0;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShader;
		vertShaderStageInfo.pName = "main";
		vertShaderStageInfo.pSpecializationInfo = nullptr;

		VkShaderModule fragShader = CreateShaderModule(frag, fragSize);

		VkPipelineShaderStageCreateInfo fragShaderStageInfo;
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.pNext = nullptr;
		fragShaderStageInfo.flags = 0;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShader;
		fragShaderStageInfo.pName = "main";
		fragShaderStageInfo.pSpecializationInfo = nullptr;

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		// Vertex input
		VkVertexInputBindingDescription vertDescriptions[10];
		VkVertexInputAttributeDescription attributeDescriptions[10];
		u32 vertexInputCount = 0;

		if (vertexInputs & VERTEX_POSITION_BIT) {
			vertDescriptions[vertexInputCount].binding = 0;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec3);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 0;
			attributeDescriptions[vertexInputCount].location = 0;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_TEXCOORD_0_BIT) {
			vertDescriptions[vertexInputCount].binding = 1;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec2);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 1;
			attributeDescriptions[vertexInputCount].location = 1;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_NORMAL_BIT) {
			vertDescriptions[vertexInputCount].binding = 2;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec3);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 2;
			attributeDescriptions[vertexInputCount].location = 2;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_TANGENT_BIT) {
			vertDescriptions[vertexInputCount].binding = 3;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec4);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 3;
			attributeDescriptions[vertexInputCount].location = 3;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}
		if (vertexInputs & VERTEX_COLOR_BIT) {
			vertDescriptions[vertexInputCount].binding = 4;
			vertDescriptions[vertexInputCount].stride = sizeof(glm::vec4);
			vertDescriptions[vertexInputCount].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			attributeDescriptions[vertexInputCount].binding = 4;
			attributeDescriptions[vertexInputCount].location = 4;
			attributeDescriptions[vertexInputCount].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[vertexInputCount].offset = 0;

			vertexInputCount++;
		}

		VkPipelineVertexInputStateCreateInfo vertexInputInfo;
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.pNext = nullptr;
		vertexInputInfo.flags = 0;
		vertexInputInfo.vertexBindingDescriptionCount = vertexInputCount;
		vertexInputInfo.pVertexBindingDescriptions = vertDescriptions;
		vertexInputInfo.vertexAttributeDescriptionCount = vertexInputCount;
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly;
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.pNext = nullptr;
		inputAssembly.flags = 0;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		////////////////////////////////////////////////////////

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = nullptr; // Will be set at render time
		viewportState.scissorCount = 1;
		viewportState.pScissors = nullptr; // Will be set at render time

		////////////////////////////////////////////////////////

		VkPipelineRasterizationStateCreateInfo rasterizer;
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.pNext = nullptr;
		rasterizer.flags = 0;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
		rasterizer.lineWidth = 1.0f;

		////////////////////////////////////////////////////////

		VkPipelineMultisampleStateCreateInfo multisampling;
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.pNext = nullptr;
		multisampling.flags = 0;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		////////////////////////////////////////////////////////

		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineDepthStencilStateCreateInfo depthStencil;
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.pNext = nullptr;
		depthStencil.flags = 0;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;
		depthStencil.front = VkStencilOpState{};
		depthStencil.back = VkStencilOpState{};
		depthStencil.minDepthBounds = 0.0f; // Optional
		depthStencil.maxDepthBounds = 1.0f; // Optional

		VkPipelineColorBlendStateCreateInfo colorBlending;
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.pNext = nullptr;
		colorBlending.flags = 0;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		////////////////////////////////////////////////////////

		VkPipelineLayoutCreateInfo pipelineLayoutInfo;
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.pNext = nullptr;
		pipelineLayoutInfo.flags = 0;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
		pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

		vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &outLayout);

		////////////////////////////////////////////////////////

		// Dynamic viewport and scissor, as the window size might change
		// (Although it shouldn't change very often)
		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
		dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateInfo.dynamicStateCount = 2;
		dynamicStateInfo.pDynamicStates = dynamicStates;

		VkGraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.pNext = nullptr;
		pipelineInfo.flags = 0;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicStateInfo;
		pipelineInfo.layout = outLayout;
		pipelineInfo.renderPass = forwardRenderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline);

		vkDestroyShaderModule(device, vertShader, nullptr);
		vkDestroyShaderModule(device, fragShader, nullptr);
	}

	void Vulkan::InitializeDescriptorSet(VkDescriptorSet descriptorSet, const DescriptorSetLayoutInfo& info, const MaterialHandle matHandle, const TextureHandle* texHandles) {
		if ((info.flags & DSF_CAMERADATA) == DSF_CAMERADATA)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformDeviceBuffer.buffer;
			bufferInfo.offset = cameraDataOffset;
			bufferInfo.range = cameraDataSize;

			UpdateDescriptorSetBuffer(descriptorSet, cameraDataBinding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		}

		if ((info.flags & DSF_LIGHTINGDATA) == DSF_LIGHTINGDATA)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformDeviceBuffer.buffer;
			bufferInfo.offset = lightingDataOffset;
			bufferInfo.range = lightingDataSize;

			UpdateDescriptorSetBuffer(descriptorSet, lightingDataBinding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		}

		if ((info.flags & DSF_INSTANCEDATA) == DSF_INSTANCEDATA)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = instanceDeviceBuffer.buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = instanceDataElementSize * maxInstanceCountPerDraw;

			UpdateDescriptorSetBuffer(descriptorSet, perInstanceDataBinding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
		}

		if ((info.flags & DSF_SHADERDATA) == DSF_SHADERDATA)
		{
			if (matHandle < 0) {
				DEBUG_ERROR("Invalid material handle");
			}
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformDeviceBuffer.buffer;
			bufferInfo.offset = shaderDataOffset + shaderDataElementSize * matHandle;
			bufferInfo.range = shaderDataElementSize;

			UpdateDescriptorSetBuffer(descriptorSet, shaderDataBinding, bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		}

		if (info.samplerCount > 0 && texHandles == nullptr) {
			DEBUG_ERROR("Invalid texture input");
		}

		for (u32 i = 0; i < info.samplerCount; i++)
		{
			const TextureHandle texHandle = texHandles[i];
			if (texHandle < 0) {
				DEBUG_ERROR("invalid texture handle");
			}
			const Texture* texture = textures[texHandle];

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = texture->view;
			imageInfo.sampler = texture->sampler;

			UpdateDescriptorSetSampler(descriptorSet, samplerBinding + i, imageInfo);
		}

		// TODO
		/*if ((info.flags & DSF_CUBEMAP) == DSF_CUBEMAP)
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = envMap.view;
			imageInfo.sampler = envMap.sampler;

			UpdateDescriptorSetSampler(descriptorSet, envMapBinding, imageInfo);
		}*/
	}

	void Vulkan::UpdateDescriptorSetSampler(const VkDescriptorSet descriptorSet, u32 binding, VkDescriptorImageInfo info) {
		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.pNext = nullptr;
		descriptorWrite.dstSet = descriptorSet;
		descriptorWrite.dstBinding = binding;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.pBufferInfo = nullptr;
		descriptorWrite.pImageInfo = &info;
		descriptorWrite.pTexelBufferView = nullptr;

		vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
	}

	void Vulkan::UpdateDescriptorSetBuffer(VkDescriptorSet descriptorSet, u32 binding, VkDescriptorBufferInfo info, VkDescriptorType type) {
		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.pNext = nullptr;
		descriptorWrite.dstSet = descriptorSet;
		descriptorWrite.dstBinding = binding;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = type;
		descriptorWrite.pBufferInfo = &info;
		descriptorWrite.pImageInfo = nullptr;
		descriptorWrite.pTexelBufferView = nullptr;

		vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
	}

	TextureHandle Vulkan::CreateTexture(const TextureCreateInfo& info) {
		VkFormat format = VK_FORMAT_UNDEFINED;
		u32 layerCount = info.type == TEXTURE_CUBEMAP ? 6 : 1;

		switch (info.compression) {
			case TEXCOMPRESSION_NONE:
				format = VK_FORMAT_R8G8B8A8_SRGB;
				break;
			case TEXCOMPRESSION_ASTC_4x4:
				format = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_5x4:
				format = VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_5x5:
				format = VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_6x5:
				format = VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_6x6:
				format = VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_8x5:
				format = VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_8x6:
				format = VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_8x8:
				format = VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_10x5:
				format = VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_10x6:
				format = VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_10x8:
				format = VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_10x10:
				format = VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_12x10:
				format = VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
				break;
			case TEXCOMPRESSION_ASTC_12x12:
				format = VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
				break;
			default:
				break;
		}

		if (info.space == COLORSPACE_LINEAR) {
			// Stupid hack...
			(*((int*)(&format)))++;
		}

		u32 mipCount = 1;
		if (info.generateMips) {
			mipCount += (u32)std::floor(std::log2(MAX(info.width, info.height)));
		}

		PoolHandle<Texture> handle;
		Texture* texture = textures.Add(handle);
		if (texture == nullptr) {
			DEBUG_ERROR("Error creating texture");
		}

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.flags = info.type == TEXTURE_CUBEMAP ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = info.width;
		imageInfo.extent.height = info.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipCount;
		imageInfo.arrayLayers = layerCount;
		imageInfo.format = format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		vkCreateImage(device, &imageInfo, nullptr, &texture->image);

		VkDeviceSize imageBytes = AllocateImage(texture->image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture->memory);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = texture->image;
		viewInfo.viewType = info.type == TEXTURE_CUBEMAP ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipCount;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = layerCount;

		vkCreateImageView(device, &viewInfo, nullptr, &texture->view);

		VkSamplerCreateInfo samplerInfo;
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.pNext = nullptr;
		samplerInfo.flags = 0;
		samplerInfo.magFilter = (VkFilter)info.filter;
		samplerInfo.minFilter = (VkFilter)info.filter;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 0;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = (r32)mipCount;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		vkCreateSampler(device, &samplerInfo, nullptr, &texture->sampler);

		// Copy data
		Buffer stagingBuffer;
		AllocateBuffer(imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer);

		void* data;
		vkMapMemory(device, stagingBuffer.memory, 0, imageBytes, 0, &data);
		memcpy(data, info.pixels, imageBytes);
		vkUnmapMemory(device, stagingBuffer.memory);

		VkCommandBuffer temp = GetTemporaryCommandBuffer();

		VkCommandBufferBeginInfo beginInfo;
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.pNext = nullptr;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		beginInfo.pInheritanceInfo = nullptr;

		vkBeginCommandBuffer(temp, &beginInfo);

		//cmds
		VkImageMemoryBarrier barrier;
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture->image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipCount;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = layerCount;

		vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkBufferImageCopy region;
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = layerCount;
		region.imageOffset = { 0,0,0 };
		region.imageExtent = { info.width,info.height,1 };

		vkCmdCopyBufferToImage(temp, stagingBuffer.buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// Generate mipmaps
		// This needs to be done even if there are no mipmaps, to convert the texture into the correct format
		VkImageMemoryBarrier mipBarrier{};
		mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		mipBarrier.pNext = nullptr;
		mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipBarrier.image = texture->image;
		mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		mipBarrier.subresourceRange.levelCount = 1;
		mipBarrier.subresourceRange.baseArrayLayer = 0;
		mipBarrier.subresourceRange.layerCount = layerCount;

		s32 mipWidth = info.width;
		s32 mipHeight = info.height;

		for (u32 i = 1; i < mipCount; i++)
		{
			mipBarrier.subresourceRange.baseMipLevel = i - 1;
			mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			mipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			mipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

			vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

			VkImageBlit blit{};
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = layerCount;
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = layerCount;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };

			vkCmdBlitImage(temp, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

			if (mipWidth > 1)
				mipWidth /= 2;
			if (mipHeight > 1)
				mipHeight /= 2;
		}

		mipBarrier.subresourceRange.baseMipLevel = mipCount - 1;
		mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		vkCmdPipelineBarrier(temp, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

		vkEndCommandBuffer(temp);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &temp;

		vkQueueSubmit(primaryQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(primaryQueue);

		vkFreeCommandBuffers(device, tempCommandPool, 1, &temp);

		FreeBuffer(stagingBuffer);
		
		return (TextureHandle)handle.Raw();
	}
	void Vulkan::FreeTexture(TextureHandle handle) {
		const Texture* texture = textures[handle];
		if (texture != nullptr) {
			vkDestroySampler(device, texture->sampler, nullptr);
			vkDestroyImageView(device, texture->view, nullptr);
			vkDestroyImage(device, texture->image, nullptr);
			vkFreeMemory(device, texture->memory, nullptr);
		}

		textures.Remove(handle);
	}

	MeshHandle Vulkan::CreateMesh(const MeshCreateInfo& data) {
		PoolHandle<Mesh> handle;
		Mesh* mesh = meshes.Add(handle);

		if (data.position != nullptr) {
			const VkDeviceSize posBytes = sizeof(glm::vec3) * data.vertexCount;
			AllocateBuffer(posBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->vertexPositionBuffer);
			CopyRawDataToBuffer(data.position, mesh->vertexPositionBuffer.buffer, posBytes);
		}

		if (data.texcoord0 != nullptr) {
			const VkDeviceSize uvBytes = sizeof(glm::vec2) * data.vertexCount;
			AllocateBuffer(uvBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->vertexTexcoord0Buffer);
			CopyRawDataToBuffer(data.texcoord0, mesh->vertexTexcoord0Buffer.buffer, uvBytes);
		}

		if (data.normal != nullptr) {
			const VkDeviceSize normalBytes = sizeof(glm::vec3) * data.vertexCount;
			AllocateBuffer(normalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->vertexNormalBuffer);
			CopyRawDataToBuffer(data.normal, mesh->vertexNormalBuffer.buffer, normalBytes);
		}

		if (data.tangent != nullptr) {
			const VkDeviceSize tangentBytes = sizeof(glm::vec4) * data.vertexCount;
			AllocateBuffer(tangentBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->vertexTangentBuffer);
			CopyRawDataToBuffer(data.tangent, mesh->vertexTangentBuffer.buffer, tangentBytes);
		}

		if (data.color != nullptr) {
			const VkDeviceSize colorBytes = sizeof(Color) * data.vertexCount;
			AllocateBuffer(colorBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->vertexColorBuffer);
			CopyRawDataToBuffer(data.color, mesh->vertexColorBuffer.buffer, colorBytes);
		}

		const VkDeviceSize indexBytes = sizeof(Triangle) * data.triangleCount;
		AllocateBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->indexBuffer);
		CopyRawDataToBuffer(data.triangles, mesh->indexBuffer.buffer, indexBytes);

		mesh->vertexCount = data.vertexCount;
		mesh->indexCount = data.triangleCount * 3;

		return (MeshHandle)handle.Raw();
	}
	void Vulkan::FreeMesh(MeshHandle handle) {
		const Mesh* mesh = meshes[handle];

		FreeBuffer(mesh->vertexPositionBuffer);
		FreeBuffer(mesh->vertexTexcoord0Buffer);
		FreeBuffer(mesh->vertexNormalBuffer);
		FreeBuffer(mesh->vertexTangentBuffer);
		FreeBuffer(mesh->vertexColorBuffer);
		FreeBuffer(mesh->indexBuffer);

		meshes.Remove(handle);
	}

	ShaderHandle Vulkan::CreateShader(const ShaderCreateInfo& info) {
		PoolHandle<Shader> handle;
		Shader* shader = shaders.Add(handle);

		shader->layoutInfo.flags = (DescriptorSetLayoutFlags)(DSF_CAMERADATA | DSF_LIGHTINGDATA | DSF_INSTANCEDATA | DSF_SHADERDATA | DSF_CUBEMAP);
		shader->layoutInfo.samplerCount = info.samplerCount;
		shader->layoutInfo.bindingCount = 5 + info.samplerCount;
		shader->vertexInputs = info.vertexInputs;

		CreateDescriptorSetLayout(shader->descriptorSetLayout, shader->layoutInfo);
		CreateShaderRenderPipeline(shader->pipelineLayout, shader->pipeline, shader->descriptorSetLayout, shader->vertexInputs, info.vertShader, info.vertShaderLength, info.fragShader, info.fragShaderLength);

		return (ShaderHandle)handle.Raw();
	}
	void Vulkan::FreeShader(ShaderHandle handle) {
		const Shader* shader = shaders[handle];

		vkDestroyPipelineLayout(device, shader->pipelineLayout, nullptr);
		vkDestroyPipeline(device, shader->pipeline, nullptr);
		vkDestroyDescriptorSetLayout(device, shader->descriptorSetLayout, nullptr);

		shaders.Remove(handle);
	}

	MaterialHandle Vulkan::CreateMaterial(const MaterialCreateInfo& info) {
		PoolHandle<Material> handle;
		Material *material = materials.Add(handle);
		const Shader* shader = shaders[info.metadata.shader];

		VkDescriptorSetAllocateInfo allocInfo;
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &shader->descriptorSetLayout;

		VkResult res = vkAllocateDescriptorSets(device, &allocInfo, &material->descriptorSet);
		if (res != VK_SUCCESS) {
			DEBUG_ERROR("Oh noes... (%d)", res);
		}

		MaterialHandle matHandle = (MaterialHandle)handle.Raw();

		InitializeDescriptorSet(material->descriptorSet, shader->layoutInfo, matHandle, info.data.textures);
		UpdateMaterialData(matHandle, (void*)info.data.data, 0, maxShaderDataBlockSize);

		return matHandle;
	}
	void Vulkan::UpdateMaterialData(MaterialHandle handle, void* data, u32 offset, u32 size) {
		if (size + offset > shaderDataElementSize) {
			DEBUG_ERROR("Invalid data size (%d) or offset (%d)", size, offset);
		}

		if (size == 0 || data == nullptr) {
			return;
		}

		u32 shaderIndex = PoolHandle<Material>(handle).Index();
		memcpy(pHostVisibleUniformData + shaderDataOffset + shaderDataElementSize * shaderIndex, data, size);
	}
	void Vulkan::UpdateMaterialTexture(MaterialHandle handle, u32 index, TextureHandle texHandle) {
		if (index >= maxSamplerCount) {
			DEBUG_ERROR("Invalid sampler index %d", index);
		}

		const Material* material = materials[handle];
		const Texture* texture = textures[texHandle];

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = texture->sampler;
		imageInfo.imageView = texture->view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		UpdateDescriptorSetSampler(material->descriptorSet, samplerBinding + index, imageInfo);
	}
	void Vulkan::FreeMaterial(MaterialHandle handle) {
		const Material* material = materials[handle];

		vkFreeDescriptorSets(device, descriptorPool, 1, &material->descriptorSet);
		materials.Remove(handle);
	}

	u8* const Vulkan::GetInstanceDataPtr(u32& outStride) {
		outStride = instanceDataElementSize;
		return pHostVisibleInstanceData;
	}
	u8* const Vulkan::GetCameraDataPtr() {
		return pHostVisibleUniformData + cameraDataOffset;
	}
	u8* const Vulkan::GetLightingDataPtr() {
		return pHostVisibleUniformData + lightingDataOffset;
	}

	void Vulkan::BeginRenderCommands() {
		const FrameData& frame = frames[currentFrameIndex];

		// Wait for drawing to finish if it hasn't
		vkWaitForFences(device, 1, &frame.cmdFence, VK_TRUE, UINT64_MAX);

		vkResetFences(device, 1, &frame.cmdFence);
		vkResetCommandPool(device, frame.cmdPool, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		beginInfo.pInheritanceInfo = nullptr; // Optional

		if (vkBeginCommandBuffer(frame.cmdBuffer, &beginInfo) != VK_SUCCESS) {
			DEBUG_ERROR("failed to begin recording command buffer!");
		}

		// Should be ready to draw now!
	}
	void Vulkan::TransferUniformBufferData() {
		const FrameData& frame = frames[currentFrameIndex];

		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(frame.cmdBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size = uniformDataSize;

		vkCmdCopyBuffer(frame.cmdBuffer, uniformHostBuffer.buffer, uniformDeviceBuffer.buffer, 1, &copyRegion);

		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;

		vkCmdPipelineBarrier(frame.cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}
	void Vulkan::TransferInstanceBufferData(u32 offset, u32 size) {
		if (offset + size > maxInstanceCount) {
			DEBUG_ERROR("Buffer copy size too large");
		}

		const FrameData& frame = frames[currentFrameIndex];

		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(frame.cmdBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = offset * instanceDataElementSize;
		copyRegion.dstOffset = offset * instanceDataElementSize;
		copyRegion.size = size * instanceDataElementSize;

		vkCmdCopyBuffer(frame.cmdBuffer, instanceHostBuffer.buffer, instanceDeviceBuffer.buffer, 1, &copyRegion);

		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;

		vkCmdPipelineBarrier(frame.cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}
	// This could be just generic...
	void Vulkan::BeginForwardRenderPass(const u32 xrSwapchainImageIndex) {
		const FrameData& frame = frames[currentFrameIndex];

		VkExtent2D extent = { xrEyeImageWidth, xrEyeImageHeight };
		SwapchainImage& swap = xrSwapchainImages[xrSwapchainImageIndex];

		VkImageView attachments[] = {
			colorAttachment.view,
			depthAttachment.view,
			swap.view
		};

		VkRenderPassAttachmentBeginInfo attachmentInfo{};
		attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
		attachmentInfo.pNext = nullptr;
		attachmentInfo.attachmentCount = 3;
		attachmentInfo.pAttachments = attachments;

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.pNext = &attachmentInfo;
		renderPassInfo.renderPass = forwardRenderPass;
		renderPassInfo.framebuffer = framebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;

		VkClearValue clearColors[3] = { {0,0,0,1}, {1.0f, 0}, {0,0,0,1} };
		renderPassInfo.clearValueCount = 3;
		renderPassInfo.pClearValues = clearColors;

		vkCmdBeginRenderPass(frame.cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)(extent.width);
		viewport.height = (float)(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(frame.cmdBuffer, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(frame.cmdBuffer, 0, 1, &scissor);
	}
	void Vulkan::BindMaterial(MaterialHandle matHandle, ShaderHandle shaderHandle, u16 instanceOffset) {
		const FrameData& frame = frames[currentFrameIndex];
		const Shader* shader = shaders[shaderHandle];
		const Material* material = materials[matHandle];

		// TODO: Bind only once when rendering many things with same shader
		vkCmdBindPipeline(frame.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);

		u32 dynamicOffset = instanceDataElementSize * instanceOffset;
		vkCmdBindDescriptorSets(frame.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipelineLayout, 0, 1, &material->descriptorSet, 1, &dynamicOffset);
	}
	void Vulkan::BindMesh(MeshHandle meshHandle, ShaderHandle shaderHandle) {
		const FrameData& frame = frames[currentFrameIndex];
		const Mesh* mesh = meshes[meshHandle];
		const Shader* shader = shaders[shaderHandle];

		VkDeviceSize offset = 0;
		if (shader->vertexInputs & VERTEX_POSITION_BIT)
			vkCmdBindVertexBuffers(frame.cmdBuffer, 0, 1, &mesh->vertexPositionBuffer.buffer, &offset);
		if (shader->vertexInputs & VERTEX_TEXCOORD_0_BIT)
			vkCmdBindVertexBuffers(frame.cmdBuffer, 1, 1, &mesh->vertexTexcoord0Buffer.buffer, &offset);
		if (shader->vertexInputs & VERTEX_NORMAL_BIT)
			vkCmdBindVertexBuffers(frame.cmdBuffer, 2, 1, &mesh->vertexNormalBuffer.buffer, &offset);
		if (shader->vertexInputs & VERTEX_TANGENT_BIT)
			vkCmdBindVertexBuffers(frame.cmdBuffer, 3, 1, &mesh->vertexTangentBuffer.buffer, &offset);
		if (shader->vertexInputs & VERTEX_COLOR_BIT)
			vkCmdBindVertexBuffers(frame.cmdBuffer, 4, 1, &mesh->vertexColorBuffer.buffer, &offset);

		vkCmdBindIndexBuffer(frame.cmdBuffer, mesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	}
	void Vulkan::Draw(MeshHandle meshHandle, u16 instanceOffset, u16 instanceCount) {
		const FrameData& frame = frames[currentFrameIndex];
		const Mesh* mesh = meshes[meshHandle];

		vkCmdDrawIndexed(frame.cmdBuffer, mesh->indexCount, instanceCount, 0, 0, 0);
	}
	void Vulkan::EndRenderPass() {
		const FrameData& frame = frames[currentFrameIndex];
		vkCmdEndRenderPass(frame.cmdBuffer);
	}
	void Vulkan::EndRenderCommands() {
		const FrameData& frame = frames[currentFrameIndex];

		if (vkEndCommandBuffer(frame.cmdBuffer) != VK_SUCCESS) {
			DEBUG_ERROR("failed to record command buffer!");
		}

		// Submit the above commands
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 0;
		submitInfo.pWaitSemaphores = nullptr;
		submitInfo.pWaitDstStageMask = nullptr;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &frame.cmdBuffer;
		submitInfo.signalSemaphoreCount = 0;
		submitInfo.pSignalSemaphores = nullptr;

		VkResult err = vkQueueSubmit(primaryQueue, 1, &submitInfo, frame.cmdFence);

		// Advance frame index
		currentFrameIndex = (currentFrameIndex + 1) % maxFramesInFlight;
	}
	
	// temp shit?
	Vulkan::XrGraphicsBindingInfo Vulkan::GetXrGraphicsBindingInfo() const {
		return {
			vkInstance, physicalDevice, device, primaryQueueFamilyIndex, 0
		};
	}
}