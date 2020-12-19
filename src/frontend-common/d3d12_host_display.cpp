#include "d3d12_host_display.h"
#include "common/assert.h"
#include "common/d3d11/shader_compiler.h"
#include "common/d3d12/context.h"
#include "common/d3d12/util.h"
#include "common/log.h"
#include "common/string_util.h"
#include "core/host_interface.h"
#include "core/settings.h"
#include "display_ps.hlsl.h"
#include "display_vs.hlsl.h"
#include <array>
#ifndef LIBRETRO
#include "frontend-common/postprocessing_shadergen.h"
#include <dxgi1_5.h>
#endif
#ifdef WITH_IMGUI
#include "imgui.h"
#include "imgui_impl_dx12.h"
#endif
Log_SetChannel(D3D12HostDisplay);

namespace FrontendCommon {

class D3D12HostDisplayTexture : public HostDisplayTexture
{
public:
  D3D12HostDisplayTexture(D3D12::Texture texture, D3D12::StagingTexture staging_texture)
    : m_texture(std::move(texture)), m_staging_texture(std::move(staging_texture))
  {
  }
  ~D3D12HostDisplayTexture() override = default;

  void* GetHandle() const override { return const_cast<D3D12::Texture*>(&m_texture); }
  u32 GetWidth() const override { return m_texture.GetWidth(); }
  u32 GetHeight() const override { return m_texture.GetHeight(); }
  bool IsDynamic() const { return m_staging_texture; }

  const D3D12::Texture& GetTexture() const { return m_texture; }
  D3D12::Texture& GetTexture() { return m_texture; }
  D3D12::StagingTexture& GetStagingTexture() { return m_staging_texture; }

  static std::unique_ptr<D3D12HostDisplayTexture> Create(u32 width, u32 height, const void* data, u32 data_stride,
                                                         bool dynamic)
  {
    static constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12::Texture texture;
    if (!texture.Create(width, height, 1, format, format, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
                        D3D12_RESOURCE_FLAG_NONE, data, data_stride, dynamic))
    {
      return {};
    }

    D3D12::StagingTexture staging_texture;
    if (dynamic)
    {
      if (!staging_texture.Create(width, height, format, dynamic))
        return {};
    }

    texture.TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    return std::make_unique<D3D12HostDisplayTexture>(std::move(texture), std::move(staging_texture));
  }

private:
  D3D12::Texture m_texture;
  D3D12::StagingTexture m_staging_texture;
};

D3D12HostDisplay::D3D12HostDisplay() = default;

D3D12HostDisplay::~D3D12HostDisplay()
{
  AssertMsg(!g_d3d12_context, "Context should have been destroyed by now");
#ifndef LIBRETRO
  AssertMsg(!m_swap_chain, "Swap chain should have been destroyed by now");
#endif
}

HostDisplay::RenderAPI D3D12HostDisplay::GetRenderAPI() const
{
  return HostDisplay::RenderAPI::D3D12;
}

void* D3D12HostDisplay::GetRenderDevice() const
{
  return g_d3d12_context->GetDevice();
}

void* D3D12HostDisplay::GetRenderContext() const
{
  return g_d3d12_context.get();
}

bool D3D12HostDisplay::HasRenderDevice() const
{
  return static_cast<bool>(g_d3d12_context);
}

bool D3D12HostDisplay::HasRenderSurface() const
{
#ifndef LIBRETRO
  return static_cast<bool>(m_swap_chain);
#else
  return true;
#endif
}

std::unique_ptr<HostDisplayTexture> D3D12HostDisplay::CreateTexture(u32 width, u32 height, const void* initial_data,
                                                                    u32 initial_data_stride, bool dynamic)
{
  return D3D12HostDisplayTexture::Create(width, height, initial_data, initial_data_stride, dynamic);
}

void D3D12HostDisplay::UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height,
                                     const void* texture_data, u32 texture_data_stride)
{
#if 0
  D3D12HostDisplayTexture* wrapped_texture = static_cast<D3D12HostDisplayTexture*>(texture);
  if (!wrapped_texture->IsDynamic())
  {
    const CD3D12_BOX dst_box(x, y, 0, x + width, y + height, 1);
    m_context->UpdateSubresource(wrapped_texture->GetD3DTexture(), 0, &dst_box, texture_data, texture_data_stride,
                                 texture_data_stride * height);
  }
  else
  {
    D3D12_MAPPED_SUBRESOURCE sr;
    HRESULT hr = m_context->Map(wrapped_texture->GetD3DTexture(), 0, D3D12_MAP_WRITE_DISCARD, 0, &sr);
    if (FAILED(hr))
      Panic("Failed to map dynamic host display texture");

    char* dst_ptr = static_cast<char*>(sr.pData) + (y * sr.RowPitch) + (x * sizeof(u32));
    const char* src_ptr = static_cast<const char*>(texture_data);
    if (sr.RowPitch == texture_data_stride)
    {
      std::memcpy(dst_ptr, src_ptr, texture_data_stride * height);
    }
    else
    {
      for (u32 row = 0; row < height; row++)
      {
        std::memcpy(dst_ptr, src_ptr, width * sizeof(u32));
        src_ptr += texture_data_stride;
        dst_ptr += sr.RowPitch;
      }
    }

    m_context->Unmap(wrapped_texture->GetD3DTexture(), 0);
  }
#endif
}

bool D3D12HostDisplay::DownloadTexture(const void* texture_handle, u32 x, u32 y, u32 width, u32 height, void* out_data,
                                       u32 out_data_stride)
{
#if 0
  ID3D12ShaderResourceView* srv =
    const_cast<ID3D12ShaderResourceView*>(static_cast<const ID3D12ShaderResourceView*>(texture_handle));
  ComPtr<ID3D12Resource> srv_resource;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
  srv->GetResource(srv_resource.GetAddressOf());
  srv->GetDesc(&srv_desc);

  if (!m_readback_staging_texture.EnsureSize(m_context.Get(), width, height, srv_desc.Format, false))
    return false;

  m_readback_staging_texture.CopyFromTexture(m_context.Get(), srv_resource.Get(), 0, x, y, 0, 0, width, height);
  return m_readback_staging_texture.ReadPixels<u32>(m_context.Get(), 0, 0, width, height, out_data_stride / sizeof(u32),
                                                    static_cast<u32*>(out_data));
#endif
  return false;
}

static constexpr std::array<DXGI_FORMAT, static_cast<u32>(HostDisplayPixelFormat::Count)>
  s_display_pixel_format_mapping = {{DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM}};

bool D3D12HostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  const DXGI_FORMAT dfmt = s_display_pixel_format_mapping[static_cast<u32>(format)];
  if (dfmt == DXGI_FORMAT_UNKNOWN)
    return false;
#if 0
  UINT support = 0;
  const UINT required = D3D12_FORMAT_SUPPORT_TEXTURE2D | D3D12_FORMAT_SUPPORT_SHADER_SAMPLE;
  return (SUCCEEDED(m_device->CheckFormatSupport(dfmt, &support) && ((support & required) == required)));
#else
  return true;
#endif
}

bool D3D12HostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                                             u32* out_pitch)
{
#if 0
  ClearDisplayTexture();

  const DXGI_FORMAT dxgi_format = s_display_pixel_format_mapping[static_cast<u32>(format)];
  if (m_display_pixels_texture.GetWidth() < width || m_display_pixels_texture.GetHeight() < height ||
      m_display_pixels_texture.GetFormat() != dxgi_format)
  {
    if (!m_display_pixels_texture.Create(width, height, 1, dxgi_format, dxgi_format, DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE, nullptr, 0, false))
    {
      return false;
    }
  }

  void* mapped_ptr;
  HRESULT hr = m_display_pixels_texture.GetResource()->Map(0, nullptr, &mapped_ptr);
  if (FAILED(hr))
  {
    Log_ErrorPrintf("Map pixels texture failed: %08X", hr);
    return false;
  }

  *out_buffer = sr.pData;
  *out_pitch = sr.RowPitch;

  SetDisplayTexture(m_display_pixels_texture.GetD3DSRV(), format, m_display_pixels_texture.GetWidth(),
                    m_display_pixels_texture.GetHeight(), 0, 0, static_cast<u32>(width), static_cast<u32>(height));
  return true;
#else
  return false;
#endif
}

void D3D12HostDisplay::EndSetDisplayPixels()
{
#if 0
  m_context->Unmap(m_display_pixels_texture.GetD3DTexture(), 0);
#endif
}

void D3D12HostDisplay::SetVSync(bool enabled)
{
#ifndef LIBRETRO
  m_vsync = enabled;
#endif
}

bool D3D12HostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device)
{
#ifndef LIBRETRO
  ComPtr<IDXGIFactory> temp_dxgi_factory;
  HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(temp_dxgi_factory.GetAddressOf()));
  if (FAILED(hr))
  {
    Log_ErrorPrintf("Failed to create DXGI factory: 0x%08X", hr);
    return false;
  }

  u32 adapter_index;
  if (!adapter_name.empty())
  {
    AdapterInfo adapter_info = GetAdapterInfo(temp_dxgi_factory.Get());
    for (adapter_index = 0; adapter_index < static_cast<u32>(adapter_info.adapter_names.size()); adapter_index++)
    {
      if (adapter_name == adapter_info.adapter_names[adapter_index])
        break;
    }
    if (adapter_index == static_cast<u32>(adapter_info.adapter_names.size()))
    {
      Log_WarningPrintf("Could not find adapter '%s', using first (%s)", std::string(adapter_name).c_str(),
                        adapter_info.adapter_names[0].c_str());
      adapter_index = 0;
    }
  }
  else
  {
    Log_InfoPrintf("No adapter selected, using first.");
    adapter_index = 0;
  }

  if (!D3D12::Context::Create(temp_dxgi_factory.Get(), adapter_index, debug_device))
    return false;

  if (FAILED(hr))
  {
    Log_ErrorPrintf("Failed to create D3D device: 0x%08X", hr);
    return false;
  }

#if 0
  // we need the specific factory for the device, otherwise MakeWindowAssociation() is flaky.
  ComPtr<IDXGIDevice> dxgi_device;
  if (FAILED(m_device.As(&dxgi_device)) || FAILED(dxgi_device->GetParent(IID_PPV_ARGS(dxgi_adapter.GetAddressOf()))) ||
      FAILED(dxgi_adapter->GetParent(IID_PPV_ARGS(m_dxgi_factory.GetAddressOf()))))
  {
    Log_WarningPrint("Failed to get parent adapter/device/factory");
    return false;
  }
#else
  m_dxgi_factory = std::move(temp_dxgi_factory);
#endif

  m_allow_tearing_supported = false;
  ComPtr<IDXGIFactory5> dxgi_factory5;
  hr = m_dxgi_factory.As(&dxgi_factory5);
  if (SUCCEEDED(hr))
  {
    BOOL allow_tearing_supported = false;
    hr = dxgi_factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing_supported,
                                            sizeof(allow_tearing_supported));
    if (SUCCEEDED(hr))
      m_allow_tearing_supported = (allow_tearing_supported == TRUE);
  }
#endif

  m_window_info = wi;
  return true;
}

bool D3D12HostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device)
{
#ifndef LIBRETRO
  if (m_window_info.type != WindowInfo::Type::Surfaceless && m_window_info.type != WindowInfo::Type::Libretro &&
      !CreateSwapChain(nullptr))
  {
    return false;
  }
#endif

  if (!CreateResources())
    return false;

#ifdef WITH_IMGUI
  if (ImGui::GetCurrentContext() && !CreateImGuiContext())
    return false;
#endif

  return true;
}

void D3D12HostDisplay::DestroyRenderDevice()
{
#ifdef WITH_IMGUI
  if (ImGui::GetCurrentContext())
    DestroyImGuiContext();
#endif

  DestroyResources();
  DestroyRenderSurface();
  if (g_d3d12_context)
    g_d3d12_context->Destroy();
}

bool D3D12HostDisplay::MakeRenderContextCurrent()
{
  return true;
}

bool D3D12HostDisplay::DoneRenderContextCurrent()
{
  return true;
}

#ifndef LIBRETRO

bool D3D12HostDisplay::CreateSwapChain(const DXGI_MODE_DESC* fullscreen_mode)
{
  if (m_window_info.type != WindowInfo::Type::Win32)
    return false;

  const HWND window_hwnd = reinterpret_cast<HWND>(m_window_info.window_handle);
  RECT client_rc{};
  GetClientRect(window_hwnd, &client_rc);
  const u32 width = static_cast<u32>(client_rc.right - client_rc.left);
  const u32 height = static_cast<u32>(client_rc.bottom - client_rc.top);

  DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
  swap_chain_desc.BufferDesc.Width = width;
  swap_chain_desc.BufferDesc.Height = height;
  swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.BufferCount = 3;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.OutputWindow = window_hwnd;
  swap_chain_desc.Windowed = TRUE;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  m_using_allow_tearing = (m_allow_tearing_supported && !fullscreen_mode);
  if (m_using_allow_tearing)
    swap_chain_desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

  if (fullscreen_mode)
  {
    swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    swap_chain_desc.Windowed = FALSE;
    swap_chain_desc.BufferDesc = *fullscreen_mode;
  }

  Log_InfoPrintf("Creating a %dx%d %s swap chain", swap_chain_desc.BufferDesc.Width, swap_chain_desc.BufferDesc.Height,
                 swap_chain_desc.Windowed ? "windowed" : "full-screen");

  HRESULT hr =
    m_dxgi_factory->CreateSwapChain(g_d3d12_context->GetCommandQueue(), &swap_chain_desc, m_swap_chain.GetAddressOf());
  if (FAILED(hr))
  {
    Log_ErrorPrintf("CreateSwapChain failed: 0x%08X", hr);
    return false;
  }

  hr = m_dxgi_factory->MakeWindowAssociation(swap_chain_desc.OutputWindow, DXGI_MWA_NO_WINDOW_CHANGES);
  if (FAILED(hr))
    Log_WarningPrintf("MakeWindowAssociation() to disable ALT+ENTER failed");

  return CreateSwapChainRTV();
}

bool D3D12HostDisplay::CreateSwapChainRTV()
{
  DXGI_SWAP_CHAIN_DESC swap_chain_desc;
  HRESULT hr = m_swap_chain->GetDesc(&swap_chain_desc);
  if (FAILED(hr))
    return false;

  for (u32 i = 0; i < swap_chain_desc.BufferCount; i++)
  {
    ComPtr<ID3D12Resource> backbuffer;
    HRESULT hr = m_swap_chain->GetBuffer(i, IID_PPV_ARGS(backbuffer.GetAddressOf()));
    if (FAILED(hr))
    {
      Log_ErrorPrintf("GetBuffer for RTV failed: 0x%08X", hr);
      return false;
    }

    D3D12::Texture tex;
    if (!tex.Adopt(std::move(backbuffer), DXGI_FORMAT_UNKNOWN, swap_chain_desc.BufferDesc.Format, DXGI_FORMAT_UNKNOWN,
                   D3D12_RESOURCE_STATE_PRESENT))
    {
      return false;
    }

    m_swap_chain_buffers.push_back(std::move(tex));
  }

  m_window_info.surface_width = swap_chain_desc.BufferDesc.Width;
  m_window_info.surface_height = swap_chain_desc.BufferDesc.Height;

  if (ImGui::GetCurrentContext())
  {
    ImGui::GetIO().DisplaySize.x = static_cast<float>(swap_chain_desc.BufferDesc.Width);
    ImGui::GetIO().DisplaySize.y = static_cast<float>(swap_chain_desc.BufferDesc.Height);
  }

  m_current_swap_chain_buffer = 0;
  return true;
}

void D3D12HostDisplay::DestroySwapChainRTVs()
{
  for (D3D12::Texture& buffer : m_swap_chain_buffers)
    buffer.Destroy(false);
  m_swap_chain_buffers.clear();
  m_current_swap_chain_buffer = 0;
}

#endif

bool D3D12HostDisplay::ChangeRenderWindow(const WindowInfo& new_wi)
{
#ifndef LIBRETRO
  DestroyRenderSurface();

  m_window_info = new_wi;
  return CreateSwapChain(nullptr);
#else
  m_window_info = new_wi;
  return true;
#endif
}

void D3D12HostDisplay::DestroyRenderSurface()
{
#ifndef LIBRETRO
  if (IsFullscreen())
    SetFullscreen(false, 0, 0, 0.0f);

  DestroySwapChainRTVs();
  m_swap_chain.Reset();
#endif
}

void D3D12HostDisplay::ResizeRenderWindow(s32 new_window_width, s32 new_window_height)
{
#ifndef LIBRETRO
  if (!m_swap_chain)
    return;

  g_d3d12_context->WaitForGPUIdle();

  DestroySwapChainRTVs();

  HRESULT hr = m_swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN,
                                           m_using_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
  if (FAILED(hr))
    Log_ErrorPrintf("ResizeBuffers() failed: 0x%08X", hr);

  if (!CreateSwapChainRTV())
    Panic("Failed to recreate swap chain RTV after resize");
#endif
}

bool D3D12HostDisplay::SupportsFullscreen() const
{
#ifndef LIBRETRO
  return true;
#else
  return false;
#endif
}

bool D3D12HostDisplay::IsFullscreen()
{
#ifndef LIBRETRO
  BOOL is_fullscreen = FALSE;
  return (m_swap_chain && SUCCEEDED(m_swap_chain->GetFullscreenState(&is_fullscreen, nullptr)) && is_fullscreen);
#else
  return false;
#endif
}

bool D3D12HostDisplay::SetFullscreen(bool fullscreen, u32 width, u32 height, float refresh_rate)
{
#ifndef LIBRETRO
  if (!m_swap_chain)
    return false;

  BOOL is_fullscreen = FALSE;
  HRESULT hr = m_swap_chain->GetFullscreenState(&is_fullscreen, nullptr);
  if (!fullscreen)
  {
    // leaving fullscreen
    if (is_fullscreen)
      return SUCCEEDED(m_swap_chain->SetFullscreenState(FALSE, nullptr));
    else
      return true;
  }

  IDXGIOutput* output;
  if (FAILED(hr = m_swap_chain->GetContainingOutput(&output)))
    return false;

  DXGI_SWAP_CHAIN_DESC current_desc;
  hr = m_swap_chain->GetDesc(&current_desc);
  if (FAILED(hr))
    return false;

  DXGI_MODE_DESC new_mode = current_desc.BufferDesc;
  new_mode.Width = width;
  new_mode.Height = height;
  new_mode.RefreshRate.Numerator = static_cast<UINT>(std::floor(refresh_rate * 1000.0f));
  new_mode.RefreshRate.Denominator = 1000u;

  DXGI_MODE_DESC closest_mode;
  if (FAILED(hr = output->FindClosestMatchingMode(&new_mode, &closest_mode, nullptr)) ||
      new_mode.Format != current_desc.BufferDesc.Format)
  {
    Log_ErrorPrintf("Failed to find closest matching mode, hr=%08X", hr);
    return false;
  }

  if (new_mode.Width == current_desc.BufferDesc.Width && new_mode.Height == current_desc.BufferDesc.Width &&
      new_mode.RefreshRate.Numerator == current_desc.BufferDesc.RefreshRate.Numerator &&
      new_mode.RefreshRate.Denominator == current_desc.BufferDesc.RefreshRate.Denominator)
  {
    Log_InfoPrintf("Fullscreen mode already set");
    return true;
  }

  DestroySwapChainRTVs();
  m_swap_chain.Reset();

  if (!CreateSwapChain(&closest_mode))
  {
    Log_ErrorPrintf("Failed to create a fullscreen swap chain");
    if (!CreateSwapChain(nullptr))
      Panic("Failed to recreate windowed swap chain");

    return false;
  }

  return true;
#else
  return false;
#endif
}

bool D3D12HostDisplay::CreateResources()
{
  D3D12::RootSignatureBuilder rsbuilder;
  rsbuilder.AddCBVParameter(0, D3D12_SHADER_VISIBILITY_ALL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_ALL);
  rsbuilder.AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_ALL);
  m_display_root_signature = rsbuilder.Create();
  if (!m_display_root_signature)
    return false;

  D3D12::GraphicsPipelineBuilder gpbuilder;
  gpbuilder.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  gpbuilder.SetRootSignature(m_display_root_signature.Get());
  gpbuilder.SetVertexShader(s_display_vs_bytecode, sizeof(s_display_vs_bytecode));
  gpbuilder.SetPixelShader(s_display_ps_bytecode, sizeof(s_display_ps_bytecode));
  gpbuilder.SetNoCullRasterizationState();
  gpbuilder.SetNoDepthTestState();
  gpbuilder.SetNoBlendingState();
  gpbuilder.SetRenderTarget(0, DXGI_FORMAT_R8G8B8A8_UNORM);
  m_display_pipeline = gpbuilder.Create(g_d3d12_context->GetDevice(), false);
  if (!m_display_pipeline)
    return false;

  gpbuilder.SetBlendState(0, true, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
                          D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_COLOR_WRITE_ENABLE_ALL);
  m_software_cursor_pipeline = gpbuilder.Create(g_d3d12_context->GetDevice(), false);
  if (!m_software_cursor_pipeline)
    return false;

  D3D12_SAMPLER_DESC desc = {};
  D3D12::SetDefaultSampler(&desc);
  desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_point_sampler))
    return false;

  g_d3d12_context->GetDevice()->CreateSampler(&desc, m_point_sampler.cpu_handle);

  desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;

  if (!g_d3d12_context->GetSamplerHeapManager().Allocate(&m_linear_sampler))
    return false;

  g_d3d12_context->GetDevice()->CreateSampler(&desc, m_linear_sampler.cpu_handle);

  if (!m_display_uniform_buffer.Create(DISPLAY_UNIFORM_BUFFER_SIZE))
    return false;

  return true;
}

void D3D12HostDisplay::DestroyResources()
{
#ifndef LIBRETRO
//   m_post_processing_chain.ClearStages();
//   m_post_processing_input_texture.Destroy();
//   m_post_processing_stages.clear();
#endif

  m_display_uniform_buffer.Destroy(false);
  g_d3d12_context->GetSamplerHeapManager().Free(m_linear_sampler);
  m_linear_sampler = {};
  g_d3d12_context->GetSamplerHeapManager().Free(m_point_sampler);
  m_point_sampler = {};
  m_software_cursor_pipeline.Reset();
  m_display_pipeline.Reset();
  m_display_root_signature.Reset();
}

bool D3D12HostDisplay::CreateImGuiContext()
{
#ifdef WITH_IMGUI
  ImGui::GetIO().DisplaySize.x = static_cast<float>(m_window_info.surface_width);
  ImGui::GetIO().DisplaySize.y = static_cast<float>(m_window_info.surface_height);

  if (!ImGui_ImplDX12_Init(g_d3d12_context->GetDevice(), D3D12::Context::NUM_COMMAND_LISTS, DXGI_FORMAT_R8G8B8A8_UNORM))
    return false;

  ImGui_ImplDX12_NewFrame();
#endif
  return true;
}

void D3D12HostDisplay::DestroyImGuiContext()
{
#ifdef WITH_IMGUI
  ImGui_ImplDX12_Shutdown();
#endif
}

bool D3D12HostDisplay::Render()
{
#ifndef LIBRETRO
  if (ShouldSkipDisplayingFrame())
  {
#ifdef WITH_IMGUI
    if (ImGui::GetCurrentContext())
    {
      ImGui::Render();
      ImGui_ImplDX12_NewFrame();
    }
#endif

    return false;
  }

  static constexpr std::array<float, 4> clear_color = {};
  D3D12::Texture& swap_chain_buf = m_swap_chain_buffers[m_current_swap_chain_buffer];
  m_current_swap_chain_buffer = ((m_current_swap_chain_buffer + 1) % static_cast<u32>(m_swap_chain_buffers.size()));

  ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();
  swap_chain_buf.TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET);
  cmdlist->ClearRenderTargetView(swap_chain_buf.GetRTVOrDSVDescriptor(), clear_color.data(), 0, nullptr);
  cmdlist->OMSetRenderTargets(1, &swap_chain_buf.GetRTVOrDSVDescriptor().cpu_handle, FALSE, nullptr);
  cmdlist->SetGraphicsRootSignature(m_display_root_signature.Get());

  RenderDisplay(cmdlist);

#ifdef WITH_IMGUI
  if (ImGui::GetCurrentContext())
    RenderImGui(cmdlist);
#endif

  RenderSoftwareCursor(cmdlist);

  swap_chain_buf.TransitionToState(D3D12_RESOURCE_STATE_PRESENT);
  g_d3d12_context->ExecuteCommandList(false);

  if (!m_vsync && m_using_allow_tearing)
    m_swap_chain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
  else
    m_swap_chain->Present(BoolToUInt32(m_vsync), 0);

#ifdef WITH_IMGUI
  if (ImGui::GetCurrentContext())
    ImGui_ImplDX12_NewFrame();
#endif

#endif

  return true;
}

void D3D12HostDisplay::RenderImGui(ID3D12GraphicsCommandList* cmdlist)
{
#ifdef WITH_IMGUI
  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdlist);
#endif
}

void D3D12HostDisplay::RenderDisplay(ID3D12GraphicsCommandList* cmdlist)
{
  if (!HasDisplayTexture())
    return;

  const auto [left, top, width, height] = CalculateDrawRect(GetWindowWidth(), GetWindowHeight(), m_display_top_margin);

#ifndef LIBRETRO
//   if (!m_post_processing_chain.IsEmpty())
//   {
//     ApplyPostProcessingChain(m_swap_chain_rtv.Get(), left, top, width, height, m_display_texture_handle,
//                              m_display_texture_width, m_display_texture_height, m_display_texture_view_x,
//                              m_display_texture_view_y, m_display_texture_view_width, m_display_texture_view_height);
//     return;
//   }
#endif

  RenderDisplay(cmdlist, left, top, width, height, m_display_texture_handle, m_display_texture_width,
                m_display_texture_height, m_display_texture_view_x, m_display_texture_view_y,
                m_display_texture_view_width, m_display_texture_view_height, m_display_linear_filtering);
}

void D3D12HostDisplay::RenderDisplay(ID3D12GraphicsCommandList* cmdlist, s32 left, s32 top, s32 width, s32 height,
                                     void* texture_handle, u32 texture_width, s32 texture_height, s32 texture_view_x,
                                     s32 texture_view_y, s32 texture_view_width, s32 texture_view_height,
                                     bool linear_filter)
{
  const float uniforms[4] = {static_cast<float>(texture_view_x) / static_cast<float>(texture_width),
                             static_cast<float>(texture_view_y) / static_cast<float>(texture_height),
                             (static_cast<float>(texture_view_width) - 0.5f) / static_cast<float>(texture_width),
                             (static_cast<float>(texture_view_height) - 0.5f) / static_cast<float>(texture_height)};
  if (!m_display_uniform_buffer.ReserveMemory(sizeof(uniforms), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
    Panic("Failed to reserve UBO space");

  const u32 ubo_offset = m_display_uniform_buffer.GetCurrentOffset();
  std::memcpy(m_display_uniform_buffer.GetCurrentHostPointer(), uniforms, sizeof(uniforms));
  m_display_uniform_buffer.CommitMemory(sizeof(uniforms));

  cmdlist->SetPipelineState(m_display_pipeline.Get());
  cmdlist->SetGraphicsRootConstantBufferView(0, m_display_uniform_buffer.GetGPUPointer() + ubo_offset);
  cmdlist->SetGraphicsRootDescriptorTable(1, reinterpret_cast<D3D12::Texture*>(texture_handle)->GetSRVDescriptor());
  cmdlist->SetGraphicsRootDescriptorTable(2, linear_filter ? m_linear_sampler : m_point_sampler);

  D3D12::SetViewportAndScissor(cmdlist, left, top, width, height);

  cmdlist->DrawInstanced(3, 1, 0, 0);
}

void D3D12HostDisplay::RenderSoftwareCursor(ID3D12GraphicsCommandList* cmdlist)
{
  if (!HasSoftwareCursor())
    return;

  const auto [left, top, width, height] = CalculateSoftwareCursorDrawRect();
  RenderSoftwareCursor(cmdlist, left, top, width, height, m_cursor_texture.get());
}

void D3D12HostDisplay::RenderSoftwareCursor(ID3D12GraphicsCommandList* cmdlist, s32 left, s32 top, s32 width,
                                            s32 height, HostDisplayTexture* texture_handle)
{
  const float uniforms[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  if (!m_display_uniform_buffer.ReserveMemory(sizeof(uniforms), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
    Panic("Failed to reserve UBO space");

  const u32 ubo_offset = m_display_uniform_buffer.GetCurrentOffset();
  std::memcpy(m_display_uniform_buffer.GetCurrentHostPointer(), uniforms, sizeof(uniforms));
  m_display_uniform_buffer.CommitMemory(sizeof(uniforms));

  cmdlist->SetPipelineState(m_display_pipeline.Get());
  cmdlist->SetGraphicsRootConstantBufferView(0, m_display_uniform_buffer.GetGPUPointer() + ubo_offset);
  cmdlist->SetGraphicsRootDescriptorTable(
    1, static_cast<D3D12HostDisplayTexture*>(texture_handle)->GetTexture().GetRTVOrDSVDescriptor());
  cmdlist->SetGraphicsRootDescriptorTable(2, m_linear_sampler);

  D3D12::SetViewportAndScissor(cmdlist, left, top, width, height);

  cmdlist->DrawInstanced(3, 1, 0, 0);
}

#ifndef LIBRETRO

D3D12HostDisplay::AdapterInfo D3D12HostDisplay::GetAdapterInfo()
{
  ComPtr<IDXGIFactory> dxgi_factory;
  HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(dxgi_factory.GetAddressOf()));
  if (FAILED(hr))
    return {};

  return GetAdapterInfo(dxgi_factory.Get());
}

D3D12HostDisplay::AdapterInfo D3D12HostDisplay::GetAdapterInfo(IDXGIFactory* dxgi_factory)
{
  AdapterInfo adapter_info;
  ComPtr<IDXGIAdapter> current_adapter;
  while (SUCCEEDED(dxgi_factory->EnumAdapters(static_cast<UINT>(adapter_info.adapter_names.size()),
                                              current_adapter.ReleaseAndGetAddressOf())))
  {
    DXGI_ADAPTER_DESC adapter_desc;
    std::string adapter_name;
    if (SUCCEEDED(current_adapter->GetDesc(&adapter_desc)))
    {
      char adapter_name_buffer[128];
      const int name_length = WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description,
                                                  static_cast<int>(std::wcslen(adapter_desc.Description)),
                                                  adapter_name_buffer, countof(adapter_name_buffer), 0, nullptr);
      if (name_length >= 0)
        adapter_name.assign(adapter_name_buffer, static_cast<size_t>(name_length));
      else
        adapter_name.assign("(Unknown)");
    }
    else
    {
      adapter_name.assign("(Unknown)");
    }

    if (adapter_info.fullscreen_modes.empty())
    {
      ComPtr<IDXGIOutput> output;
      if (SUCCEEDED(current_adapter->EnumOutputs(0, &output)))
      {
        UINT num_modes = 0;
        if (SUCCEEDED(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, nullptr)))
        {
          std::vector<DXGI_MODE_DESC> modes(num_modes);
          if (SUCCEEDED(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, modes.data())))
          {
            for (const DXGI_MODE_DESC& mode : modes)
            {
              adapter_info.fullscreen_modes.push_back(StringUtil::StdStringFromFormat(
                "%u x %u @ %f hz", mode.Width, mode.Height,
                static_cast<float>(mode.RefreshRate.Numerator) / static_cast<float>(mode.RefreshRate.Denominator)));
            }
          }
        }
      }
    }

    // handle duplicate adapter names
    if (std::any_of(adapter_info.adapter_names.begin(), adapter_info.adapter_names.end(),
                    [&adapter_name](const std::string& other) { return (adapter_name == other); }))
    {
      std::string original_adapter_name = std::move(adapter_name);

      u32 current_extra = 2;
      do
      {
        adapter_name = StringUtil::StdStringFromFormat("%s (%u)", original_adapter_name.c_str(), current_extra);
        current_extra++;
      } while (std::any_of(adapter_info.adapter_names.begin(), adapter_info.adapter_names.end(),
                           [&adapter_name](const std::string& other) { return (adapter_name == other); }));
    }

    adapter_info.adapter_names.push_back(std::move(adapter_name));
  }

  return adapter_info;
}

bool D3D12HostDisplay::SetPostProcessingChain(const std::string_view& config)
{
  return false;
}

#else // LIBRETRO

bool D3D12HostDisplay::SetPostProcessingChain(const std::string_view& config)
{
  return false;
}

#endif

} // namespace FrontendCommon
