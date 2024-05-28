#define CGLTF_IMPLEMENTATION
#include "gltf.h"
#include "system.h"
#include <algorithm>

size_t ProcessVertexBuffer(cgltf_accessor* accessor, u8** pOutBuffer, size_t outElementSize) {
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

	const u8* pSrc = (u8*)buffer->data + bufView->offset + accessor->offset;
	for (int i = 0; i < accessor->count; i++) {
		int dstOffset = outElementSize * i;
		int srcOffset = srcStride * i;
		memcpy(*pOutBuffer + dstOffset, pSrc + srcOffset, copySize);
	}

	return accessor->count;
}

// TODO: Validate that element size matches source...
void ProcessVertexAttributeData(Rendering::MeshCreateInfo& meshInfo, cgltf_attribute* attr) {
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

	meshInfo.vertexCount = ProcessVertexBuffer(attr->data, pOutBuffer, outElementSize);
}

bool LoadGLTF(const u8* const buf, u32 bufferSize, cgltf_data** outData, AAssetManager *assetManager) {
	cgltf_options options{};
	cgltf_result result = cgltf_parse(&options, buf, bufferSize, outData);
	if (result != cgltf_result_success) {
		DEBUG_LOG("Failed to load gltf :c");
		return false;
	}

	if ((*outData)->file_type == cgltf_file_type_glb) {
		cgltf_load_buffers(&options, *outData, nullptr);
	}

	return true;
}

bool LoadGLTF(const char* path, const char* fname, cgltf_data** outData, AAssetManager *assetManager) {
	char filePath[1024];
	sprintf(filePath, "%s/%s", path, fname);

	u32 fileSize;
	u8* buf = (u8*)AllocFileBytes(filePath, fileSize, assetManager);

	if (!LoadGLTF(buf, fileSize, outData, assetManager)) {
		return false;
	}

	free(buf);

	// Load buffers
	if ((*outData)->file_type == cgltf_file_type_gltf) {
		for (int i = 0; i < (*outData)->buffers_count; i++) {
			cgltf_buffer &gltfBuffer = (*outData)->buffers[i];
			u32 bufferSize;
			char bufferPath[1024];
			sprintf(bufferPath, "%s/%s", path, gltfBuffer.uri);
			gltfBuffer.data = (void*)AllocFileBytes(bufferPath, bufferSize, assetManager);
			gltfBuffer.data_free_method = cgltf_data_free_method_memory_free;
		}
	}

	return true;
}

void FreeGLTFData(cgltf_data* data) {
	if (data == nullptr) {
		return;
	}

	cgltf_free(data);
}

bool FindGLTFMeshByName(const cgltf_data* const data, const char* meshName, cgltf_mesh& outMesh) {
	if (data == nullptr) {
		DEBUG_LOG("Data is null!");
		return false;
	}

	cgltf_mesh* foundMesh = nullptr;

	for (int i = 0; i < data->meshes_count; i++) {
		cgltf_mesh &mesh = data->meshes[i];
		if (strcmp(mesh.name, meshName) == 0) {
			foundMesh = &mesh;
			break;
		}
	}

	if (foundMesh == nullptr) {
		DEBUG_LOG("Mesh %s not found", meshName);
		return false;
	}

	outMesh = *foundMesh;
	return true;
}

// TODO:  Support multiple primitives
bool GetGLTFMeshInfo(const cgltf_mesh& mesh, Rendering::MeshCreateInfo& outMeshInfo) {
	if (mesh.primitives_count == 0) {
		DEBUG_LOG("Mesh %s has no primitives", mesh.name);
		return false;
	}

	cgltf_primitive &prim = mesh.primitives[0];
	if (prim.type != cgltf_primitive_type_triangles) {
		DEBUG_LOG("Only triangles supported for now");
		return false;
	}

	for (int i = 0; i < prim.attributes_count; i++) {
		cgltf_attribute &attr = prim.attributes[i];
		ProcessVertexAttributeData(outMeshInfo, &attr);
	}

	size_t indexCount = ProcessVertexBuffer(prim.indices, (u8**)&outMeshInfo.triangles, sizeof(u32));
	outMeshInfo.triangleCount = indexCount / 3;
	DEBUG_LOG("Mesh triangle count = %d", outMeshInfo.triangleCount);

	return true;
}

void DebugPrintNodes(const cgltf_node* const node, u32 indent) {
	DEBUG_LOG("%*s%s", indent, "->", node->name);
	for (int i = 0; i < node->children_count; i++) {
		auto child = node->children[i];
		DebugPrintNodes(child, indent + 1);
	}
}