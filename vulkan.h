#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include "rendering.h"
#include "material.h"
#include "memory_pool.h"
#include "xr.h"

#define SWAPCHAIN_MIN_IMAGE_COUNT 3

namespace Rendering {
	class Vulkan {
	public:
		Vulkan(const XR::XRInstance* const xrInstance);
		~Vulkan();

		void CreateXRSwapchain(const XR::XRInstance* const xrInstance);
		void WaitForAllCommands();
		TextureHandle CreateTexture(const TextureCreateInfo& info);
		void FreeTexture(TextureHandle handle);
		MeshHandle CreateMesh(const MeshCreateInfo& info);
		void FreeMesh(MeshHandle handle);
		ShaderHandle CreateShader(const ShaderCreateInfo& info);
		void FreeShader(ShaderHandle handle);
		MaterialHandle CreateMaterial(const MaterialCreateInfo& info);
		void UpdateMaterialData(MaterialHandle handle, void* data, u32 offset, u32 size);
		void UpdateMaterialTexture(MaterialHandle handle, u32 index, TextureHandle texture);
		void FreeMaterial(MaterialHandle handle);

		u8* const GetInstanceDataPtr(u32& outStride);
		u8* const GetCameraDataPtr();
		u8* const GetLightingDataPtr();
		void BeginRenderCommands();
		void TransferUniformBufferData();
		void TransferInstanceBufferData(u32 offset, u32 size);
		void BeginForwardRenderPass(const u32 xrSwapchainImageIndex);
		void BindMaterial(MaterialHandle matHandle, ShaderHandle shaderHandle, u16 instanceOffset);
		void BindMesh(MeshHandle meshHandle, ShaderHandle shaderHandle);
		void Draw(MeshHandle meshHandle, u16 instanceOffset, u16 instanceCount);
		void EndRenderPass();
		void EndRenderCommands();

		struct XrGraphicsBindingInfo {
			VkInstance instance;
			VkPhysicalDevice physicalDevice;
			VkDevice device;
			u32 queueFamilyIndex;
			u32 queueIndex;
		};

		// Bleh
		XrGraphicsBindingInfo GetXrGraphicsBindingInfo() const;
	private:
		struct Buffer {
			VkBuffer buffer;
			VkDeviceMemory memory;
		};

		struct Texture {
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
			VkSampler sampler;
		};

		struct Mesh {
			u32 vertexCount;
			Buffer vertexPositionBuffer;
			Buffer vertexTexcoord0Buffer;
			Buffer vertexNormalBuffer;
			Buffer vertexTangentBuffer;
			Buffer vertexColorBuffer;

			u32 indexCount;
			Buffer indexBuffer;
		};

		enum DescriptorSetLayoutFlags
		{
			DSF_NONE = 0,
			DSF_CAMERADATA = 1,
			DSF_LIGHTINGDATA = 1 << 1,
			DSF_INSTANCEDATA = 1 << 2,
			DSF_SHADERDATA = 1 << 3,
			DSF_CUBEMAP = 1 << 4,
		};

		struct DescriptorSetLayoutInfo
		{
			DescriptorSetLayoutFlags flags;
			u32 samplerCount;
			u32 bindingCount;
		};

		struct Shader {
			VkPipelineLayout pipelineLayout;
			VkPipeline pipeline;
			VkDescriptorSetLayout descriptorSetLayout;
			DescriptorSetLayoutInfo layoutInfo;
			VertexAttribFlags vertexInputs;
		};

		struct Material {
			VkDescriptorSet descriptorSet;
		};

		struct FramebufferAttachemnt {
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
		};

		struct SwapchainImage {
			VkImage image;
			VkImageView view;
		};

		struct FrameData {
			VkCommandPool cmdPool;
			VkCommandBuffer cmdBuffer; // Recorded each frame
			VkFence cmdFence; // Used to wait for previous frame to complete rendering before recording new commands
		};

		bool IsPhysicalDeviceSuitable(VkPhysicalDevice physicalDevice, u32& outQueueFamilyIndex);
		void CreateLogicalDevice(const XR::XRInstance* const xrInstance);
		void CreateForwardRenderPass();
		void CreateRenderPasses();
		void FreeRenderPasses();
		void CreateFramebufferAttachments();
		void FreeFramebufferAttachments();
		void CreateFramebuffer();
		void FreeFramebuffer();
		void CreateFrameData();
		void FreeFrameData();
		
		void CreateUniformBuffers();
		void FreeUniformBuffers();
		void CreatePerInstanceBuffers();
		void FreePerInstanceBuffers();

		s32 GetDeviceMemoryTypeIndex(u32 typeFilter, VkMemoryPropertyFlags propertyFlags);
		VkCommandBuffer GetTemporaryCommandBuffer();
		void AllocateMemory(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& outMemory);
		void AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, Buffer& outBuffer);
		void AllocateImage(VkImage image, VkMemoryPropertyFlags memProps, VkDeviceMemory& outMemory);
		void CopyBuffer(const VkBuffer& src, const VkBuffer& dst, VkDeviceSize size);
		void CopyRawDataToBuffer(void* src, const VkBuffer& dst, VkDeviceSize size);
		void FreeBuffer(const Buffer& buffer);
		VkShaderModule CreateShaderModule(const char* code, const u32 size);
		void CreateDescriptorSetLayout(VkDescriptorSetLayout& layout, const DescriptorSetLayoutInfo& info);
		void CreateShaderRenderPipeline(VkPipelineLayout& outLayout, VkPipeline &outPipeline, const VkDescriptorSetLayout &descSetLayout, VertexAttribFlags vertexInputs, const char* vert, u32 vertSize, const char* frag, u32 fragSize);
		void InitializeDescriptorSet(VkDescriptorSet descriptorSet, const DescriptorSetLayoutInfo& info, const MaterialHandle matHandle, const TextureHandle* textures);
		void UpdateDescriptorSetSampler(VkDescriptorSet descriptorSet, u32 binding, VkDescriptorImageInfo info);
		void UpdateDescriptorSetBuffer(VkDescriptorSet descriptorSet, u32 binding, VkDescriptorBufferInfo info, VkDescriptorType type);

		VkInstance vkInstance;

		VkSurfaceKHR xrSurface;
		VkSurfaceCapabilitiesKHR xrSurfaceCapabilities;

		VkPhysicalDevice physicalDevice;
		struct PhysicalDeviceInfo
		{
			VkPhysicalDeviceProperties properties;
			VkPhysicalDeviceMemoryProperties memProperties;

			std::vector<VkQueueFamilyProperties> queueFamilies;
		} physicalDeviceInfo;

		VkDevice device;
		u32 primaryQueueFamilyIndex = 0;
		VkQueue primaryQueue;
		
		static constexpr u32 maxFramesInFlight = 2;
		FrameData frames[maxFramesInFlight];
		u32 currentFrameIndex = 0;

		VkCommandPool tempCommandPool; // Used for allocating temporary cmd buffers

		u32 xrEyeImageWidth, xrEyeImageHeight;
		std::vector<SwapchainImage> xrSwapchainImages;

		VkDescriptorPool descriptorPool;

		// Render passes
		VkRenderPass forwardRenderPass;
		VkRenderPass finalBlitRenderPass;

		// Uniform data
		VkDeviceSize uniformDataSize = 0;
		VkDeviceSize cameraDataOffset = 0;
		VkDeviceSize cameraDataSize = 0;
		VkDeviceSize lightingDataOffset = 0;
		VkDeviceSize lightingDataSize = 0;
		VkDeviceSize shaderDataOffset = 0;
		VkDeviceSize shaderDataElementSize = 0;
		VkDeviceSize shaderDataSize = 0;
		u8* pHostVisibleUniformData = nullptr;

		Buffer uniformHostBuffer;
		Buffer uniformDeviceBuffer;

		// Per instance data in storage buffer for larger instance count
		VkDeviceSize instanceDataElementSize = 0;
		VkDeviceSize instanceDataSize = 0;
		u8* pHostVisibleInstanceData = nullptr;

		Buffer instanceHostBuffer;
		Buffer instanceDeviceBuffer;

		// Uniform shader bindings
		static constexpr u32 cameraDataBinding = 0;
		static constexpr u32 lightingDataBinding = 1;
		static constexpr u32 perInstanceDataBinding = 2;
		static constexpr u32 shaderDataBinding = 3;

		static constexpr u32 samplerBinding = 4; // 4 - 11 reserved for generic samplers
		Pool<Texture> textures = Pool<Texture>(maxTextureCount);

		Pool<Mesh> meshes = Pool<Mesh>(maxVertexBufferCount);
		Pool<Shader> shaders = Pool<Shader>(maxShaderCount);
		Pool<Material> materials = Pool<Material>(maxMaterialCount);

		static constexpr u32 envMapBinding = 12;

		FramebufferAttachemnt colorAttachment;
		FramebufferAttachemnt depthAttachment;
		VkFramebuffer framebuffer;

        // Proc addresses
        PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
        PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;
        PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2;
        PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2;
    };
}