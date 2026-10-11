/*
 * Copyright 2026 Aaditya More
 * SPDX-License-Identifier: MIT
 *
 * Xclipse diagnostic: name the buffer behind each GPU page fault.
 *
 * Samsung's sgpu kernel answers every GPU page fault with a full GPU reset, and keeps the faulting
 * page addresses for the DRM file that faulted: SGPU_INFO_GPU_PAGE_FAULTS (0x101), at most 4
 * pending, consumed by reading. With debug.radv_xclipse_log >= 1 the winsys asks after every
 * submission and logs each fault (logcat tag RADV_VMFAULT) with the BO that holds the page, or
 * the nearest BOs on either side when none does, and any recently freed BO that held it.
 *
 * Every BO is recorded when it is created or imported: VA range, size, flags, domain, priority,
 * time, the caller of the winsys, and (radeon_winsys::buffer_set_owner) the Vulkan object that owns
 * it and the caller of radv_bo_create/from_fd/from_ptr. The last 4096 destroyed BOs stay in a ring.
 * Callers are logged as offsets into this library, for llvm-addr2line on the unstripped build.
 */

#include "radv_amdgpu_vmfault.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_winsys.h"

#include "ac_linux_drm.h"
#include "ac_xclipse_log.h"
#include "util/macros.h"
#include "util/os_time.h"
#include "util/simple_mtx.h"
#include "util/u_math.h"
#include "vk_enum_to_str.h"

#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <android/log.h>
#define VMFAULT_LOG(...) __android_log_print(ANDROID_LOG_WARN, "RADV_VMFAULT", __VA_ARGS__)
#else
#define VMFAULT_LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

#define VMFAULT_PAGE       4096ull
#define VMFAULT_FREED_RING 4096
#define VMFAULT_FREED_SHOW 3

/* include/uapi/drm/sgpu_drm.h in the sgpu kernel. */
#define SGPU_INFO_GPU_PAGE_FAULTS     0x101
#define SGPU_INFO_GPU_PAGE_FAULTS_MAX 4
struct sgpu_query_gpu_page_faults {
   uint32_t num_faults;
   uint64_t fault_addr[SGPU_INFO_GPU_PAGE_FAULTS_MAX];
};
static_assert(sizeof(struct sgpu_query_gpu_page_faults) == 40, "struct drm_sgpu_query_gpu_page_faults");

struct vmfault_rec {
   const struct radv_amdgpu_winsys_bo *bo; /* NULL in the freed ring */
   uint64_t va;                             /* 48 bits, as the kernel reports faults */
   uint64_t va_size;                        /* mapped: whole pages, plus VM_PAD_1PAGE's extra page */
   uint64_t size;
   uint64_t created_ns, freed_ns, parked_ns;
   uint64_t owner_handle;
   const void *ws_caller;    /* the caller of the winsys create or import */
   const void *owner_caller; /* the caller of radv_bo_create/from_fd/from_ptr */
   int32_t owner_type;       /* VkObjectType, or -1 when no owner was given */
   uint32_t flags;           /* enum radeon_bo_flag */
   uint8_t domain;
   uint8_t priority;
   uint8_t kind; /* enum radv_xclipse_bo_kind */
   bool internal;
};

struct radv_xclipse_vmfault {
   simple_mtx_t lock;
   /* radv_amdgpu_winsys_bo::xclipse_rec is the index of a BO's entry plus one. */
   struct vmfault_rec *live;
   uint32_t live_count, live_cap;
   /* Destroyed BOs; the ring holds the last VMFAULT_FREED_RING of freed_count. */
   uint64_t freed_count;
   struct vmfault_rec freed[VMFAULT_FREED_RING];
   uint32_t faults;
   bool announced;
   bool query_failed;
};

static uint64_t
vmfault_va48(uint64_t va)
{
   return va & ((1ull << 48) - 1);
}

void
radv_xclipse_vmfault_init(struct radv_amdgpu_winsys *ws)
{
   if (ws->info.xclipse_model == AC_XCLIPSE_NONE)
      return;

   struct radv_xclipse_vmfault *vf = calloc(1, sizeof(*vf));
   if (!vf)
      return;
   simple_mtx_init(&vf->lock, mtx_plain);
   ws->vmfault = vf;
   ws->base.buffer_set_owner = radv_xclipse_vmfault_set_owner;
}

void
radv_xclipse_vmfault_finish(struct radv_amdgpu_winsys *ws)
{
   struct radv_xclipse_vmfault *vf = ws->vmfault;
   if (!vf)
      return;

   ws->vmfault = NULL;
   ws->base.buffer_set_owner = NULL;
   simple_mtx_destroy(&vf->lock);
   free(vf->live);
   free(vf);
}

/* Callers hold vf->lock. */
static struct vmfault_rec *
vmfault_find(struct radv_xclipse_vmfault *vf, const struct radv_amdgpu_winsys_bo *bo)
{
   if (!bo->xclipse_rec)
      return NULL;
   struct vmfault_rec *r = &vf->live[bo->xclipse_rec - 1];
   assert(r->bo == bo);
   return r;
}

void
radv_xclipse_vmfault_bo_add(struct radv_amdgpu_winsys *ws, struct radv_amdgpu_winsys_bo *bo,
                            enum radv_xclipse_bo_kind kind, uint32_t flags, const void *caller)
{
   struct radv_xclipse_vmfault *vf = ws->vmfault;
   if (!vf)
      return;

   uint64_t va_size = align64(bo->base.size, VMFAULT_PAGE);
   if (flags & RADEON_FLAG_VM_PAD_1PAGE)
      va_size += VMFAULT_PAGE;

   simple_mtx_lock(&vf->lock);
   if (vf->live_count == vf->live_cap) {
      const uint32_t cap = MAX2(1024, vf->live_cap * 2);
      struct vmfault_rec *live = realloc(vf->live, cap * sizeof(*live));
      if (!live) {
         simple_mtx_unlock(&vf->lock);
         return; /* this BO goes untracked */
      }
      vf->live = live;
      vf->live_cap = cap;
   }

   vf->live[vf->live_count++] = (struct vmfault_rec){
      .bo = bo,
      .va = vmfault_va48(bo->base.va),
      .va_size = va_size,
      .size = bo->base.size,
      .created_ns = os_time_get_nano(),
      .ws_caller = caller,
      .owner_type = -1,
      .flags = flags,
      .domain = bo->base.initial_domain,
      .priority = bo->priority,
      .kind = kind,
   };
   bo->xclipse_rec = vf->live_count;
   simple_mtx_unlock(&vf->lock);
}

void
radv_xclipse_vmfault_bo_park(struct radv_amdgpu_winsys *ws, struct radv_amdgpu_winsys_bo *bo, bool parked,
                             const void *caller)
{
   struct radv_xclipse_vmfault *vf = ws->vmfault;
   if (!vf)
      return;

   simple_mtx_lock(&vf->lock);
   struct vmfault_rec *r = vmfault_find(vf, bo);
   if (r) {
      const uint64_t now = os_time_get_nano();
      if (parked) {
         r->parked_ns = now;
      } else {
         /* Handed out again: a new owner, as if created now. */
         r->parked_ns = 0;
         r->created_ns = now;
         r->ws_caller = caller;
         r->owner_type = -1;
         r->owner_handle = 0;
         r->owner_caller = NULL;
         r->internal = false;
         r->priority = bo->priority;
      }
   }
   simple_mtx_unlock(&vf->lock);
}

void
radv_xclipse_vmfault_bo_remove(struct radv_amdgpu_winsys *ws, struct radv_amdgpu_winsys_bo *bo)
{
   struct radv_xclipse_vmfault *vf = ws->vmfault;
   if (!vf)
      return;

   simple_mtx_lock(&vf->lock);
   struct vmfault_rec *r = vmfault_find(vf, bo);
   if (r) {
      struct vmfault_rec *f = &vf->freed[vf->freed_count++ % VMFAULT_FREED_RING];
      *f = *r;
      f->bo = NULL;
      f->freed_ns = os_time_get_nano();

      const uint32_t i = bo->xclipse_rec - 1;
      if (i != --vf->live_count) {
         vf->live[i] = vf->live[vf->live_count];
         ((struct radv_amdgpu_winsys_bo *)vf->live[i].bo)->xclipse_rec = i + 1;
      }
      bo->xclipse_rec = 0;
   }
   simple_mtx_unlock(&vf->lock);
}

void
radv_xclipse_vmfault_set_owner(struct radeon_winsys *_ws, struct radeon_winsys_bo *_bo, int32_t object_type,
                               uint64_t object_handle, bool internal, const void *caller)
{
   struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
   struct radv_xclipse_vmfault *vf = ws->vmfault;
   if (!vf)
      return;

   simple_mtx_lock(&vf->lock);
   struct vmfault_rec *r = vmfault_find(vf, radv_amdgpu_winsys_bo(_bo));
   if (r) {
      r->owner_type = object_type;
      r->owner_handle = object_handle;
      r->owner_caller = caller;
      r->internal = internal;
   }
   simple_mtx_unlock(&vf->lock);
}

/* A code address as an offset into the library that holds it. */
static size_t
vmfault_lib_offset(const void *addr)
{
   Dl_info info;
   uintptr_t offset = (uintptr_t)addr;
   if (addr && dladdr(addr, &info) && info.dli_fbase)
      offset -= (uintptr_t)info.dli_fbase;
   return offset;
}

static void
vmfault_describe(const struct vmfault_rec *r, uint64_t now, char *buf, size_t len)
{
   static const char *const kind_name[] = {"create", "virtual", "from_ptr", "from_fd"};

   const char *owner = "-";
   if (r->owner_type >= 0) {
      owner = vk_ObjectType_to_str((VkObjectType)r->owner_type);
      if (!strncmp(owner, "VK_OBJECT_TYPE_", 15))
         owner += 15;
   }

   /* Live BOs give their age, freed ones how long they lived. */
   int n = snprintf(buf, len,
                    "va 0x%012" PRIx64 "-0x%012" PRIx64 " size 0x%" PRIx64 " %s flags 0x%x dom %u prio %u owner %s%s"
                    " 0x%" PRIx64 " by +0x%zx winsys +0x%zx %s %.3f s",
                    r->va, r->va + r->va_size, r->size, r->kind < ARRAY_SIZE(kind_name) ? kind_name[r->kind] : "?",
                    r->flags, r->domain, r->priority, owner, r->internal ? " (internal)" : "", r->owner_handle,
                    vmfault_lib_offset(r->owner_caller), vmfault_lib_offset(r->ws_caller),
                    r->freed_ns ? "lived" : "age", ((r->freed_ns ? r->freed_ns : now) - r->created_ns) / 1e9);
   if (r->parked_ns && n > 0 && (size_t)n < len)
      snprintf(buf + n, len - n, ", parked in the IB cache %.3f s before", (now - r->parked_ns) / 1e9);
}

/* Caller holds vf->lock. */
static void
vmfault_report(struct radv_xclipse_vmfault *vf, uint64_t fault_addr)
{
   const uint64_t now = os_time_get_nano();
   const uint64_t page = vmfault_va48(fault_addr) & ~(VMFAULT_PAGE - 1);
   const uint64_t page_end = page + VMFAULT_PAGE;
   const uint32_t id = ++vf->faults;
   char desc[512];

   VMFAULT_LOG("[VMFAULT] #%u page 0x%012" PRIx64 " t=%.6f live=%u freed=%" PRIu64, id, page, now / 1e9,
               vf->live_count, vf->freed_count);

   const struct vmfault_rec *below = NULL, *above = NULL;
   unsigned hits = 0;
   for (uint32_t i = 0; i < vf->live_count; i++) {
      const struct vmfault_rec *r = &vf->live[i];
      const uint64_t end = r->va + r->va_size;
      if (r->va < page_end && end > page) {
         vmfault_describe(r, now, desc, sizeof(desc));
         VMFAULT_LOG("[VMFAULT] #%u in %s, at +0x%" PRIx64, id, desc, page > r->va ? page - r->va : 0);
         hits++;
      } else if (end <= page) {
         if (!below || end > below->va + below->va_size)
            below = r;
      } else if (!above || r->va < above->va) {
         above = r;
      }
   }

   if (!hits) {
      if (below) {
         vmfault_describe(below, now, desc, sizeof(desc));
         VMFAULT_LOG("[VMFAULT] #%u no live BO; 0x%" PRIx64 " past the end of %s", id,
                     page - (below->va + below->va_size), desc);
      }
      if (above) {
         vmfault_describe(above, now, desc, sizeof(desc));
         VMFAULT_LOG("[VMFAULT] #%u no live BO; 0x%" PRIx64 " before %s", id, above->va - page_end, desc);
      }
      if (!below && !above)
         VMFAULT_LOG("[VMFAULT] #%u no live BO at all", id);
   }

   /* Freed BOs that held the page, newest first. */
   const uint64_t kept = MIN2(vf->freed_count, VMFAULT_FREED_RING);
   unsigned freed_hits = 0;
   for (uint64_t k = 0; k < kept; k++) {
      const struct vmfault_rec *r = &vf->freed[(vf->freed_count - 1 - k) % VMFAULT_FREED_RING];
      if (r->va >= page_end || r->va + r->va_size <= page)
         continue;
      if (++freed_hits <= VMFAULT_FREED_SHOW) {
         vmfault_describe(r, now, desc, sizeof(desc));
         VMFAULT_LOG("[VMFAULT] #%u freed %.3f s before: %s, at +0x%" PRIx64, id, (now - r->freed_ns) / 1e9, desc,
                     page > r->va ? page - r->va : 0);
      }
   }
   if (freed_hits > VMFAULT_FREED_SHOW)
      VMFAULT_LOG("[VMFAULT] #%u and %u older freed BOs", id, freed_hits - VMFAULT_FREED_SHOW);
}

void
radv_xclipse_vmfault_poll(struct radv_amdgpu_winsys *ws)
{
   struct radv_xclipse_vmfault *vf = ws->vmfault;
   if (!vf || vf->query_failed || ac_xclipse_log_level() < 1)
      return;

   simple_mtx_lock(&vf->lock);
   if (!vf->announced) {
      vf->announced = true;
      VMFAULT_LOG("[VMFAULT] watching for GPU page faults: %u live BOs, %" PRIu64 " freed", vf->live_count,
                  vf->freed_count);
   }

   /* The kernel holds at most 4 records; after a full answer, ask again. */
   for (unsigned round = 0; round < 4; round++) {
      struct sgpu_query_gpu_page_faults q;
      memset(&q, 0, sizeof(q));
      const int r = ac_drm_query_info(ws->dev, SGPU_INFO_GPU_PAGE_FAULTS, sizeof(q), &q);
      if (r) {
         /* A kernel without the query: stop asking. */
         vf->query_failed = true;
         VMFAULT_LOG("[VMFAULT] page-fault query failed (%d); not asking again", r);
         break;
      }

      const uint32_t n = MIN2(q.num_faults, SGPU_INFO_GPU_PAGE_FAULTS_MAX);
      for (uint32_t i = 0; i < n; i++)
         vmfault_report(vf, q.fault_addr[i]);
      if (n < SGPU_INFO_GPU_PAGE_FAULTS_MAX)
         break;
   }
   simple_mtx_unlock(&vf->lock);
}
