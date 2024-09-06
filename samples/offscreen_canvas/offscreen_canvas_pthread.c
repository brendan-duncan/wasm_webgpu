// This sample is like offscreen_canvas.c, but instead shows how to perform OffscreenCanvas
// rendering with WebGPU from a pthread.

#include <stdio.h>
#include <emscripten/em_math.h>
#include <emscripten/proxying.h>
#include "lib_webgpu.h"

WGpuAdapter adapter;
WGpuDevice device;
WGpuQueue queue;
WGpuCanvasContext canvasContext;
OffscreenCanvasId canvasId;
pthread_t thread;
em_proxying_queue* pthread_queue;

void *dummy_thread_main(void *arg);
void actual_thread_main(void *arg);
void ObtainedWebGpuAdapter(WGpuAdapter result, void *userData);
void ObtainedWebGpuDevice(WGpuDevice result, void *userData);
WGPU_BOOL raf(double time, void *userData);
EM_BOOL window_resize(int eventType, const EmscriptenUiEvent *uiEvent, void *userData);

int main(int argc, char **argv) // runs in main thread
{
  // To render to an existing HTML Canvas element from a Wasm Worker using WebGPU:

  // 1. Let's first create a Wasm Worker to do the rendering. It will start with
  //    a dummy main function, since it needs to wait to receive the WebGPU
  //    canvas via a postMessage.
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create(&thread, &attr, &dummy_thread_main, 0);

  // 2. convert the HTML Canvas element to be renderable from a Worker
  //    by transferring its control to an OffscreenCanvas:

  //    This function must be given a custom ID number, that will be used to
  //    identify that OffscreenCanvas element in C/C++ side code. If you want to
  //    render to multiple Canvases from different Workers, you should devise
  //    some kind of Canvas ID counter scheme here. We'll use a fixed ID 42.

  //    After control has been transferred offscreen, this canvas element will
  //    be permanently offscreen-controlled, there is no browser API to undo
  //    this. (except to delete the DOM element and create a new one)
  canvasId = 42;
  canvas_transfer_control_to_offscreen("canvas", canvasId);

  // 3. Each OffscreenCanvas is owned by exactly one thread, so we must
  //    postMessage() our newly created OffscreenCanvas to the Worker to pass
  //    the ownership. Here we pass the ID of the OffscreenCanvas we created
  //    above. After this line, our main thread cannot access this
  //    OffscreenCanvas anymore.
  offscreen_canvas_post_to_pthread(canvasId, thread);

  // 4. Now the OffscreenCanvas is available for the Worker, so make it start
  //    executing its main function to initialize WebGPU in the Worker and
  //    start rendering.
  pthread_queue = em_proxying_queue_create();
  emscripten_proxy_async(pthread_queue, thread, actual_thread_main, 0);

  // 5. One particular gotcha with rendering with OffscreenCanvas is that resizing the canvas
  //    becomes more complicated. If we want to perform e.g. fullscreen rendering,
  //    we need a custom resize strategy. See the window_resize handler below.
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 0, window_resize);
}

void *dummy_thread_main(void *arg) // runs in pthread
{
  // Do not quit the pthread when falling out of main, but lie dormant for async
  // events to be processed in this pthread context.
  emscripten_exit_with_live_runtime();
}

void actual_thread_main(void *arg) // runs in pthread
{
  navigator_gpu_request_adapter_async_simple(ObtainedWebGpuAdapter);
}

void ObtainedWebGpuAdapter(WGpuAdapter result, void *userData) // runs in pthread
{
  adapter = result;
  wgpu_adapter_request_device_async_simple(adapter, ObtainedWebGpuDevice);
}

void ObtainedWebGpuDevice(WGpuDevice result, void *userData) // runs in pthread
{
  device = result;
  queue = wgpu_device_get_queue(device);

  // 6. Instead of using the usual main thread function
  //      wgpu_canvas_get_webgpu_context(cssSelector);
  //    to initialize a WebGPU context on the canvas, use the Offscreen Canvas
  //    variant of this function
  //      wgpu_offscreen_canvas_get_webgpu_context(OffscreenCanvasId id);
  //    to initialize the context.
  canvasContext = wgpu_offscreen_canvas_get_webgpu_context(canvasId);

  // 7. The rest of the WebGPU engine usage proceeds as it did in the main
  //    thread case.
  WGpuCanvasConfiguration config = WGPU_CANVAS_CONFIGURATION_DEFAULT_INITIALIZER;
  config.device = device;
  config.format = navigator_gpu_get_preferred_canvas_format();
  wgpu_canvas_context_configure(canvasContext, &config);

  // In particular, Web Workers can use requestAnimationFrame() like the main
  // thread to perform rendering.
  wgpu_request_animation_frame_loop(raf, 0);
}

double hue2color(double hue) // runs in pthread
{
  hue = emscripten_math_fmod(hue, 1.0);
  if (hue < 1.0 / 6.0) return 6.0 * hue;
  if (hue < 1.0 / 2.0) return 1;
  if (hue < 2.0 / 3.0) return 4.0 - 6.0 * hue;
  return 0;
}

WGPU_BOOL raf(double time, void *userData) // runs in pthread
{
  WGpuCommandEncoder encoder = wgpu_device_create_command_encoder_simple(device);

  WGpuRenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_DEFAULT_INITIALIZER;
  colorAttachment.view = wgpu_canvas_context_get_current_texture_view(canvasContext);

  double hue = time * 0.00005;
  colorAttachment.clearValue.r = hue2color(hue + 1.0 / 3.0);
  colorAttachment.clearValue.g = hue2color(hue);
  colorAttachment.clearValue.b = hue2color(hue - 1.0 / 3.0);
  colorAttachment.clearValue.a = 1.0;
  colorAttachment.loadOp = WGPU_LOAD_OP_CLEAR;

  WGpuRenderPassDescriptor passDesc = {};
  passDesc.numColorAttachments = 1;
  passDesc.colorAttachments = &colorAttachment;

  wgpu_render_pass_encoder_end(wgpu_command_encoder_begin_render_pass_1color_0depth(encoder, &passDesc));
  wgpu_queue_submit_one_and_destroy(queue, wgpu_command_encoder_finish(encoder));

  return EM_TRUE;
}

int rttWidth, rttHeight;

void resize_offscreen_canvas(void *arg) // runs in pthread
{
  EM_ASM({console.error(`Resized Offscreen Canvas to new size ${$0}x${$1}`)}, rttWidth, rttHeight);
  offscreen_canvas_set_size(canvasId, rttWidth, rttHeight);
}

EM_BOOL window_resize(int eventType, const EmscriptenUiEvent *uiEvent, void *userData) // runs in main thread
{
  double innerWidth = EM_ASM_DOUBLE(return window.innerWidth);
  double innerHeight = EM_ASM_DOUBLE(return window.innerHeight);
  double dpr = EM_ASM_DOUBLE(return window.devicePixelRatio);

  // Recall that the canvas size in web browsers is dictated by two different size fields:
  // a) the canvas CSS style width and height (of type double) that govern the visible CSS
  //    size of the Canvas element on the web page, and
  // b) the render target size of the Canvas front buffer that WebGPU(/WebGL) renders to.
  //    (of type integer).

  // The Canvas render target is stretched to cover the HTML area that the Canvas element
  // encompasses.
  // If we want pixel-perfect rendering, these two sizes need to be modified in sync
  // whenever we want to change the visible canvas size. The size a) is governed from the main
  // thread; however, when using OffscreenCanvas, the size b) must be controlled by the Web
  // Worker that holds ownership of the OffscreenCanvas.

  // Therefore to update the size, modify size a) here in the main browser thread:
  emscripten_set_element_css_size("canvas", innerWidth, innerHeight);

  // ... and update size b) by posting a message to the Worker that owns the OffscreenCanvas
  // so that the render target size is correctly updated in the Worker thread.

  rttWidth = (int)(innerWidth*dpr);
  rttHeight = (int)(innerHeight*dpr);
  emscripten_proxy_async(pthread_queue, thread, resize_offscreen_canvas, 0);
  return EM_TRUE;
}
