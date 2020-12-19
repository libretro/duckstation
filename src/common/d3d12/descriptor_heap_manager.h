// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "../types.h"
#include "../windows_headers.h"
#include <bitset>
#include <d3d12.h>
#include <map>
#include <vector>
#include <wrl/client.h>

namespace D3D12 {
// This class provides an abstraction for D3D12 descriptor heaps.
struct DescriptorHandle final
{
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
  u32 index = 0;

  operator bool() const { return cpu_handle.ptr != 0; }

  operator D3D12_CPU_DESCRIPTOR_HANDLE() const { return cpu_handle; }
  operator D3D12_GPU_DESCRIPTOR_HANDLE() const { return gpu_handle; }
};

class DescriptorHeapManager final
{
public:
  DescriptorHeapManager();
  ~DescriptorHeapManager();

  ID3D12DescriptorHeap* GetDescriptorHeap() const { return m_descriptor_heap.Get(); }
  u32 GetDescriptorIncrementSize() const { return m_descriptor_increment_size; }

  bool Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 num_descriptors, bool shader_visible);

  bool Allocate(DescriptorHandle* handle);
  void Free(const DescriptorHandle& handle);
  void Free(u32 index);

private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_descriptor_heap;
  u32 m_num_descriptors = 0;
  u32 m_descriptor_increment_size = 0;

  D3D12_CPU_DESCRIPTOR_HANDLE m_heap_base_cpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE m_heap_base_gpu = {};

  static constexpr u32 BITSET_SIZE = 1024;
  using BitSetType = std::bitset<BITSET_SIZE>;
  std::vector<BitSetType> m_free_slots = {};
};

#if 0

class SamplerHeapManager final
{
public:
  SamplerHeapManager();
  ~SamplerHeapManager();

  ID3D12DescriptorHeap* GetDescriptorHeap() const { return m_descriptor_heap.Get(); }

  bool Create(ID3D12Device* device, u32 num_descriptors);
  bool Lookup(const D3D12_SAMPLER_DESC& ss, D3D12_CPU_DESCRIPTOR_HANDLE* handle);
  void Clear();

private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_descriptor_heap;
  u32 m_num_descriptors = 0;
  u32 m_descriptor_increment_size = 0;
  u32 m_current_offset = 0;

  D3D12_CPU_DESCRIPTOR_HANDLE m_heap_base_cpu;

  struct SamplerComparator
  {
    bool operator()(const D3D12_SAMPLER_DESC& lhs, const D3D12_SAMPLER_DESC& rhs) const;
  };

  std::map<D3D12_SAMPLER_DESC, D3D12_CPU_DESCRIPTOR_HANDLE, SamplerComparator> m_sampler_map;
};

#endif
} // namespace D3D12
