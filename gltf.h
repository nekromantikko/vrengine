#pragma once
#include "rendering.h"
#include <android/asset_manager.h>
#include "cgltf.h"

bool LoadGLTF(const u8* const buf, u32 bufferSize, cgltf_data** outData, AAssetManager *assetManager);
bool LoadGLTF(const char* path, const char* fname, cgltf_data** outData, AAssetManager *assetManager);
void FreeGLTFData(cgltf_data* data);
bool FindGLTFMeshByName(const cgltf_data* const data, const char* meshName, cgltf_mesh& outMesh);
bool GetGLTFMeshInfo(const cgltf_mesh& mesh, Rendering::MeshCreateInfo& outMeshInfo);
void DebugPrintNodes(const cgltf_node* const node, u32 indent);
