/*
 * xdtest: on-device Vulkan tests for the Xclipse 940 driver work.
 *
 * Loads either the phone's stock Vulkan stack (libvulkan.so, which loads Samsung's driver) or a
 * Mesa Android HAL driver such as vulkan.radeon.so, runs one test, and prints one JSON object on
 * stdout. Progress goes to stderr, so a crash still shows the last step reached. The same binary
 * drives both drivers, so every result is an A/B pair.
 *
 * usage: xdtest --driver stock|PATH --test info|device|submit|compute|render [--out DIR] [--tag NAME]
 */
#define VK_NO_PROTOTYPES
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <hardware/hwvulkan.h>
#include <vulkan/vulkan.h>

#include "fill_comp.h"
#include "tri_frag.h"
#include "tri_vert.h"

/* A fence that hasn't signaled after this long means the GPU stopped responding. */
#define GPU_TIMEOUT_NS 5000000000ull

/* ---- JSON output: one object, streamed as the test runs ---- */

static int jdepth;
static bool jfirst[16] = {true};
static char jclose_ch[16];

static void jsep(void)
{
   if (!jfirst[jdepth])
      putchar(',');
   jfirst[jdepth] = false;
}

static void jputs(const char *s)
{
   putchar('"');
   for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
      if (*p == '"' || *p == '\\')
         printf("\\%c", *p);
      else if (*p < 0x20)
         printf("\\u%04x", *p);
      else
         putchar(*p);
   }
   putchar('"');
}

static void jkey(const char *k)
{
   jsep();
   if (k) {
      jputs(k);
      putchar(':');
   }
}

static void jopen(const char *k, char open)
{
   jkey(k);
   putchar(open);
   jdepth++;
   jfirst[jdepth] = true;
   jclose_ch[jdepth] = open == '{' ? '}' : ']';
}

static void jclose(void)
{
   putchar(jclose_ch[jdepth]);
   jdepth--;
}

static void jstr(const char *k, const char *v) { jkey(k); jputs(v ? v : ""); }
static void jint(const char *k, long long v) { jkey(k); printf("%lld", v); }
static void jnum(const char *k, double v) { jkey(k); printf("%.3f", v); }
static void jbool(const char *k, bool v) { jkey(k); fputs(v ? "true" : "false", stdout); }

static void jhex(const char *k, uint32_t v)
{
   char buf[16];
   snprintf(buf, sizeof(buf), "0x%x", v);
   jstr(k, buf);
}

/* Ends the run: closes any open JSON levels, records the status and exits. */
static void finish(const char *status, const char *fmt, ...)
{
   while (jdepth > 1)
      jclose();
   jstr("status", status);
   if (fmt) {
      char msg[512];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(msg, sizeof(msg), fmt, ap);
      va_end(ap);
      jstr("error", msg);
      fprintf(stderr, "[xdtest] %s: %s\n", status, msg);
   }
   jclose();
   putchar('\n');
   fflush(stdout);
   _exit(strcmp(status, "pass") == 0 ? 0 : 1);
}

#define VKCHECK(name, call)                                                                        \
   do {                                                                                            \
      VkResult r_ = (call);                                                                        \
      if (r_ != VK_SUCCESS)                                                                        \
         finish("fail", "%s failed: VkResult %d", name, r_);                                       \
   } while (0)

static void step(const char *what) { fprintf(stderr, "[xdtest] %s\n", what); }

static double now_ms(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* ---- Vulkan entry points, loaded by hand because there may be no loader ---- */

#define INSTANCE_FUNCS(X)                                                                          \
   X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties2)            \
   X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceMemoryProperties)                          \
   X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkEnumerateDeviceExtensionProperties)             \
   X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)

#define DEVICE_FUNCS(X)                                                                            \
   X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkQueueSubmit) X(vkCreateFence) X(vkDestroyFence)       \
   X(vkWaitForFences) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers)    \
   X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateBuffer) X(vkDestroyBuffer)               \
   X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory)       \
   X(vkMapMemory) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout)   \
   X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout)             \
   X(vkCreateComputePipelines) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline)                    \
   X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets)                 \
   X(vkUpdateDescriptorSets) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)       \
   X(vkCmdPipelineBarrier) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements)       \
   X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCmdBeginRendering)           \
   X(vkCmdEndRendering) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdDraw)                         \
   X(vkCmdCopyImageToBuffer)

#define DECLARE(fn) static PFN_##fn fn;
INSTANCE_FUNCS(DECLARE)
DEVICE_FUNCS(DECLARE)

static PFN_vkGetInstanceProcAddr gipa;
static PFN_vkCreateInstance create_instance_fn;

static VkInstance inst;
static VkPhysicalDevice pdev;
static VkPhysicalDeviceProperties props;
static VkPhysicalDeviceMemoryProperties memprops;
static VkQueueFamilyProperties qfams[16];
static uint32_t qfam_count = 16;
static uint32_t qfam = UINT32_MAX;
static VkDevice dev;
static VkQueue queue;
static VkCommandPool pool;

static const char *out_dir = "/data/local/tmp/xd/out";
static const char *tag;

/* "stock" goes through the system loader; anything else is a path to an Android HAL driver. */
static const char *load_driver(const char *spec)
{
   if (!strcmp(spec, "stock")) {
      void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
      if (!lib)
         return dlerror();
      gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
      if (!gipa)
         return "libvulkan.so has no vkGetInstanceProcAddr";
      create_instance_fn = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
      return create_instance_fn ? NULL : "vkCreateInstance missing";
   }

   void *lib = dlopen(spec, RTLD_NOW | RTLD_LOCAL);
   if (!lib)
      return dlerror();
   hwvulkan_module_t *mod = (hwvulkan_module_t *)dlsym(lib, HAL_MODULE_INFO_SYM_AS_STR);
   if (!mod || !mod->common.methods || !mod->common.methods->open)
      return "no Vulkan HAL module (HMI) in the driver";
   hw_device_t *hd = NULL;
   if (mod->common.methods->open(&mod->common, HWVULKAN_DEVICE_0, &hd) || !hd)
      return "HAL open failed";
   hwvulkan_device_t *hal = (hwvulkan_device_t *)hd;
   gipa = hal->GetInstanceProcAddr;
   create_instance_fn = hal->CreateInstance;
   return gipa && create_instance_fn ? NULL : "HAL entry points missing";
}

static void start_instance(void)
{
   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "xdtest",
      .apiVersion = VK_API_VERSION_1_3,
   };
   VkInstanceCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   step("creating instance");
   double t = now_ms();
   VKCHECK("vkCreateInstance", create_instance_fn(&ici, NULL, &inst));
   jnum("ms_instance", now_ms() - t);

   const char *missing = NULL;
#define LOAD_INSTANCE(fn)                                                                          \
   if (!(fn = (PFN_##fn)gipa(inst, #fn)) && !missing)                                              \
      missing = #fn;
   INSTANCE_FUNCS(LOAD_INSTANCE)
   if (missing)
      finish("fail", "instance function %s missing", missing);

   uint32_t n = 1;
   VkResult r = vkEnumeratePhysicalDevices(inst, &n, &pdev);
   if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0)
      finish("fail", "no physical device (VkResult %d, count %u)", r, n);

   VkPhysicalDeviceVulkan11Properties p11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
   };
   VkPhysicalDeviceDriverProperties drv = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
      .pNext = &p11,
   };
   VkPhysicalDeviceProperties2 p2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &drv,
   };
   vkGetPhysicalDeviceProperties2(pdev, &p2);
   props = p2.properties;

   char api[32];
   snprintf(api, sizeof(api), "%u.%u.%u", VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
   jstr("device_name", props.deviceName);
   jstr("api_version", api);
   jhex("driver_version", props.driverVersion);
   jhex("vendor_id", props.vendorID);
   jhex("device_id", props.deviceID);
   jint("driver_id", drv.driverID);
   jstr("driver_name", drv.driverName);
   jstr("driver_info", drv.driverInfo);
   jint("subgroup_size", p11.subgroupSize);

   vkGetPhysicalDeviceMemoryProperties(pdev, &memprops);
   vkGetPhysicalDeviceQueueFamilyProperties(pdev, &qfam_count, qfams);
   const VkQueueFlags want = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
   for (uint32_t i = 0; i < qfam_count; i++) {
      if ((qfams[i].queueFlags & want) == want) {
         qfam = i;
         break;
      }
   }
}

/* ---- info: what the driver exposes, especially what DXVK and vkd3d-proton care about ---- */

static void run_info(void)
{
   VkPhysicalDeviceVulkan13Features f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
   };
   VkPhysicalDeviceVulkan12Features f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &f13,
   };
   VkPhysicalDeviceFeatures2 f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &f12,
   };
   vkGetPhysicalDeviceFeatures2(pdev, &f2);
   const VkPhysicalDeviceFeatures *f = &f2.features;

   jopen("features", '{');
   jbool("textureCompressionBC", f->textureCompressionBC);
   jbool("textureCompressionETC2", f->textureCompressionETC2);
   jbool("textureCompressionASTC_LDR", f->textureCompressionASTC_LDR);
   jbool("geometryShader", f->geometryShader);
   jbool("tessellationShader", f->tessellationShader);
   jbool("shaderFloat64", f->shaderFloat64);
   jbool("shaderInt64", f->shaderInt64);
   jbool("shaderInt16", f->shaderInt16);
   jbool("depthClamp", f->depthClamp);
   jbool("depthBounds", f->depthBounds);
   jbool("fillModeNonSolid", f->fillModeNonSolid);
   jbool("wideLines", f->wideLines);
   jbool("logicOp", f->logicOp);
   jbool("dualSrcBlend", f->dualSrcBlend);
   jbool("multiViewport", f->multiViewport);
   jbool("samplerAnisotropy", f->samplerAnisotropy);
   jbool("pipelineStatisticsQuery", f->pipelineStatisticsQuery);
   jbool("occlusionQueryPrecise", f->occlusionQueryPrecise);
   jbool("shaderClipDistance", f->shaderClipDistance);
   jbool("shaderCullDistance", f->shaderCullDistance);
   jbool("vertexPipelineStoresAndAtomics", f->vertexPipelineStoresAndAtomics);
   jbool("fragmentStoresAndAtomics", f->fragmentStoresAndAtomics);
   jbool("shaderStorageImageWriteWithoutFormat", f->shaderStorageImageWriteWithoutFormat);
   jbool("robustBufferAccess", f->robustBufferAccess);
   jbool("samplerFilterMinmax", f12.samplerFilterMinmax);
   jbool("shaderFloat16", f12.shaderFloat16);
   jbool("descriptorIndexing", f12.descriptorIndexing);
   jbool("timelineSemaphore", f12.timelineSemaphore);
   jbool("bufferDeviceAddress", f12.bufferDeviceAddress);
   jbool("vulkanMemoryModel", f12.vulkanMemoryModel);
   jbool("dynamicRendering", f13.dynamicRendering);
   jbool("synchronization2", f13.synchronization2);
   jbool("maintenance4", f13.maintenance4);
   jclose();

   static const struct {
      const char *name;
      VkFormat format;
   } formats[] = {
      {"BC1_RGBA_UNORM", VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
      {"BC3_UNORM", VK_FORMAT_BC3_UNORM_BLOCK},
      {"BC4_UNORM", VK_FORMAT_BC4_UNORM_BLOCK},
      {"BC5_UNORM", VK_FORMAT_BC5_UNORM_BLOCK},
      {"BC6H_UFLOAT", VK_FORMAT_BC6H_UFLOAT_BLOCK},
      {"BC7_UNORM", VK_FORMAT_BC7_UNORM_BLOCK},
      {"ETC2_R8G8B8A8_UNORM", VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK},
      {"ASTC_4x4_UNORM", VK_FORMAT_ASTC_4x4_UNORM_BLOCK},
      {"R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
      {"B10G11R11_UFLOAT", VK_FORMAT_B10G11R11_UFLOAT_PACK32},
      {"D24_UNORM_S8_UINT", VK_FORMAT_D24_UNORM_S8_UINT},
      {"D32_SFLOAT_S8_UINT", VK_FORMAT_D32_SFLOAT_S8_UINT},
   };
   jopen("format_optimal_features", '{');
   for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
      VkFormatProperties fp;
      vkGetPhysicalDeviceFormatProperties(pdev, formats[i].format, &fp);
      jhex(formats[i].name, fp.optimalTilingFeatures);
   }
   jclose();

   jopen("limits", '{');
   jint("maxImageDimension2D", props.limits.maxImageDimension2D);
   jint("maxComputeWorkGroupInvocations", props.limits.maxComputeWorkGroupInvocations);
   jint("maxComputeSharedMemorySize", props.limits.maxComputeSharedMemorySize);
   jint("maxBoundDescriptorSets", props.limits.maxBoundDescriptorSets);
   jint("maxPerStageDescriptorSampledImages", props.limits.maxPerStageDescriptorSampledImages);
   jint("maxPushConstantsSize", props.limits.maxPushConstantsSize);
   jint("maxMemoryAllocationCount", props.limits.maxMemoryAllocationCount);
   jnum("timestampPeriod", props.limits.timestampPeriod);
   jclose();

   jopen("memory_heaps", '[');
   for (uint32_t i = 0; i < memprops.memoryHeapCount; i++) {
      jopen(NULL, '{');
      jint("size_mb", (long long)(memprops.memoryHeaps[i].size >> 20));
      jhex("flags", memprops.memoryHeaps[i].flags);
      jclose();
   }
   jclose();
   jopen("memory_types", '[');
   for (uint32_t i = 0; i < memprops.memoryTypeCount; i++) {
      jopen(NULL, '{');
      jint("heap", memprops.memoryTypes[i].heapIndex);
      jhex("flags", memprops.memoryTypes[i].propertyFlags);
      jclose();
   }
   jclose();
   jopen("queue_families", '[');
   for (uint32_t i = 0; i < qfam_count; i++) {
      jopen(NULL, '{');
      jhex("flags", qfams[i].queueFlags);
      jint("count", qfams[i].queueCount);
      jint("timestamp_bits", qfams[i].timestampValidBits);
      jclose();
   }
   jclose();

   uint32_t n = 0;
   vkEnumerateDeviceExtensionProperties(pdev, NULL, &n, NULL);
   VkExtensionProperties *ext = calloc(n ? n : 1, sizeof(*ext));
   vkEnumerateDeviceExtensionProperties(pdev, NULL, &n, ext);
   jint("extension_count", n);
   jopen("extensions", '[');
   for (uint32_t i = 0; i < n; i++)
      jstr(NULL, ext[i].extensionName);
   jclose();
   free(ext);
}

/* ---- device and queue ---- */

static void start_device(bool dynamic_rendering)
{
   if (qfam == UINT32_MAX)
      finish("fail", "no queue family with both graphics and compute");
   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = qfam,
      .queueCount = 1,
      .pQueuePriorities = &prio,
   };
   VkPhysicalDeviceVulkan13Features f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .dynamicRendering = dynamic_rendering,
   };
   VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &f13,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &qci,
   };
   step("creating device");
   double t = now_ms();
   VKCHECK("vkCreateDevice", vkCreateDevice(pdev, &dci, NULL, &dev));
   jnum("ms_device", now_ms() - t);

   const char *missing = NULL;
#define LOAD_DEVICE(fn)                                                                            \
   if (!(fn = (PFN_##fn)vkGetDeviceProcAddr(dev, #fn)) && !missing)                                \
      missing = #fn;
   DEVICE_FUNCS(LOAD_DEVICE)
   if (missing)
      finish("fail", "device function %s missing", missing);
   vkGetDeviceQueue(dev, qfam, 0, &queue);
}

static VkCommandBuffer begin_cmd(void)
{
   if (!pool) {
      VkCommandPoolCreateInfo pci = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .queueFamilyIndex = qfam,
      };
      VKCHECK("vkCreateCommandPool", vkCreateCommandPool(dev, &pci, NULL, &pool));
   }
   VkCommandBufferAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer cb;
   VKCHECK("vkAllocateCommandBuffers", vkAllocateCommandBuffers(dev, &ai, &cb));
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   VKCHECK("vkBeginCommandBuffer", vkBeginCommandBuffer(cb, &bi));
   return cb;
}

/* Submits cb (or no command buffers at all) and waits. Returns the wall time in ms. */
static double submit_and_wait(VkCommandBuffer cb, const char *what)
{
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   VKCHECK("vkCreateFence", vkCreateFence(dev, &fci, NULL, &fence));
   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &cb,
   };
   step(what);
   double t = now_ms();
   VKCHECK("vkQueueSubmit", vkQueueSubmit(queue, cb ? 1 : 0, cb ? &si : NULL, fence));
   VkResult r = vkWaitForFences(dev, 1, &fence, VK_TRUE, GPU_TIMEOUT_NS);
   double ms = now_ms() - t;
   if (r == VK_TIMEOUT)
      finish("fail", "%s: GPU did not finish within %llu ms", what, GPU_TIMEOUT_NS / 1000000);
   if (r != VK_SUCCESS)
      finish("fail", "%s: vkWaitForFences returned %d", what, r);
   vkDestroyFence(dev, fence, NULL);
   return ms;
}

static int find_memory(uint32_t type_bits, VkMemoryPropertyFlags want)
{
   for (uint32_t i = 0; i < memprops.memoryTypeCount; i++)
      if ((type_bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want)
         return (int)i;
   return -1;
}

struct buffer {
   VkBuffer buf;
   VkDeviceMemory mem;
   void *map;
};

/* A host-visible, coherent buffer, mapped for its whole life. */
static struct buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
   struct buffer b = {0};
   VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   VKCHECK("vkCreateBuffer", vkCreateBuffer(dev, &bci, NULL, &b.buf));
   VkMemoryRequirements req;
   vkGetBufferMemoryRequirements(dev, b.buf, &req);
   int type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   if (type < 0)
      finish("fail", "no host-visible coherent memory type for a buffer");
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = (uint32_t)type,
   };
   VKCHECK("vkAllocateMemory", vkAllocateMemory(dev, &mai, NULL, &b.mem));
   VKCHECK("vkBindBufferMemory", vkBindBufferMemory(dev, b.buf, b.mem, 0));
   VKCHECK("vkMapMemory", vkMapMemory(dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map));
   return b;
}

static void free_buffer(struct buffer *b)
{
   vkDestroyBuffer(dev, b->buf, NULL);
   vkFreeMemory(dev, b->mem, NULL);
}

static VkShaderModule make_shader(const uint32_t *code, size_t size)
{
   VkShaderModuleCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = size,
      .pCode = code,
   };
   VkShaderModule m;
   VKCHECK("vkCreateShaderModule", vkCreateShaderModule(dev, &ci, NULL, &m));
   return m;
}

/* ---- submit: a fence-only submit and an empty command buffer ---- */

static void run_submit(void)
{
   jnum("ms_fence_only", submit_and_wait(VK_NULL_HANDLE, "fence-only submit"));
   VkCommandBuffer cb = begin_cmd();
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   jnum("ms_empty_cmdbuf", submit_and_wait(cb, "empty command buffer submit"));
}

/* ---- compute: a dispatch that writes a pattern the host checks element by element ---- */

static void run_compute(void)
{
   enum { N = 65536 };
   struct buffer b = make_buffer(N * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
   memset(b.map, 0xee, N * sizeof(uint32_t)); /* poison, so missed writes show up */

   VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &binding,
   };
   VkDescriptorSetLayout dsl;
   VKCHECK("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
   };
   VkPipelineLayout layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &layout));

   VkShaderModule cs = make_shader(fill_comp, sizeof(fill_comp));
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = cs,
            .pName = "main",
         },
      .layout = layout,
   };
   VkPipeline pipeline;
   step("creating compute pipeline");
   double t = now_ms();
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));
   jnum("ms_pipeline", now_ms() - t);

   VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size,
   };
   VkDescriptorPool dpool;
   VKCHECK("vkCreateDescriptorPool", vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dpool,
      .descriptorSetCount = 1,
      .pSetLayouts = &dsl,
   };
   VkDescriptorSet set;
   VKCHECK("vkAllocateDescriptorSets", vkAllocateDescriptorSets(dev, &dsai, &set));
   VkDescriptorBufferInfo dbi = {b.buf, 0, VK_WHOLE_SIZE};
   VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &dbi,
   };
   vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);

   VkCommandBuffer cb = begin_cmd();
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
   vkCmdDispatch(cb, N / 64, 1, 1);
   VkMemoryBarrier to_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                        &to_host, 0, NULL, 0, NULL);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   jnum("ms_gpu", submit_and_wait(cb, "compute dispatch"));

   const uint32_t *data = b.map;
   uint32_t bad = 0, first = 0;
   for (uint32_t i = 0; i < N; i++) {
      if (data[i] != i * 3u + 7u && bad++ == 0)
         first = i;
   }
   jint("elements", N);
   jint("mismatches", bad);
   if (bad)
      finish("fail", "%u of %u elements wrong; first at %u: got 0x%08x, want 0x%08x", bad, N, first,
             data[first], first * 3u + 7u);

   vkDestroyDescriptorPool(dev, dpool, NULL);
   vkDestroyPipeline(dev, pipeline, NULL);
   vkDestroyShaderModule(dev, cs, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   free_buffer(&b);
}

/* ---- render: one triangle offscreen, read back and checked pixel by pixel ---- */

static void save_ppm(const uint8_t *rgba, int w, int h, const char *name)
{
   char path[512];
   snprintf(path, sizeof(path), "%s/%s_%s.ppm", out_dir, name, tag);
   FILE *f = fopen(path, "wb");
   if (!f) {
      fprintf(stderr, "[xdtest] warning: can't write %s\n", path);
      return;
   }
   fprintf(f, "P6\n%d %d\n255\n", w, h);
   for (int i = 0; i < w * h; i++)
      fwrite(rgba + i * 4, 1, 3, f);
   fclose(f);
   jstr("image", path);
}

static bool near(const uint8_t *px, int r, int g, int b, int a)
{
   const int want[4] = {r, g, b, a};
   for (int i = 0; i < 4; i++)
      if (abs(px[i] - want[i]) > 2)
         return false;
   return true;
}

static void run_render(void)
{
   enum { W = 128, H = 128 };
   const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {W, H, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkImage image;
   VKCHECK("vkCreateImage", vkCreateImage(dev, &ici, NULL, &image));
   VkMemoryRequirements req;
   vkGetImageMemoryRequirements(dev, image, &req);
   int type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   if (type < 0)
      type = find_memory(req.memoryTypeBits, 0);
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = (uint32_t)type,
   };
   VkDeviceMemory image_mem;
   VKCHECK("vkAllocateMemory", vkAllocateMemory(dev, &mai, NULL, &image_mem));
   VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, image, image_mem, 0));
   const VkImageSubresourceRange color_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
   VkImageViewCreateInfo vci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = color_range,
   };
   VkImageView view;
   VKCHECK("vkCreateImageView", vkCreateImageView(dev, &vci, NULL, &view));
   struct buffer readback = make_buffer(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

   VkShaderModule vs = make_shader(tri_vert, sizeof(tri_vert));
   VkShaderModule fs = make_shader(tri_frag, sizeof(tri_frag));
   VkPipelineShaderStageCreateInfo stages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vs,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fs,
         .pName = "main",
      },
   };
   VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   };
   VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };
   VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
   };
   VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
   };
   VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment,
   };
   const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dynamic_states,
   };
   VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &format,
   };
   VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   VkPipelineLayout layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &layout));
   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic,
      .layout = layout,
   };
   VkPipeline pipeline;
   step("creating graphics pipeline");
   double t = now_ms();
   VKCHECK("vkCreateGraphicsPipelines",
           vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));
   jnum("ms_pipeline", now_ms() - t);

   VkCommandBuffer cb = begin_cmd();
   VkImageMemoryBarrier to_color = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = color_range,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
                        &to_color);
   VkRenderingAttachmentInfo attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = view,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue.color.float32 = {0.0f, 0.0f, 0.25f, 1.0f},
   };
   VkRenderingInfo rendering = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {W, H}},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &attachment,
   };
   vkCmdBeginRendering(cb, &rendering);
   VkViewport viewport = {0.0f, 0.0f, (float)W, (float)H, 0.0f, 1.0f};
   VkRect2D scissor = {{0, 0}, {W, H}};
   vkCmdSetViewport(cb, 0, 1, &viewport);
   vkCmdSetScissor(cb, 0, 1, &scissor);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   vkCmdDraw(cb, 3, 1, 0, 0);
   vkCmdEndRendering(cb);
   VkImageMemoryBarrier to_transfer = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = color_range,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_transfer);
   VkBufferImageCopy region = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {W, H, 1},
   };
   vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buf, 1,
                          &region);
   VkMemoryBarrier to_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                        &to_host, 0, NULL, 0, NULL);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   jnum("ms_gpu", submit_and_wait(cb, "triangle draw"));

   /* Every pixel should be exactly the triangle's orange or the clear color. */
   const uint8_t *px = readback.map;
   save_ppm(px, W, H, "render");
   int covered = 0, cleared = 0, other = 0;
   for (int i = 0; i < W * H; i++) {
      if (near(px + i * 4, 255, 128, 0, 255))
         covered++;
      else if (near(px + i * 4, 0, 0, 64, 255))
         cleared++;
      else
         other++;
   }
   jint("pixels_triangle", covered);
   jint("pixels_clear", cleared);
   jint("pixels_other", other);
   const uint8_t *center = px + (H / 2 * W + W / 2) * 4;
   const uint8_t *corner = px + (2 * W + 2) * 4;
   if (!near(center, 255, 128, 0, 255) || !near(corner, 0, 0, 64, 255) || other > 0)
      finish("fail",
             "wrong pixels: center %u,%u,%u,%u corner %u,%u,%u,%u, %d pixels neither color",
             center[0], center[1], center[2], center[3], corner[0], corner[1], corner[2],
             corner[3], other);

   vkDestroyPipeline(dev, pipeline, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyShaderModule(dev, vs, NULL);
   vkDestroyShaderModule(dev, fs, NULL);
   free_buffer(&readback);
   vkDestroyImageView(dev, view, NULL);
   vkDestroyImage(dev, image, NULL);
   vkFreeMemory(dev, image_mem, NULL);
}

int main(int argc, char **argv)
{
   const char *driver = NULL, *test = NULL;
   for (int i = 1; i + 1 < argc; i += 2) {
      if (!strcmp(argv[i], "--driver"))
         driver = argv[i + 1];
      else if (!strcmp(argv[i], "--test"))
         test = argv[i + 1];
      else if (!strcmp(argv[i], "--out"))
         out_dir = argv[i + 1];
      else if (!strcmp(argv[i], "--tag"))
         tag = argv[i + 1];
   }
   if (!driver || !test) {
      fprintf(stderr, "usage: xdtest --driver stock|PATH --test info|device|submit|compute|render "
                      "[--out DIR] [--tag NAME]\n");
      return 2;
   }
   if (!tag)
      tag = strcmp(driver, "stock") ? "custom" : "stock";

   jopen(NULL, '{');
   jstr("test", test);
   jstr("driver", tag);

   step("loading driver");
   double t = now_ms();
   const char *err = load_driver(driver);
   if (err)
      finish("fail", "loading the driver: %s", err);
   jnum("ms_load", now_ms() - t);

   start_instance();
   if (!strcmp(test, "info")) {
      run_info();
      vkDestroyInstance(inst, NULL);
      finish("pass", NULL);
   }

   start_device(!strcmp(test, "render"));
   if (!strcmp(test, "submit"))
      run_submit();
   else if (!strcmp(test, "compute"))
      run_compute();
   else if (!strcmp(test, "render"))
      run_render();
   else if (strcmp(test, "device"))
      finish("fail", "unknown test '%s'", test);

   step("tearing down");
   if (pool)
      vkDestroyCommandPool(dev, pool, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   finish("pass", NULL);
   return 0;
}
