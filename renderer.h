#pragma once
#include "vulkan.h"
#include <string>
#include <unordered_map>
#include <compare>

namespace Rendering {
	class Drawcall {
		u64 id;
	public:
		Drawcall() = default;
		Drawcall(const Drawcall& other) = default;
		Drawcall(u16 dataIndex, MeshHandle mesh, MaterialHandle mat, RenderLayer layer);

		//std::strong_ordering operator<=>(const Drawcall& other) const;

		inline RenderLayer Layer() const;
		inline MaterialHandle Material() const;
		inline MeshHandle Mesh() const;
		inline u16 DataIndex() const;
	};

	class Renderer {
	public:
		Renderer(const XR::XRInstance* const xrInstance);
		~Renderer();

		void CreateXRSwapchain(const XR::XRInstance* const xrInstance);

		MeshHandle CreateMesh(std::string name, const MeshCreateInfo& data);
		TextureHandle CreateTexture(std::string name, const TextureCreateInfo& info);
		ShaderHandle CreateShader(std::string name, const ShaderCreateInfo& info);
		MaterialHandle CreateMaterial(std::string name, const MaterialCreateInfo& info);

		void UpdateCameraRaw(const CameraData& data);
		//void UpdateMainLight(const Quaternion& direction, const Color& color);
		void UpdateAmbientLight(const Color& color);
		void DrawMesh(MeshHandle mesh, MaterialHandle material, const glm::mat4x4& transform);
		void DrawMeshInstanced(MeshHandle mesh, MaterialHandle material, u16 count, const glm::mat4x4* transforms);

		void Render(const u32 xrSwapchainImageIndex);

		// This kind of defeats the point of wrapping the implementation, figure out a better way to do this
		const Vulkan* GetImplementation() const;
		
	private:
		void RecalculateCameraMatrices();

		CameraData* cameraData;

		LightingData* lightingData;

		u8* instanceData;
		u32 instanceDataStride;

		struct DrawcallData {
			u16 instanceCount;
			u16 instanceOffset;
		} *drawcallData;

		Drawcall* renderQueue;
		u16 drawcallCount;
		u16 instanceCount;

		Vulkan vulkan;

		std::unordered_map<std::string, MeshHandle> meshNameMap;
		std::unordered_map<std::string, TextureHandle> textureNameMap;
		std::unordered_map<std::string, ShaderHandle> shaderNameMap;
		std::unordered_map<std::string, MaterialHandle> materialNameMap;

		std::unordered_map<ShaderHandle, ShaderMetadata> shaderMetadataMap;
		std::unordered_map<MaterialHandle, MaterialMetadata> materialMetadataMap;
	};
}