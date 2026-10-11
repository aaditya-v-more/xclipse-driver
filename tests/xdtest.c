/*
 * xdtest: on-device Vulkan tests for the Xclipse 940 driver work.
 *
 * Loads either the phone's stock Vulkan stack (libvulkan.so, which loads Samsung's driver) or a
 * Mesa Android HAL driver such as vulkan.radeon.so, runs one test, and prints one JSON object on
 * stdout. Progress goes to stderr, so a crash still shows the last step reached. The same binary
 * drives both drivers, so every result is an A/B pair.
 *
 * usage: xdtest --driver stock|PATH --test NAME [--out DIR] [--tag NAME]
 *   correctness: info device submit compute render tess scratch scratchcs queries xfb xfbmany
 *                xfbmanygs textures mips mrt vertex depth storage
 *   performance: alu bandwidth fill draws compile
 */
#define VK_NO_PROTOTYPES
#include <dlfcn.h>
#include <math.h>
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

#include "alu_comp.h"
#include "complex_comp.h"
#include "copy_comp.h"
#include "depth_read_comp.h"
#include "depth_vert.h"
#include "fill_comp.h"
#include "fill_scratch_comp.h"
#include "flat_frag.h"
#include "grid_vert.h"
#include "sample_comp.h"
#include "scratch_vert.h"
#include "screen_vert.h"
#include "tess_tesc.h"
#include "tess_tese.h"
#include "tiny_vert.h"
#include "tri_frag.h"
#include "tri_geom.h"
#include "tri_vert.h"
#include "vertex_vert.h"
#include "xfb_many_geom.h"
#include "inputs_frag.h"
#include "mrt_add_frag.h"
#include "mrt_frag.h"
#include "mips_read_comp.h"
#include "storage_write_comp.h"
#include "storage_read_comp.h"
#include "mrt_read_comp.h"
#include "xfb_many_vert.h"
#include "xfb_vert.h"

/* A fence that hasn't signaled after this long means the GPU stopped responding. */
#define GPU_TIMEOUT_NS 5000000000ull
/* Performance tests: warm up for at least WARMUP_MS (the GPU clock governor ramps up with load),
 * then RUNS measured runs; the median is reported. */
#define WARMUP_MS 250.0
#define RUNS 7

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

static void jruns(const char *k, const double *v, int n)
{
   jopen(k, '[');
   for (int i = 0; i < n; i++)
      jnum(NULL, v[i]);
   jclose();
}

/* Set once there's a device: destroys it, for finish() on a failure. */
static void (*destroy_device_on_failure)(void);

/* Ends the run: closes any open JSON levels, records the status and exits. After a failure, the
 * device is destroyed first, so the driver can log what it learns at teardown (such as GPU page
 * faults in the last submission). */
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
   if (strcmp(status, "pass") && destroy_device_on_failure) {
      void (*destroy)(void) = destroy_device_on_failure;
      destroy_device_on_failure = NULL;
      destroy();
   }
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

static int cmp_double(const void *a, const void *b)
{
   double x = *(const double *)a, y = *(const double *)b;
   return (x > y) - (x < y);
}

static double median(const double *v, int n)
{
   double s[64];
   memcpy(s, v, n * sizeof(double));
   qsort(s, n, sizeof(double), cmp_double);
   return n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
}

/* ---- Vulkan entry points, loaded by hand because there may be no loader ---- */

#define INSTANCE_FUNCS(X)                                                                          \
   X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties2)            \
   X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceMemoryProperties)                          \
   X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkEnumerateDeviceExtensionProperties)             \
   X(vkGetPhysicalDeviceFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)

#define DEVICE_FUNCS(X)                                                                            \
   X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) X(vkQueueSubmit) X(vkCreateFence)     \
   X(vkDestroyFence) X(vkWaitForFences) X(vkCreateCommandPool) X(vkDestroyCommandPool)             \
   X(vkAllocateCommandBuffers)                                                                     \
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
   X(vkCmdCopyImageToBuffer) X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCmdResetQueryPool)      \
   X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults) X(vkCmdCopyBuffer) X(vkCmdPushConstants)         \
   X(vkCmdFillBuffer) X(vkCmdBeginQuery) X(vkCmdEndQuery) X(vkCmdCopyBufferToImage)                \
   X(vkCreateSampler) X(vkDestroySampler) X(vkCmdBindVertexBuffers) X(vkCmdBindVertexBuffers2)   \
   X(vkCreateBufferView) X(vkDestroyBufferView)

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
static VkQueryPool ts_pool;

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

/* ---- device, commands and resources ---- */

/* What a test needs enabled beyond the basics. */
enum {
   NEED_QUERIES = 1, NEED_XFB = 2, NEED_GS = 4, NEED_TESS = 8, NEED_BC = 16, NEED_VERTEX = 32,
   NEED_STORAGE = 64
};

/* Set by start_device for the tests with needs: what the driver has of what they use. */
static bool has_geometry_shader, has_tessellation_shader, has_stats_query, has_pg_query, has_xfb,
   has_xfb_queries, has_bc, has_vertex_stores, has_vertex_input_dynamic, has_storage_nofmt;

static bool has_device_extension(const char *name)
{
   VkExtensionProperties exts[512];
   uint32_t n = 512;
   VkResult r = vkEnumerateDeviceExtensionProperties(pdev, NULL, &n, exts);
   if (r != VK_SUCCESS && r != VK_INCOMPLETE)
      return false;
   for (uint32_t i = 0; i < n; i++)
      if (!strcmp(exts[i].extensionName, name))
         return true;
   return false;
}

/* After a failure, objects are left alive; the driver frees what it has to. Destroying the
 * instance as well is what destroys RADV's winsys. */
static void destroy_device(void)
{
   vkDeviceWaitIdle(dev);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
}

static void start_device(bool dynamic_rendering, unsigned needs)
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
   VkPhysicalDeviceFeatures features = {0};
   VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT pgq = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT,
   };
   VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT,
   };
   const char *pgq_ext = VK_EXT_PRIMITIVES_GENERATED_QUERY_EXTENSION_NAME;
   VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vid = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
   };
   const char *xfb_ext = VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
   const char *vid_ext = VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME;
   const char *exts[3];
   void **chain = &f13.pNext; /* where the next enabled feature struct goes */
   if (needs & NEED_QUERIES) {
      bool pgq_listed = has_device_extension(pgq_ext);
      VkPhysicalDeviceFeatures2 f2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = pgq_listed ? &pgq : NULL,
      };
      vkGetPhysicalDeviceFeatures2(pdev, &f2);
      has_geometry_shader = f2.features.geometryShader;
      has_stats_query = f2.features.pipelineStatisticsQuery;
      has_pg_query = pgq_listed && pgq.primitivesGeneratedQuery;
      features.geometryShader = has_geometry_shader;
      features.pipelineStatisticsQuery = has_stats_query;
      dci.pEnabledFeatures = &features;
      if (has_pg_query) {
         pgq = (VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT,
            .primitivesGeneratedQuery = VK_TRUE,
         };
         *chain = &pgq;
         chain = &pgq.pNext;
         exts[dci.enabledExtensionCount++] = pgq_ext;
      }
   }
   if (needs & NEED_GS) {
      VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      vkGetPhysicalDeviceFeatures2(pdev, &f2);
      has_geometry_shader = f2.features.geometryShader;
      features.geometryShader = has_geometry_shader;
      dci.pEnabledFeatures = &features;
   }
   if (needs & NEED_TESS) {
      VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      vkGetPhysicalDeviceFeatures2(pdev, &f2);
      has_tessellation_shader = f2.features.tessellationShader;
      features.tessellationShader = has_tessellation_shader;
      dci.pEnabledFeatures = &features;
   }
   if (needs & NEED_BC) {
      VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      vkGetPhysicalDeviceFeatures2(pdev, &f2);
      has_bc = f2.features.textureCompressionBC;
      features.textureCompressionBC = has_bc;
      dci.pEnabledFeatures = &features;
   }
   if (needs & NEED_STORAGE) {
      VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      vkGetPhysicalDeviceFeatures2(pdev, &f2);
      has_storage_nofmt = f2.features.shaderStorageImageWriteWithoutFormat;
      features.shaderStorageImageWriteWithoutFormat = has_storage_nofmt;
      dci.pEnabledFeatures = &features;
   }
   if (needs & NEED_VERTEX) {
      bool vid_listed = has_device_extension(vid_ext);
      VkPhysicalDeviceFeatures2 f2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = vid_listed ? &vid : NULL,
      };
      vkGetPhysicalDeviceFeatures2(pdev, &f2);
      has_vertex_stores = f2.features.vertexPipelineStoresAndAtomics;
      has_vertex_input_dynamic = vid_listed && vid.vertexInputDynamicState;
      features.vertexPipelineStoresAndAtomics = has_vertex_stores;
      dci.pEnabledFeatures = &features;
      if (has_vertex_input_dynamic) {
         vid = (VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
            .vertexInputDynamicState = VK_TRUE,
         };
         *chain = &vid;
         chain = &vid.pNext;
         exts[dci.enabledExtensionCount++] = vid_ext;
      }
   }
   if ((needs & NEED_XFB) && has_device_extension(xfb_ext)) {
      VkPhysicalDeviceTransformFeedbackPropertiesEXT props = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT,
      };
      VkPhysicalDeviceProperties2 p2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
         .pNext = &props,
      };
      VkPhysicalDeviceFeatures2 f2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &xfb,
      };
      vkGetPhysicalDeviceProperties2(pdev, &p2);
      vkGetPhysicalDeviceFeatures2(pdev, &f2);
      has_xfb = xfb.transformFeedback;
      has_xfb_queries = has_xfb && props.transformFeedbackQueries;
      if (has_xfb) {
         xfb = (VkPhysicalDeviceTransformFeedbackFeaturesEXT){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT,
            .transformFeedback = VK_TRUE,
         };
         *chain = &xfb;
         chain = &xfb.pNext;
         exts[dci.enabledExtensionCount++] = xfb_ext;
      }
   }
   dci.ppEnabledExtensionNames = exts;
   step("creating device");
   double t = now_ms();
   VKCHECK("vkCreateDevice", vkCreateDevice(pdev, &dci, NULL, &dev));
   destroy_device_on_failure = destroy_device;
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

/* GPU timing: timestamps at the top and bottom of the measured commands. */
static void ts_begin(VkCommandBuffer cb)
{
   if (!qfams[qfam].timestampValidBits)
      finish("skip", "the queue has no timestamps");
   if (!ts_pool) {
      VkQueryPoolCreateInfo qci = {
         .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
         .queryType = VK_QUERY_TYPE_TIMESTAMP,
         .queryCount = 2,
      };
      VKCHECK("vkCreateQueryPool", vkCreateQueryPool(dev, &qci, NULL, &ts_pool));
   }
   vkCmdResetQueryPool(cb, ts_pool, 0, 2);
   vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ts_pool, 0);
}

static void ts_end(VkCommandBuffer cb)
{
   vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ts_pool, 1);
}

static double ts_read_ms(void)
{
   uint64_t t[2];
   VKCHECK("vkGetQueryPoolResults",
           vkGetQueryPoolResults(dev, ts_pool, 0, 2, sizeof(t), t, sizeof(t[0]),
                                 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
   uint32_t bits = qfams[qfam].timestampValidBits;
   uint64_t mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
   return (double)((t[1] - t[0]) & mask) * props.limits.timestampPeriod / 1e6;
}

typedef void (*record_fn)(VkCommandBuffer cb, void *arg);

/* Records `record` between timestamps, runs it, and returns the GPU time in ms. */
static double gpu_time(record_fn record, void *arg, const char *what)
{
   VkCommandBuffer cb = begin_cmd();
   ts_begin(cb);
   record(cb, arg);
   ts_end(cb);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   submit_and_wait(cb, what);
   return ts_read_ms();
}

/* Repeats `record` for at least WARMUP_MS of wall time, so the GPU clock has ramped up. */
static void warm_up(record_fn record, void *arg, const char *what)
{
   double start = now_ms();
   int n = 0;
   while (n < 2 || now_ms() - start < WARMUP_MS) {
      gpu_time(record, arg, what);
      n++;
   }
}

/* Warm-up, then RUNS measured runs into runs[]; returns their median. */
static double measure(record_fn record, void *arg, const char *what, double *runs)
{
   warm_up(record, arg, what);
   for (int i = 0; i < RUNS; i++)
      runs[i] = gpu_time(record, arg, what);
   return median(runs, RUNS);
}

/* The GPU clock right after measuring, for reading results across runs. */
static void record_gpu_clock(void)
{
   char buf[32] = {0};
   FILE *f = fopen("/sys/kernel/gpu/gpu_clock", "r");
   if (f) {
      if (fgets(buf, sizeof(buf), f))
         jint("gpu_clock_khz_after", atoll(buf));
      fclose(f);
   }
}

static int find_memory(uint32_t type_bits, VkMemoryPropertyFlags want)
{
   for (uint32_t i = 0; i < memprops.memoryTypeCount; i++)
      if ((type_bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want)
         return (int)i;
   return -1;
}

static VkDeviceMemory alloc_memory(VkMemoryRequirements req, VkMemoryPropertyFlags want)
{
   int type = find_memory(req.memoryTypeBits, want);
   if (type < 0 && !(want & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
      type = find_memory(req.memoryTypeBits, 0);
   if (type < 0)
      finish("fail", "no memory type with flags 0x%x", want);
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = (uint32_t)type,
   };
   VkDeviceMemory mem;
   VKCHECK("vkAllocateMemory", vkAllocateMemory(dev, &mai, NULL, &mem));
   return mem;
}

struct buffer {
   VkBuffer buf;
   VkDeviceMemory mem;
   void *map;
};

/* A buffer in host-visible coherent memory (mapped for its whole life), or in device-local
 * memory when `host` is false. */
static struct buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host)
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
   b.mem = alloc_memory(req, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                  : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   VKCHECK("vkBindBufferMemory", vkBindBufferMemory(dev, b.buf, b.mem, 0));
   if (host)
      VKCHECK("vkMapMemory", vkMapMemory(dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map));
   return b;
}

static void free_buffer(struct buffer *b)
{
   vkDestroyBuffer(dev, b->buf, NULL);
   vkFreeMemory(dev, b->mem, NULL);
}

/* Appends one problem to a comma-separated list of them. */
static void add_problem(char *list, size_t size, const char *fmt, ...)
{
   size_t len = strlen(list);
   if (len && len + 2 < size) {
      strcpy(list + len, ", ");
      len += 2;
   }
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(list + len, size - len, fmt, ap);
   va_end(ap);
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

struct compute {
   VkShaderModule module;
   VkDescriptorSetLayout dsl;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkDescriptorPool dpool;
   VkDescriptorSet set;
};

/* A compute pipeline over storage buffers at bindings 0..nbuf-1 (bound when `bufs` is given),
 * with an optional push-constant block. *ms gets the shader module plus pipeline creation time. */
static struct compute make_compute(const uint32_t *code, size_t size, const VkBuffer *bufs,
                                   uint32_t nbuf, uint32_t push_size,
                                   const VkSpecializationInfo *spec, double *ms)
{
   struct compute c = {0};
   VkDescriptorSetLayoutBinding bindings[4];
   for (uint32_t i = 0; i < nbuf; i++) {
      bindings[i] = (VkDescriptorSetLayoutBinding){
         .binding = i,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      };
   }
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = nbuf,
      .pBindings = bindings,
   };
   VKCHECK("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout(dev, &dslci, NULL, &c.dsl));
   VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size};
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &c.dsl,
      .pushConstantRangeCount = push_size ? 1 : 0,
      .pPushConstantRanges = &push,
   };
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &c.layout));

   double t = now_ms();
   c.module = make_shader(code, size);
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = c.module,
            .pName = "main",
            .pSpecializationInfo = spec,
         },
      .layout = c.layout,
   };
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &c.pipeline));
   *ms = now_ms() - t;

   if (bufs) {
      VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nbuf};
      VkDescriptorPoolCreateInfo dpci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .maxSets = 1,
         .poolSizeCount = 1,
         .pPoolSizes = &pool_size,
      };
      VKCHECK("vkCreateDescriptorPool", vkCreateDescriptorPool(dev, &dpci, NULL, &c.dpool));
      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = c.dpool,
         .descriptorSetCount = 1,
         .pSetLayouts = &c.dsl,
      };
      VKCHECK("vkAllocateDescriptorSets", vkAllocateDescriptorSets(dev, &dsai, &c.set));
      VkDescriptorBufferInfo infos[4];
      VkWriteDescriptorSet writes[4];
      for (uint32_t i = 0; i < nbuf; i++) {
         infos[i] = (VkDescriptorBufferInfo){bufs[i], 0, VK_WHOLE_SIZE};
         writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = c.set,
            .dstBinding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &infos[i],
         };
      }
      vkUpdateDescriptorSets(dev, nbuf, writes, 0, NULL);
   }
   return c;
}

static void free_compute(struct compute *c)
{
   if (c->dpool)
      vkDestroyDescriptorPool(dev, c->dpool, NULL);
   vkDestroyPipeline(dev, c->pipeline, NULL);
   vkDestroyShaderModule(dev, c->module, NULL);
   vkDestroyPipelineLayout(dev, c->layout, NULL);
   vkDestroyDescriptorSetLayout(dev, c->dsl, NULL);
}

static void bind_compute(VkCommandBuffer cb, const struct compute *c)
{
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, c->pipeline);
   if (c->set)
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, c->layout, 0, 1, &c->set, 0,
                              NULL);
}

struct graphics {
   VkShaderModule vs, tcs, tes, gs, fs;
   VkPipelineLayout layout;
   VkPipeline pipeline;
};

/* A graphics pipeline for dynamic rendering into one color attachment, with dynamic viewport and
 * scissor, optional tessellation shaders (patches of 3 control points), an optional geometry
 * shader, optional alpha blending and an optional vertex-stage push-constant block. */
static struct graphics make_graphics_tess(const uint32_t *vs_code, size_t vs_size,
                                          const uint32_t *tcs_code, size_t tcs_size,
                                          const uint32_t *tes_code, size_t tes_size,
                                          const uint32_t *gs_code, size_t gs_size,
                                          const uint32_t *fs_code, size_t fs_size, VkFormat format,
                                          bool blend, uint32_t push_size, double *ms)
{
   struct graphics g = {0};
   VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT, 0, push_size};
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = push_size ? 1 : 0,
      .pPushConstantRanges = &push,
   };
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &g.layout));

   double t = now_ms();
   g.vs = make_shader(vs_code, vs_size);
   g.fs = make_shader(fs_code, fs_size);
   VkPipelineShaderStageCreateInfo stages[5] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = g.vs,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = g.fs,
         .pName = "main",
      },
   };
   uint32_t stage_count = 2;
   if (tcs_code) {
      g.tcs = make_shader(tcs_code, tcs_size);
      g.tes = make_shader(tes_code, tes_size);
      stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
         .module = g.tcs,
         .pName = "main",
      };
      stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
         .module = g.tes,
         .pName = "main",
      };
   }
   if (gs_code) {
      g.gs = make_shader(gs_code, gs_size);
      stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
         .module = g.gs,
         .pName = "main",
      };
   }
   VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   };
   VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = tcs_code ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };
   VkPipelineTessellationStateCreateInfo tessellation = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
      .patchControlPoints = 3,
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
   VkPipelineColorBlendAttachmentState blend_attachment = {
      .blendEnable = blend,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = 0xf,
   };
   VkPipelineColorBlendStateCreateInfo blend_state = {
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
   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = stage_count,
      .pStages = stages,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pTessellationState = tcs_code ? &tessellation : NULL,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend_state,
      .pDynamicState = &dynamic,
      .layout = g.layout,
   };
   VKCHECK("vkCreateGraphicsPipelines",
           vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &g.pipeline));
   *ms = now_ms() - t;
   return g;
}

static struct graphics make_graphics_gs(const uint32_t *vs_code, size_t vs_size,
                                        const uint32_t *gs_code, size_t gs_size,
                                        const uint32_t *fs_code, size_t fs_size, VkFormat format,
                                        bool blend, uint32_t push_size, double *ms)
{
   return make_graphics_tess(vs_code, vs_size, NULL, 0, NULL, 0, gs_code, gs_size, fs_code, fs_size,
                             format, blend, push_size, ms);
}

static struct graphics make_graphics(const uint32_t *vs_code, size_t vs_size,
                                     const uint32_t *fs_code, size_t fs_size, VkFormat format,
                                     bool blend, uint32_t push_size, double *ms)
{
   return make_graphics_gs(vs_code, vs_size, NULL, 0, fs_code, fs_size, format, blend, push_size,
                           ms);
}

static void free_graphics(struct graphics *g)
{
   vkDestroyPipeline(dev, g->pipeline, NULL);
   vkDestroyPipelineLayout(dev, g->layout, NULL);
   vkDestroyShaderModule(dev, g->vs, NULL);
   vkDestroyShaderModule(dev, g->tcs, NULL);
   vkDestroyShaderModule(dev, g->tes, NULL);
   vkDestroyShaderModule(dev, g->gs, NULL);
   vkDestroyShaderModule(dev, g->fs, NULL);
}

struct target {
   VkImage image;
   VkDeviceMemory mem;
   VkImageView view;
   uint32_t w, h;
};

static const VkImageSubresourceRange color_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

/* A device-local RGBA8 render target that can also be copied from. */
static struct target make_target(uint32_t w, uint32_t h)
{
   struct target t = {.w = w, .h = h};
   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = {w, h, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VKCHECK("vkCreateImage", vkCreateImage(dev, &ici, NULL, &t.image));
   VkMemoryRequirements req;
   vkGetImageMemoryRequirements(dev, t.image, &req);
   t.mem = alloc_memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, t.image, t.mem, 0));
   VkImageViewCreateInfo vci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = t.image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange = color_range,
   };
   VKCHECK("vkCreateImageView", vkCreateImageView(dev, &vci, NULL, &t.view));
   return t;
}

static void free_target(struct target *t)
{
   vkDestroyImageView(dev, t->view, NULL);
   vkDestroyImage(dev, t->image, NULL);
   vkFreeMemory(dev, t->mem, NULL);
}

/* Starts rendering into `t`, cleared to `clear`, with the viewport and scissor covering it. */
static void begin_target(VkCommandBuffer cb, const struct target *t, const float clear[4])
{
   VkImageMemoryBarrier to_color = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = t->image,
      .subresourceRange = color_range,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
                        &to_color);
   VkRenderingAttachmentInfo attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = t->view,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue.color.float32 = {clear[0], clear[1], clear[2], clear[3]},
   };
   VkRenderingInfo rendering = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {t->w, t->h}},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &attachment,
   };
   vkCmdBeginRendering(cb, &rendering);
   VkViewport viewport = {0.0f, 0.0f, (float)t->w, (float)t->h, 0.0f, 1.0f};
   VkRect2D scissor = {{0, 0}, {t->w, t->h}};
   vkCmdSetViewport(cb, 0, 1, &viewport);
   vkCmdSetScissor(cb, 0, 1, &scissor);
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

static void dispatch_fill(const uint32_t *code, size_t size)
{
   enum { N = 65536 };
   struct buffer b = make_buffer(N * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
   memset(b.map, 0xee, N * sizeof(uint32_t)); /* poison, so missed writes show up */

   double ms;
   step("creating compute pipeline");
   struct compute c = make_compute(code, size, &b.buf, 1, 0, NULL, &ms);
   jnum("ms_pipeline", ms);

   VkCommandBuffer cb = begin_cmd();
   bind_compute(cb, &c);
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

   free_compute(&c);
   free_buffer(&b);
}

static void run_compute(void)
{
   dispatch_fill(fill_comp, sizeof(fill_comp));
}

/* The same pattern, with every value passing through scratch memory on its way out. */
static void run_scratch_cs(void)
{
   dispatch_fill(fill_scratch_comp, sizeof(fill_scratch_comp));
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

enum { PIC_W = 128, PIC_H = 128 };

/* Draws VERTICES vertices with g into a PIC_W x PIC_H target cleared to dark blue, saves the
 * picture as NAME and returns it (RGBA8, for the caller to free). The GPU time goes in MS_KEY
 * unless that's NULL. */
static uint8_t *draw_picture(struct graphics *g, uint32_t vertices, const char *name,
                             const char *ms_key)
{
   enum { W = PIC_W, H = PIC_H };
   struct target t = make_target(W, H);
   struct buffer readback = make_buffer(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);

   VkCommandBuffer cb = begin_cmd();
   const float clear[4] = {0.0f, 0.0f, 0.25f, 1.0f};
   begin_target(cb, &t, clear);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipeline);
   vkCmdDraw(cb, vertices, 1, 0, 0);
   vkCmdEndRendering(cb);
   VkImageMemoryBarrier to_transfer = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = t.image,
      .subresourceRange = color_range,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_transfer);
   VkBufferImageCopy region = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {W, H, 1},
   };
   vkCmdCopyImageToBuffer(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buf, 1,
                          &region);
   VkMemoryBarrier to_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                        &to_host, 0, NULL, 0, NULL);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   const double ms = submit_and_wait(cb, name);
   if (ms_key)
      jnum(ms_key, ms);

   uint8_t *px = malloc(W * H * 4);
   if (!px)
      finish("fail", "out of memory");
   memcpy(px, readback.map, W * H * 4);
   save_ppm(px, W, H, name);
   free_buffer(&readback);
   free_target(&t);
   return px;
}

/* Checks a picture of tri.vert's triangle: every pixel is its orange or the clear color. */
static void check_triangle(const uint8_t *px)
{
   enum { W = PIC_W, H = PIC_H };
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
}

static void run_render(void)
{
   double ms;
   step("creating graphics pipeline");
   struct graphics g = make_graphics(tri_vert, sizeof(tri_vert), tri_frag, sizeof(tri_frag),
                                     VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);
   jnum("ms_pipeline", ms);
   uint8_t *px = draw_picture(&g, 3, "render", "ms_gpu");
   check_triangle(px);
   free(px);
   free_graphics(&g);
}

/* The same triangle as one tessellated patch. The hull shader reads the tess factor and off-chip
 * ring descriptors from a table the driver points the shader's first two SGPRs at, and the domain
 * shader reads the off-chip ring through its own pointer. */
static void run_tess(void)
{
   if (!has_tessellation_shader)
      finish("skip", "no tessellationShader");
   double ms;
   step("creating tessellation pipeline");
   struct graphics g = make_graphics_tess(tri_vert, sizeof(tri_vert), tess_tesc, sizeof(tess_tesc),
                                          tess_tese, sizeof(tess_tese), NULL, 0, tri_frag,
                                          sizeof(tri_frag), VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);
   jnum("ms_pipeline", ms);
   uint8_t *px = draw_picture(&g, 3, "tess", "ms_gpu");
   check_triangle(px);
   free(px);
   free_graphics(&g);
}

/* grid.vert's 4096 small triangles, drawn by a vertex shader that passes every position through
 * scratch memory, against the same grid drawn without it. Many waves use scratch at once, so
 * waves that share scratch space move triangles. */
static void run_scratch(void)
{
   enum { VERTICES = 3 * 4096 };
   double ms;
   step("creating graphics pipelines");
   struct graphics ref = make_graphics(grid_vert, sizeof(grid_vert), tri_frag, sizeof(tri_frag),
                                       VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);
   struct graphics g = make_graphics(scratch_vert, sizeof(scratch_vert), tri_frag,
                                     sizeof(tri_frag), VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);
   jnum("ms_pipeline", ms);
   uint8_t *want = draw_picture(&ref, VERTICES, "scratch_reference", NULL);
   uint8_t *got = draw_picture(&g, VERTICES, "scratch", "ms_gpu");
   int covered = 0, different = 0;
   for (int i = 0; i < PIC_W * PIC_H; i++) {
      covered += near(want + i * 4, 255, 128, 0, 255);
      different += memcmp(want + i * 4, got + i * 4, 4) != 0;
   }
   jint("pixels_triangles", covered);
   jint("pixels_different", different);
   if (!covered)
      finish("fail", "the grid drawn without scratch has no triangles");
   if (different)
      finish("fail", "%d of %d pixels differ from the grid drawn without scratch", different,
             PIC_W * PIC_H);
   free(want);
   free(got);
   free_graphics(&g);
   free_graphics(&ref);
}

/* ---- queries: pipeline statistics as D3D11 uses them (through DXVK), and primitives generated ----
 *
 * Draws INSTANCES triangles with a plain pipeline and again with a geometry shader that turns
 * every triangle into two, each in its own submission and inside a pipeline-statistics query with
 * all eleven statistics (what DXVK asks for) and, where the driver has
 * VK_EXT_primitives_generated_query, a primitives-generated query. RADV counts geometry-shader
 * primitives and generated primitives from NGG shaders itself; on the Xclipse it used to ask the
 * kernel for GDS to do that, the kernel has none, and the submission failed. The counts the test
 * can predict are checked; the rest (fragment invocations, clipping output) are recorded.
 *
 * To find which part of it a driver chokes on, these environment variables leave parts out:
 * XD_QUERY_STATS (the statistics mask, hex; 0 = no statistics query), XD_QUERY_PG=0 (no
 * primitives-generated query) and XD_QUERY_GS=0 (no geometry-shader draw). */

static const char *const stat_names[] = {
   "ia_vertices",          "ia_primitives",       "vs_invocations", "gs_invocations",
   "gs_primitives",        "clipping_invocations", "clipping_primitives",
   "fs_invocations",       "tcs_patches",         "tes_invocations", "cs_invocations",
};
#define NUM_STATS (sizeof(stat_names) / sizeof(stat_names[0]))

static unsigned env_uint(const char *name, unsigned fallback)
{
   const char *v = getenv(name);
   return v && *v ? (unsigned)strtoul(v, NULL, 16) : fallback;
}

static void run_queries(void)
{
   enum { W = 128, H = 128, INSTANCES = 5 };
   const uint32_t stats_mask = env_uint("XD_QUERY_STATS", (1u << NUM_STATS) - 1) &
                               ((1u << NUM_STATS) - 1);
   const bool use_pg = has_pg_query && env_uint("XD_QUERY_PG", 1);
   const bool use_gs = has_geometry_shader && env_uint("XD_QUERY_GS", 1);
   if (stats_mask && !has_stats_query)
      finish("skip", "no pipelineStatisticsQuery");
   jbool("has_geometry_shader", has_geometry_shader);
   jbool("has_primitives_generated_query", has_pg_query);
   jhex("stats_mask", stats_mask);
   jbool("pg_query", use_pg);
   jbool("gs_draw", use_gs);

   struct target t = make_target(W, H);
   double ms;
   step("creating graphics pipelines");
   struct graphics pipes[2];
   pipes[0] = make_graphics(tri_vert, sizeof(tri_vert), tri_frag, sizeof(tri_frag),
                            VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);
   const int num_pipes = use_gs ? 2 : 1;
   if (use_gs)
      pipes[1] = make_graphics_gs(tri_vert, sizeof(tri_vert), tri_geom, sizeof(tri_geom), tri_frag,
                                  sizeof(tri_frag), VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);

   VkQueryPool stats_pool = VK_NULL_HANDLE, pg_pool = VK_NULL_HANDLE;
   VkQueryPoolCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS,
      .queryCount = num_pipes,
      .pipelineStatistics = stats_mask,
   };
   if (stats_mask)
      VKCHECK("vkCreateQueryPool", vkCreateQueryPool(dev, &qci, NULL, &stats_pool));
   if (use_pg) {
      qci.queryType = VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT;
      qci.pipelineStatistics = 0;
      VKCHECK("vkCreateQueryPool", vkCreateQueryPool(dev, &qci, NULL, &pg_pool));
   }

   static const char *const what[2] = {"plain draw inside queries",
                                        "geometry-shader draw inside queries"};
   const float clear[4] = {0.0f, 0.0f, 0.25f, 1.0f};
   for (int i = 0; i < num_pipes; i++) {
      VkCommandBuffer cb = begin_cmd();
      if (stats_pool)
         vkCmdResetQueryPool(cb, stats_pool, i, 1);
      if (pg_pool)
         vkCmdResetQueryPool(cb, pg_pool, i, 1);
      begin_target(cb, &t, clear);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[i].pipeline);
      if (stats_pool)
         vkCmdBeginQuery(cb, stats_pool, i, 0);
      if (pg_pool)
         vkCmdBeginQuery(cb, pg_pool, i, 0);
      vkCmdDraw(cb, 3, INSTANCES, 0, 0);
      if (pg_pool)
         vkCmdEndQuery(cb, pg_pool, i);
      if (stats_pool)
         vkCmdEndQuery(cb, stats_pool, i);
      vkCmdEndRendering(cb);
      VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
      jnum(i == 0 ? "ms_gpu" : "ms_gpu_gs", submit_and_wait(cb, what[i]));
   }

   /* Results come back packed: one value per statistic in the mask, in bit order. */
   uint64_t packed[2][NUM_STATS], pg[2] = {0};
   step("reading query results");
   if (stats_pool)
      VKCHECK("vkGetQueryPoolResults (statistics)",
              vkGetQueryPoolResults(dev, stats_pool, 0, num_pipes, sizeof(packed), packed,
                                    sizeof(packed[0]),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
   if (pg_pool)
      VKCHECK("vkGetQueryPoolResults (primitives generated)",
              vkGetQueryPoolResults(dev, pg_pool, 0, num_pipes, sizeof(pg), pg, sizeof(pg[0]),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

   /* What each pipeline must count: the geometry shader doubles every triangle. Fragment
    * invocations and the clipper's output depend on the hardware, so they're only recorded. */
   char wrong[400] = "";
   for (int i = 0; i < num_pipes; i++) {
      const uint64_t prims = i == 0 ? INSTANCES : 2 * INSTANCES;
      const uint64_t want[NUM_STATS] = {
         3 * INSTANCES, INSTANCES, 3 * INSTANCES, i == 0 ? 0 : INSTANCES, i == 0 ? 0 : prims,
         prims,         UINT64_MAX, UINT64_MAX, 0, 0, 0,
      };
      jopen(i == 0 ? "plain" : "gs", '{');
      unsigned n = 0;
      for (unsigned s = 0; s < NUM_STATS; s++) {
         if (!(stats_mask & (1u << s)))
            continue;
         const uint64_t got = packed[i][n++];
         jint(stat_names[s], (long long)got);
         if (want[s] != UINT64_MAX && got != want[s])
            snprintf(wrong + strlen(wrong), sizeof(wrong) - strlen(wrong),
                     "%s%s %s %llu (want %llu)", wrong[0] ? ", " : "", i == 0 ? "plain" : "gs",
                     stat_names[s], (unsigned long long)got, (unsigned long long)want[s]);
      }
      if (pg_pool) {
         jint("primitives_generated", (long long)pg[i]);
         if (pg[i] != prims)
            snprintf(wrong + strlen(wrong), sizeof(wrong) - strlen(wrong),
                     "%s%s primitives_generated %llu (want %llu)", wrong[0] ? ", " : "",
                     i == 0 ? "plain" : "gs", (unsigned long long)pg[i],
                     (unsigned long long)prims);
      }
      jclose();
   }
   if (wrong[0])
      finish("fail", "wrong counts: %s", wrong);

   if (pg_pool)
      vkDestroyQueryPool(dev, pg_pool, NULL);
   if (stats_pool)
      vkDestroyQueryPool(dev, stats_pool, NULL);
   for (int i = 0; i < num_pipes; i++)
      free_graphics(&pipes[i]);
   free_target(&t);
}

/* ---- textures: sampling the formats DXVK uses for D3D11 games ---- */

/* Writes an 8x8 texture's upload data and the RGBA each texel must read as. */
typedef void (*texel_fill_fn)(uint8_t *data, float want[64][4]);

static uint8_t pattern8(int i, int c) { return (uint8_t)((i % 8) * 32 + (i / 8) * 4 + c * 67); }

static float srgb_to_linear(float c)
{
   return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static void fill_rgba8(uint8_t *d, float want[64][4], bool bgra, bool srgb)
{
   for (int i = 0; i < 64; i++) {
      for (int c = 0; c < 4; c++) {
         const uint8_t v = pattern8(i, c);
         d[i * 4 + (bgra && c < 3 ? 2 - c : c)] = v;
         want[i][c] = srgb && c < 3 ? srgb_to_linear(v / 255.0f) : v / 255.0f;
      }
   }
}

static void fill_rgba8_unorm(uint8_t *d, float want[64][4]) { fill_rgba8(d, want, false, false); }
static void fill_bgra8_unorm(uint8_t *d, float want[64][4]) { fill_rgba8(d, want, true, false); }
static void fill_rgba8_srgb(uint8_t *d, float want[64][4]) { fill_rgba8(d, want, false, true); }

/* Half floats and their values, also used by the vertex test. */
static const struct {
   uint16_t bits;
   float value;
} halfs[] = {
   {0x0000, 0.0f},    {0x3400, 0.25f},    {0x3800, 0.5f},
   {0x3c00, 1.0f},    {0x4000, 2.0f},     {0x4940, 10.5f},
   {0x63d0, 1000.0f}, {0xbc00, -1.0f},    {0xb000, -0.125f},
   {0x7bff, 65504.0f}, {0x0400, 6.103515625e-05f}, {0x3555, 0.333251953125f},
};
enum { HALFS = sizeof(halfs) / sizeof(halfs[0]) };

static void fill_rgba16f(uint8_t *d, float want[64][4])
{
   for (int i = 0; i < 64; i++) {
      for (int c = 0; c < 4; c++) {
         const int k = (i * 4 + c * 5 + i / 8) % HALFS;
         d[(i * 4 + c) * 2] = halfs[k].bits & 0xff;
         d[(i * 4 + c) * 2 + 1] = halfs[k].bits >> 8;
         want[i][c] = halfs[k].value;
      }
   }
}

static void fill_r11g11b10f(uint8_t *d, float want[64][4])
{
   /* 5-bit exponent, and whether the top mantissa bit is set: 0, 0.25, 0.5, 1, 1.5, 2, 3, 6. */
   static const struct {
      uint32_t exp, half;
      float value;
   } vals[] = {{0, 0, 0.0f},  {13, 0, 0.25f}, {14, 0, 0.5f}, {15, 0, 1.0f},
               {15, 1, 1.5f}, {16, 0, 2.0f},  {16, 1, 3.0f}, {17, 1, 6.0f}};
   for (int i = 0; i < 64; i++) {
      const int r = i % 8, g = (i / 8) % 8, b = (i + i / 8) % 8;
      const uint32_t word = (vals[r].exp << 6 | vals[r].half << 5) |
                            (vals[g].exp << 6 | vals[g].half << 5) << 11 |
                            (vals[b].exp << 5 | vals[b].half << 4) << 22;
      memcpy(d + i * 4, &word, 4);
      want[i][0] = vals[r].value;
      want[i][1] = vals[g].value;
      want[i][2] = vals[b].value;
      want[i][3] = 1.0f;
   }
}

static void fill_rgb10a2(uint8_t *d, float want[64][4])
{
   for (int i = 0; i < 64; i++) {
      const uint32_t x = i % 8, y = i / 8;
      const uint32_t r = (x * 130 + y * 7) % 1024, g = (y * 130 + x * 11) % 1024,
                     b = ((x + y) * 60 + 3) % 1024, a = (x + 2 * y) % 4;
      const uint32_t word = r | g << 10 | b << 20 | a << 30;
      memcpy(d + i * 4, &word, 4);
      want[i][0] = r / 1023.0f;
      want[i][1] = g / 1023.0f;
      want[i][2] = b / 1023.0f;
      want[i][3] = a / 3.0f;
   }
}

static void fill_r32f(uint8_t *d, float want[64][4])
{
   for (int i = 0; i < 64; i++) {
      const float v = (i % 8) * 1.5f - (i / 8) * 0.25f + 0.125f;
      memcpy(d + i * 4, &v, 4);
      want[i][0] = v;
      want[i][1] = want[i][2] = 0.0f;
      want[i][3] = 1.0f;
   }
}

/* Depth reads back in red only; the other channels aren't defined. */
static void fill_d16(uint8_t *d, float want[64][4])
{
   for (int i = 0; i < 64; i++) {
      const uint16_t v = (uint16_t)(i * 1000 + 17);
      memcpy(d + i * 2, &v, 2);
      want[i][0] = v / 65535.0f;
   }
}

/* D24's depth aspect copies as 32-bit words with the depth in the low 24 bits. */
static void fill_d24(uint8_t *d, float want[64][4])
{
   for (int i = 0; i < 64; i++) {
      const uint32_t v = (uint32_t)i * 250000u + 99u;
      memcpy(d + i * 4, &v, 4);
      want[i][0] = v / 16777215.0f;
   }
}

static void fill_d32f(uint8_t *d, float want[64][4])
{
   for (int i = 0; i < 64; i++) {
      const float v = i / 64.0f + 0.001f;
      memcpy(d + i * 4, &v, 4);
      want[i][0] = v;
   }
}

/* Block-compressed textures: 2x2 blocks of 4x4 texels, each block with its own endpoints and code
 * pattern, so that both modes of each block type are covered. */
static const uint16_t bc1_colors[4][2] = {
   {0xf800, 0x001f}, {0x001f, 0xf800}, {0xffff, 0x07e0}, {0x8408, 0x279c}};
static const uint8_t bc4_ends[4][2] = {{255, 0}, {0, 255}, {200, 40}, {40, 200}};

static unsigned bc1_code(int b, int t)
{
   const unsigned codes[4] = {t % 4, 3 - t % 4, (t / 4) % 4, (t * 3) % 4};
   return codes[b];
}

static unsigned bc4_code(int b, int t)
{
   const unsigned codes[4] = {t % 8, (t + 3) % 8, (t * 3) % 8, 7 - t % 8};
   return codes[b];
}

static unsigned bc2_alpha(int b, int t) { return (t + 5 * b) % 16; }

static void rgb565(uint16_t c, float rgb[3])
{
   const unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
   rgb[0] = ((r << 3) | (r >> 2)) / 255.0f;
   rgb[1] = ((g << 2) | (g >> 4)) / 255.0f;
   rgb[2] = ((b << 3) | (b >> 2)) / 255.0f;
}

/* A BC1 color block: two RGB565 endpoints and a 2-bit code per texel. */
static void put_bc1(uint8_t *d, int b)
{
   uint32_t bits = 0;
   for (int t = 0; t < 16; t++)
      bits |= bc1_code(b, t) << (2 * t);
   d[0] = bc1_colors[b][0] & 0xff, d[1] = bc1_colors[b][0] >> 8;
   d[2] = bc1_colors[b][1] & 0xff, d[3] = bc1_colors[b][1] >> 8;
   memcpy(d + 4, &bits, 4);
}

/* What texel t of BC1 color block b reads as. BC1 has a three-color mode, with transparent black,
 * when the first endpoint isn't the larger one; BC2 and BC3 always use four colors. */
static void bc1_texel(int b, int t, bool always_four, float out[4])
{
   float e0[3], e1[3];
   rgb565(bc1_colors[b][0], e0);
   rgb565(bc1_colors[b][1], e1);
   const bool four = always_four || bc1_colors[b][0] > bc1_colors[b][1];
   const unsigned code = bc1_code(b, t);
   for (int k = 0; k < 3; k++) {
      if (code < 2)
         out[k] = code ? e1[k] : e0[k];
      else if (four)
         out[k] = code == 2 ? (2 * e0[k] + e1[k]) / 3 : (e0[k] + 2 * e1[k]) / 3;
      else
         out[k] = code == 2 ? (e0[k] + e1[k]) / 2 : 0.0f;
   }
   out[3] = !four && code == 3 ? 0.0f : 1.0f;
}

/* A BC4 block (also BC3's alpha and each half of BC5): two 8-bit endpoints and a 3-bit code per
 * texel. Block b uses endpoint pair e. */
static void put_bc4(uint8_t *d, int b, int e)
{
   uint64_t bits = 0;
   for (int t = 0; t < 16; t++)
      bits |= (uint64_t)bc4_code(b, t) << (3 * t);
   d[0] = bc4_ends[e][0], d[1] = bc4_ends[e][1];
   for (int k = 0; k < 6; k++)
      d[2 + k] = (uint8_t)(bits >> (8 * k));
}

static float bc4_texel(int b, int e, int t)
{
   const unsigned code = bc4_code(b, t), e0 = bc4_ends[e][0], e1 = bc4_ends[e][1];
   if (code < 2)
      return (code ? e1 : e0) / 255.0f;
   if (e0 > e1)
      return ((8 - code) * e0 + (code - 1) * e1) / (7 * 255.0f);
   if (code >= 6)
      return code == 6 ? 0.0f : 1.0f;
   return ((6 - code) * e0 + (code - 1) * e1) / (5 * 255.0f);
}

enum bc_kind { BC1, BC2, BC3, BC4, BC5, BC7 };

static void put_bc7(uint8_t *blk, int b);
static void bc7_texel(int b, int t, float w[4]);

/* Writes variant b (0 to 3) of a block of this kind. */
static void put_bc_block(uint8_t *blk, enum bc_kind kind, int b)
{
   if (kind == BC7) {
      put_bc7(blk, b);
      return;
   }
   if (kind == BC2)
      for (int t = 0; t < 16; t += 2)
         blk[t / 2] = (uint8_t)(bc2_alpha(b, t) | bc2_alpha(b, t + 1) << 4);
   if (kind == BC3 || kind == BC4 || kind == BC5)
      put_bc4(blk, b, b);
   if (kind == BC5)
      put_bc4(blk + 8, (b + 1) % 4, (b + 1) % 4);
   if (kind == BC1)
      put_bc1(blk, b);
   else if (kind == BC2 || kind == BC3)
      put_bc1(blk + 8, b);
}

/* What texel t of block variant b reads as. */
static void bc_texel(enum bc_kind kind, int b, int t, float w[4])
{
   if (kind == BC7) {
      bc7_texel(b, t, w);
      return;
   }
   if (kind <= BC3)
      bc1_texel(b, t, kind != BC1, w);
   if (kind == BC2)
      w[3] = bc2_alpha(b, t) / 15.0f;
   if (kind == BC3)
      w[3] = bc4_texel(b, b, t);
   if (kind >= BC4) {
      w[0] = bc4_texel(b, b, t);
      w[1] = kind == BC5 ? bc4_texel((b + 1) % 4, (b + 1) % 4, t) : 0.0f;
      w[2] = 0.0f;
      w[3] = 1.0f;
   }
}

static unsigned bc_block_size(enum bc_kind kind) { return kind == BC1 || kind == BC4 ? 8 : 16; }

static void fill_bc(uint8_t *d, float want[64][4], enum bc_kind kind)
{
   for (int b = 0; b < 4; b++)
      put_bc_block(d + b * bc_block_size(kind), kind, b);
   for (int i = 0; i < 64; i++) {
      const int x = i % 8, y = i / 8, b = (y / 4) * 2 + x / 4, t = (y % 4) * 4 + x % 4;
      bc_texel(kind, b, t, want[i]);
   }
}

static void fill_bc1(uint8_t *d, float want[64][4]) { fill_bc(d, want, BC1); }
static void fill_bc2(uint8_t *d, float want[64][4]) { fill_bc(d, want, BC2); }
static void fill_bc3(uint8_t *d, float want[64][4]) { fill_bc(d, want, BC3); }
static void fill_bc4(uint8_t *d, float want[64][4]) { fill_bc(d, want, BC4); }
static void fill_bc5(uint8_t *d, float want[64][4]) { fill_bc(d, want, BC5); }

static void put_bits(uint8_t *d, unsigned *pos, uint32_t v, unsigned n)
{
   for (unsigned k = 0; k < n; k++, (*pos)++)
      if (v >> k & 1)
         d[*pos / 8] |= (uint8_t)(1u << (*pos % 8));
}

/* BC7 in mode 6: one subset, RGBA endpoints of 7 bits plus a p-bit each, and a 4-bit index per
 * texel (3 bits for the first). Texel t gets index t, so every weight is used. */
static const uint8_t bc7_ends[4][2][4] = {
   {{0, 64, 128, 254}, {255, 191, 127, 1}},
   {{17, 33, 99, 255}, {200, 10, 60, 0}},
   {{255, 255, 255, 255}, {1, 1, 1, 1}},
   {{120, 40, 200, 100}, {121, 41, 201, 101}},
};
static const uint8_t bc7_weights[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

static void put_bc7(uint8_t *blk, int b)
{
   unsigned pos = 0;
   memset(blk, 0, 16);
   put_bits(blk, &pos, 1u << 6, 7);
   for (int c = 0; c < 4; c++) {
      put_bits(blk, &pos, bc7_ends[b][0][c] >> 1, 7);
      put_bits(blk, &pos, bc7_ends[b][1][c] >> 1, 7);
   }
   put_bits(blk, &pos, bc7_ends[b][0][0] & 1, 1);
   put_bits(blk, &pos, bc7_ends[b][1][0] & 1, 1);
   for (unsigned t = 0; t < 16; t++)
      put_bits(blk, &pos, t, t ? 4 : 3);
}

static void bc7_texel(int b, int t, float w[4])
{
   for (int c = 0; c < 4; c++)
      w[c] = (((64 - bc7_weights[t]) * bc7_ends[b][0][c] + bc7_weights[t] * bc7_ends[b][1][c] + 32) >>
              6) / 255.0f;
}

static void fill_bc7(uint8_t *d, float want[64][4]) { fill_bc(d, want, BC7); }

static const struct texture_format {
   const char *name;
   VkFormat format;
   VkImageAspectFlags aspect;
   int channels;    /* channels checked */
   float tolerance; /* absolute up to 1, relative above */
   texel_fill_fn fill;
} texture_formats[] = {
   {"RGBA8", VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.006f, fill_rgba8_unorm},
   {"BGRA8", VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.006f, fill_bgra8_unorm},
   {"RGBA8_SRGB", VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.006f, fill_rgba8_srgb},
   {"RGBA16F", VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.002f, fill_rgba16f},
   {"R11G11B10F", VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.002f,
    fill_r11g11b10f},
   {"RGB10A2", VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.002f,
    fill_rgb10a2},
   {"R32F", VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 1e-5f, fill_r32f},
   {"D16", VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 0.0002f, fill_d16},
   {"D24S8", VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1e-5f, fill_d24},
   {"D32F", VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1e-5f, fill_d32f},
   {"D32FS8", VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1e-5f, fill_d32f},
   {"BC1", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.01f, fill_bc1},
   {"BC2", VK_FORMAT_BC2_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.01f, fill_bc2},
   {"BC3", VK_FORMAT_BC3_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.01f, fill_bc3},
   {"BC4", VK_FORMAT_BC4_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.01f, fill_bc4},
   /* RADV on Xclipse re-encodes BC5 as EAC RG11 and BC7 as BC3 for the texture unit
    * (radv_bc_emulation_format), losing a few steps of 255 on BC5 and tens on BC7 by design. */
   {"BC5", VK_FORMAT_BC5_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.04f, fill_bc5},
   {"BC7", VK_FORMAT_BC7_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0.2f, fill_bc7},
};

static bool texel_close(const float *got, const float *want, int channels, float tolerance)
{
   for (int c = 0; c < channels; c++) {
      const float scale = fabsf(want[c]) > 1.0f ? fabsf(want[c]) : 1.0f;
      if (!(fabsf(got[c] - want[c]) <= tolerance * scale)) /* NaN, from a texel never written, fails */
         return false;
   }
   return true;
}

/* 64 RGBA texels, with null for what isn't a number. */
static void jtexels(const char *k, const float (*v)[4])
{
   jopen(k, '[');
   for (int i = 0; i < 64; i++) {
      for (int c = 0; c < 4; c++) {
         if (isfinite(v[i][c])) {
            jnum(NULL, v[i][c]);
         } else {
            jkey(NULL);
            fputs("null", stdout);
         }
      }
   }
   jclose();
}

/* Uploads an 8x8 texture in each format both drivers list as sampled, reads every texel back in a
 * compute shader, exactly and through a linear filter where the format has one, and compares
 * with the values the uploads encode. */
static void run_textures(void)
{
   enum { N = 64 };
   jbool("has_bc", has_bc);
   struct buffer upload = make_buffer(N * 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
   struct buffer out =
      make_buffer(2 * N * sizeof(float[4]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

   VkSamplerCreateInfo sci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
   };
   VkSampler linear, nearest;
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &linear));
   sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &nearest));

   const VkDescriptorSetLayoutBinding bindings[2] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2,
      .pBindings = bindings,
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
   VkShaderModule module = make_shader(sample_comp, sizeof(sample_comp));
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module,
            .pName = "main",
         },
      .layout = layout,
   };
   VkPipeline pipeline;
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));
   const VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = pool_sizes,
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

   char wrong[400] = "";
   jopen("formats", '[');
   for (size_t f = 0; f < sizeof(texture_formats) / sizeof(texture_formats[0]); f++) {
      const struct texture_format *tf = &texture_formats[f];
      VkFormatProperties fp;
      vkGetPhysicalDeviceFormatProperties(pdev, tf->format, &fp);
      const VkFormatFeatureFlags need =
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
      const bool supported = (fp.optimalTilingFeatures & need) == need;
      const bool filters =
         fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      jopen(NULL, '{');
      jstr("format", tf->name);
      jhex("features", fp.optimalTilingFeatures);
      jbool("supported", supported);
      if (!supported) {
         jclose();
         continue;
      }

      VkImageCreateInfo ici = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = tf->format,
         .extent = {8, 8, 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      VkImage image;
      VKCHECK("vkCreateImage", vkCreateImage(dev, &ici, NULL, &image));
      VkMemoryRequirements req;
      vkGetImageMemoryRequirements(dev, image, &req);
      VkDeviceMemory mem = alloc_memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, image, mem, 0));
      const bool stencil = tf->format == VK_FORMAT_D24_UNORM_S8_UINT ||
                           tf->format == VK_FORMAT_D32_SFLOAT_S8_UINT;
      const VkImageSubresourceRange all = {
         tf->aspect | (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0), 0, 1, 0, 1};
      VkImageViewCreateInfo vci = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = image,
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = tf->format,
         .subresourceRange = {tf->aspect, 0, 1, 0, 1},
      };
      VkImageView view;
      VKCHECK("vkCreateImageView", vkCreateImageView(dev, &vci, NULL, &view));

      float want[N][4] = {{0}};
      memset(upload.map, 0, N * 16);
      tf->fill(upload.map, want);
      memset(out.map, 0xff, 2 * N * sizeof(float[4])); /* NaN */

      VkDescriptorImageInfo image_info = {filters ? linear : nearest, view,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkDescriptorBufferInfo buffer_info = {out.buf, 0, VK_WHOLE_SIZE};
      const VkWriteDescriptorSet writes[2] = {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer_info,
         },
      };
      vkUpdateDescriptorSets(dev, 2, writes, 0, NULL);

      VkCommandBuffer cb = begin_cmd();
      VkImageMemoryBarrier barrier = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = image,
         .subresourceRange = all,
      };
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                           0, NULL, 0, NULL, 1, &barrier);
      const VkBufferImageCopy region = {
         .imageSubresource = {tf->aspect, 0, 0, 1},
         .imageExtent = {8, 8, 1},
      };
      vkCmdCopyBufferToImage(cb, upload.buf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                             &region);
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 0, NULL, 0, NULL, 1, &barrier);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
      vkCmdDispatch(cb, 1, 1, 1);
      VkMemoryBarrier to_host = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &to_host, 0, NULL, 0, NULL);
      VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
      char what[64];
      snprintf(what, sizeof(what), "sampling %s", tf->name);
      submit_and_wait(cb, what);

      /* fetched[64], then filtered[64]. The filtered sample sits halfway between a texel and its
       * right and lower neighbors (clamped at the edges), so it reads as the four's average. */
      const float(*got)[4] = out.map;
      int fetch_bad = 0, filter_bad = 0;
      char first[200] = "";
      for (int i = 0; i < N; i++) {
         const int x = i % 8, y = i / 8, x1 = x < 7 ? x + 1 : 7, y1 = y < 7 ? y + 1 : 7;
         float avg[4];
         for (int c = 0; c < 4; c++)
            avg[c] = (want[i][c] + want[y * 8 + x1][c] + want[y1 * 8 + x][c] +
                      want[y1 * 8 + x1][c]) / 4;
         const bool fetch_ok = texel_close(got[i], want[i], tf->channels, tf->tolerance);
         const bool filter_ok =
            !filters || texel_close(got[N + i], avg, tf->channels, tf->tolerance);
         const float *g = fetch_ok ? got[N + i] : got[i], *w = fetch_ok ? avg : want[i];
         if ((!fetch_ok || !filter_ok) && !first[0])
            snprintf(first, sizeof(first), "%s at (%d,%d): got (%g, %g, %g, %g), want (%g, %g, %g, %g)",
                     fetch_ok ? "filtered" : "fetched", x, y, g[0], g[1], g[2], g[3], w[0], w[1],
                     w[2], w[3]);
         fetch_bad += !fetch_ok;
         filter_bad += !filter_ok;
      }
      jint("fetch_wrong", fetch_bad);
      if (filters)
         jint("filter_wrong", filter_bad);
      if (first[0]) {
         jstr("first_wrong", first);
         jtexels("fetched", got);
         jtexels("want", (const float(*)[4])want);
         add_problem(wrong, sizeof(wrong), "%s %d fetched/%d filtered", tf->name, fetch_bad,
                     filter_bad);
      }
      jclose();
      vkDestroyImageView(dev, view, NULL);
      vkDestroyImage(dev, image, NULL);
      vkFreeMemory(dev, mem, NULL);
   }
   jclose();
   if (wrong[0])
      finish("fail", "wrong texels: %s", wrong);

   vkDestroyDescriptorPool(dev, dpool, NULL);
   vkDestroyPipeline(dev, pipeline, NULL);
   vkDestroyShaderModule(dev, module, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   vkDestroySampler(dev, nearest, NULL);
   vkDestroySampler(dev, linear, NULL);
   free_buffer(&out);
   free_buffer(&upload);
}

/* ---- mips: textures with full mip chains, and cube maps, as games load them ---- */

enum { MIP_RGBA8 = -1 }; /* a mip_case kind besides the enum bc_kind ones */

static const struct mip_case {
   const char *name;
   VkFormat format;
   int kind;
   uint32_t width, height;
   bool cube; /* six layers, also sampled by direction */
   float tolerance;
} mip_cases[] = {
   {"RGBA8 64x64", VK_FORMAT_R8G8B8A8_UNORM, MIP_RGBA8, 64, 64, false, 0.006f},
   {"RGBA8 2048x2048", VK_FORMAT_R8G8B8A8_UNORM, MIP_RGBA8, 2048, 2048, false, 0.006f},
   {"RGBA8 640x360", VK_FORMAT_R8G8B8A8_UNORM, MIP_RGBA8, 640, 360, false, 0.006f},
   {"RGBA8 cube 128", VK_FORMAT_R8G8B8A8_UNORM, MIP_RGBA8, 128, 128, true, 0.006f},
   {"BC1 1024x1024", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, BC1, 1024, 1024, false, 0.01f},
   {"BC3 1024x1024", VK_FORMAT_BC3_UNORM_BLOCK, BC3, 1024, 1024, false, 0.01f},
   {"BC4 512x512", VK_FORMAT_BC4_UNORM_BLOCK, BC4, 512, 512, false, 0.01f},
   {"BC5 1024x1024", VK_FORMAT_BC5_UNORM_BLOCK, BC5, 1024, 1024, false, 0.04f},
   {"BC7 512x512", VK_FORMAT_BC7_UNORM_BLOCK, BC7, 512, 512, false, 0.2f},
   {"BC1 cube 256", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, BC1, 256, 256, true, 0.01f},
};

static uint32_t mip_hash(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
   uint32_t h = a * 0x9e3779b1u ^ b * 0x85ebca77u ^ c * 0xc2b2ae3du ^ d * 0x27d4eb2fu;
   h ^= h >> 15;
   h *= 0x2c1b3c6du;
   return h ^ h >> 12;
}

/* Bytes of level `level`, layer `layer` of a case: RGBA8 texels from a hash of where they are, or
 * BC blocks whose variant comes from such a hash. */
static size_t mip_level_size(const struct mip_case *c, uint32_t w, uint32_t h)
{
   return c->kind == MIP_RGBA8 ? (size_t)w * h * 4
                               : (size_t)((w + 3) / 4) * ((h + 3) / 4) * bc_block_size(c->kind);
}

static void mip_fill(const struct mip_case *c, uint8_t *d, uint32_t w, uint32_t h, int level,
                     int layer)
{
   if (c->kind == MIP_RGBA8) {
      for (uint32_t y = 0; y < h; y++)
         for (uint32_t x = 0; x < w; x++) {
            const uint32_t v = mip_hash(x, y, level, layer);
            memcpy(d + ((size_t)y * w + x) * 4, &v, 4);
         }
      return;
   }
   const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, size = bc_block_size(c->kind);
   for (uint32_t by = 0; by < bh; by++)
      for (uint32_t bx = 0; bx < bw; bx++)
         put_bc_block(d + ((size_t)by * bw + bx) * size, c->kind,
                      mip_hash(bx, by, level, layer) % 4);
}

static void mip_texel(const struct mip_case *c, uint32_t x, uint32_t y, int level, int layer,
                      float want[4])
{
   if (c->kind == MIP_RGBA8) {
      const uint32_t v = mip_hash(x, y, level, layer);
      for (int i = 0; i < 4; i++)
         want[i] = (v >> (8 * i) & 255) / 255.0f;
      return;
   }
   bc_texel(c->kind, mip_hash(x / 4, y / 4, level, layer) % 4, (y % 4) * 4 + x % 4, want);
}

/* Uploads each case's whole mip chain (and six faces for a cube) with one copy region per level,
 * and reads back a grid of every level and layer in a compute shader, textureGrad included: the
 * Xclipse 920 can't decode GFX11's partial NSA address encoding that it takes (see gfx10_nsa in
 * ACO). On Xclipse, RADV keeps BC4,
 * BC5 and BC7 as a second, re-encoded plane that a compute pass fills after each copy, so those
 * show whether that pass covers every level. */
static void run_mips(void)
{
   enum { MAX_LEVELS = 12, MAX_LAYERS = 6, GRID = 16 };
   const size_t ncases = sizeof(mip_cases) / sizeof(mip_cases[0]);
   jbool("texture_compression_bc", has_bc);

   step("creating pipeline");
   VkSamplerCreateInfo sci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = VK_LOD_CLAMP_NONE,
   };
   VkSampler sampler;
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &sampler));
   const VkDescriptorSetLayoutBinding bindings[3] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 3,
      .pBindings = bindings,
   };
   VkDescriptorSetLayout dsl;
   VKCHECK("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
   const VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int32_t[3])};
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &range,
   };
   VkPipelineLayout layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &layout));
   VkShaderModule cs = make_shader(mips_read_comp, sizeof(mips_read_comp));
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
   VkPipeline pipe;
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe));
   const VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
                                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = pool_sizes,
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
   const size_t out_size = (size_t)MAX_LAYERS * MAX_LEVELS * GRID * GRID * 4 * sizeof(float[4]);
   struct buffer out = make_buffer(out_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

   char wrong[800] = "";
   jopen("cases", '[');
   for (size_t k = 0; k < ncases; k++) {
      const struct mip_case *c = &mip_cases[k];
      jopen(NULL, '{');
      jstr("case", c->name);
      VkFormatProperties fp;
      vkGetPhysicalDeviceFormatProperties(pdev, c->format, &fp);
      const VkFormatFeatureFlags need =
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
      jbool("supported", (fp.optimalTilingFeatures & need) == need);
      if ((fp.optimalTilingFeatures & need) != need) {
         jclose();
         continue;
      }
      uint32_t levels = 1;
      while ((c->width > c->height ? c->width : c->height) >> levels)
         levels++;
      const uint32_t layers = c->cube ? 6 : 1;
      jint("levels", levels);

      /* The texture, and a cube for the cube binding (the texture itself, or a 4x4 dummy). */
      VkImage images[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
      VkDeviceMemory mems[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
      for (int i = 0; i < (c->cube ? 1 : 2); i++) {
         VkImageCreateInfo ici = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = c->cube || i ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = i ? VK_FORMAT_R8G8B8A8_UNORM : c->format,
            .extent = {i ? 4 : c->width, i ? 4 : c->height, 1},
            .mipLevels = i ? 1 : levels,
            .arrayLayers = i ? 6 : layers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         };
         VKCHECK("vkCreateImage", vkCreateImage(dev, &ici, NULL, &images[i]));
         VkMemoryRequirements req;
         vkGetImageMemoryRequirements(dev, images[i], &req);
         mems[i] = alloc_memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
         VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, images[i], mems[i], 0));
      }
      const VkImage cube_image = c->cube ? images[0] : images[1];
      VkImageView views[2];
      for (int i = 0; i < 2; i++) {
         VkImageViewCreateInfo vci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = i ? cube_image : images[0],
            .viewType = i ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = i && !c->cube ? VK_FORMAT_R8G8B8A8_UNORM : c->format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0,
                                 i ? 6 : layers},
         };
         VKCHECK("vkCreateImageView", vkCreateImageView(dev, &vci, NULL, &views[i]));
      }

      size_t total = 0, offsets[MAX_LEVELS];
      for (uint32_t l = 0; l < levels; l++) {
         offsets[l] = total;
         total += layers * mip_level_size(c, c->width >> l ? c->width >> l : 1,
                                          c->height >> l ? c->height >> l : 1);
      }
      struct buffer upload = make_buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
      VkBufferImageCopy regions[MAX_LEVELS];
      for (uint32_t l = 0; l < levels; l++) {
         const uint32_t w = c->width >> l ? c->width >> l : 1, h = c->height >> l ? c->height >> l : 1;
         for (uint32_t a = 0; a < layers; a++)
            mip_fill(c, (uint8_t *)upload.map + offsets[l] + a * mip_level_size(c, w, h), w, h, l,
                     a);
         regions[l] = (VkBufferImageCopy){
            .bufferOffset = offsets[l],
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, layers},
            .imageExtent = {w, h, 1},
         };
      }
      memset(out.map, 0xff, out_size); /* NaN */
      const VkDescriptorImageInfo image_infos[2] = {
         {sampler, views[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {sampler, views[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      };
      const VkDescriptorBufferInfo buffer_info = {out.buf, 0, VK_WHOLE_SIZE};
      VkWriteDescriptorSet writes[3];
      for (uint32_t b = 0; b < 3; b++)
         writes[b] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = b,
            .descriptorCount = 1,
            .descriptorType = bindings[b].descriptorType,
            .pImageInfo = b < 2 ? &image_infos[b] : NULL,
            .pBufferInfo = b < 2 ? NULL : &buffer_info,
         };
      vkUpdateDescriptorSets(dev, 3, writes, 0, NULL);

      VkCommandBuffer cb = begin_cmd();
      VkImageMemoryBarrier barriers[2];
      for (int i = 0; i < 2; i++)
         barriers[i] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = i ? images[1] : images[0],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0,
                                 VK_REMAINING_ARRAY_LAYERS},
         };
      const uint32_t nimages = c->cube ? 1 : 2;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                           0, NULL, 0, NULL, nimages, barriers);
      vkCmdCopyBufferToImage(cb, upload.buf, images[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             levels, regions);
      for (int i = 0; i < 2; i++) {
         barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
         barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 0, NULL, 0, NULL, nimages, barriers);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
      const int32_t pc[3] = {(int32_t)levels, (int32_t)layers, c->cube};
      vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
      vkCmdDispatch(cb, 1, 1, 1);
      VkMemoryBarrier to_host = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &to_host, 0, NULL, 0, NULL);
      VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
      submit_and_wait(cb, c->name);

      /* Wrong texels per way of reading, and which levels have any. */
      static const char *read_names[4] = {"fetch", "sample", "grad", "cube"};
      const float(*got)[4] = out.map;
      int bad[4] = {0};
      uint32_t bad_levels = 0;
      char first[200] = "";
      for (uint32_t a = 0; a < layers; a++) {
         for (uint32_t l = 0; l < levels; l++) {
            const uint32_t w = c->width >> l ? c->width >> l : 1, h = c->height >> l ? c->height >> l : 1;
            const uint32_t nx = w < GRID ? w : GRID, ny = h < GRID ? h : GRID;
            for (uint32_t gy = 0; gy < ny; gy++) {
               for (uint32_t gx = 0; gx < nx; gx++) {
                  const uint32_t x = gx * w / nx + w / nx / 2, y = gy * h / ny + h / ny / 2;
                  float want[4];
                  mip_texel(c, x, y, l, a, want);
                  const size_t i = (((size_t)a * levels + l) * GRID * GRID + gy * GRID + gx) * 4;
                  for (int r = 0; r < (c->cube ? 4 : 3); r++) {
                     if (texel_close(got[i + r], want, 4, c->tolerance))
                        continue;
                     bad_levels |= 1u << l;
                     if (!bad[r]++ && !first[0])
                        snprintf(first, sizeof(first),
                                 "%s layer %u level %u (%u,%u): got (%g, %g, %g, %g), want (%g, %g, "
                                 "%g, %g)",
                                 read_names[r], a, l, x, y, got[i + r][0], got[i + r][1],
                                 got[i + r][2], got[i + r][3], want[0], want[1], want[2], want[3]);
                  }
               }
            }
         }
      }
      for (int r = 0; r < 4; r++) {
         char key[40];
         snprintf(key, sizeof(key), "wrong_%s", read_names[r]);
         jint(key, bad[r]);
      }
      jopen("wrong_levels", '[');
      for (uint32_t l = 0; l < levels; l++)
         if (bad_levels >> l & 1)
            jint(NULL, l);
      jclose();
      if (first[0])
         jstr("first_wrong", first);
      jclose();
      if (bad[0] + bad[1] + bad[2] + bad[3])
         add_problem(wrong, sizeof(wrong), "%s %d/%d/%d/%d", c->name, bad[0], bad[1], bad[2],
                     bad[3]);

      free_buffer(&upload);
      for (int i = 0; i < 2; i++) {
         vkDestroyImageView(dev, views[i], NULL);
         if (images[i]) {
            vkDestroyImage(dev, images[i], NULL);
            vkFreeMemory(dev, mems[i], NULL);
         }
      }
   }
   jclose();
   if (wrong[0])
      finish("fail", "wrong texels (fetch/sample/grad/cube): %s", wrong);

   free_buffer(&out);
   vkDestroyDescriptorPool(dev, dpool, NULL);
   vkDestroyPipeline(dev, pipe, NULL);
   vkDestroyShaderModule(dev, cs, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   vkDestroySampler(dev, sampler, NULL);
}

/* ---- mrt: eight render targets at once, with clears and additive blending, as deferred
 * renderers use them ---- */

enum mrt_kind { MRT_UNORM8, MRT_SRGB8, MRT_UNORM10_2, MRT_FLOAT };

static const struct mrt_format {
   const char *name;
   VkFormat format;
   int channels; /* stored; the others sample as 0, and alpha as 1 */
   enum mrt_kind kind;
   float tolerance; /* absolute up to 1, relative above */
} mrt_formats[8] = {
   {"RGBA8", VK_FORMAT_R8G8B8A8_UNORM, 4, MRT_UNORM8, 0.006f},
   {"RGBA16F", VK_FORMAT_R16G16B16A16_SFLOAT, 4, MRT_FLOAT, 0.002f},
   {"RGB10A2", VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, MRT_UNORM10_2, 0.003f},
   {"R11G11B10F", VK_FORMAT_B10G11R11_UFLOAT_PACK32, 3, MRT_FLOAT, 0.035f},
   {"RG16F", VK_FORMAT_R16G16_SFLOAT, 2, MRT_FLOAT, 0.002f},
   {"R32F", VK_FORMAT_R32_SFLOAT, 1, MRT_FLOAT, 1e-5f},
   {"BGRA8", VK_FORMAT_B8G8R8A8_UNORM, 4, MRT_UNORM8, 0.006f},
   /* The hardware's sRGB encode can land one 8-bit step off exact rounding, which is up to 0.009
    * in linear near 1. */
   {"RGBA8_SRGB", VK_FORMAT_R8G8B8A8_SRGB, 4, MRT_SRGB8, 0.01f},
};

static float linear_to_srgb(float c)
{
   return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/* What channel i of a target in this format keeps of a written v. Float formats round within the
 * tolerance. */
static float mrt_store(enum mrt_kind kind, int i, float v)
{
   const float c = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
   switch (kind) {
   case MRT_UNORM8:
      return roundf(c * 255.0f) / 255.0f;
   case MRT_SRGB8:
      return i == 3 ? roundf(c * 255.0f) / 255.0f
                    : srgb_to_linear(roundf(linear_to_srgb(c) * 255.0f) / 255.0f);
   case MRT_UNORM10_2:
      return i == 3 ? roundf(c * 3.0f) / 3.0f : roundf(c * 1023.0f) / 1023.0f;
   default:
      return v;
   }
}

/* The clear, the first pass (mrt.frag) over the top half, and the second (mrt_add.frag, added
 * with blending) over the right half, as the host works them out. */
static void mrt_clear(int k, float c[4])
{
   c[0] = k / 8.0f, c[1] = 1.0f - k / 8.0f, c[2] = 0.375f, c[3] = 0.75f;
}

static void mrt_expected(int k, int x, int y, float want[4])
{
   const struct mrt_format *f = &mrt_formats[k];
   const float add[3] = {(k + 1) / 16.0f, (8 - k) / 32.0f, 0.0625f};
   float clear[4];
   mrt_clear(k, clear);
   for (int i = 0; i < 4; i++) {
      if (i >= f->channels) {
         want[i] = i == 3 ? 1.0f : 0.0f;
         continue;
      }
      float v = y < 128 ? ((x * (k + 1) + y * (2 * k + 3) + 40 * i) & 255) / 255.0f : clear[i];
      v = mrt_store(f->kind, i, v);
      if (x >= 128 && i < 3)
         v = mrt_store(f->kind, i, v + add[i]);
      want[i] = v;
   }
}

/* A pipeline drawing screen.vert's triangle into n attachments, either writing them or adding
 * to their colors (alpha kept). */
static VkPipeline make_mrt_pipeline(VkShaderModule vs, VkShaderModule fs, const VkFormat *formats,
                                    uint32_t n, bool add, VkPipelineLayout layout)
{
   const VkPipelineShaderStageCreateInfo stages[2] = {
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
      .lineWidth = 1.0f,
   };
   VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   VkPipelineColorBlendAttachmentState attachments[8];
   for (uint32_t k = 0; k < n; k++) {
      attachments[k] = (VkPipelineColorBlendAttachmentState){
         .blendEnable = add,
         .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .colorBlendOp = VK_BLEND_OP_ADD,
         .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
         .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .alphaBlendOp = VK_BLEND_OP_ADD,
         .colorWriteMask = 0xf,
      };
   }
   VkPipelineColorBlendStateCreateInfo blend_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = n,
      .pAttachments = attachments,
   };
   const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dynamic_states,
   };
   VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = n,
      .pColorAttachmentFormats = formats,
   };
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
      .pColorBlendState = &blend_state,
      .pDynamicState = &dynamic,
      .layout = layout,
   };
   VkPipeline pipeline;
   VKCHECK("vkCreateGraphicsPipelines",
           vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));
   return pipeline;
}

/* Clears 8 256x256 targets of the formats D3D11 games render to, writes all 8 at once over the
 * top half, adds to them with blending over the right half, and samples them back in a compute
 * shader, as a deferred renderer reads its G-buffer. */
static void run_mrt(void)
{
   enum { W = 256, H = 256, N = 8, GRID = 64 };
   const uint32_t n = props.limits.maxColorAttachments < N ? props.limits.maxColorAttachments : N;
   jint("attachments", n);
   VkFormat formats[N];
   VkImage images[N];
   VkDeviceMemory mems[N];
   VkImageView views[N];
   for (uint32_t k = 0; k < n; k++) {
      const struct mrt_format *f = &mrt_formats[k];
      VkFormatProperties fp;
      vkGetPhysicalDeviceFormatProperties(pdev, f->format, &fp);
      const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
      if ((fp.optimalTilingFeatures & need) != need)
         finish("fail", "%s isn't a blendable, sampleable color attachment (features 0x%x)",
                f->name, fp.optimalTilingFeatures);
      formats[k] = f->format;
      VkImageCreateInfo ici = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = f->format,
         .extent = {W, H, 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      VKCHECK("vkCreateImage", vkCreateImage(dev, &ici, NULL, &images[k]));
      VkMemoryRequirements req;
      vkGetImageMemoryRequirements(dev, images[k], &req);
      mems[k] = alloc_memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, images[k], mems[k], 0));
      VkImageViewCreateInfo vci = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = images[k],
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = f->format,
         .subresourceRange = color_range,
      };
      VKCHECK("vkCreateImageView", vkCreateImageView(dev, &vci, NULL, &views[k]));
   }

   step("creating pipelines");
   VkPipelineLayoutCreateInfo empty_plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   VkPipelineLayout draw_layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &empty_plci, NULL, &draw_layout));
   VkShaderModule vs = make_shader(screen_vert, sizeof(screen_vert));
   VkShaderModule fs_write = make_shader(mrt_frag, sizeof(mrt_frag));
   VkShaderModule fs_add = make_shader(mrt_add_frag, sizeof(mrt_add_frag));
   VkPipeline write_pipe = make_mrt_pipeline(vs, fs_write, formats, n, false, draw_layout);
   VkPipeline add_pipe = make_mrt_pipeline(vs, fs_add, formats, n, true, draw_layout);

   VkSamplerCreateInfo sci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
   };
   VkSampler sampler;
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &sampler));
   const VkDescriptorSetLayoutBinding bindings[2] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, N, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2,
      .pBindings = bindings,
   };
   VkDescriptorSetLayout dsl;
   VKCHECK("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
   };
   VkPipelineLayout read_layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &read_layout));
   VkShaderModule cs = make_shader(mrt_read_comp, sizeof(mrt_read_comp));
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = cs,
            .pName = "main",
         },
      .layout = read_layout,
   };
   VkPipeline read_pipe;
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &read_pipe));
   const VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, N},
                                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = pool_sizes,
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
   struct buffer out =
      make_buffer(N * GRID * GRID * sizeof(float[4]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
   memset(out.map, 0xff, N * GRID * GRID * sizeof(float[4])); /* NaN */
   VkDescriptorImageInfo image_infos[N];
   for (uint32_t k = 0; k < N; k++) /* targets past the device's limit read target 0 */
      image_infos[k] = (VkDescriptorImageInfo){sampler, views[k < n ? k : 0],
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
   VkDescriptorBufferInfo buffer_info = {out.buf, 0, VK_WHOLE_SIZE};
   const VkWriteDescriptorSet writes[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 0,
         .descriptorCount = N,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = image_infos,
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &buffer_info,
      },
   };
   vkUpdateDescriptorSets(dev, 2, writes, 0, NULL);

   VkCommandBuffer cb = begin_cmd();
   VkImageMemoryBarrier barriers[N];
   VkRenderingAttachmentInfo color[N];
   for (uint32_t k = 0; k < n; k++) {
      barriers[k] = (VkImageMemoryBarrier){
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = images[k],
         .subresourceRange = color_range,
      };
      color[k] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = views[k],
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      };
      mrt_clear(k, color[k].clearValue.color.float32);
   }
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, n,
                        barriers);
   VkRenderingInfo ri = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {W, H}},
      .layerCount = 1,
      .colorAttachmentCount = n,
      .pColorAttachments = color,
   };
   vkCmdBeginRendering(cb, &ri);
   const VkViewport viewport = {0.0f, 0.0f, W, H, 0.0f, 1.0f};
   vkCmdSetViewport(cb, 0, 1, &viewport);
   const VkRect2D top = {{0, 0}, {W, H / 2}}, right = {{W / 2, 0}, {W / 2, H}};
   vkCmdSetScissor(cb, 0, 1, &top);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, write_pipe);
   vkCmdDraw(cb, 3, 1, 0, 0);
   vkCmdSetScissor(cb, 0, 1, &right);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, add_pipe);
   vkCmdDraw(cb, 3, 1, 0, 0);
   vkCmdEndRendering(cb);
   for (uint32_t k = 0; k < n; k++) {
      barriers[k].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barriers[k].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barriers[k].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      barriers[k].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   }
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, n, barriers);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, read_pipe);
   vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, read_layout, 0, 1, &set, 0, NULL);
   vkCmdDispatch(cb, GRID / 8, GRID / 8, 1);
   VkMemoryBarrier to_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                        &to_host, 0, NULL, 0, NULL);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   jnum("ms_gpu", submit_and_wait(cb, "mrt"));

   /* Regions: drawn (top left), drawn and added to (top right), cleared (bottom left), cleared
    * and added to (bottom right). */
   static const char *region_names[4] = {"drawn", "drawn_added", "cleared", "cleared_added"};
   const float(*got)[4] = out.map;
   char wrong[400] = "";
   jopen("targets", '[');
   for (uint32_t k = 0; k < n; k++) {
      const struct mrt_format *f = &mrt_formats[k];
      int bad[4] = {0};
      char first[4][160] = {""};
      for (int gy = 0; gy < GRID; gy++) {
         for (int gx = 0; gx < GRID; gx++) {
            const int x = gx * 4 + 1, y = gy * 4 + 2, r = (y >= H / 2) * 2 + (x >= W / 2);
            const float *g = got[(k * GRID + gy) * GRID + gx];
            float want[4];
            mrt_expected(k, x, y, want);
            if (texel_close(g, want, 4, f->tolerance))
               continue;
            if (!bad[r]++)
               snprintf(first[r], sizeof(first[r]),
                        "(%d,%d): got (%g, %g, %g, %g), want (%g, %g, %g, %g)", x, y, g[0], g[1],
                        g[2], g[3], want[0], want[1], want[2], want[3]);
         }
      }
      jopen(NULL, '{');
      jstr("format", f->name);
      for (int r = 0; r < 4; r++) {
         char key[40];
         snprintf(key, sizeof(key), "wrong_%s", region_names[r]);
         jint(key, bad[r]);
         if (bad[r]) {
            snprintf(key, sizeof(key), "first_wrong_%s", region_names[r]);
            jstr(key, first[r]);
         }
      }
      jclose();
      if (bad[0] + bad[1] + bad[2] + bad[3])
         add_problem(wrong, sizeof(wrong), "target %u %s %d/%d/%d/%d", k, f->name, bad[0], bad[1],
                     bad[2], bad[3]);
   }
   jclose();
   if (wrong[0])
      finish("fail", "wrong texels (drawn/drawn+added/cleared/cleared+added): %s", wrong);

   free_buffer(&out);
   vkDestroyDescriptorPool(dev, dpool, NULL);
   vkDestroyPipeline(dev, read_pipe, NULL);
   vkDestroyShaderModule(dev, cs, NULL);
   vkDestroyPipelineLayout(dev, read_layout, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   vkDestroySampler(dev, sampler, NULL);
   vkDestroyPipeline(dev, add_pipe, NULL);
   vkDestroyPipeline(dev, write_pipe, NULL);
   vkDestroyShaderModule(dev, fs_add, NULL);
   vkDestroyShaderModule(dev, fs_write, NULL);
   vkDestroyShaderModule(dev, vs, NULL);
   vkDestroyPipelineLayout(dev, draw_layout, NULL);
   for (uint32_t k = 0; k < n; k++) {
      vkDestroyImageView(dev, views[k], NULL);
      vkDestroyImage(dev, images[k], NULL);
      vkFreeMemory(dev, mems[k], NULL);
   }
}

/* ---- vertex: fetching vertex attributes in the formats D3D11 games use ---- */

enum attr_kind {
   ATTR_FLOAT, ATTR_HALF, ATTR_UNORM8, ATTR_SNORM8, ATTR_UINT8, ATTR_BGRA8,
   ATTR_UNORM16, ATTR_SNORM16, ATTR_UINT16, ATTR_SINT16, ATTR_RGB10A2, ATTR_UINT32,
};

enum { VERTEX_ATTRIBUTES = 16, VERTEX_STRIDE = 96, VERTEX_COUNT = 64 };

/* vertex.vert's inputs, interleaved in one binding, each at its natural alignment. */
static const struct vertex_format {
   const char *name;
   VkFormat format;
   enum attr_kind kind;
   int channels;
   uint32_t offset;
} vertex_formats[VERTEX_ATTRIBUTES] = {
   {"R32G32B32_SFLOAT", VK_FORMAT_R32G32B32_SFLOAT, ATTR_FLOAT, 3, 0},
   {"R32G32_SFLOAT", VK_FORMAT_R32G32_SFLOAT, ATTR_FLOAT, 2, 12},
   {"R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT, ATTR_HALF, 4, 20},
   {"R16G16_SFLOAT", VK_FORMAT_R16G16_SFLOAT, ATTR_HALF, 2, 28},
   {"R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM, ATTR_UNORM8, 4, 32},
   {"R8G8B8A8_SNORM", VK_FORMAT_R8G8B8A8_SNORM, ATTR_SNORM8, 4, 36},
   {"R8G8B8A8_UINT", VK_FORMAT_R8G8B8A8_UINT, ATTR_UINT8, 4, 40},
   {"B8G8R8A8_UNORM", VK_FORMAT_B8G8R8A8_UNORM, ATTR_BGRA8, 4, 44},
   {"R16G16B16A16_SNORM", VK_FORMAT_R16G16B16A16_SNORM, ATTR_SNORM16, 4, 48},
   {"R16G16_SNORM", VK_FORMAT_R16G16_SNORM, ATTR_SNORM16, 2, 56},
   {"R16G16_UNORM", VK_FORMAT_R16G16_UNORM, ATTR_UNORM16, 2, 60},
   {"A2B10G10R10_UNORM", VK_FORMAT_A2B10G10R10_UNORM_PACK32, ATTR_RGB10A2, 4, 64},
   {"R32_UINT", VK_FORMAT_R32_UINT, ATTR_UINT32, 1, 68},
   {"R16G16B16A16_UINT", VK_FORMAT_R16G16B16A16_UINT, ATTR_UINT16, 4, 72},
   {"R16G16B16A16_SINT", VK_FORMAT_R16G16B16A16_SINT, ATTR_SINT16, 4, 80},
   {"R16G16B16A16_UNORM", VK_FORMAT_R16G16B16A16_UNORM, ATTR_UNORM16, 4, 88},
};

/* Writes attribute a of vertex v (counted across the whole buffer) at d, and works out what the
 * shader reads: missing channels read as 0, and alpha as 1. Vertices 0 to 3 start with each
 * format's edge values. */
static void put_attribute(uint8_t *d, int v, int a, float want[4])
{
   static const uint8_t edge8[4] = {0x00, 0x7f, 0x80, 0xff};
   static const uint16_t edge16[4] = {0x0000, 0x7fff, 0x8000, 0xffff};
   const struct vertex_format *f = &vertex_formats[a];
   want[0] = want[1] = want[2] = 0.0f, want[3] = 1.0f;
   if (f->kind == ATTR_RGB10A2) {
      const uint32_t r = (v * 37 + a) & 1023, g = (v * 211 + 5) & 1023, b = (v * 419 + 9) & 1023;
      const uint32_t w = r | g << 10 | b << 20 | (uint32_t)(v & 3) << 30;
      memcpy(d, &w, 4);
      want[0] = r / 1023.0f, want[1] = g / 1023.0f, want[2] = b / 1023.0f, want[3] = (v & 3) / 3.0f;
      return;
   }
   for (int j = 0; j < f->channels; j++) {
      const unsigned h = v * 37 + a * 101 + j * 13 + 7;
      const uint8_t b8 = v < 4 && j == 0 ? edge8[v] : h & 255;
      const uint16_t b16 = v < 4 && j == 0 ? edge16[v] : (h * 40503u) & 0xffff;
      switch (f->kind) {
      case ATTR_FLOAT: {
         const float x = (v - 40) * 0.25f + j * 64.0f + a * 1024.0f;
         memcpy(d + 4 * j, &x, 4);
         want[j] = x;
         break;
      }
      case ATTR_HALF: {
         const int k = (v * 4 + j * 5 + a * 3) % HALFS;
         d[2 * j] = halfs[k].bits & 0xff;
         d[2 * j + 1] = halfs[k].bits >> 8;
         want[j] = halfs[k].value;
         break;
      }
      case ATTR_UNORM8:
         d[j] = b8, want[j] = b8 / 255.0f;
         break;
      case ATTR_SNORM8:
         d[j] = b8, want[j] = fmaxf((int8_t)b8 / 127.0f, -1.0f);
         break;
      case ATTR_UINT8:
         d[j] = b8, want[j] = b8;
         break;
      case ATTR_BGRA8: /* bytes B, G, R, A */
         d[j] = b8, want[j == 3 ? 3 : 2 - j] = b8 / 255.0f;
         break;
      case ATTR_UINT32: {
         const uint32_t u = v * 65537u + 17u; /* exact as a float */
         memcpy(d, &u, 4);
         want[j] = (float)u;
         break;
      }
      default: /* 16 bits per channel */
         d[2 * j] = b16 & 0xff;
         d[2 * j + 1] = b16 >> 8;
         want[j] = f->kind == ATTR_UNORM16  ? b16 / 65535.0f
                   : f->kind == ATTR_SNORM16 ? fmaxf((int16_t)b16 / 32767.0f, -1.0f)
                   : f->kind == ATTR_UINT16  ? (float)b16
                                             : (float)(int16_t)b16;
         break;
      }
   }
}

/* A pipeline that only runs vertex.vert, with vertex_formats in binding 0, rasterizing nothing. */
static VkPipeline make_vertex_pipeline(VkShaderModule vs, VkPipelineLayout layout,
                                       const VkDynamicState *dynamic_states, uint32_t dynamic_count)
{
   const VkVertexInputBindingDescription binding = {0, VERTEX_STRIDE, VK_VERTEX_INPUT_RATE_VERTEX};
   VkVertexInputAttributeDescription attributes[VERTEX_ATTRIBUTES];
   for (uint32_t a = 0; a < VERTEX_ATTRIBUTES; a++)
      attributes[a] = (VkVertexInputAttributeDescription){a, 0, vertex_formats[a].format,
                                                          vertex_formats[a].offset};
   const VkPipelineShaderStageCreateInfo stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vs,
      .pName = "main",
   };
   VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &binding,
      .vertexAttributeDescriptionCount = VERTEX_ATTRIBUTES,
      .pVertexAttributeDescriptions = attributes,
   };
   VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
   };
   const VkViewport viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
   const VkRect2D scissor = {{0, 0}, {1, 1}};
   VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor,
   };
   VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .rasterizerDiscardEnable = VK_TRUE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .lineWidth = 1.0f,
   };
   VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   VkPipelineColorBlendStateCreateInfo blend_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
   };
   VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = dynamic_count,
      .pDynamicStates = dynamic_states,
   };
   VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
   };
   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = 1,
      .pStages = &stage,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend_state,
      .pDynamicState = &dynamic,
      .layout = layout,
   };
   VkPipeline pipeline;
   VKCHECK("vkCreateGraphicsPipelines",
           vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));
   return pipeline;
}

/* Reads 16 attribute formats from an interleaved vertex buffer three ways: vertex input in the
 * pipeline; the same with the stride set at bind time (DXVK sets strides that way); and all of it
 * set at draw time with VK_EXT_vertex_input_dynamic_state, which RADV compiles into a separate
 * vertex fetch prolog, as it does for DXVK's pipeline libraries. Each way draws its own instance
 * from its own part of the buffer, and the vertex shader stores what it read. */
static void run_vertex(void)
{
   enum { PATHS = 3, A = VERTEX_ATTRIBUTES, N = VERTEX_COUNT };
   static const char *path_names[PATHS] = {"pipeline", "bind_stride", "dynamic"};
   if (!has_vertex_stores)
      finish("skip", "no vertexPipelineStoresAndAtomics");
   jbool("vertex_input_dynamic_state", has_vertex_input_dynamic);
   PFN_vkCmdSetVertexInputEXT vkCmdSetVertexInputEXT = NULL;
   if (has_vertex_input_dynamic &&
       !(vkCmdSetVertexInputEXT =
            (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(dev, "vkCmdSetVertexInputEXT")))
      finish("fail", "device function vkCmdSetVertexInputEXT missing");
   const int paths = has_vertex_input_dynamic ? PATHS : PATHS - 1;
   for (int a = 0; a < A; a++) {
      VkFormatProperties fp;
      vkGetPhysicalDeviceFormatProperties(pdev, vertex_formats[a].format, &fp);
      if (!(fp.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
         finish("fail", "%s isn't a vertex format (buffer features 0x%x)", vertex_formats[a].name,
                fp.bufferFeatures);
   }

   struct buffer vb = make_buffer(PATHS * N * VERTEX_STRIDE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
   struct buffer out =
      make_buffer(PATHS * N * A * sizeof(float[4]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
   memset(vb.map, 0, PATHS * N * VERTEX_STRIDE);
   memset(out.map, 0xff, PATHS * N * A * sizeof(float[4])); /* NaN */
   float(*want)[4] = malloc(PATHS * N * A * sizeof(float[4]));
   for (int v = 0; v < PATHS * N; v++)
      for (int a = 0; a < A; a++)
         put_attribute((uint8_t *)vb.map + v * VERTEX_STRIDE + vertex_formats[a].offset, v, a,
                       want[v * A + a]);

   step("creating pipelines");
   const VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                 VK_SHADER_STAGE_VERTEX_BIT, NULL};
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
   VkShaderModule vs = make_shader(vertex_vert, sizeof(vertex_vert));
   const VkDynamicState stride_state = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
   const VkDynamicState input_state = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
   VkPipeline pipes[PATHS] = {
      make_vertex_pipeline(vs, layout, NULL, 0),
      make_vertex_pipeline(vs, layout, &stride_state, 1),
      has_vertex_input_dynamic ? make_vertex_pipeline(vs, layout, &input_state, 1) : VK_NULL_HANDLE,
   };

   const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
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
   VkDescriptorBufferInfo buffer_info = {out.buf, 0, VK_WHOLE_SIZE};
   const VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &buffer_info,
   };
   vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);

   VkCommandBuffer cb = begin_cmd();
   VkRenderingInfo ri = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {1, 1}},
      .layerCount = 1,
   };
   vkCmdBeginRendering(cb, &ri);
   vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
   for (int p = 0; p < paths; p++) {
      const VkDeviceSize offset = (VkDeviceSize)p * N * VERTEX_STRIDE, stride = VERTEX_STRIDE;
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[p]);
      if (p == 1) {
         vkCmdBindVertexBuffers2(cb, 0, 1, &vb.buf, &offset, NULL, &stride);
      } else {
         if (p == 2) {
            const VkVertexInputBindingDescription2EXT b2 = {
               .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
               .binding = 0,
               .stride = VERTEX_STRIDE,
               .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
               .divisor = 1,
            };
            VkVertexInputAttributeDescription2EXT a2[A];
            for (uint32_t a = 0; a < A; a++)
               a2[a] = (VkVertexInputAttributeDescription2EXT){
                  .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
                  .location = a,
                  .binding = 0,
                  .format = vertex_formats[a].format,
                  .offset = vertex_formats[a].offset,
               };
            vkCmdSetVertexInputEXT(cb, 1, &b2, A, a2);
         }
         vkCmdBindVertexBuffers(cb, 0, 1, &vb.buf, &offset);
      }
      vkCmdDraw(cb, N, 1, 0, p);
   }
   vkCmdEndRendering(cb);
   VkMemoryBarrier to_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                        &to_host, 0, NULL, 0, NULL);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   jnum("ms_gpu", submit_and_wait(cb, "vertex"));

   const float(*got)[4] = out.map;
   char wrong[1500] = "";
   jopen("paths", '[');
   for (int p = 0; p < paths; p++) {
      jopen(NULL, '{');
      jstr("path", path_names[p]);
      jopen("wrong_formats", '[');
      for (int a = 0; a < A; a++) {
         int bad = 0;
         char first[200] = "";
         for (int v = p * N; v < (p + 1) * N; v++) {
            const float *g = got[v * A + a], *w = want[v * A + a];
            bool ok = true;
            for (int i = 0; i < 4; i++)
               ok &= fabsf(g[i] - w[i]) <= 1e-6f * fmaxf(1.0f, fabsf(w[i])); /* false for NaN */
            if (!ok && !bad++)
               snprintf(first, sizeof(first), "vertex %d: got (%g, %g, %g, %g), want (%g, %g, %g, %g)",
                        v - p * N, g[0], g[1], g[2], g[3], w[0], w[1], w[2], w[3]);
         }
         if (!bad)
            continue;
         jopen(NULL, '{');
         jstr("format", vertex_formats[a].name);
         jint("wrong", bad);
         jstr("first_wrong", first);
         jclose();
         add_problem(wrong, sizeof(wrong), "%s %s %d/%d", path_names[p], vertex_formats[a].name,
                     bad, N);
      }
      jclose();
      jclose();
   }
   jclose();
   if (wrong[0])
      finish("fail", "wrong attributes (way, format, vertices): %s", wrong);

   free(want);
   vkDestroyDescriptorPool(dev, dpool, NULL);
   for (int p = 0; p < paths; p++)
      vkDestroyPipeline(dev, pipes[p], NULL);
   vkDestroyShaderModule(dev, vs, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   free_buffer(&out);
   free_buffer(&vb);
}

/* ---- depth: depth and stencil testing, and sampling what they leave, as shadow maps do ---- */

static const struct depth_format {
   const char *name;
   VkFormat format;
   bool stencil;
} depth_formats[] = {
   {"D16", VK_FORMAT_D16_UNORM, false},
   {"D32F", VK_FORMAT_D32_SFLOAT, false},
   {"D24S8", VK_FORMAT_D24_UNORM_S8_UINT, true},
   {"D32FS8", VK_FORMAT_D32_SFLOAT_S8_UINT, true}, /* what DXVK uses for D24S8 without it */
};

/* The planes depth.vert draws, as (constant, per u, per v). */
static const float depth_plane1[4] = {0.25f, 0.5f, 0.1f}, depth_plane2[4] = {0.75f, -0.5f, 0.05f},
                   depth_plane3[4] = {0.1f, 0.0f, 0.0f};

/* What run_depth's draws leave at texel (x, y) of the 256x256 target. The top half gets plane 1
 * (stencil set to 0x5a), then plane 2 where it is nearer (stencil incremented), then with a
 * stencil format plane 3 where the stencil is 0x5b. Rows 128 to 223 get plane 2 only (stencil 0
 * incremented to 1); the rest keeps the clear (depth 1, stencil 0). Returns false where planes 1
 * and 2 are too close to say which one wins. */
static bool depth_expected(int x, int y, bool stencil, float *depth, int *s)
{
   const float u = (x + 0.5f) / 256.0f, v = (y + 0.5f) / 256.0f;
   const float z1 = depth_plane1[0] + depth_plane1[1] * u + depth_plane1[2] * v;
   const float z2 = depth_plane2[0] + depth_plane2[1] * u + depth_plane2[2] * v;
   if (y < 128) {
      *depth = z2 < z1 ? (stencil ? depth_plane3[0] : z2) : z1;
      *s = z2 < z1 ? 0x5b : 0x5a;
      return fabsf(z1 - z2) > 0.01f;
   }
   *depth = y < 224 ? z2 : 1.0f;
   *s = y < 224;
   return true;
}

static VkPipeline make_depth_pipeline(VkShaderModule vs, VkPipelineLayout layout,
                                      const struct depth_format *f, VkCompareOp depth_compare,
                                      VkStencilOpState stencil)
{
   const VkPipelineShaderStageCreateInfo stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vs,
      .pName = "main",
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
      .lineWidth = 1.0f,
   };
   VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = depth_compare,
      .stencilTestEnable = f->stencil,
      .front = stencil,
      .back = stencil,
   };
   VkPipelineColorBlendStateCreateInfo blend_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
   };
   const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dynamic_states,
   };
   VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .depthAttachmentFormat = f->format,
      .stencilAttachmentFormat = f->stencil ? f->format : VK_FORMAT_UNDEFINED,
   };
   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = 1,
      .pStages = &stage,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depth_stencil,
      .pColorBlendState = &blend_state,
      .pDynamicState = &dynamic,
      .layout = layout,
   };
   VkPipeline pipeline;
   VKCHECK("vkCreateGraphicsPipelines",
           vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));
   return pipeline;
}

/* Draws overlapping depth planes into a 256x256 depth (and stencil) target with no fragment
 * shader, as shadow passes do, testing depth with LESS and stencil with ALWAYS and EQUAL, then
 * reads it back in a compute shader: depth, stencil, and depth comparisons with nearest and linear
 * filtering. The target starts with a fast clear and moves to a read-only layout, as a game's
 * depth buffer does before its lighting pass samples it. */
static void run_depth(void)
{
   enum { W = 256, H = 256, GRID = 64, CELLS = GRID * GRID };
   const size_t nformats = sizeof(depth_formats) / sizeof(depth_formats[0]);

   step("creating pipelines");
   const VkPushConstantRange plane_range = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float[4])};
   VkPipelineLayoutCreateInfo draw_plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &plane_range,
   };
   VkPipelineLayout draw_layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &draw_plci, NULL, &draw_layout));
   VkShaderModule vs = make_shader(depth_vert, sizeof(depth_vert));

   VkSampler nearest, shadow_nearest, shadow_linear;
   VkSamplerCreateInfo sci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
   };
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &nearest));
   sci.compareEnable = VK_TRUE;
   sci.compareOp = VK_COMPARE_OP_LESS;
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &shadow_nearest));
   sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &shadow_linear));

   const VkDescriptorSetLayoutBinding bindings[6] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 6,
      .pBindings = bindings,
   };
   VkDescriptorSetLayout dsl;
   VKCHECK("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
   VkPipelineLayoutCreateInfo read_plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
   };
   VkPipelineLayout read_layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &read_plci, NULL, &read_layout));
   VkShaderModule cs = make_shader(depth_read_comp, sizeof(depth_read_comp));
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = cs,
            .pName = "main",
         },
      .layout = read_layout,
   };
   VkPipeline read_pipe;
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &read_pipe));
   const VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
                                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = pool_sizes,
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

   /* What the stencil binding reads for formats without stencil. */
   VkImageCreateInfo dummy_ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8_UINT,
      .extent = {1, 1, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkImage dummy;
   VKCHECK("vkCreateImage", vkCreateImage(dev, &dummy_ici, NULL, &dummy));
   VkMemoryRequirements dummy_req;
   vkGetImageMemoryRequirements(dev, dummy, &dummy_req);
   VkDeviceMemory dummy_mem = alloc_memory(dummy_req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, dummy, dummy_mem, 0));
   VkImageViewCreateInfo dummy_vci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = dummy,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = VK_FORMAT_R8_UINT,
      .subresourceRange = color_range,
   };
   VkImageView dummy_view;
   VKCHECK("vkCreateImageView", vkCreateImageView(dev, &dummy_vci, NULL, &dummy_view));
   VkCommandBuffer cb = begin_cmd();
   VkImageMemoryBarrier dummy_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = dummy,
      .subresourceRange = color_range,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, NULL, 0, NULL, 1, &dummy_barrier);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   submit_and_wait(cb, "dummy image layout");

   struct buffer want = make_buffer(CELLS * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
   struct buffer out =
      make_buffer(CELLS * sizeof(float[4]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
   char wrong[600] = "";
   jopen("formats", '[');
   for (size_t k = 0; k < nformats; k++) {
      const struct depth_format *f = &depth_formats[k];
      jopen(NULL, '{');
      jstr("format", f->name);
      VkFormatProperties fp;
      vkGetPhysicalDeviceFormatProperties(pdev, f->format, &fp);
      const VkFormatFeatureFlags need =
         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
      jbool("supported", (fp.optimalTilingFeatures & need) == need);
      if ((fp.optimalTilingFeatures & need) != need) {
         jclose();
         continue;
      }
      const VkImageAspectFlags aspects =
         VK_IMAGE_ASPECT_DEPTH_BIT | (f->stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
      VkImageCreateInfo ici = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = f->format,
         .extent = {W, H, 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      VkImage image;
      VKCHECK("vkCreateImage", vkCreateImage(dev, &ici, NULL, &image));
      VkMemoryRequirements req;
      vkGetImageMemoryRequirements(dev, image, &req);
      VkDeviceMemory mem = alloc_memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, image, mem, 0));
      VkImageView views[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
      const VkImageAspectFlags view_aspects[3] = {aspects, VK_IMAGE_ASPECT_DEPTH_BIT,
                                                  VK_IMAGE_ASPECT_STENCIL_BIT};
      for (int i = 0; i < (f->stencil ? 3 : 2); i++) {
         VkImageViewCreateInfo vci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = f->format,
            .subresourceRange = {view_aspects[i], 0, 1, 0, 1},
         };
         VKCHECK("vkCreateImageView", vkCreateImageView(dev, &vci, NULL, &views[i]));
      }

      const VkStencilOpState set_5a = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP,
                                       VK_COMPARE_OP_ALWAYS, 0xff, 0xff, 0x5a};
      const VkStencilOpState increment = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                          VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 0xff, 0xff, 0};
      const VkStencilOpState equal_5b = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                                         VK_COMPARE_OP_EQUAL, 0xff, 0xff, 0x5b};
      VkPipeline pipes[3] = {
         make_depth_pipeline(vs, draw_layout, f, VK_COMPARE_OP_LESS, set_5a),
         make_depth_pipeline(vs, draw_layout, f, VK_COMPARE_OP_LESS, increment),
         f->stencil ? make_depth_pipeline(vs, draw_layout, f, VK_COMPARE_OP_ALWAYS, equal_5b)
                    : VK_NULL_HANDLE,
      };

      float *want_depth = want.map;
      for (int gy = 0; gy < GRID; gy++) {
         for (int gx = 0; gx < GRID; gx++) {
            int s;
            depth_expected(gx * 4 + 1, gy * 4 + 2, f->stencil, &want_depth[gy * GRID + gx], &s);
         }
      }
      memset(out.map, 0xff, CELLS * sizeof(float[4])); /* NaN */
      const VkDescriptorImageInfo image_infos[4] = {
         {nearest, views[1], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
         f->stencil
            ? (VkDescriptorImageInfo){nearest, views[2], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}
            : (VkDescriptorImageInfo){nearest, dummy_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {shadow_nearest, views[1], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
         {shadow_linear, views[1], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
      };
      const VkDescriptorBufferInfo buffer_infos[2] = {{want.buf, 0, VK_WHOLE_SIZE},
                                                      {out.buf, 0, VK_WHOLE_SIZE}};
      VkWriteDescriptorSet writes[6];
      for (uint32_t b = 0; b < 6; b++)
         writes[b] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = b,
            .descriptorCount = 1,
            .descriptorType = bindings[b].descriptorType,
            .pImageInfo = b < 4 ? &image_infos[b] : NULL,
            .pBufferInfo = b < 4 ? NULL : &buffer_infos[b - 4],
         };
      vkUpdateDescriptorSets(dev, 6, writes, 0, NULL);

      cb = begin_cmd();
      VkImageMemoryBarrier barrier = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = image,
         .subresourceRange = {aspects, 0, 1, 0, 1},
      };
      const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tests, 0, 0, NULL, 0, NULL, 1,
                           &barrier);
      VkRenderingAttachmentInfo attachment = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = views[0],
         .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue.depthStencil = {1.0f, 0},
      };
      VkRenderingInfo ri = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
         .renderArea = {{0, 0}, {W, H}},
         .layerCount = 1,
         .pDepthAttachment = &attachment,
         .pStencilAttachment = f->stencil ? &attachment : NULL,
      };
      vkCmdBeginRendering(cb, &ri);
      const VkViewport viewport = {0.0f, 0.0f, W, H, 0.0f, 1.0f};
      vkCmdSetViewport(cb, 0, 1, &viewport);
      const VkRect2D scissors[3] = {{{0, 0}, {W, 128}}, {{0, 0}, {W, 224}}, {{0, 0}, {W, H}}};
      const float *planes[3] = {depth_plane1, depth_plane2, depth_plane3};
      for (int d = 0; d < (f->stencil ? 3 : 2); d++) {
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[d]);
         vkCmdSetScissor(cb, 0, 1, &scissors[d]);
         vkCmdPushConstants(cb, draw_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float[4]),
                            planes[d]);
         vkCmdDraw(cb, 6, 1, 0, 0);
      }
      vkCmdEndRendering(cb);
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      vkCmdPipelineBarrier(cb, tests, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
                           &barrier);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, read_pipe);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, read_layout, 0, 1, &set, 0,
                              NULL);
      vkCmdDispatch(cb, GRID / 8, GRID / 8, 1);
      VkMemoryBarrier to_host = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &to_host, 0, NULL, 0, NULL);
      VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
      submit_and_wait(cb, f->name);

      /* Checks: depth, stencil, nearest comparison, linear comparison. The linear one is checked
       * only where the four texels it filters have nearly the same depth. */
      static const char *check_names[4] = {"depth", "stencil", "compare", "pcf"};
      const float(*got)[4] = out.map;
      int bad[4] = {0};
      char first[4][160] = {""};
      for (int gy = 0; gy < GRID; gy++) {
         for (int gx = 0; gx < GRID; gx++) {
            const int x = gx * 4 + 1, y = gy * 4 + 2;
            const float *g = got[gy * GRID + gx];
            float d, lo = 2.0f, hi = -1.0f;
            int s;
            if (!depth_expected(x, y, f->stencil, &d, &s))
               continue;
            bool decided = true;
            for (int n = 0; n < 4; n++) {
               float dn;
               int sn;
               decided &= depth_expected(x + n % 2, y + n / 2, f->stencil, &dn, &sn);
               lo = fminf(lo, dn), hi = fmaxf(hi, dn);
            }
            const bool ok[4] = {
               fabsf(g[0] - d) <= 1e-4f,
               !f->stencil || g[1] == s,
               g[2] == 1.0f,
               !(decided && hi - lo < 0.006f) || g[3] == 1.0f,
            };
            for (int c = 0; c < 4; c++)
               if (!ok[c] && !bad[c]++)
                  snprintf(first[c], sizeof(first[c]),
                           "(%d,%d): got (%g, %g, %g, %g), want depth %g stencil %d", x, y, g[0],
                           g[1], g[2], g[3], d, s);
         }
      }
      for (int c = 0; c < 4; c++) {
         char key[40];
         snprintf(key, sizeof(key), "wrong_%s", check_names[c]);
         jint(key, bad[c]);
         if (bad[c]) {
            snprintf(key, sizeof(key), "first_wrong_%s", check_names[c]);
            jstr(key, first[c]);
         }
      }
      jclose();
      if (bad[0] + bad[1] + bad[2] + bad[3])
         add_problem(wrong, sizeof(wrong), "%s %d/%d/%d/%d", f->name, bad[0], bad[1], bad[2],
                     bad[3]);

      for (int d = 0; d < 3; d++)
         if (pipes[d])
            vkDestroyPipeline(dev, pipes[d], NULL);
      for (int i = 0; i < 3; i++)
         if (views[i])
            vkDestroyImageView(dev, views[i], NULL);
      vkDestroyImage(dev, image, NULL);
      vkFreeMemory(dev, mem, NULL);
   }
   jclose();
   if (wrong[0])
      finish("fail", "wrong texels (depth/stencil/compare/pcf): %s", wrong);

   free_buffer(&out);
   free_buffer(&want);
   vkDestroyImageView(dev, dummy_view, NULL);
   vkDestroyImage(dev, dummy, NULL);
   vkFreeMemory(dev, dummy_mem, NULL);
   vkDestroyDescriptorPool(dev, dpool, NULL);
   vkDestroyPipeline(dev, read_pipe, NULL);
   vkDestroyShaderModule(dev, cs, NULL);
   vkDestroyPipelineLayout(dev, read_layout, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
   vkDestroySampler(dev, shadow_linear, NULL);
   vkDestroySampler(dev, shadow_nearest, NULL);
   vkDestroySampler(dev, nearest, NULL);
   vkDestroyShaderModule(dev, vs, NULL);
   vkDestroyPipelineLayout(dev, draw_layout, NULL);
}

/* ---- xfb: transform feedback, which DXVK uses for D3D11 stream output ---- */

static void run_xfb(void)
{
   enum { W = 128, H = 128, INSTANCES = 5, VERTS = 3 * INSTANCES, STRIDE = 16 };
   if (!has_xfb)
      finish("skip", "no transformFeedback");
   const bool use_query = has_xfb_queries && env_uint("XD_XFB_QUERY", 1);
   jbool("has_transform_feedback_queries", has_xfb_queries);
   jbool("xfb_query", use_query);

   /* Extension entry points, loaded here because only this test enables the extension. */
#define XFB_FUNCS(X)                                                                               \
   X(vkCmdBindTransformFeedbackBuffersEXT) X(vkCmdBeginTransformFeedbackEXT)                       \
   X(vkCmdEndTransformFeedbackEXT) X(vkCmdBeginQueryIndexedEXT) X(vkCmdEndQueryIndexedEXT)
   XFB_FUNCS(DECLARE)
   const char *missing = NULL;
   XFB_FUNCS(LOAD_DEVICE)
#undef XFB_FUNCS
   if (missing)
      finish("fail", "device function %s missing", missing);

   struct target t = make_target(W, H);
   double ms;
   step("creating graphics pipeline");
   struct graphics g = make_graphics(xfb_vert, sizeof(xfb_vert), tri_frag, sizeof(tri_frag),
                                     VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);

   /* Twice the room the draw needs: writes past its end show up as changed fill bytes. */
   const VkDeviceSize size = 2 * VERTS * STRIDE, zero = 0;
   struct buffer out = make_buffer(size, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT, true);
   struct buffer counter = make_buffer(4, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT,
                                       true);
   memset(out.map, 0xff, size);
   *(uint32_t *)counter.map = 0xdeadbeef;

   VkQueryPool qpool = VK_NULL_HANDLE;
   if (use_query) {
      VkQueryPoolCreateInfo qci = {
         .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
         .queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT,
         .queryCount = 1,
      };
      VKCHECK("vkCreateQueryPool", vkCreateQueryPool(dev, &qci, NULL, &qpool));
   }

   /* Begin without counter buffers, so writing starts at offset 0; end with one, as DXVK does,
    * so the driver stores how many bytes it wrote. */
   VkCommandBuffer cb = begin_cmd();
   if (qpool)
      vkCmdResetQueryPool(cb, qpool, 0, 1);
   const float clear[4] = {0.0f, 0.0f, 0.25f, 1.0f};
   begin_target(cb, &t, clear);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
   vkCmdBindTransformFeedbackBuffersEXT(cb, 0, 1, &out.buf, &zero, &size);
   if (qpool)
      vkCmdBeginQueryIndexedEXT(cb, qpool, 0, 0, 0);
   vkCmdBeginTransformFeedbackEXT(cb, 0, 0, NULL, NULL);
   vkCmdDraw(cb, 3, INSTANCES, 0, 0);
   vkCmdEndTransformFeedbackEXT(cb, 0, 1, &counter.buf, &zero);
   if (qpool)
      vkCmdEndQueryIndexedEXT(cb, qpool, 0, 0);
   vkCmdEndRendering(cb);
   VkMemoryBarrier to_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
                       VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, &to_host, 0, NULL, 0, NULL);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   jnum("ms_gpu", submit_and_wait(cb, "transform feedback draw"));

   /* Vertex k is vertex k % 3 of instance k / 3: instances are processed in order. */
   step("reading results");
   char wrong[400] = "";
   const float *v = out.map;
   unsigned bad = 0;
   for (unsigned k = 0; k < VERTS; k++) {
      const float want[4] = {(float)(k % 3), (float)(k / 3), 1.0f, 2.0f};
      if (memcmp(&v[4 * k], want, sizeof(want)) && !bad++)
         snprintf(wrong, sizeof(wrong), "vertex %u is (%g, %g, %g, %g), want (%g, %g, 1, 2)", k,
                  v[4 * k], v[4 * k + 1], v[4 * k + 2], v[4 * k + 3], want[0], want[1]);
   }
   unsigned past_end = 0;
   for (unsigned i = VERTS * STRIDE / 4; i < size / 4; i++)
      past_end += ((const uint32_t *)out.map)[i] != 0xffffffff;
   const uint32_t bytes = *(const uint32_t *)counter.map;
   jint("vertices_wrong", bad);
   jint("dwords_written_past_end", past_end);
   jint("counter_bytes", bytes);
   if (past_end)
      snprintf(wrong + strlen(wrong), sizeof(wrong) - strlen(wrong),
               "%s%u dwords written past the end", wrong[0] ? ", " : "", past_end);
   if (bytes != VERTS * STRIDE)
      snprintf(wrong + strlen(wrong), sizeof(wrong) - strlen(wrong),
               "%scounter %u bytes (want %u)", wrong[0] ? ", " : "", bytes, VERTS * STRIDE);
   if (qpool) {
      uint64_t q[2] = {0};
      VKCHECK("vkGetQueryPoolResults (transform feedback)",
              vkGetQueryPoolResults(dev, qpool, 0, 1, sizeof(q), q, sizeof(q),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      jint("primitives_written", (long long)q[0]);
      jint("primitives_needed", (long long)q[1]);
      if (q[0] != INSTANCES || q[1] != INSTANCES)
         snprintf(wrong + strlen(wrong), sizeof(wrong) - strlen(wrong),
                  "%squery %llu written, %llu needed (want %u)", wrong[0] ? ", " : "",
                  (unsigned long long)q[0], (unsigned long long)q[1], INSTANCES);
   }
   if (wrong[0])
      finish("fail", "%s", wrong);

   if (qpool)
      vkDestroyQueryPool(dev, qpool, NULL);
   free_buffer(&counter);
   free_buffer(&out);
   free_graphics(&g);
   free_target(&t);
}

/* ---- xfbmany, xfbmanygs: transform feedback over many workgroups, into two buffers ---- */

/* Whether a buffer 0 record is a vertex: (index, 0, 1, 2) with an index below `verts`. */
static bool xfb_record_ok(const float *r, unsigned verts)
{
   return r[0] >= 0.0f && r[0] < (float)verts && r[0] == (float)(unsigned)r[0] && r[1] == 0.0f &&
          r[2] == 1.0f && r[3] == 2.0f;
}

/* Which corners of triangle t don't hold their own vertex (bit 0 = first). */
static unsigned xfb_wrong_corners(const float *rec, unsigned t, unsigned verts)
{
   unsigned corners = 0;
   for (unsigned i = 0; i < 3; i++) {
      const unsigned slot = 3 * t + i;
      corners |= (rec[4 * slot] != (float)slot || !xfb_record_ok(&rec[4 * slot], verts)) << i;
   }
   return corners;
}

/* Draws a grid of triangles in two draws, the second resuming from the counters the first one
 * stored (as DXVK does between draws), and captures every vertex's index into two buffers, from
 * the vertex shader or from a geometry shader. Transform feedback must keep draw order across the
 * draw's many workgroups, and the buffers must agree vertex for vertex. */
static void run_xfb_many_with(bool gs)
{
   enum { W = 256, H = 256 };
   /* XD_XFB_TRIS: triangles drawn (hex, like the other XD_ variables). */
   const unsigned tris = env_uint("XD_XFB_TRIS", 0x6000), half = tris / 2, verts = 3 * tris;
   if (!has_xfb)
      finish("skip", "no transformFeedback");
   if (gs && !has_geometry_shader)
      finish("skip", "no geometryShader");
   const bool use_query = has_xfb_queries && env_uint("XD_XFB_QUERY", 1);
   jint("triangles", tris);
   jbool("xfb_query", use_query);

#define XFB_FUNCS(X)                                                                               \
   X(vkCmdBindTransformFeedbackBuffersEXT) X(vkCmdBeginTransformFeedbackEXT)                       \
   X(vkCmdEndTransformFeedbackEXT) X(vkCmdBeginQueryIndexedEXT) X(vkCmdEndQueryIndexedEXT)
   XFB_FUNCS(DECLARE)
   const char *missing = NULL;
   XFB_FUNCS(LOAD_DEVICE)
#undef XFB_FUNCS
   if (missing)
      finish("fail", "device function %s missing", missing);

   struct target t = make_target(W, H);
   double ms;
   /* XD_XFB_FRAG=1: a pixel shader that reads many inputs instead of a constant one. */
   const bool many_inputs = env_uint("XD_XFB_FRAG", 0) != 0;
   const uint32_t *frag = many_inputs ? inputs_frag : tri_frag;
   const size_t frag_size = many_inputs ? sizeof(inputs_frag) : sizeof(tri_frag);
   jbool("many_ps_inputs", many_inputs);
   step("creating graphics pipeline");
   struct graphics g =
      gs ? make_graphics_gs(grid_vert, sizeof(grid_vert), xfb_many_geom, sizeof(xfb_many_geom), frag,
                            frag_size, VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms)
         : make_graphics(xfb_many_vert, sizeof(xfb_many_vert), frag, frag_size,
                         VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);

   /* Buffer 0 gets a vec4 per vertex, buffer 1 a uint. Twice the room the draws need: writes past
    * the end show up as changed fill bytes. The counters sit at offsets 0 and 8 of one buffer. */
   const VkDeviceSize sizes[2] = {2ull * verts * 16, 2ull * verts * 4}, zeros[2] = {0, 0};
   const VkDeviceSize counter_offsets[2] = {0, 8};
   struct buffer out[2];
   VkBuffer out_bufs[2];
   for (int i = 0; i < 2; i++) {
      out[i] = make_buffer(sizes[i], VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT, true);
      out_bufs[i] = out[i].buf;
      memset(out[i].map, 0xff, sizes[i]);
   }
   struct buffer counter = make_buffer(16, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT,
                                       true);
   memset(counter.map, 0xee, 16);
   const VkBuffer counters[2] = {counter.buf, counter.buf};

   VkQueryPool qpool = VK_NULL_HANDLE;
   if (use_query) {
      VkQueryPoolCreateInfo qci = {
         .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
         .queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT,
         .queryCount = 1,
      };
      VKCHECK("vkCreateQueryPool", vkCreateQueryPool(dev, &qci, NULL, &qpool));
   }

   VkCommandBuffer cb = begin_cmd();
   if (qpool) {
      vkCmdResetQueryPool(cb, qpool, 0, 1);
      vkCmdBeginQueryIndexedEXT(cb, qpool, 0, 0, 0);
   }
   const float clear[4] = {0.0f, 0.0f, 0.25f, 1.0f};
   for (unsigned part = 0; part < 2; part++) {
      const unsigned first = part ? half : 0, count = part ? tris - half : half;
      if (part) {
         /* The second draw starts where the first one's counters say it stopped. */
         VkMemoryBarrier counters_written = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
            .dstAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
         };
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
                              VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &counters_written, 0, NULL,
                              0, NULL);
      }
      begin_target(cb, &t, clear);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
      vkCmdBindTransformFeedbackBuffersEXT(cb, 0, 2, out_bufs, zeros, sizes);
      vkCmdBeginTransformFeedbackEXT(cb, 0, part ? 2 : 0, part ? counters : NULL,
                                     part ? counter_offsets : NULL);
      vkCmdDraw(cb, 3 * count, 1, 3 * first, 0);
      vkCmdEndTransformFeedbackEXT(cb, 0, 2, counters, counter_offsets);
      vkCmdEndRendering(cb);
   }
   if (qpool)
      vkCmdEndQueryIndexedEXT(cb, qpool, 0, 0);
   VkMemoryBarrier to_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
                       VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, &to_host, 0, NULL, 0, NULL);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   jnum("ms_gpu", submit_and_wait(cb, "transform feedback draws"));

   /* Slot k of each buffer should hold vertex k. Records that aren't a vertex at all, vertices
    * captured twice or never, and vertices captured out of draw order are counted separately. */
   step("reading results");
   const float *rec = out[0].map;
   const uint32_t *index = out[1].map;
   unsigned char *seen = calloc(verts, 1);
   if (!seen)
      finish("fail", "out of host memory");
   unsigned invalid = 0, duplicated = 0, missing_verts = 0, out_of_order = 0, disagree = 0;
   unsigned torn = 0, first_wrong = UINT32_MAX, first_wrong_holds = 0;
   /* Where it went wrong, for the first few places: runs of slots holding no vertex
    * ("slot+count"), and slots holding the wrong vertex ("slot:vertex"). */
   char holes[256] = "", misplaced[256] = "";
   unsigned hole_runs = 0, misplaced_listed = 0;
   for (unsigned k = 0; k < verts; k++) {
      if (!xfb_record_ok(&rec[4 * k], verts)) {
         /* List each run of bad slots once, from its first slot. */
         if (k == 0 || xfb_record_ok(&rec[4 * (k - 1)], verts)) {
            unsigned run = 1;
            while (k + run < verts && !xfb_record_ok(&rec[4 * (k + run)], verts))
               run++;
            if (hole_runs++ < 8)
               add_problem(holes, sizeof(holes), "%u+%u", k, run);
         }
         invalid++;
         continue;
      }
      const unsigned v = (unsigned)rec[4 * k];
      duplicated += seen[v]++ > 0;
      disagree += index[k] != v;
      if (v != k) {
         if (!out_of_order++) {
            first_wrong = k;
            first_wrong_holds = v;
         }
         if (misplaced_listed++ < 12)
            add_problem(misplaced, sizeof(misplaced), "%u:%u", k, v);
      }
   }
   for (unsigned v = 0; v < verts; v++)
      missing_verts += !seen[v];
   free(seen);
   /* Wherever a triangle lands, its three vertices must stay together and in order. */
   for (unsigned k = 0; k + 2 < verts; k += 3) {
      const float a = rec[4 * k], b = rec[4 * (k + 1)], c = rec[4 * (k + 2)];
      torn += !((unsigned)a % 3 == 0 && b == a + 1.0f && c == a + 2.0f);
   }
   /* Runs of triangles whose slots don't hold their own vertices, as "first+count/corners=value":
    * which corners went wrong anywhere in the run (bit 0 = first), and what the first wrong slot
    * holds (-1 for no vertex). */
   char wrong_runs[512] = "";
   unsigned wrong_run_count = 0;
   for (unsigned t = 0; t < tris;) {
      unsigned corners = xfb_wrong_corners(rec, t, verts);
      if (!corners) {
         t++;
         continue;
      }
      const unsigned first = t;
      const unsigned slot = 3 * t + (unsigned)__builtin_ctz(corners);
      const int holds = xfb_record_ok(&rec[4 * slot], verts) ? (int)rec[4 * slot] : -1;
      unsigned all_corners = 0;
      for (; t < tris && (corners = xfb_wrong_corners(rec, t, verts)); t++)
         all_corners |= corners;
      if (wrong_run_count++ < 16)
         add_problem(wrong_runs, sizeof(wrong_runs), "%u+%u/%u=%d", first, t - first, all_corners, holds);
   }
   unsigned past_end = 0;
   for (int i = 0; i < 2; i++)
      for (size_t j = sizes[i] / 8; j < sizes[i] / 4; j++)
         past_end += ((const uint32_t *)out[i].map)[j] != 0xffffffff;
   const uint32_t *c = counter.map;
   jint("counter_bytes_0", c[0]);
   jint("counter_bytes_1", c[2]);
   jint("records_invalid", invalid);
   jint("vertices_missing", missing_verts);
   jint("vertices_duplicated", duplicated);
   jint("vertices_out_of_order", out_of_order);
   jint("triangles_torn", torn);
   jint("buffers_disagree", disagree);
   jint("dwords_written_past_end", past_end);
   if (hole_runs)
      jstr("hole_runs", holes);
   if (out_of_order)
      jstr("misplaced", misplaced);
   if (wrong_run_count)
      jstr("wrong_runs", wrong_runs);

   char wrong[512] = "";
#define WRONG(...) add_problem(wrong, sizeof(wrong), __VA_ARGS__)
   if (c[0] != verts * 16 || c[2] != verts * 4)
      WRONG("counters %u and %u bytes (want %u and %u)", c[0], c[2], verts * 16, verts * 4);
   if (invalid)
      WRONG("%u of %u slots hold no vertex", invalid, verts);
   if (missing_verts || duplicated)
      WRONG("%u vertices missing, %u captured twice", missing_verts, duplicated);
   if (out_of_order)
      WRONG("%u vertices out of draw order (slot %u holds vertex %u)", out_of_order, first_wrong,
            first_wrong_holds);
   if (torn)
      WRONG("%u triangles torn apart", torn);
   if (disagree)
      WRONG("the buffers disagree at %u slots", disagree);
   if (past_end)
      WRONG("%u dwords written past the end", past_end);
   if (qpool) {
      uint64_t q[2] = {0};
      VKCHECK("vkGetQueryPoolResults (transform feedback)",
              vkGetQueryPoolResults(dev, qpool, 0, 1, sizeof(q), q, sizeof(q),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      jint("primitives_written", (long long)q[0]);
      jint("primitives_needed", (long long)q[1]);
      if (q[0] != tris || q[1] != tris)
         WRONG("query %llu written, %llu needed (want %u)", (unsigned long long)q[0],
               (unsigned long long)q[1], tris);
   }
#undef WRONG
   if (wrong[0])
      finish("fail", "%s", wrong);

   if (qpool)
      vkDestroyQueryPool(dev, qpool, NULL);
   free_buffer(&counter);
   free_buffer(&out[1]);
   free_buffer(&out[0]);
   free_graphics(&g);
   free_target(&t);
}

static void run_xfb_many(void) { run_xfb_many_with(false); }
static void run_xfb_many_gs(void) { run_xfb_many_with(true); }

/* ---- alu: FP32 FMA throughput ---- */

struct alu_args {
   struct compute *c;
   uint32_t groups, iterations;
};

static void record_alu(VkCommandBuffer cb, void *arg)
{
   struct alu_args *a = arg;
   /* Matches the shader's push block: m and k start at offset 16 (vec4 alignment). */
   struct {
      uint32_t iterations;
      float seed;
      float pad[2];
      float m[4];
      float k[4];
   } push = {a->iterations, 1.0f, {0, 0}, {0.999f, 0.9991f, 0.9992f, 0.9993f},
             {0.001f, 0.0011f, 0.0012f, 0.0013f}};
   bind_compute(cb, a->c);
   vkCmdPushConstants(cb, a->c->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
   vkCmdDispatch(cb, a->groups, 1, 1);
}

static void run_alu(void)
{
   const uint32_t groups = 1024, invocations = groups * 256;
   struct buffer out = make_buffer((VkDeviceSize)invocations * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   false);
   double ms, runs[RUNS];
   struct compute c = make_compute(alu_comp, sizeof(alu_comp), &out.buf, 1, 48, NULL, &ms);
   struct alu_args a = {&c, groups, 4096};
   double gpu_ms = measure(record_alu, &a, "FMA dispatch", runs);
   double gflop = (double)invocations * a.iterations * 64 / 1e9;
   jnum("gflop_per_run", gflop);
   jruns("ms_runs", runs, RUNS);
   jnum("ms_gpu", gpu_ms);
   jnum("gflops", gflop / (gpu_ms / 1e3));
   record_gpu_clock();
   free_compute(&c);
   free_buffer(&out);
}

/* ---- bandwidth: a shader copy and vkCmdCopyBuffer between device-local buffers ---- */

struct copy_args {
   struct compute *c;
   VkBuffer src, dst;
   VkDeviceSize size;
   bool copy_command;
};

static void record_copy(VkCommandBuffer cb, void *arg)
{
   struct copy_args *a = arg;
   for (int i = 0; i < 4; i++) {
      if (a->copy_command) {
         VkBufferCopy region = {0, 0, a->size};
         vkCmdCopyBuffer(cb, a->src, a->dst, 1, &region);
      } else {
         bind_compute(cb, a->c);
         vkCmdDispatch(cb, (uint32_t)(a->size / 16 / 256), 1, 1);
      }
   }
}

static void run_bandwidth(void)
{
   const VkDeviceSize size = 64ull << 20;
   const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
   struct buffer src = make_buffer(size, usage, false), dst = make_buffer(size, usage, false);
   VkCommandBuffer cb = begin_cmd();
   vkCmdFillBuffer(cb, src.buf, 0, size, 0x3f800000);
   VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
   submit_and_wait(cb, "buffer fill");

   double ms, runs[RUNS];
   VkBuffer bufs[2] = {src.buf, dst.buf};
   struct compute c = make_compute(copy_comp, sizeof(copy_comp), bufs, 2, 0, NULL, &ms);
   const double gb = 4.0 * 2 * size / 1e9; /* four copies, each reads and writes `size` */
   struct copy_args a = {&c, src.buf, dst.buf, size, false};

   double shader_ms = measure(record_copy, &a, "shader copy", runs);
   jruns("ms_runs_shader", runs, RUNS);
   jnum("gbps_shader_copy", gb / (shader_ms / 1e3));
   a.copy_command = true;
   double cmd_ms = measure(record_copy, &a, "vkCmdCopyBuffer", runs);
   jruns("ms_runs_copy_command", runs, RUNS);
   jnum("gbps_copy_command", gb / (cmd_ms / 1e3));
   record_gpu_clock();

   free_compute(&c);
   free_buffer(&src);
   free_buffer(&dst);
}

/* ---- fill: full-screen overdraw at 1080p, opaque and blended ---- */

struct fill_args {
   struct graphics *g;
   struct target *t;
   uint32_t layers;
};

static void record_fill(VkCommandBuffer cb, void *arg)
{
   struct fill_args *a = arg;
   const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
   begin_target(cb, a->t, clear);
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, a->g->pipeline);
   vkCmdDraw(cb, 3, a->layers, 0, 0);
   vkCmdEndRendering(cb);
}

static void run_fill(void)
{
   struct target t = make_target(1920, 1080);
   double ms, runs[RUNS];
   struct graphics opaque = make_graphics(screen_vert, sizeof(screen_vert), flat_frag,
                                          sizeof(flat_frag), VK_FORMAT_R8G8B8A8_UNORM, false, 0, &ms);
   struct graphics blended = make_graphics(screen_vert, sizeof(screen_vert), flat_frag,
                                           sizeof(flat_frag), VK_FORMAT_R8G8B8A8_UNORM, true, 0, &ms);
   struct fill_args a = {&opaque, &t, 128};
   const double gpix = (double)t.w * t.h * a.layers / 1e9;

   double opaque_ms = measure(record_fill, &a, "opaque fill", runs);
   jruns("ms_runs_opaque", runs, RUNS);
   jnum("gpix_s_opaque", gpix / (opaque_ms / 1e3));
   a.g = &blended;
   double blend_ms = measure(record_fill, &a, "blended fill", runs);
   jruns("ms_runs_blend", runs, RUNS);
   jnum("gpix_s_blend", gpix / (blend_ms / 1e3));
   record_gpu_clock();

   free_graphics(&opaque);
   free_graphics(&blended);
   free_target(&t);
}

/* ---- draws: CPU and GPU cost per draw, with and without a pipeline switch per draw ---- */

struct draw_args {
   struct graphics *g[2];
   struct target *t;
   uint32_t count;
   bool switch_pipelines;
   double record_ms;
};

static void record_draws(VkCommandBuffer cb, void *arg)
{
   struct draw_args *a = arg;
   const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
   begin_target(cb, a->t, clear);
   double t = now_ms();
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, a->g[0]->pipeline);
   for (uint32_t i = 0; i < a->count; i++) {
      if (a->switch_pipelines)
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, a->g[i & 1]->pipeline);
      float push[4] = {(float)((i * 37) % 190) / 100.0f - 0.95f,
                       (float)((i * 91) % 190) / 100.0f - 0.95f, (float)(i & 255) / 255.0f, 0.5f};
      vkCmdPushConstants(cb, a->g[0]->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
      vkCmdDraw(cb, 3, 1, 0, 0);
   }
   a->record_ms = now_ms() - t;
   vkCmdEndRendering(cb);
}

static void run_draws(void)
{
   struct target t = make_target(512, 512);
   double ms;
   struct graphics opaque = make_graphics(tiny_vert, sizeof(tiny_vert), flat_frag,
                                          sizeof(flat_frag), VK_FORMAT_R8G8B8A8_UNORM, false, 16, &ms);
   struct graphics blended = make_graphics(tiny_vert, sizeof(tiny_vert), flat_frag,
                                           sizeof(flat_frag), VK_FORMAT_R8G8B8A8_UNORM, true, 16, &ms);
   struct draw_args a = {{&opaque, &blended}, &t, 50000, false, 0.0};

   for (int variant = 0; variant < 2; variant++) {
      a.switch_pipelines = variant == 1;
      double gpu[RUNS], cpu[RUNS];
      warm_up(record_draws, &a, "draws (warm-up)");
      for (int i = 0; i < RUNS; i++) {
         gpu[i] = gpu_time(record_draws, &a, a.switch_pipelines ? "draws with pipeline switches"
                                                                : "draws");
         cpu[i] = a.record_ms;
      }
      const char *k_cpu = variant ? "us_record_per_draw_switching" : "us_record_per_draw";
      const char *k_gpu = variant ? "us_gpu_per_draw_switching" : "us_gpu_per_draw";
      jnum(k_cpu, median(cpu, RUNS) * 1e3 / a.count);
      jnum(k_gpu, median(gpu, RUNS) * 1e3 / a.count);
   }
   jint("draws_per_run", a.count);
   record_gpu_clock();

   free_graphics(&opaque);
   free_graphics(&blended);
   free_target(&t);
}

/* ---- compile: pipeline creation time for distinct variants of a heavier shader ---- */

static void run_compile(void)
{
   enum { VARIANTS = 16 };
   const uint32_t salt = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16);
   double ms[VARIANTS];
   for (uint32_t v = 0; v < VARIANTS; v++) {
      const uint32_t values[2] = {v, salt};
      const VkSpecializationMapEntry entries[2] = {
         {0, 0, sizeof(uint32_t)},
         {1, sizeof(uint32_t), sizeof(uint32_t)},
      };
      const VkSpecializationInfo spec = {2, entries, sizeof(values), values};
      fprintf(stderr, "[xdtest] compiling variant %u\n", v);
      struct compute c = make_compute(complex_comp, sizeof(complex_comp), NULL, 1, 0, &spec, &ms[v]);
      free_compute(&c);
   }
   double total = 0;
   for (int i = 0; i < VARIANTS; i++)
      total += ms[i];
   jruns("ms_each", ms, VARIANTS);
   jnum("ms_first", ms[0]);
   jnum("ms_median", median(ms, VARIANTS));
   jnum("ms_total", total);
}

/* ---- storage: compute writes every level of a storage image the way GameNative's BCn layer
 * decodes BC textures (one dispatch per level through a view of that level, between the layer's
 * own barriers, the blocks read from a storage buffer or a texel buffer); the levels are copied
 * out and compared byte for byte, then read through a sampler as the game does ---- */

enum storage_kind { ST_UNORM8, ST_SNORM8, ST_FLOAT16, ST_FLOAT32, ST_UINT32 };

static const struct storage_format {
   const char *name;
   VkFormat format;
   enum storage_kind kind;
   int channels;
} storage_formats[] = {
   {"RGBA8_UNORM", VK_FORMAT_R8G8B8A8_UNORM, ST_UNORM8, 4},
   {"R8_UNORM", VK_FORMAT_R8_UNORM, ST_UNORM8, 1},
   {"RGBA8_SNORM", VK_FORMAT_R8G8B8A8_SNORM, ST_SNORM8, 4},
   {"RG8_SNORM", VK_FORMAT_R8G8_SNORM, ST_SNORM8, 2},
   {"RGBA16F", VK_FORMAT_R16G16B16A16_SFLOAT, ST_FLOAT16, 4},
   {"RGBA32F", VK_FORMAT_R32G32B32A32_SFLOAT, ST_FLOAT32, 4},
   {"R32_UINT", VK_FORMAT_R32_UINT, ST_UINT32, 1},
};

/* The shader's hash (storage_write.comp). */
static uint32_t storage_hash(uint32_t x, uint32_t y, uint32_t level)
{
   uint32_t h = x * 0x9E3779B1u ^ y * 0x85EBCA77u ^ level * 0xC2B2AE3Du;
   h ^= h >> 15;
   h *= 0x2C1B3C6Du;
   h ^= h >> 12;
   return h;
}

static int storage_bytes(const struct storage_format *f)
{
   return f->channels *
          (f->kind == ST_FLOAT32 || f->kind == ST_UINT32 ? 4 : f->kind == ST_FLOAT16 ? 2 : 1);
}

/* b / 256 as a half: exact for 0 <= b < 256. */
static uint16_t half_of_byte(uint32_t b)
{
   if (!b)
      return 0;
   int k = 0;
   while (b >> (k + 1))
      k++;
   return (uint16_t)((k + 7) << 10 | (b - (1u << k)) << (10 - k));
}

/* The bytes the shader's store of texel (x, y) of `level` leaves, given its block's word. */
static void storage_expected(const struct storage_format *f, uint32_t x, uint32_t y, uint32_t level,
                             uint32_t src, uint8_t *out)
{
   const uint32_t h = storage_hash(x, y, level) ^ src;
   for (int c = 0; c < f->channels; c++) {
      const uint32_t b = h >> (8 * c) & 255;
      switch (f->kind) {
      case ST_UNORM8:
         out[c] = (uint8_t)b;
         break;
      case ST_SNORM8:
         out[c] = (uint8_t)(int8_t)((int)(b % 255) - 127);
         break;
      case ST_FLOAT16: {
         const uint16_t half = half_of_byte(b);
         memcpy(out + 2 * c, &half, 2);
         break;
      }
      case ST_FLOAT32: {
         const float v = (float)b / 256.0f;
         memcpy(out + 4 * c, &v, 4);
         break;
      }
      case ST_UINT32:
         memcpy(out, &h, 4);
         break;
      }
   }
}

static VkImage make_storage_image(VkFormat format, uint32_t size, uint32_t levels,
                                  VkDeviceMemory *mem)
{
   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {size, size, 1},
      .mipLevels = levels,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      /* The Wrapper's usage: transfer both ways, sampled and storage. */
      .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkImage image;
   VKCHECK("vkCreateImage", vkCreateImage(dev, &ici, NULL, &image));
   VkMemoryRequirements req;
   vkGetImageMemoryRequirements(dev, image, &req);
   *mem = alloc_memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   VKCHECK("vkBindImageMemory", vkBindImageMemory(dev, image, *mem, 0));
   return image;
}

static VkImageView make_level_view(VkImage image, VkFormat format, uint32_t level,
                                   uint32_t levels)
{
   VkImageViewCreateInfo vci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, levels, 0, 1},
   };
   VkImageView view;
   VKCHECK("vkCreateImageView", vkCreateImageView(dev, &vci, NULL, &view));
   return view;
}

static VkImageMemoryBarrier storage_barrier(VkImage image, VkImageLayout from, VkImageLayout to,
                                            VkAccessFlags src, VkAccessFlags dst, uint32_t level,
                                            uint32_t levels)
{
   return (VkImageMemoryBarrier){
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = src,
      .dstAccessMask = dst,
      .oldLayout = from,
      .newLayout = to,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, levels, 0, 1},
   };
}

static void run_storage(void)
{
   enum { SIZE = 512, LEVELS = 10, SOURCES = 4 };
   static const char *source_names[SOURCES] = {"const", "ssbo", "texel_uint", "texel_rgba8"};
   const size_t nformats = sizeof(storage_formats) / sizeof(storage_formats[0]);
   jbool("write_without_format", has_storage_nofmt);
   if (!has_storage_nofmt)
      finish("skip", "no shaderStorageImageWriteWithoutFormat");

   step("creating pipeline");
   const VkDescriptorSetLayoutBinding bindings[5] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {2, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {3, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 5,
      .pBindings = bindings,
   };
   VkDescriptorSetLayout dsl;
   VKCHECK("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
   const VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int32_t[3])};
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &range,
   };
   VkPipelineLayout layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &layout));
   VkShaderModule cs = make_shader(storage_write_comp, sizeof(storage_write_comp));
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
   VkPipeline pipe;
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe));
   const VkDescriptorPoolSize pool_sizes[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * LEVELS},
                                               {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 2 * LEVELS},
                                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LEVELS}};
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = LEVELS,
      .poolSizeCount = 3,
      .pPoolSizes = pool_sizes,
   };
   VkDescriptorPool dpool;
   VKCHECK("vkCreateDescriptorPool", vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
   VkDescriptorSetLayout layouts[LEVELS];
   for (int l = 0; l < LEVELS; l++)
      layouts[l] = dsl;
   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dpool,
      .descriptorSetCount = LEVELS,
      .pSetLayouts = layouts,
   };
   VkDescriptorSet sets[LEVELS];
   VKCHECK("vkAllocateDescriptorSets", vkAllocateDescriptorSets(dev, &dsai, sets));

   /* The readback through a sampler (storage_read.comp). */
   const VkDescriptorSetLayoutBinding read_bindings[2] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
   };
   dslci.bindingCount = 2;
   dslci.pBindings = read_bindings;
   VkDescriptorSetLayout read_dsl;
   VKCHECK("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout(dev, &dslci, NULL, &read_dsl));
   const VkPushConstantRange read_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int32_t)};
   plci.pSetLayouts = &read_dsl;
   plci.pPushConstantRanges = &read_range;
   VkPipelineLayout read_layout;
   VKCHECK("vkCreatePipelineLayout", vkCreatePipelineLayout(dev, &plci, NULL, &read_layout));
   VkShaderModule read_cs = make_shader(storage_read_comp, sizeof(storage_read_comp));
   cpci.stage.module = read_cs;
   cpci.layout = read_layout;
   VkPipeline read_pipe;
   VKCHECK("vkCreateComputePipelines",
           vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &read_pipe));
   const VkDescriptorPoolSize read_pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
   dpci.maxSets = 1;
   dpci.poolSizeCount = 2;
   dpci.pPoolSizes = read_pool_sizes;
   VkDescriptorPool read_pool;
   VKCHECK("vkCreateDescriptorPool", vkCreateDescriptorPool(dev, &dpci, NULL, &read_pool));
   dsai.descriptorPool = read_pool;
   dsai.descriptorSetCount = 1;
   dsai.pSetLayouts = &read_dsl;
   VkDescriptorSet read_set;
   VKCHECK("vkAllocateDescriptorSets", vkAllocateDescriptorSets(dev, &dsai, &read_set));
   VkSamplerCreateInfo sci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = VK_LOD_CLAMP_NONE,
   };
   VkSampler sampler;
   VKCHECK("vkCreateSampler", vkCreateSampler(dev, &sci, NULL, &sampler));
   const size_t sampled_size = (size_t)LEVELS * 256 * sizeof(float[4]);
   struct buffer sampled = make_buffer(sampled_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

   /* One word per 4x4 block of level 0, read three ways. */
   const uint32_t nblocks = (SIZE / 4) * (SIZE / 4);
   struct buffer blocks = make_buffer(
      nblocks * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
      true);
   uint32_t *words = blocks.map;
   for (uint32_t i = 0; i < nblocks; i++)
      words[i] = storage_hash(i, 7, 11);
   VkBufferView block_views[2];
   for (int i = 0; i < 2; i++) {
      VkBufferViewCreateInfo bvci = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
         .buffer = blocks.buf,
         .format = i ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R32_UINT,
         .range = VK_WHOLE_SIZE,
      };
      VKCHECK("vkCreateBufferView", vkCreateBufferView(dev, &bvci, NULL, &block_views[i]));
   }
   /* The binding a format doesn't use gets a 4x4 image of the other type. */
   VkDeviceMemory dummy_mems[2];
   VkImage dummies[2] = {make_storage_image(VK_FORMAT_R8G8B8A8_UNORM, 4, 1, &dummy_mems[0]),
                         make_storage_image(VK_FORMAT_R32_UINT, 4, 1, &dummy_mems[1])};
   VkImageView dummy_views[2] = {make_level_view(dummies[0], VK_FORMAT_R8G8B8A8_UNORM, 0, 1),
                                 make_level_view(dummies[1], VK_FORMAT_R32_UINT, 0, 1)};
   size_t offsets[LEVELS], max_total = 0;
   for (int l = 0; l < LEVELS; l++) {
      offsets[l] = max_total;
      max_total += (size_t)(SIZE >> l) * (SIZE >> l) * 16;
   }
   struct buffer readback = make_buffer(max_total, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);

   char wrong[800] = "";
   jopen("cases", '[');
   for (size_t k = 0; k < nformats; k++) {
      const struct storage_format *f = &storage_formats[k];
      jopen(NULL, '{');
      jstr("case", f->name);
      VkFormatProperties fp;
      vkGetPhysicalDeviceFormatProperties(pdev, f->format, &fp);
      const VkFormatFeatureFlags need =
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
      jbool("supported", (fp.optimalTilingFeatures & need) == need);
      if ((fp.optimalTilingFeatures & need) != need) {
         jclose();
         continue;
      }
      const int bpt = storage_bytes(f);
      const int kind =
         f->kind == ST_UNORM8 ? 0 : f->kind == ST_SNORM8 ? 1 : f->kind == ST_UINT32 ? 3 : 2;
      VkDeviceMemory mem;
      VkImage image = make_storage_image(f->format, SIZE, LEVELS, &mem);
      VkImageView views[LEVELS];
      for (int l = 0; l < LEVELS; l++) {
         views[l] = make_level_view(image, f->format, l, 1);
         const VkDescriptorImageInfo image_infos[2] = {
            {VK_NULL_HANDLE, kind == 3 ? dummy_views[0] : views[l], VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, kind == 3 ? views[l] : dummy_views[1], VK_IMAGE_LAYOUT_GENERAL},
         };
         const VkDescriptorBufferInfo buffer_info = {blocks.buf, 0, VK_WHOLE_SIZE};
         VkWriteDescriptorSet writes[5];
         for (uint32_t b = 0; b < 5; b++)
            writes[b] = (VkWriteDescriptorSet){
               .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
               .dstSet = sets[l],
               .dstBinding = b,
               .descriptorCount = 1,
               .descriptorType = bindings[b].descriptorType,
               .pImageInfo = b < 2 ? &image_infos[b] : NULL,
               .pBufferInfo = b == 4 ? &buffer_info : NULL,
               .pTexelBufferView = b == 2 || b == 3 ? &block_views[b - 2] : NULL,
            };
         vkUpdateDescriptorSets(dev, 5, writes, 0, NULL);
      }
      VkImageView full_view = make_level_view(image, f->format, 0, LEVELS);
      {
         const VkDescriptorImageInfo tex_info = {sampler, kind == 3 ? dummy_views[0] : full_view,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
         const VkDescriptorBufferInfo out_info = {sampled.buf, 0, VK_WHOLE_SIZE};
         const VkWriteDescriptorSet read_writes[2] = {
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .dstSet = read_set,
             .dstBinding = 0,
             .descriptorCount = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo = &tex_info},
            {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .dstSet = read_set,
             .dstBinding = 1,
             .descriptorCount = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .pBufferInfo = &out_info},
         };
         vkUpdateDescriptorSets(dev, 2, read_writes, 0, NULL);
      }

      int bad[SOURCES] = {0}, bad_sampled = 0;
      uint32_t bad_levels = 0;
      char first[200] = "";
      for (int source = 0; source < SOURCES; source++) {
         memset(readback.map, 0xaa, max_total);
         memset(sampled.map, 0xff, sampled_size); /* NaN */
         VkCommandBuffer cb = begin_cmd();
         /* DXVK's image waits in TRANSFER_DST for the upload the layer replaces. */
         VkImageMemoryBarrier start[3] = {
            storage_barrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                            VK_ACCESS_TRANSFER_WRITE_BIT, 0, LEVELS),
            storage_barrier(dummies[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                            VK_ACCESS_SHADER_WRITE_BIT, 0, 1),
            storage_barrier(dummies[1], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                            VK_ACCESS_SHADER_WRITE_BIT, 0, 1),
         };
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, NULL, 0, NULL, 3, start);
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
         for (int l = 0; l < LEVELS; l++) {
            /* The layer's barriers around one level's decode, with its stage and access masks. */
            VkImageMemoryBarrier to_general =
               storage_barrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, l, 1);
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
                                 &to_general);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &sets[l], 0,
                                    NULL);
            const int32_t pc[3] = {l, source, kind};
            vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
            const uint32_t w = SIZE >> l;
            vkCmdDispatch(cb, (w + 7) / 8, (w + 7) / 8, 1);
            VkImageMemoryBarrier back = storage_barrier(
               image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_ACCESS_SHADER_WRITE_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                  VK_ACCESS_TRANSFER_READ_BIT,
               l, 1);
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, NULL, 0, NULL, 1, &back);
         }
         VkImageMemoryBarrier to_copy = storage_barrier(
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0, LEVELS);
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                              0, NULL, 0, NULL, 1, &to_copy);
         VkBufferImageCopy regions[LEVELS];
         for (int l = 0; l < LEVELS; l++)
            regions[l] = (VkBufferImageCopy){
               .bufferOffset = offsets[l],
               .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1},
               .imageExtent = {SIZE >> l, SIZE >> l, 1},
            };
         vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buf,
                                LEVELS, regions);
         if (kind != 3) {
            /* Then to the layout DXVK samples it in, and 16x16 texels of every level through a
             * sampler. */
            VkImageMemoryBarrier to_sample = storage_barrier(
               image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 0, LEVELS);
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
                                 &to_sample);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, read_pipe);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, read_layout, 0, 1,
                                    &read_set, 0, NULL);
            const int32_t levels_pc = LEVELS;
            vkCmdPushConstants(cb, read_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(levels_pc),
                               &levels_pc);
            vkCmdDispatch(cb, 1, 1, 1);
         }
         VkMemoryBarrier to_host = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
         };
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, NULL, 0, NULL);
         VKCHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
         submit_and_wait(cb, f->name);

         for (int l = 0; l < LEVELS; l++) {
            const uint32_t w = SIZE >> l;
            const uint8_t *level_bytes = (const uint8_t *)readback.map + offsets[l];
            for (uint32_t y = 0; y < w; y++) {
               for (uint32_t x = 0; x < w; x++) {
                  const uint32_t src = source ? words[(y / 4) * ((w + 3) / 4) + x / 4] : 0;
                  uint8_t want[16];
                  storage_expected(f, x, y, l, src, want);
                  const uint8_t *got = level_bytes + ((size_t)y * w + x) * bpt;
                  if (!memcmp(got, want, bpt))
                     continue;
                  bad_levels |= 1u << l;
                  if (!bad[source]++ && !first[0]) {
                     char got_hex[40] = "", want_hex[40] = "";
                     for (int b = 0; b < bpt; b++) {
                        snprintf(got_hex + 2 * b, sizeof(got_hex) - 2 * b, "%02x", got[b]);
                        snprintf(want_hex + 2 * b, sizeof(want_hex) - 2 * b, "%02x", want[b]);
                     }
                     snprintf(first, sizeof(first), "%s level %d (%u,%u): got %s, want %s",
                              source_names[source], l, x, y, got_hex, want_hex);
                  }
               }
            }
         }
         if (kind == 3)
            continue;
         const float(*got)[4] = sampled.map;
         for (int l = 0; l < LEVELS; l++) {
            const uint32_t w = SIZE >> l, n = w < 16 ? w : 16;
            for (uint32_t gy = 0; gy < n; gy++) {
               for (uint32_t gx = 0; gx < n; gx++) {
                  const uint32_t x = gx * w / n + w / n / 2, y = gy * w / n + w / n / 2;
                  const uint32_t src = source ? words[(y / 4) * ((w + 3) / 4) + x / 4] : 0;
                  const uint32_t h = storage_hash(x, y, l) ^ src;
                  const float *g = got[l * 256 + gy * 16 + gx];
                  bool ok = true;
                  for (int c = 0; c < f->channels; c++) {
                     const uint32_t b = h >> (8 * c) & 255;
                     const float want = f->kind == ST_UNORM8   ? (float)b / 255.0f
                                        : f->kind == ST_SNORM8 ? (float)((int)(b % 255) - 127) / 127.0f
                                                               : (float)b / 256.0f;
                     const float tolerance = f->kind == ST_UNORM8   ? 0.5f / 255.0f
                                             : f->kind == ST_SNORM8 ? 0.5f / 127.0f
                                                                    : 1e-6f;
                     ok = ok && fabsf(g[c] - want) <= tolerance;
                  }
                  if (ok)
                     continue;
                  bad_levels |= 1u << l;
                  if (!bad_sampled++ && !first[0])
                     snprintf(first, sizeof(first),
                              "sampled after %s level %d (%u,%u): got (%g, %g, %g, %g)",
                              source_names[source], l, x, y, g[0], g[1], g[2], g[3]);
               }
            }
         }
      }
      jint("wrong_sampled", bad_sampled);
      for (int source = 0; source < SOURCES; source++) {
         char key[40];
         snprintf(key, sizeof(key), "wrong_%s", source_names[source]);
         jint(key, bad[source]);
      }
      jopen("wrong_levels", '[');
      for (int l = 0; l < LEVELS; l++)
         if (bad_levels >> l & 1)
            jint(NULL, l);
      jclose();
      if (first[0])
         jstr("first_wrong", first);
      jclose();
      if (bad[0] + bad[1] + bad[2] + bad[3] + bad_sampled)
         add_problem(wrong, sizeof(wrong), "%s %d/%d/%d/%d/%d", f->name, bad[0], bad[1], bad[2],
                     bad[3], bad_sampled);

      vkDestroyImageView(dev, full_view, NULL);
      for (int l = 0; l < LEVELS; l++)
         vkDestroyImageView(dev, views[l], NULL);
      vkDestroyImage(dev, image, NULL);
      vkFreeMemory(dev, mem, NULL);
   }
   jclose();
   if (wrong[0])
      finish("fail", "wrong texels (const/ssbo/texel_uint/texel_rgba8/sampled): %s", wrong);

   free_buffer(&readback);
   free_buffer(&sampled);
   vkDestroySampler(dev, sampler, NULL);
   vkDestroyDescriptorPool(dev, read_pool, NULL);
   vkDestroyPipeline(dev, read_pipe, NULL);
   vkDestroyShaderModule(dev, read_cs, NULL);
   vkDestroyPipelineLayout(dev, read_layout, NULL);
   vkDestroyDescriptorSetLayout(dev, read_dsl, NULL);
   for (int i = 0; i < 2; i++) {
      vkDestroyImageView(dev, dummy_views[i], NULL);
      vkDestroyImage(dev, dummies[i], NULL);
      vkFreeMemory(dev, dummy_mems[i], NULL);
      vkDestroyBufferView(dev, block_views[i], NULL);
   }
   free_buffer(&blocks);
   vkDestroyDescriptorPool(dev, dpool, NULL);
   vkDestroyPipeline(dev, pipe, NULL);
   vkDestroyShaderModule(dev, cs, NULL);
   vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyDescriptorSetLayout(dev, dsl, NULL);
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
      fprintf(stderr, "usage: xdtest --driver stock|PATH --test NAME [--out DIR] [--tag NAME]\n"
                      "  correctness: info device submit compute render tess scratch scratchcs\n"
                      "               queries xfb xfbmany xfbmanygs textures mips mrt vertex depth\n"
                      "               storage\n"
                      "  performance: alu bandwidth fill draws compile\n");
      return 2;
   }
   if (!tag)
      tag = strcmp(driver, "stock") ? "custom" : "stock";

   static const struct {
      const char *name;
      void (*run)(void);
      bool rendering;
      unsigned needs;
   } tests[] = {
      {"device", NULL, false, 0},           {"submit", run_submit, false, 0},
      {"compute", run_compute, false, 0},   {"render", run_render, true, 0},
      {"tess", run_tess, true, NEED_TESS},  {"scratch", run_scratch, true, 0},
      {"scratchcs", run_scratch_cs, false, 0},
      {"queries", run_queries, true, NEED_QUERIES}, {"xfb", run_xfb, true, NEED_XFB},
      {"xfbmany", run_xfb_many, true, NEED_XFB},
      {"xfbmanygs", run_xfb_many_gs, true, NEED_XFB | NEED_GS},
      {"textures", run_textures, false, NEED_BC}, {"mips", run_mips, false, NEED_BC},
      {"mrt", run_mrt, true, 0},
      {"vertex", run_vertex, true, NEED_VERTEX}, {"depth", run_depth, true, 0},
      {"storage", run_storage, false, NEED_STORAGE},
      {"alu", run_alu, false, 0},           {"bandwidth", run_bandwidth, false, 0},
      {"fill", run_fill, true, 0},          {"draws", run_draws, true, 0},
      {"compile", run_compile, false, 0},
   };
   int which = -1;
   for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
      if (!strcmp(test, tests[i].name))
         which = (int)i;

   jopen(NULL, '{');
   jstr("test", test);
   jstr("driver", tag);
   if (which < 0 && strcmp(test, "info"))
      finish("fail", "unknown test '%s'", test);

   step("loading driver");
   double t = now_ms();
   const char *err = load_driver(driver);
   if (err)
      finish("fail", "loading the driver: %s", err);
   jnum("ms_load", now_ms() - t);

   start_instance();
   if (which < 0) {
      run_info();
      vkDestroyInstance(inst, NULL);
      finish("pass", NULL);
   }

   start_device(tests[which].rendering, tests[which].needs);
   if (tests[which].run)
      tests[which].run();

   step("tearing down");
   if (ts_pool)
      vkDestroyQueryPool(dev, ts_pool, NULL);
   if (pool)
      vkDestroyCommandPool(dev, pool, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   finish("pass", NULL);
   return 0;
}
