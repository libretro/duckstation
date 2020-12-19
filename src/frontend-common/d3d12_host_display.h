#pragma once
#include "common/d3d12/descriptor_heap_manager.h"
#include "common/d3d12/staging_texture.h"
#include "common/d3d12/stream_buffer.h"
#include "common/d3d12/texture.h"
#include "common/window_info.h"
#include "common/windows_headers.h"
#include "core/host_display.h"
#include <d3d12.h>
#include <dxgi.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <wrl/client.h>

namespace FrontendCommon {

class D3D12HostDisplay : public HostDisplay
{
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  D3D12HostDisplay();
  ~D3D12HostDisplay();

  virtual RenderAPI GetRenderAPI() const override;
  virtual void* GetRenderDevice() const override;
  virtual void* GetRenderContext() const override;

  virtual bool HasRenderDevice() const override;
  virtual bool HasRenderSurface() const override;

  virtual bool CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device) override;
  virtual bool InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device) override;
  virtual void DestroyRenderDevice() override;

  virtual bool MakeRenderContextCurrent() override;
  virtual bool DoneRenderContextCurrent() override;

  virtual bool ChangeRenderWindow(const WindowInfo& new_wi) override;
  virtual void ResizeRenderWindow(s32 new_window_width, s32 new_window_height) override;
  virtual bool SupportsFullscreen() const override;
  virtual bool IsFullscreen() override;
  virtual bool SetFullscreen(bool fullscreen, u32 width, u32 height, float refresh_rate) override;
  virtual void DestroyRenderSurface() override;

  virtual bool SetPostProcessingChain(const std::string_view& config) override;

  std::unique_ptr<HostDisplayTexture> CreateTexture(u32 width, u32 height, const void* initial_data,
                                                    u32 initial_data_stride, bool dynamic) override;
  void UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height, const void* texture_data,
                     u32 texture_data_stride) override;
  bool DownloadTexture(const void* texture_handle, u32 x, u32 y, u32 width, u32 height, void* out_data,
                       u32 out_data_stride) override;
  bool SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const override;
  bool BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                             u32* out_pitch) override;
  void EndSetDisplayPixels() override;

  virtual void SetVSync(bool enabled) override;

  virtual bool Render() override;

#ifndef LIBRETRO
  struct AdapterInfo
  {
    std::vector<std::string> adapter_names;
    std::vector<std::string> fullscreen_modes;
  };
  static AdapterInfo GetAdapterInfo();
#endif

protected:
  static constexpr u32 DISPLAY_UNIFORM_BUFFER_SIZE = 65536;

#ifndef LIBRETRO
  static AdapterInfo GetAdapterInfo(IDXGIFactory* dxgi_factory);
#endif

  virtual bool CreateResources() override;
  virtual void DestroyResources() override;

  virtual bool CreateImGuiContext();
  virtual void DestroyImGuiContext();

#ifndef LIBRETRO
  bool CreateSwapChain(const DXGI_MODE_DESC* fullscreen_mode);
  bool CreateSwapChainRTV();
  void DestroySwapChainRTVs();
#endif

  void RenderDisplay(ID3D12GraphicsCommandList* cmdlist);
  void RenderSoftwareCursor(ID3D12GraphicsCommandList* cmdlist);
  void RenderImGui(ID3D12GraphicsCommandList* cmdlist);

  void RenderDisplay(ID3D12GraphicsCommandList* cmdlist, s32 left, s32 top, s32 width, s32 height, void* texture_handle,
                     u32 texture_width, s32 texture_height, s32 texture_view_x, s32 texture_view_y,
                     s32 texture_view_width, s32 texture_view_height, bool linear_filter);
  void RenderSoftwareCursor(ID3D12GraphicsCommandList* cmdlist, s32 left, s32 top, s32 width, s32 height,
                            HostDisplayTexture* texture_handle);

#ifndef LIBRETRO
  ComPtr<IDXGIFactory> m_dxgi_factory;
  ComPtr<IDXGISwapChain> m_swap_chain;
  std::vector<D3D12::Texture> m_swap_chain_buffers;
  u32 m_current_swap_chain_buffer = 0;
#endif

  ComPtr<ID3D12RootSignature> m_display_root_signature;
  ComPtr<ID3D12PipelineState> m_display_pipeline;
  ComPtr<ID3D12PipelineState> m_software_cursor_pipeline;
  D3D12::DescriptorHandle m_point_sampler{};
  D3D12::DescriptorHandle m_linear_sampler{};

  D3D12::Texture m_display_pixels_texture;
  D3D12::StreamBuffer m_display_uniform_buffer;
  D3D12::AutoStagingTexture m_readback_staging_texture;

#ifndef LIBRETRO
  bool m_allow_tearing_supported = false;
  bool m_using_allow_tearing = false;
  bool m_vsync = true;
#endif
};

} // namespace FrontendCommon
