#pragma once
#include "typedef.h"

template <typename T>
class PoolHandle {
	u64 raw;
public:
	PoolHandle() = default;
	PoolHandle(const PoolHandle& other) = default;
	PoolHandle(u64 raw) : raw(raw) {}
	PoolHandle(u32 index, u32 gen) {
		raw = ((u64)gen << 32ull) | (u64)index;
	}

	inline u32 Index() const {
		return (u32)(raw % 0x100000000);
	}

	inline u32 Generation() const {
		return (u32)(raw >> 32ull);
	}
	
	inline const u64 Raw() const {
		return raw;
	}

	bool operator==(const PoolHandle& other) const {
		return raw == other.raw; 
	}
	bool operator!=(const PoolHandle& other) const { 
		return raw != other.raw; 
	}
};

template<typename T, typename THandle = PoolHandle<T>>
class Pool
{
private:
	T* objs;
	THandle* handles;
	u32* erase;

	u32 size;
	u32 count;
public:
	Pool() {
		Init(0);
	}
	Pool(u32 s) {
		Init(s);
	}
	void Free() {
		free(objs);
		free(handles);
		free(erase);
	}
	~Pool() {
		Free();
	}
	void Init(u32 s) {
		size = s;
		count = 0;

		objs = (T*)calloc(size, sizeof(T));
		handles = (THandle*)calloc(size, sizeof(THandle));
		erase = (u32*)calloc(size, sizeof(u32));

		for (u32 i = 0; i < size; i++)
		{
			handles[i] = THandle(i, 0);
			erase[i] = i;
		}
	}
	T* Add(THandle& outHandle) {
		if (count >= size) {
			return nullptr;
		}

		THandle handle = handles[count++];
		const u32 arrayIndex = handle.Index();
		outHandle = handle;
		return &objs[arrayIndex];
	}
	T* Get(const THandle handle) const {
		const u32 arrayIndex = handle.Index();
		const u32 handleIndex = erase[arrayIndex];
		const THandle h = handles[handleIndex];
		if (h != handle) {
			return nullptr;
		}
		return &objs[arrayIndex];
	}
	T* operator[](THandle handle) {
		return Get(handle);
	}
	bool Remove(const THandle handle) {
		const u32 arrayIndex = handle.Index();
		const u32 handleIndex = erase[arrayIndex];
		THandle h = handles[handleIndex];
		if (h != handle) {
			return false;
		}
		handles[handleIndex] = handles[--count];
		handles[count] = THandle(arrayIndex, h.Generation() + 1);

		const u32 swap = erase[arrayIndex];
		erase[arrayIndex] = erase[count];
		erase[count] = swap;
		return true;
	}
	u32 Count() const {
		return count;
	}
	bool GetHandle(u32 index, THandle& outHandle) const {
		if (index >= count) {
			return false;
		}
		outHandle = handles[index];
		return true;
	}
};