#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include "quaternion.h"

namespace Rendering {
	constexpr u32 maxMaterialCount = 256;
	constexpr u32 maxShaderCount = 64;
	constexpr u32 maxTextureCount = 256;
	constexpr u32 maxVertexBufferCount = 256;
	constexpr u32 maxDrawcallCount = 1024; // Should be more than enough for mobile VR...
	constexpr u32 maxInstanceCount = 32768; // This is not max instances per drawcall, but in general
	constexpr u32 maxInstanceCountPerDraw = 1024; // TODO: Get this from VkPhysicalDeviceLimits
	constexpr u32 maxSamplerCount = 8;

	typedef glm::vec3 VertexPos;
	typedef glm::vec2 VertexUV;
	typedef glm::vec3 VertexNormal;
	typedef glm::vec4 VertexTangent;
	typedef glm::vec4 Color;

	typedef u64 ShaderHandle;
	typedef u64 TextureHandle;
	typedef u64 MaterialHandle;
	typedef u64 MeshHandle;

	struct Triangle
	{
		u32 index[3];

		Triangle(): index{} {}
		Triangle(u32 a, u32 b, u32 c) {
			index[0] = a;
			index[1] = b;
			index[2] = c;
		}
	};

	enum VertexAttribFlags
	{
		VERTEX_POSITION_BIT = 1 << 0,
		VERTEX_TEXCOORD_0_BIT = 1 << 1,
		VERTEX_TEXCOORD_1_BIT = 1 << 2,
		VERTEX_TEXCOORD_2_BIT = 1 << 3,
		VERTEX_TEXCOORD_3_BIT = 1 << 4,
		VERTEX_NORMAL_BIT = 1 << 5,
		VERTEX_TANGENT_BIT = 1 << 6,
		VERTEX_COLOR_BIT = 1 << 7,
		VERTEX_JOINTS_BIT = 1 << 8,
		VERTEX_WEIGHTS_BIT = 1 << 9,
	};

	struct MeshCreateInfo
	{
		u32 vertexCount;
		glm::vec3* position;
		glm::vec2* texcoord0;
		glm::vec3* normal;
		glm::vec4* tangent;
		Color* color;
		u32 triangleCount;
		Triangle* triangles;
	};

	///////////////////////////////////////

	enum ColorSpace
	{
		COLORSPACE_SRGB,
		COLORSPACE_LINEAR,
	};

	enum TextureFilter
	{
		TEXFILTER_NEAREST = 0,
		TEXFILTER_LINEAR = 1
	};

	enum TextureType
	{
		TEXTURE_2D,
		TEXTURE_CUBEMAP
	};

	struct TextureCreateInfo
	{
		u32 width, height;
		TextureType type;
		ColorSpace space;
		TextureFilter filter;
		bool generateMips;
		u8* pixels;
	};

	////////////////////////////////////////

	struct CameraData {
		glm::mat4 view[2]; // Transformation from world space to view (camera) space
		glm::mat4 proj[2]; // Transformation from view space to screen space
		glm::vec3 pos[2];
	};

	struct Camera {
		glm::mat4x4 transform;
		r32 fov;
		r32 nearClip;
		r32 farClip;
		b32 stereo;
		r32 stereoEyeDist;
		r32 stereoFocalDist;
	};

	// TODO: Support additional lights
	struct LightingData {
		glm::mat4 mainLightMat;
		glm::mat4 mainLightProjMat;
		Color mainLightColor;
		glm::vec4 mainLightDirection;
		Color ambientColor;
	};

	struct PerInstanceData {
		glm::mat4 model;
	};

}