#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_multiview : enable

layout(location = 0) in vec2 v_uv;
layout(location = 2) in vec3 v_normal;
layout(location = 6) in vec3 v_color;

layout(location = 0) out vec4 outColor;

layout(binding = 4) uniform sampler2D _texture;

void main() {
	vec2 stereoColor = v_color.rg;
	
	stereoColor.r *= 1 - gl_ViewIndex;
	stereoColor.g *= gl_ViewIndex;

	vec3 texColor = texture(_texture, v_uv).rgb;

	outColor = vec4(texColor, 1.0);
}