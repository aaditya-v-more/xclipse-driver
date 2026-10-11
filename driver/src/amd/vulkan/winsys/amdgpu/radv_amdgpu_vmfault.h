/*
 * Copyright 2026 Aaditya More
 * SPDX-License-Identifier: MIT
 *
 * Xclipse diagnostic: name the buffer behind each GPU page fault (radv_amdgpu_vmfault.c).
 */

#ifndef RADV_AMDGPU_VMFAULT_H
#define RADV_AMDGPU_VMFAULT_H

#include <stdbool.h>
#include <stdint.h>

struct radeon_winsys;
struct radeon_winsys_bo;
struct radv_amdgpu_winsys;
struct radv_amdgpu_winsys_bo;

enum radv_xclipse_bo_kind {
   RADV_XCLIPSE_BO_CREATE,
   RADV_XCLIPSE_BO_VIRTUAL,
   RADV_XCLIPSE_BO_FROM_PTR,
   RADV_XCLIPSE_BO_FROM_FD,
};

/* Xclipse only; on other chips every call below returns at once. */
void radv_xclipse_vmfault_init(struct radv_amdgpu_winsys *ws);
void radv_xclipse_vmfault_finish(struct radv_amdgpu_winsys *ws);

/* BO lifetime: add after a create or import, remove when it is really destroyed. The IB cache
 * parks BOs instead of destroying them; park(true) under its lock, park(false) on reuse. */
void radv_xclipse_vmfault_bo_add(struct radv_amdgpu_winsys *ws, struct radv_amdgpu_winsys_bo *bo,
                                 enum radv_xclipse_bo_kind kind, uint32_t flags, const void *caller);
void radv_xclipse_vmfault_bo_park(struct radv_amdgpu_winsys *ws, struct radv_amdgpu_winsys_bo *bo, bool parked,
                                  const void *caller);
void radv_xclipse_vmfault_bo_remove(struct radv_amdgpu_winsys *ws, struct radv_amdgpu_winsys_bo *bo);

/* radeon_winsys::buffer_set_owner. */
void radv_xclipse_vmfault_set_owner(struct radeon_winsys *ws, struct radeon_winsys_bo *bo, int32_t object_type,
                                    uint64_t object_handle, bool internal, const void *caller);

/* After every submission: read the kernel's fault records and log them. */
void radv_xclipse_vmfault_poll(struct radv_amdgpu_winsys *ws);

#endif /* RADV_AMDGPU_VMFAULT_H */
