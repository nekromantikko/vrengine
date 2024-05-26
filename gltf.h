#pragma once
#include "rendering.h"
#include <android/asset_manager.h>
#include "cgltf.h"

bool LoadGLTF(const char* path, const char* fname, cgltf_data** outData, AAssetManager *assetManager);
void FreeGLTFData(cgltf_data* data);
bool GetGLTFMeshInfo(const cgltf_data* const data, const char* meshName, Rendering::MeshCreateInfo& outMeshInfo);
