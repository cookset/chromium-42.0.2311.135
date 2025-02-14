// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/gpu_surfaceless_browser_compositor_output_surface.h"

#include "cc/output/compositor_frame.h"
#include "content/browser/compositor/buffer_queue.h"
#include "content/browser/compositor/reflector_impl.h"
#include "content/browser/gpu/gpu_surface_tracker.h"
#include "content/common/gpu/client/context_provider_command_buffer.h"
#include "content/common/gpu/client/gl_helper.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/gles2_interface.h"

namespace content {

GpuSurfacelessBrowserCompositorOutputSurface::
    GpuSurfacelessBrowserCompositorOutputSurface(
        const scoped_refptr<ContextProviderCommandBuffer>& context,
        int surface_id,
        IDMap<BrowserCompositorOutputSurface>* output_surface_map,
        const scoped_refptr<ui::CompositorVSyncManager>& vsync_manager,
        scoped_ptr<cc::OverlayCandidateValidator> overlay_candidate_validator,
        unsigned internalformat,
        bool use_own_gl_helper,
        BrowserGpuMemoryBufferManager* gpu_memory_buffer_manager)
    : GpuBrowserCompositorOutputSurface(context,
                                        surface_id,
                                        output_surface_map,
                                        vsync_manager,
                                        overlay_candidate_validator.Pass()),
      internalformat_(internalformat),
      gpu_memory_buffer_manager_(gpu_memory_buffer_manager) {
  capabilities_.uses_default_gl_framebuffer = false;
  capabilities_.flipped_output_surface = true;
  // TODO(alexst): Can't enable this on ARM since it hangs the device
  // (crbug.com/470185). Alternatively remove this entirely once the compositor
  // scheduling is optimized enough to work well with surfaceless.
#if defined(ARCH_CPU_X86_FAMILY)
  capabilities_.max_frames_pending = 2;
#endif
}

GpuSurfacelessBrowserCompositorOutputSurface::
    ~GpuSurfacelessBrowserCompositorOutputSurface() {
}

void GpuSurfacelessBrowserCompositorOutputSurface::SwapBuffers(
    cc::CompositorFrame* frame) {
  DCHECK(output_surface_);

  GLuint texture = output_surface_->current_texture_id();
  output_surface_->SwapBuffers(frame->gl_frame_data->sub_buffer_rect);
  const gfx::Size& size = frame->gl_frame_data->size;
  context_provider_->ContextGL()->ScheduleOverlayPlaneCHROMIUM(
      0,
      GL_OVERLAY_TRANSFORM_NONE_CHROMIUM,
      texture,
      0,
      0,
      size.width(),
      size.height(),
      0,
      0,
      1.0f,
      1.0f);
  GpuBrowserCompositorOutputSurface::SwapBuffers(frame);
}

void GpuSurfacelessBrowserCompositorOutputSurface::OnSwapBuffersComplete() {
  DCHECK(output_surface_);
  output_surface_->PageFlipComplete();
  GpuBrowserCompositorOutputSurface::OnSwapBuffersComplete();
}

void GpuSurfacelessBrowserCompositorOutputSurface::BindFramebuffer() {
  DCHECK(output_surface_);
  output_surface_->BindFramebuffer();
}

void GpuSurfacelessBrowserCompositorOutputSurface::Reshape(
    const gfx::Size& size,
    float scale_factor) {
  GpuBrowserCompositorOutputSurface::Reshape(size, scale_factor);
  DCHECK(output_surface_);
  output_surface_->Reshape(SurfaceSize(), scale_factor);
}

bool GpuSurfacelessBrowserCompositorOutputSurface::BindToClient(
    cc::OutputSurfaceClient* client) {
  if (!GpuBrowserCompositorOutputSurface::BindToClient(client))
    return false;
  gl_helper_.reset(new GLHelper(context_provider_->ContextGL(),
                                context_provider_->ContextSupport()));
  output_surface_.reset(
      new BufferQueue(context_provider_, internalformat_, gl_helper_.get(),
                      gpu_memory_buffer_manager_, surface_id_));
  return output_surface_->Initialize();
}

}  // namespace content
