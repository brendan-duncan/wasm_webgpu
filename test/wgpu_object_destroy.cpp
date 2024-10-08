// flags: -sEXIT_RUNTIME=0

#include "lib_webgpu.h"
#include <assert.h>

void ObtainedWebGpuDevice(WGpuDevice result, void *userData)
{
  assert(wgpu_get_num_live_objects() == 3); // Adapter, Device and Queue
  wgpu_object_destroy(result); // Destroying device should destroy also the queue.

  assert(wgpu_get_num_live_objects() == 1); // Adapter should remain

  EM_ASM(window.close());
}

void ObtainedWebGpuAdapter(WGpuAdapter result, void *userData)
{
  wgpu_adapter_request_device_async_simple(result, ObtainedWebGpuDevice);
}

int main()
{
  navigator_gpu_request_adapter_async_simple(ObtainedWebGpuAdapter);
}
