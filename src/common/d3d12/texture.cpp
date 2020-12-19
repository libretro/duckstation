#include "texture.h"
#include "../align.h"
#include "../assert.h"
#include "../log.h"
#include "context.h"
#include "util.h"
Log_SetChannel(D3D12);

namespace D3D12 {

static Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureUploadBuffer(ID3D12Device* device, u32 buffer_size)
{
  const D3D12_HEAP_PROPERTIES heap_properties = {D3D12_HEAP_TYPE_UPLOAD};
  const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                    0,
                                    buffer_size,
                                    1,
                                    1,
                                    1,
                                    DXGI_FORMAT_UNKNOWN,
                                    {1, 0},
                                    D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                    D3D12_RESOURCE_FLAG_NONE};

  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  HRESULT hr = g_d3d12_context->GetDevice()->CreateCommittedResource(
    &heap_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
  AssertMsg(SUCCEEDED(hr), "Create texture upload buffer");
  return resource;
}

Texture::Texture() = default;

Texture::Texture(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) : m_resource(std::move(resource))
{
  const D3D12_RESOURCE_DESC desc = GetDesc();
  m_width = desc.Width;
  m_height = desc.Height;
  m_samples = desc.SampleDesc.Count;
  m_format = desc.Format;
}

Texture::Texture(Texture&& texture)
  : m_resource(std::move(texture.m_resource)), m_srv_descriptor(texture.m_srv_descriptor),
    m_rtv_or_dsv_descriptor(texture.m_rtv_or_dsv_descriptor), m_width(texture.m_width), m_height(texture.m_height),
    m_samples(texture.m_samples), m_format(texture.m_format), m_state(texture.m_state)
{
  texture.m_srv_descriptor = {};
  texture.m_rtv_or_dsv_descriptor = {};
  texture.m_width = 0;
  texture.m_height = 0;
  texture.m_samples = 0;
  texture.m_format = DXGI_FORMAT_UNKNOWN;
  texture.m_state = D3D12_RESOURCE_STATE_COMMON;
}

Texture::~Texture()
{
  Destroy();
}

Texture& Texture::operator=(Texture&& texture)
{
  Destroy();
  m_resource = std::move(texture.m_resource);
  m_srv_descriptor = texture.m_srv_descriptor;
  m_rtv_or_dsv_descriptor = texture.m_rtv_or_dsv_descriptor;
  m_width = texture.m_width;
  m_height = texture.m_height;
  m_samples = texture.m_samples;
  m_format = texture.m_format;
  m_state = texture.m_state;
  texture.m_srv_descriptor = {};
  texture.m_rtv_or_dsv_descriptor = {};
  texture.m_width = 0;
  texture.m_height = 0;
  texture.m_samples = 0;
  texture.m_format = DXGI_FORMAT_UNKNOWN;
  texture.m_state = D3D12_RESOURCE_STATE_COMMON;
  return *this;
}

D3D12_RESOURCE_DESC Texture::GetDesc() const
{
  return m_resource->GetDesc();
}

bool Texture::Create(u32 width, u32 height, u32 samples, DXGI_FORMAT format, DXGI_FORMAT srv_format,
                     DXGI_FORMAT rtv_format, DXGI_FORMAT dsv_format, D3D12_RESOURCE_FLAGS flags,
                     const void* initial_data /* = nullptr */, u32 initial_data_stride /* = 0 */,
                     bool dynamic /* = false */)
{
  constexpr D3D12_HEAP_PROPERTIES heap_properties = {D3D12_HEAP_TYPE_DEFAULT};

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = samples;
  desc.Layout = dynamic ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;

  D3D12_CLEAR_VALUE optimized_clear_value = {};
  D3D12_RESOURCE_STATES state;
  if (rtv_format != DXGI_FORMAT_UNKNOWN)
  {
    optimized_clear_value.Format = rtv_format;
    state = D3D12_RESOURCE_STATE_RENDER_TARGET;
  }
  else if (dsv_format != DXGI_FORMAT_UNKNOWN)
  {
    optimized_clear_value.Format = dsv_format;
    state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  }
  else
  {
    state = (initial_data != nullptr) ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  }

  ComPtr<ID3D12Resource> resource;
  HRESULT hr = g_d3d12_context->GetDevice()->CreateCommittedResource(
    &heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state,
    (rtv_format != DXGI_FORMAT_UNKNOWN || dsv_format != DXGI_FORMAT_UNKNOWN) ? &optimized_clear_value : nullptr,
    IID_PPV_ARGS(resource.GetAddressOf()));
  if (FAILED(hr))
  {
    Log_ErrorPrintf("Create texture failed: 0x%08X", hr);
    return false;
  }

  DescriptorHandle srv_descriptor, rtv_descriptor;
  if (srv_format != DXGI_FORMAT_UNKNOWN)
  {
    if (!CreateSRVDescriptor(resource.Get(), srv_format, samples > 1, &srv_descriptor))
      return false;
  }

  if (rtv_format != DXGI_FORMAT_UNKNOWN)
  {
    Assert(dsv_format == DXGI_FORMAT_UNKNOWN);
    if (!CreateRTVDescriptor(resource.Get(), rtv_format, samples > 1, &rtv_descriptor))
    {
      if (srv_descriptor)
        g_d3d12_context->GetDescriptorHeapManager().Free(srv_descriptor);

      return false;
    }
  }
  else if (dsv_format != DXGI_FORMAT_UNKNOWN)
  {
    if (!CreateDSVDescriptor(resource.Get(), dsv_format, samples > 1, &rtv_descriptor))
    {
      if (srv_descriptor)
        g_d3d12_context->GetDescriptorHeapManager().Free(srv_descriptor);

      return false;
    }
  }

  if (initial_data)
  {
    const u32 copy_pitch = Common::AlignUpPow2(initial_data_stride, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    const u32 initial_data_size = copy_pitch * height;
    ComPtr<ID3D12Resource> upload_buffer = CreateTextureUploadBuffer(g_d3d12_context->GetDevice(), initial_data_size);
    if (!upload_buffer)
      return false;

    u8* dst_ptr;
    hr = upload_buffer->Map(0, nullptr, reinterpret_cast<void**>(&dst_ptr));
    if (FAILED(hr))
    {
      Log_ErrorPrintf("Mapping upload buffer failed: %08X", hr);
      return false;
    }

    const u8* src_ptr = static_cast<const u8*>(initial_data);
    const u32 copy_size = GetTexelSize(format) * width;
    for (u32 row = 0; row < height; row++)
    {
      std::memcpy(dst_ptr, src_ptr, copy_size);
      src_ptr += initial_data_stride;
      dst_ptr += copy_pitch;
    }

    const D3D12_RANGE written_range = {0u, initial_data_size};
    upload_buffer->Unmap(0, &written_range);

    D3D12_TEXTURE_COPY_LOCATION src;
    src.pResource = upload_buffer.Get();
    src.SubresourceIndex = 0;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Width = width;
    src.PlacedFootprint.Footprint.Height = height;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = copy_pitch;
    src.PlacedFootprint.Footprint.Format = format;

    D3D12_TEXTURE_COPY_LOCATION dst;
    dst.pResource = resource.Get();
    dst.SubresourceIndex = 0;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    const D3D12_BOX src_box{0u, 0u, 0u, width, height, 1u};
    g_d3d12_context->GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, &src_box);
    ResourceBarrier(g_d3d12_context->GetCommandList(), resource.Get(), state,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    g_d3d12_context->DeferResourceDestruction(upload_buffer.Get());
    state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  }

  Destroy(true);

  m_resource = std::move(resource);
  m_srv_descriptor = std::move(srv_descriptor);
  m_rtv_or_dsv_descriptor = std::move(rtv_descriptor);
  m_width = desc.Width;
  m_height = desc.Height;
  m_samples = desc.SampleDesc.Count;
  m_format = desc.Format;
  m_state = state;
  return true;
}

bool Texture::Adopt(ComPtr<ID3D12Resource> texture, DXGI_FORMAT srv_format, DXGI_FORMAT rtv_format,
                    DXGI_FORMAT dsv_format, D3D12_RESOURCE_STATES state)
{
  const D3D12_RESOURCE_DESC desc(texture->GetDesc());

  DescriptorHandle srv_descriptor, rtv_descriptor;
  if (srv_format != DXGI_FORMAT_UNKNOWN)
  {
    if (!CreateSRVDescriptor(texture.Get(), srv_format, desc.SampleDesc.Count > 1, &srv_descriptor))
      return false;
  }

  if (rtv_format != DXGI_FORMAT_UNKNOWN)
  {
    Assert(dsv_format == DXGI_FORMAT_UNKNOWN);
    if (!CreateRTVDescriptor(texture.Get(), rtv_format, desc.SampleDesc.Count > 1, &rtv_descriptor))
    {
      if (srv_descriptor)
        g_d3d12_context->GetDescriptorHeapManager().Free(srv_descriptor);

      return false;
    }
  }
  else if (dsv_format != DXGI_FORMAT_UNKNOWN)
  {
    if (!CreateDSVDescriptor(texture.Get(), dsv_format, desc.SampleDesc.Count > 1, &rtv_descriptor))
    {
      if (srv_descriptor)
        g_d3d12_context->GetDescriptorHeapManager().Free(srv_descriptor);

      return false;
    }
  }

  m_resource = std::move(texture);
  m_srv_descriptor = std::move(srv_descriptor);
  m_rtv_or_dsv_descriptor = std::move(rtv_descriptor);
  m_width = desc.Width;
  m_height = desc.Height;
  m_samples = desc.SampleDesc.Count;
  m_format = desc.Format;
  m_state = state;
  return true;
}

void Texture::Destroy(bool defer /* = true */)
{
  if (defer)
  {
    if (m_srv_descriptor)
    {
      g_d3d12_context->DeferDescriptorDestruction(g_d3d12_context->GetDescriptorHeapManager(), m_srv_descriptor.index);
      m_srv_descriptor = {};
    }
    if (m_rtv_or_dsv_descriptor)
    {
      g_d3d12_context->DeferDescriptorDestruction(g_d3d12_context->GetRTVHeapManager(), m_rtv_or_dsv_descriptor.index);
      m_rtv_or_dsv_descriptor = {};
    }
    if (m_resource)
    {
      g_d3d12_context->DeferResourceDestruction(m_resource.Get());
      m_resource.Reset();
    }
  }
  else
  {
    if (m_srv_descriptor)
    {
      g_d3d12_context->GetDescriptorHeapManager().Free(m_srv_descriptor.index);
      m_srv_descriptor = {};
    }
    if (m_rtv_or_dsv_descriptor)
    {
      g_d3d12_context->GetRTVHeapManager().Free(m_rtv_or_dsv_descriptor.index);
      m_rtv_or_dsv_descriptor = {};
    }
    if (m_resource)
      m_resource.Reset();
  }

  m_width = 0;
  m_height = 0;
  m_samples = 0;
  m_format = DXGI_FORMAT_UNKNOWN;
}

void Texture::TransitionToState(D3D12_RESOURCE_STATES state) const
{
  if (m_state == state)
    return;

  ResourceBarrier(g_d3d12_context->GetCommandList(), m_resource.Get(), m_state, state);
  m_state = state;
}

bool Texture::CreateSRVDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, bool multisampled, DescriptorHandle* dh)
{
  if (!g_d3d12_context->GetDescriptorHeapManager().Allocate(dh))
  {
    Log_ErrorPrintf("Failed to allocate SRV descriptor");
    return false;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {
    format, multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D,
    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
  if (!multisampled)
    desc.Texture2D.MipLevels = 1;

  g_d3d12_context->GetDevice()->CreateShaderResourceView(resource, &desc, dh->cpu_handle);
  return true;
}

bool Texture::CreateRTVDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, bool multisampled, DescriptorHandle* dh)
{
  if (!g_d3d12_context->GetRTVHeapManager().Allocate(dh))
  {
    Log_ErrorPrintf("Failed to allocate SRV descriptor");
    return false;
  }

  D3D12_RENDER_TARGET_VIEW_DESC desc = {format,
                                        multisampled ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D};

  g_d3d12_context->GetDevice()->CreateRenderTargetView(resource, &desc, dh->cpu_handle);
  return true;
}

bool Texture::CreateDSVDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, bool multisampled, DescriptorHandle* dh)
{
  if (!g_d3d12_context->GetDSVHeapManager().Allocate(dh))
  {
    Log_ErrorPrintf("Failed to allocate SRV descriptor");
    return false;
  }

  D3D12_DEPTH_STENCIL_VIEW_DESC desc = {
    format, multisampled ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D, D3D12_DSV_FLAG_NONE};

  g_d3d12_context->GetDevice()->CreateDepthStencilView(resource, &desc, dh->cpu_handle);
  return true;
}

} // namespace D3D12