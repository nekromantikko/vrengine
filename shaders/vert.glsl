#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_multiview : enable

layout(location = 0) in vec3 app_pos;
layout(location = 1) in vec2 app_uv;
//vec2 app_uv = vec2(0.0);
//layout(location = 2) in vec3 app_normal;
vec3 app_normal = vec3(0.0);
//layout(location = 3) in vec4 app_tangent;
vec4 app_tangent = vec4(0.0);
//layout(location = 4) in vec4 app_color;
vec4 app_color = vec4(1.0);

layout(binding = 0) uniform CameraData
{
	mat4 view[2];
	mat4 proj[2];
	vec3 pos[2];
} cameraData;

layout(binding = 1) uniform LightingData
{
	mat4 mainLightMat;
	mat4 mainLightProjMat;
	vec4 mainLightColor;
} lightingData;

struct PerInstanceData
{
	mat4 model;
};
layout(binding = 2) uniform InstanceData
{
	PerInstanceData data[1024];
} instanceData;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_lightSpacePos;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out vec3 v_worldPos;
layout(location = 4) out vec3 v_bitangent;
layout(location = 5) out vec3 v_tangent;
layout(location = 6) out vec3 v_color;

void main() {
	PerInstanceData instance = instanceData.data[gl_InstanceIndex];
	gl_Position = cameraData.proj[gl_ViewIndex] * cameraData.view[gl_ViewIndex] * instance.model * vec4(app_pos, 1.0);
	v_uv = app_uv;
	
	mat4 normalMatrix = transpose(inverse(instance.model));
	v_normal = (normalMatrix * vec4(app_normal, 0.0)).xyz;
	v_tangent = (normalMatrix * vec4(app_tangent.xyz, 0.0)).xyz;
	vec3 bitangent = cross(app_normal, app_tangent.xyz) * app_tangent.w;
	v_bitangent	= (normalMatrix * vec4(bitangent, 0.0)).xyz;
	v_worldPos = (instance.model * vec4(app_pos, 1.0)).xyz;
	v_lightSpacePos = lightingData.mainLightProjMat * lightingData.mainLightMat * vec4(v_worldPos, 1.0);
	v_color = app_color.rgb;
}