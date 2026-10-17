/*
 * Copyright © 2026 xclipse-driver contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "util/u_dynarray.h"
#include "nir.h"
#include "nir_builder.h"
#include "radv_nir.h"

/**
 * The Xclipse 940 texture unit can't linear-filter 32-bit float or 16-bit UNORM/SNORM formats: a
 * linear sampler returns the nearest texel (xdtest `textures`). Samsung's driver withholds
 * SAMPLED_IMAGE_FILTER_LINEAR for them, but DXVK and apps rely on it, so RADV keeps advertising it
 * and filters in the shader instead.
 *
 * Every float 2D sample checks the image descriptor's format and the sampler's filter at run time
 * (one uniform branch when the descriptors are uniform, constant-folded for immutable samplers).
 * For an affected format under a linear filter, it gathers the 2x2 footprint of each read component
 * at an explicit level (gather4 applies the address modes and border color) and blends it, at one
 * level or two for a linear mip filter. Everything else keeps the hardware sample. Depth
 * comparisons (PCF) filter correctly and are left alone.
 */

/* GFX11 image formats (desc_gfx_level on the 940) the texture unit only point-samples. */
static const unsigned unfilterable_formats[] = {
   7,  /* 16_UNORM */
   8,  /* 16_SNORM */
   22, /* 32_FLOAT */
   23, /* 16_16_UNORM */
   24, /* 16_16_SNORM */
   50, /* 32_32_FLOAT */
   51, /* 16_16_16_16_UNORM */
   52, /* 16_16_16_16_SNORM */
   60, /* 32_32_32_FLOAT */
   63, /* 32_32_32_32_FLOAT */
   75, /* 32_FLOAT_CLAMP (upgraded depth) */
   81, /* X24_8_32_FLOAT (depth of D32_SFLOAT_S8_UINT) */
};

static nir_tex_instr *
build_tex(nir_builder *b, nir_tex_instr *orig, nir_texop op, nir_def *coord, nir_def *lod, nir_alu_type dest_type,
          unsigned component, bool with_offset)
{
   nir_tex_src srcs[8];
   unsigned num_srcs = 0;

   for (unsigned i = 0; i < orig->num_srcs; i++) {
      switch (orig->src[i].src_type) {
      case nir_tex_src_texture_deref:
      case nir_tex_src_texture_offset:
      case nir_tex_src_texture_handle:
      case nir_tex_src_texture_heap_offset:
         srcs[num_srcs++] = nir_tex_src_for_ssa(orig->src[i].src_type, orig->src[i].src.ssa);
         break;
      case nir_tex_src_sampler_deref:
      case nir_tex_src_sampler_offset:
      case nir_tex_src_sampler_handle:
      case nir_tex_src_sampler_heap_offset:
         if (op != nir_texop_txs && op != nir_texop_query_levels && op != nir_texop_descriptor_amd)
            srcs[num_srcs++] = nir_tex_src_for_ssa(orig->src[i].src_type, orig->src[i].src.ssa);
         break;
      case nir_tex_src_offset:
         if (with_offset)
            srcs[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_offset, orig->src[i].src.ssa);
         break;
      default:
         break;
      }
   }
   if (coord)
      srcs[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
   if (lod)
      srcs[num_srcs++] = nir_tex_src_for_ssa(nir_tex_src_lod, lod);

   nir_tex_instr *tex = nir_tex_instr_create(b->shader, num_srcs);
   tex->op = op;
   tex->sampler_dim = orig->sampler_dim;
   tex->is_array = orig->is_array;
   tex->coord_components = coord ? orig->coord_components : 0;
   tex->dest_type = dest_type;
   tex->component = component;
   tex->texture_index = orig->texture_index;
   tex->sampler_index = orig->sampler_index;
   tex->texture_non_uniform = orig->texture_non_uniform;
   tex->sampler_non_uniform = orig->sampler_non_uniform;
   tex->can_speculate = orig->can_speculate;
   memcpy(tex->src, srcs, num_srcs * sizeof(srcs[0]));
   nir_def_init(&tex->instr, &tex->def, nir_tex_instr_dest_size(tex), 32);
   nir_builder_instr_insert(b, &tex->instr);
   return tex;
}

/* x + (y - x) * t, built from ops ACO selects (this runs after flrp and fdot lowering). */
static nir_def *
lerp(nir_builder *b, nir_def *x, nir_def *y, nir_def *t)
{
   if (t->num_components != x->num_components)
      t = nir_replicate(b, t, x->num_components);
   return nir_ffma(b, nir_fsub(b, y, x), t, x);
}

static nir_def *
length2(nir_builder *b, nir_def *v)
{
   return nir_ffma(b, nir_channel(b, v, 0), nir_channel(b, v, 0),
                   nir_fmul(b, nir_channel(b, v, 1), nir_channel(b, v, 1)));
}

/* Bilinear filter of the read components at an integer level, from gather4. */
static nir_def *
bilinear(nir_builder *b, nir_tex_instr *orig, nir_def *coord, nir_def *level, nir_component_mask_t read)
{
   nir_def *size = nir_i2f32(b, nir_channels(b, &build_tex(b, orig, nir_texop_txs, NULL, level, nir_type_int32, 0, false)->def, 0x3));
   nir_def *uv = nir_fadd_imm(b, nir_fmul(b, nir_channels(b, coord, 0x3), size), -0.5);
   nir_def *base = nir_ffloor(b, uv);
   nir_def *fx = nir_fsub(b, nir_channel(b, uv, 0), nir_channel(b, base, 0));
   nir_def *fy = nir_fsub(b, nir_channel(b, uv, 1), nir_channel(b, base, 1));
   nir_def *flod = nir_i2f32(b, level);

   /* Gather at the corner the four texels share, where the footprint the hardware picks can't
    * round to a neighbour: at a texel center it rounds its own way, which needn't match floor(). */
   nir_def *corner = nir_fdiv(b, nir_fadd_imm(b, base, 1.0), size);
   nir_def *gcoord[3] = {nir_channel(b, corner, 0), nir_channel(b, corner, 1), NULL};
   if (coord->num_components > 2)
      gcoord[2] = nir_channel(b, coord, 2);
   coord = nir_vec(b, gcoord, coord->num_components);

   nir_def *comps[4];
   for (unsigned c = 0; c < 4; c++) {
      if (!(read & BITFIELD_BIT(c))) {
         comps[c] = nir_imm_float(b, 0.0f);
         continue;
      }
      /* gather4 returns (u0,v1), (u1,v1), (u1,v0), (u0,v0). */
      nir_def *g = &build_tex(b, orig, nir_texop_tg4, coord, flod, nir_type_float32, c, true)->def;
      nir_def *top = lerp(b, nir_channel(b, g, 3), nir_channel(b, g, 2), fx);
      nir_def *bottom = lerp(b, nir_channel(b, g, 0), nir_channel(b, g, 1), fx);
      comps[c] = lerp(b, top, bottom, fy);
   }
   return nir_vec(b, comps, 4);
}

static void
lower_tex(nir_builder *b, nir_tex_instr *tex)
{
   /* The hardware sample stays where it is, unconditional, so ACO can still overlap it with the
    * shader's other fetches; only the rare emulated path branches. */
   b->cursor = nir_after_instr(&tex->instr);

   nir_def *coord = nir_get_tex_src(tex, nir_tex_src_coord);
   nir_def *src_lod = nir_get_tex_src(tex, nir_tex_src_lod);
   nir_def *bias = nir_get_tex_src(tex, nir_tex_src_bias);
   nir_def *ddx = nir_get_tex_src(tex, nir_tex_src_ddx);
   nir_def *ddy = nir_get_tex_src(tex, nir_tex_src_ddy);
   const nir_component_mask_t read = nir_def_components_read(&tex->def);

   nir_def *image = &build_tex(b, tex, nir_texop_descriptor_amd, NULL, NULL, nir_type_int32, 0, false)->def;
   nir_def *sampler = &build_tex(b, tex, nir_texop_sampler_descriptor_amd, NULL, NULL, nir_type_int32, 0, false)->def;

   /* FORMAT is image word1 bits 20-27 in the GFX11 layout; XY_MAG/MIN_FILTER are sampler word2
    * bits 20-21 and 22-23, where bit 0 of each means bilinear (also for anisotropic). */
   nir_def *format = nir_ubitfield_extract_imm(b, nir_channel(b, image, 1), 20, 8);
   uint32_t masks[3] = {0};
   for (unsigned i = 0; i < ARRAY_SIZE(unfilterable_formats); i++)
      masks[unfilterable_formats[i] / 32] |= BITFIELD_BIT(unfilterable_formats[i] % 32);
   nir_def *word = nir_ushr_imm(b, format, 5);
   nir_def *mask = nir_bcsel(b, nir_ieq_imm(b, word, 0), nir_imm_int(b, masks[0]),
                             nir_bcsel(b, nir_ieq_imm(b, word, 1), nir_imm_int(b, masks[1]),
                                       nir_bcsel(b, nir_ieq_imm(b, word, 2), nir_imm_int(b, masks[2]),
                                                 nir_imm_int(b, 0))));
   nir_def *affected = nir_ine_imm(b, nir_iand_imm(b, nir_ushr(b, mask, format), 1), 0);
   nir_def *word2 = nir_channel(b, sampler, 2);
   nir_def *linear = nir_ine_imm(b, nir_iand_imm(b, word2, BITFIELD_BIT(20) | BITFIELD_BIT(22)), 0);

   nir_if *nif = nir_push_if(b, nir_iand(b, affected, linear));
   nir_def *emulated;
   {
      nir_def *levels = &build_tex(b, tex, nir_texop_query_levels, NULL, NULL, nir_type_int32, 0, false)->def;
      nir_def *word1 = nir_channel(b, sampler, 1);
      nir_def *lod;
      bool clamped = false;

      if (src_lod) {
         lod = src_lod;
      } else if (ddx) {
         nir_def *size0 = nir_i2f32(b, nir_channels(b, &build_tex(b, tex, nir_texop_txs, NULL, nir_imm_int(b, 0),
                                                                   nir_type_int32, 0, false)->def, 0x3));
         nir_def *dx = nir_fmul(b, nir_channels(b, ddx, 0x3), size0);
         nir_def *dy = nir_fmul(b, nir_channels(b, ddy, 0x3), size0);
         nir_def *rho2 = nir_fmax(b, length2(b, dx), length2(b, dy));
         lod = nir_fmul_imm(b, nir_flog2(b, rho2), 0.5);
      } else if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         /* The hardware query includes the sampler's bias and LOD clamp. */
         lod = nir_channel(b, &build_tex(b, tex, nir_texop_lod, coord, NULL, nir_type_float32, 0, false)->def, 0);
         clamped = true;
      } else {
         lod = nir_imm_float(b, 0.0f);
      }
      if (bias)
         lod = nir_fadd(b, lod, bias);
      if (!clamped) {
         /* LOD_BIAS is word2 bits 0-13 (signed, 8 fraction bits); MIN_LOD and MAX_LOD are word1 bits
          * 0-11 and 12-23 (unsigned, 8 fraction bits). */
         lod = nir_fadd(b, lod, nir_fmul_imm(b, nir_i2f32(b, nir_ibitfield_extract_imm(b, word2, 0, 14)), 1.0 / 256));
         nir_def *min_lod = nir_fmul_imm(b, nir_u2f32(b, nir_ubitfield_extract_imm(b, word1, 0, 12)), 1.0 / 256);
         nir_def *max_lod = nir_fmul_imm(b, nir_u2f32(b, nir_ubitfield_extract_imm(b, word1, 12, 12)), 1.0 / 256);
         lod = nir_fmin(b, nir_fmax(b, lod, min_lod), max_lod);
      }
      nir_def *last = nir_iadd_imm(b, levels, -1);
      lod = nir_fmin(b, nir_fmax(b, lod, nir_imm_float(b, 0.0f)), nir_i2f32(b, last));

      /* MIP_FILTER is word2 bits 26-27: 0 none (base level), 1 point, 2 linear. */
      nir_def *mip_filter = nir_ubitfield_extract_imm(b, word2, 26, 2);
      nir_def *mip_linear = nir_ieq_imm(b, mip_filter, 2);
      nir_def *base = nir_bcsel(b, mip_linear, nir_ffloor(b, lod), nir_ffloor(b, nir_fadd_imm(b, lod, 0.5)));
      base = nir_bcsel(b, nir_ieq_imm(b, mip_filter, 0), nir_imm_float(b, 0.0f), base);
      nir_def *frac = nir_bcsel(b, mip_linear, nir_fsub(b, lod, base), nir_imm_float(b, 0.0f));
      nir_def *level0 = nir_f2i32(b, base);

      nir_def *r0 = bilinear(b, tex, coord, level0, read);
      nir_if *nif1 = nir_push_if(b, nir_fgt_imm(b, frac, 0.0f));
      nir_def *r1 = bilinear(b, tex, coord, nir_imin(b, nir_iadd_imm(b, level0, 1), last), read);
      nir_def *blended = lerp(b, r0, r1, frac);
      nir_pop_if(b, nif1);
      emulated = nir_if_phi(b, blended, r0);
      emulated = nir_trim_vector(b, emulated, tex->def.num_components);
   }
   nir_pop_if(b, nif);

   /* The phi is in another block, so rewrite every use but its own by hand. */
   nir_def *result = nir_if_phi(b, emulated, &tex->def);
   nir_foreach_use_including_if_safe (src, &tex->def) {
      if (nir_src_is_if(src) || nir_src_use_instr(src) != nir_def_instr(result))
         nir_src_rewrite(src, result);
   }
}

static bool
can_lower(const nir_tex_instr *tex)
{
   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
      break;
   default:
      return false;
   }
   if (tex->sampler_dim != GLSL_SAMPLER_DIM_2D || tex->is_shadow || tex->is_sparse ||
       nir_alu_type_get_base_type(tex->dest_type) != nir_type_float || tex->def.bit_size != 32 ||
       tex->def.num_components > 4)
      return false;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_projector:
      case nir_tex_src_comparator:
      case nir_tex_src_min_lod:
      case nir_tex_src_plane:
         return false;
      default:
         break;
      }
   }
   return nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref) >= 0 ||
          nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle) >= 0 ||
          nir_tex_instr_src_index(tex, nir_tex_src_sampler_heap_offset) >= 0;
}

bool
radv_nir_lower_xclipse_filter(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl (impl, shader) {
      struct util_dynarray texs;
      util_dynarray_init(&texs, NULL);

      nir_foreach_block (block, impl) {
         nir_foreach_instr (instr, block) {
            if (instr->type == nir_instr_type_tex && can_lower(nir_instr_as_tex(instr)))
               util_dynarray_append(&texs, nir_instr_as_tex(instr));
         }
      }

      nir_builder b = nir_builder_create(impl);
      util_dynarray_foreach (&texs, nir_tex_instr *, tex)
         lower_tex(&b, *tex);

      if (util_dynarray_num_elements(&texs, nir_tex_instr *)) {
         progress = true;
         nir_progress(true, impl, nir_metadata_none);
      } else {
         nir_no_progress(impl);
      }
      util_dynarray_fini(&texs);
   }

   if (progress)
      shader->info.uses_resource_info_query = true;
   return progress;
}
