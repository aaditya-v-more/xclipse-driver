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
 *                xfbmanygs
 *   performance: alu bandwidth fill draws compile
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

#include "alu_comp.h"
#include "complex_comp.h"
#include "copy_comp.h"
#include "fill_comp.h"
#include "fill_scratch_comp.h"
#include "flat_frag.h"
#include "grid_vert.h"
#include "scratch_vert.h"
#include "screen_vert.h"
#include "tess_tesc.h"
#include "tess_tese.h"
#include "tiny_vert.h"
#include "tri_frag.h"
#include "tri_geom.h"
#include "tri_vert.h"
#include "xfb_many_geom.h"
#include "inputs_frag.h"
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
   X(vkCmdFillBuffer) X(vkCmdBeginQuery) X(vkCmdEndQuery)

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
enum { NEED_QUERIES = 1, NEED_XFB = 2, NEED_GS = 4, NEED_TESS = 8 };

/* Set by start_device for the queries and xfb tests: what the driver has of what they use. */
static bool has_geometry_shader, has_tessellation_shader, has_stats_query, has_pg_query, has_xfb,
   has_xfb_queries;

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
   const char *xfb_ext = VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
   const char *exts[2];
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
                      "               queries xfb xfbmany xfbmanygs\n"
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
