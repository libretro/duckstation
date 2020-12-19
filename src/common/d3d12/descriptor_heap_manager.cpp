// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "descriptor_heap_manager.h"
#include "../assert.h"
#include "../log.h"
#include "context.h"
Log_SetChannel(DescriptorHeapManager);

namespace D3D12 {
DescriptorHeapManager::DescriptorHeapManager() = default;
DescriptorHeapManager::~DescriptorHeapManager() = default;

bool DescriptorHeapManager::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 num_descriptors,
                                   bool shader_visible)
{
  D3D12_DESCRIPTOR_HEAP_DESC desc = {type, static_cast<UINT>(num_descriptors),
                                     shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE :
                                                      D3D12_DESCRIPTOR_HEAP_FLAG_NONE};

  HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptor_heap));
  AssertMsg(SUCCEEDED(hr), "Create descriptor heap");
  if (FAILED(hr))
    return false;

  m_heap_base_cpu = m_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
  m_heap_base_gpu = m_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
  m_num_descriptors = num_descriptors;
  m_descriptor_increment_size = device->GetDescriptorHandleIncrementSize(type);

  // Set all slots to unallocated (1)
  const u32 bitset_count = num_descriptors / BITSET_SIZE + (((num_descriptors % BITSET_SIZE) != 0) ? 1 : 0);
  m_free_slots.resize(bitset_count);
  for (BitSetType& bs : m_free_slots)
    bs.flip();

  return true;
}

bool DescriptorHeapManager::Allocate(DescriptorHandle* handle)
{
  // Start past the temporary slots, no point in searching those.
  for (u32 group = 0; group < m_free_slots.size(); group++)
  {
    BitSetType& bs = m_free_slots[group];
    if (bs.none())
      continue;

    u32 bit = 0;
    for (; bit < BITSET_SIZE; bit++)
    {
      if (bs[bit])
        break;
    }

    u32 index = group * BITSET_SIZE + bit;
    bs[bit] = false;

    handle->index = index;
    handle->cpu_handle.ptr = m_heap_base_cpu.ptr + index * m_descriptor_increment_size;
    handle->gpu_handle.ptr = m_heap_base_gpu.ptr + index * m_descriptor_increment_size;
    return true;
  }

  Panic("Out of fixed descriptors");
  return false;
}

void DescriptorHeapManager::Free(u32 index)
{
  Assert(index < m_num_descriptors);

  u32 group = index / BITSET_SIZE;
  u32 bit = index % BITSET_SIZE;
  m_free_slots[group][bit] = true;
}

void DescriptorHeapManager::Free(const DescriptorHandle& handle)
{
  if (handle)
    Free(handle.index);
}

#if 0
SamplerHeapManager::SamplerHeapManager() = default;
SamplerHeapManager::~SamplerHeapManager() = default;

bool SamplerHeapManager::Lookup(const D3D12_SAMPLER_DESC& ss, D3D12_CPU_DESCRIPTOR_HANDLE* handle)
{
  const auto it = m_sampler_map.find(ss);
  if (it != m_sampler_map.end())
  {
    *handle = it->second;
    return true;
  }

  if (m_current_offset == m_num_descriptors)
  {
    // We can clear at any time because the descriptors are copied prior to execution.
    // It's still not free, since we have to recreate all our samplers again.
    Log_WarningPrintf("Out of samplers, resetting CPU heap");
    Clear();
  }

  const D3D12_CPU_DESCRIPTOR_HANDLE new_handle = {m_heap_base_cpu.ptr + m_current_offset * m_descriptor_increment_size};
  g_d3d12_context->GetDevice()->CreateSampler(&ss, new_handle);

  m_sampler_map.emplace(ss, new_handle);
  m_current_offset++;
  *handle = new_handle;
  return true;
}

void SamplerHeapManager::Clear()
{
  m_sampler_map.clear();
  m_current_offset = 0;
}

bool SamplerHeapManager::Create(ID3D12Device* device, u32 num_descriptors)
{
  const D3D12_DESCRIPTOR_HEAP_DESC desc = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, num_descriptors};
  HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptor_heap));
  AssertMsg(SUCCEEDED(hr), "Failed to create sampler descriptor heap");
  if (FAILED(hr))
    return false;

  m_num_descriptors = num_descriptors;
  m_descriptor_increment_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  m_heap_base_cpu = m_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
  return true;
}
#endif
} // namespace D3D12
