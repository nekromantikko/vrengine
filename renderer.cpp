#include "renderer.h"
#include "system.h"
#include <algorithm>
#include <math.h>

namespace Rendering {
	Drawcall::Drawcall(u16 dataIndex, MeshHandle mesh, MaterialHandle mat, RenderLayer layer) {
		id = 0;
		id += (u64)(dataIndex % 0x1000);
		id += (u64)mesh << 12ull;
		id += (u64)mat << 20ull;
		id += (u64)layer << 56ull;
	}

	/*std::strong_ordering Drawcall::operator<=>(const Drawcall& other) const {
		if (id < other.id) return std::strong_ordering::less;
		if (id > other.id) return std::strong_ordering::greater;
		else return std::strong_ordering::equal;
	}*/

	RenderLayer Drawcall::Layer() const {
		return (RenderLayer)(id >> 56ull);
	}
	MaterialHandle Drawcall::Material() const {
		return (MaterialHandle)((id >> 20ull) % 0x1000);
	}
	MeshHandle Drawcall::Mesh() const {
		return (MeshHandle)((id >> 12ull) % 0x100);
	}
	u16 Drawcall::DataIndex() const {
		return (u16)(id % 0x1000);
	}


	Renderer::Renderer(const XR::XRInstance* const xrInstance): vulkan(xrInstance) {
		drawcallData = (DrawcallData*)calloc(maxDrawcallCount, sizeof(DrawcallData));
		instanceData = vulkan.GetInstanceDataPtr(instanceDataStride);

		renderQueue = (Drawcall*)calloc(maxDrawcallCount, sizeof(Drawcall));
		drawcallCount = 0;
		instanceCount = 0;

		cameraData = (CameraData*)vulkan.GetCameraDataPtr();
		lightingData = (LightingData*)vulkan.GetLightingDataPtr();
	}

	Renderer::~Renderer() {
		free(drawcallData);
		free(renderQueue);
	}

	void Renderer::CreateXRSwapchain(const XR::XRInstance* const xrInstance) {
		vulkan.CreateXRSwapchain(xrInstance);
	}

	MeshHandle Renderer::CreateMesh(std::string name, const MeshCreateInfo& info) {
		/*if (meshNameMap.contains(name)) {
			DEBUG_ERROR("Mesh with name %s already exists", name.c_str());
		}*/

		if (info.triangles == nullptr) {
			DEBUG_ERROR("Triangles cannot be null");
		}

		auto handle = vulkan.CreateMesh(info);
		meshNameMap[name] = handle;
		return handle;
	}

	TextureHandle Renderer::CreateTexture(std::string name, const TextureCreateInfo& info) {
		/*if (textureNameMap.contains(name)) {
			DEBUG_ERROR("Texture with name %s already exists", name.c_str());
		}*/

		auto handle = vulkan.CreateTexture(info);
		textureNameMap[name] = handle;
		return handle;
	}

	ShaderHandle Renderer::CreateShader(std::string name, const ShaderCreateInfo& info) {
		/*if (shaderNameMap.contains(name)) {
			DEBUG_ERROR("Shader with name %s already exists", name.c_str());
		}*/

		if (info.metadata.dataLayout.dataSize > maxShaderDataBlockSize) {
			DEBUG_ERROR("Data size too large");
		}

		auto handle = vulkan.CreateShader(info);
		meshNameMap[name] = handle;
		shaderMetadataMap[handle] = info.metadata;
		return handle;
	}

	MaterialHandle Renderer::CreateMaterial(std::string name, const MaterialCreateInfo& info) {
		/*if (materialNameMap.contains(name)) {
			DEBUG_ERROR("Material with name %s already exists", name.c_str());
		}*/

		auto handle = vulkan.CreateMaterial(info);
		materialNameMap[name] = handle;
		materialMetadataMap[handle] = info.metadata;
		return handle;
	}

	void Renderer::UpdateCameraRaw(const CameraData& data) {
		*cameraData = data;
	}

	void Renderer::UpdateMainLight(const Quaternion& rotation, const Color& color) {
		lightingData->mainLightColor = color;
		static const r32 shadowmapArea = 25.0f;
		lightingData->mainLightProjMat = glm::ortho(-shadowmapArea / 2.0f, shadowmapArea / 2.0f, shadowmapArea / 2.0f, -shadowmapArea / 2.0f, -1024.0f, 1024.0f);
		lightingData->mainLightDirection = -glm::vec4(rotation*glm::vec3(0.0f, 0.0f, 1.0f), 0.0);
	}

	void Renderer::UpdateAmbientLight(const Color& color) {
		lightingData->ambientColor = color;
	}

	void Renderer::DrawMesh(MeshHandle mesh, MaterialHandle material, const glm::mat4x4& transform) {
		DrawMeshInstanced(mesh, material, 1, &transform);
	}

	void Renderer::DrawMeshInstanced(MeshHandle mesh, MaterialHandle material, u16 count, const glm::mat4x4* transforms) {
		u16 instanceOffset = instanceCount;
		instanceCount += count;
		u16 callIndex = drawcallCount++;

		DrawcallData data = { count, instanceOffset };

		drawcallData[callIndex] = data;

		// This is bad if I change the size of PerInstanceData...
		PerInstanceData* instances = (PerInstanceData*)transforms;
		u64 instanceByteOffset = instanceDataStride * instanceOffset;
		memcpy(instanceData + instanceByteOffset, instances, sizeof(PerInstanceData) * count);

		RenderLayer layer = shaderMetadataMap[materialMetadataMap[material].shader].layer;
		Drawcall call(callIndex, mesh, material, layer);

		renderQueue[callIndex] = call;
	}

	void Renderer::Render(const u32 xrSwapchainImageIndex) {
		// Sort drawcalls
		// std::sort(&renderQueue[0], &renderQueue[drawcallCount]);

		vulkan.BeginRenderCommands();
		vulkan.TransferUniformBufferData();
		static bool32 firstFrameRendered = false;
		if (!firstFrameRendered) {
			vulkan.TransferInstanceBufferData(0, instanceCount);
			firstFrameRendered = true;
		}
		vulkan.BeginForwardRenderPass(xrSwapchainImageIndex);

		MeshHandle previousMesh = -1;
		MaterialHandle previousMaterial = -1;
		for (u32 i = 0; i < drawcallCount; i++) {
			const Drawcall& call = renderQueue[i];
			MeshHandle meshHandle = call.Mesh();
			MaterialHandle matHandle = call.Material();
			MaterialMetadata matData = materialMetadataMap[matHandle];
			u16 dataIndex = call.DataIndex();
			DrawcallData data = drawcallData[dataIndex];
			
			if (matHandle != previousMaterial) {
				vulkan.BindMaterial(matHandle, matData.shader, data.instanceOffset);
			}
			if (meshHandle != previousMesh) {
				vulkan.BindMesh(meshHandle, matData.shader);
			}

			vulkan.Draw(meshHandle, data.instanceOffset, data.instanceCount);
		}
		vulkan.EndRenderPass();
		vulkan.EndRenderCommands();

		// Clear render queue
		drawcallCount = 0;
		instanceCount = 0;
	}

	const Vulkan* Renderer::GetImplementation() const {
		return &vulkan;
	}
}