#pragma once
#include "typedef.h"
#include "rendering.h"

struct AstcHeader {
				u8 magic[4];
				u8 block_x;
				u8 block_y;
				u8 block_z;
				u8 dim_x[3];
				u8 dim_y[3];
				u8 dim_z[3];
};

bool IsValidAstc(const AstcHeader* const header) {
	if (header->magic[0] != 0x13 ||
					header->magic[1] != 0xAB ||
					header->magic[2] != 0xA1 ||
					header->magic[3] != 0x5C) {
		return false;
	}

	return true;
}

u32 DecodeDim(const u8* const dim) {
	return dim[0] + (dim[1] << 8) + (dim[2] << 16);
}

bool GetAstcInfo(const u8* const buffer, u32& outWidth, u32& outHeight, Rendering::TextureCompression& outCompression) {
	const AstcHeader* header = (AstcHeader*)buffer;
	if (!IsValidAstc(header)) {
		return false;
	}

	outWidth = DecodeDim(header->dim_x);
	outHeight = DecodeDim(header->dim_y);
	// TODO: Support 3D textures?

	outCompression = (Rendering::TextureCompression)(((u32)(header->block_x) << 4) | ((u32)(header->block_y) << 8));
	return true;
}

bool GetAstcPayload(const u8* const buffer, u8** outPayload) {
	const AstcHeader* header = (AstcHeader*)buffer;
	if (!IsValidAstc(header)) {
		return false;
	}

	*outPayload = (u8*)(header + 1);
	return true;
}