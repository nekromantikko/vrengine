#pragma once
#include "rendering.h"
#include <string>
#include <vector>

constexpr u64 maxShaderDataBlockSize = 256;

namespace Rendering {

	enum RenderLayer { //2 bits 
		RENDER_LAYER_OPAQUE = 0,
		RENDER_LAYER_TRANSPARENT = 1,
		RENDER_LAYER_OVERLAY = 2,
		RENDER_LAYER_SKYBOX = 3
	};

	enum ShaderPropertyType {
		SHADER_PROPERTY_FLOAT,
		SHADER_PROPERTY_VEC2,
		SHADER_PROPERTY_VEC4,
		SHADER_PROPERTY_INT,
		SHADER_PROPERTY_IVEC2,
		SHADER_PROPERTY_IVEC4,
		SHADER_PROPERTY_UINT,
		SHADER_PROPERTY_UVEC2,
		SHADER_PROPERTY_UVEC4,
		SHADER_PROPERTY_MAT2,
		SHADER_PROPERTY_MAT4
	};

	struct ShaderPropertyInfo {
		std::string name;
		ShaderPropertyType type;
		u32 count; // If it's an array
		u32 offset; // Offset to beginning of the uniform block in bytes
	};

	struct ShaderDataLayout {
		u32 dataSize;
		u32 propertyCount;
		std::vector<ShaderPropertyInfo> properties;
	};

	struct ShaderMetadata {
		RenderLayer layer;
		ShaderDataLayout dataLayout;
	};

	struct ShaderCreateInfo {
		ShaderMetadata metadata;
		VertexAttribFlags vertexInputs;
		u32 samplerCount;
		char* vertShader;
		u32 vertShaderLength;
		char* fragShader;
		u32 fragShaderLength;
	};

	struct MaterialData {
		char data[maxShaderDataBlockSize];
		TextureHandle textures[maxSamplerCount];
	};

	struct MaterialMetadata {
		ShaderHandle shader;
		bool castShadows;
	};

	struct MaterialCreateInfo {
		MaterialMetadata metadata;
		MaterialData data;
	};
}