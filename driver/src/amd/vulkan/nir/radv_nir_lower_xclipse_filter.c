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
 * For an affected format under a linear filter, it blends the texels around the coordinate in a
 * loop: the hardware point-samples these formats exactly at a texel center, so each texel is one
 * explicit-LOD sample there (which applies the address modes, the border color and the format's
 * missing channels), four per level, at one level or two for a linear mip filter. A loop keeps
 * the emulation to one sample instruction and a little ALU per sample site: unrolled, its code and
 * registers cost every fragment shader occupancy whether or not it ever runs. Everything else
 * keeps the hardware sample. Depth comparisons (PCF) filter correctly and are left alone.
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

static nir_def *
length2(nir_builder *b, nir_def *v)
{
   return nir_ffma(b, nir_channel(b, v, 0), nir_channel(b, v, 0),
                   nir_fmul(b, nir_channel(b, v, 1), nir_channel(b, v, 1)));
}

/* Adds texel i (bit 0: right, bit 1: lower, bit 2: next level) of the 2x2 footprints at level0 and
 * level0 + 1 around coord to acc, weighted for bilinear filtering and the mip blend `frac`. */
static nir_def *
add_texel(nir_builder *b, nir_tex_instr *orig, nir_def *coord, nir_def *i, nir_def *level0, nir_def *frac,
          nir_def *lod_bias, nir_def *acc)
{
   nir_def *second = nir_ine_imm(b, nir_iand_imm(b, i, 4), 0);
   nir_def *level = nir_iadd(b, level0, nir_b2i32(b, second));
   nir_def *size = nir_i2f32(b, nir_channels(b, &build_tex(b, orig, nir_texop_txs, NULL, level, nir_type_int32, 0, false)->def, 0x3));
   nir_def *uv = nir_fadd_imm(b, nir_fmul(b, nir_channels(b, coord, 0x3), size), -0.5);
   nir_def *base = nir_ffloor(b, uv);
   nir_def *f = nir_fsub(b, uv, base);
   nir_def *pick = nir_vec2(b, nir_ine_imm(b, nir_iand_imm(b, i, 1), 0), nir_ine_imm(b, nir_iand_imm(b, i, 2), 0));

   /* The texel's center: unambiguous, so the hardware's point sample returns exactly that texel. */
   nir_def *center = nir_fdiv(b, nir_fadd(b, base, nir_bcsel(b, pick, nir_imm_vec2(b, 1.5, 1.5), nir_imm_vec2(b, 0.5, 0.5))), size);
   nir_def *tcoord[3] = {nir_channel(b, center, 0), nir_channel(b, center, 1), NULL};
   if (coord->num_components > 2)
      tcoord[2] = nir_channel(b, coord, 2);
   /* The sampler's LOD bias applies to explicit LODs too, so take it back off. */
   nir_def *lod = nir_fsub(b, nir_i2f32(b, level), lod_bias);
   nir_def *texel = &build_tex(b, orig, nir_texop_txl, nir_vec(b, tcoord, coord->num_components), lod,
                               nir_type_float32, 0, true)->def;

   nir_def *w = nir_bcsel(b, pick, f, nir_fsub_imm(b, 1.0, f));
   nir_def *weight = nir_fmul(b, nir_fmul(b, nir_channel(b, w, 0), nir_channel(b, w, 1)),
                              nir_bcsel(b, second, frac, nir_fsub_imm(b, 1.0, frac)));
   return nir_ffma(b, texel, nir_replicate(b, weight, 4), acc);
}

/* The branch goes right before the sample's first use in its block (or at the block's end), so
 * samples that follow it stay in the same block and ACO can still clause them together. */
static nir_cursor
emulation_point(nir_tex_instr *tex)
{
   for (nir_instr *instr = nir_instr_next(&tex->instr); instr; instr = nir_instr_next(instr)) {
      if (instr->type == nir_instr_type_jump)
         break;
      nir_foreach_use (src, &tex->def) {
         if (nir_src_use_instr(src) == instr)
            return nir_before_instr(instr);
      }
   }
   return nir_after_block_before_jump(tex->instr.block);
}

static void
lower_tex(nir_builder *b, nir_tex_instr *tex, unsigned mode)
{
   /* The hardware sample stays where it is, unconditional, so ACO can still overlap it with the
    * shader's other fetches; only the rare emulated path branches. */
   b->cursor = emulation_point(tex);

   /* Only the uses that exist now move to the result; the emulation adds its own. */
   struct util_dynarray uses;
   util_dynarray_init(&uses, NULL);
   nir_foreach_use_including_if (src, &tex->def)
      util_dynarray_append(&uses, src);

   nir_def *coord = nir_get_tex_src(tex, nir_tex_src_coord);
   nir_def *src_lod = nir_get_tex_src(tex, nir_tex_src_lod);
   nir_def *bias = nir_get_tex_src(tex, nir_tex_src_bias);
   nir_def *ddx = nir_get_tex_src(tex, nir_tex_src_ddx);
   nir_def *ddy = nir_get_tex_src(tex, nir_tex_src_ddy);

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
   /* FORCE_UNNORMALIZED is sampler word0 bit 15; the emulation assumes normalized coordinates. */
   nir_def *normalized = nir_ieq_imm(b, nir_iand_imm(b, nir_channel(b, sampler, 0), BITFIELD_BIT(15)), 0);

   nir_if *nif = nir_push_if(b, nir_iand(b, nir_iand(b, affected, linear), normalized));
   nir_def *emulated;
   {
      nir_def *levels = &build_tex(b, tex, nir_texop_query_levels, NULL, NULL, nir_type_int32, 0, false)->def;
      nir_def *word1 = nir_channel(b, sampler, 1);
      nir_def *lod;
      nir_def *lod_bias = nir_fmul_imm(b, nir_i2f32(b, nir_ibitfield_extract_imm(b, word2, 0, 14)), 1.0 / 256);

      if (src_lod) {
         lod = src_lod;
      } else if (ddx || b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         /* Fragment shaders take the derivatives of the coordinate; that is ALU, where the LOD
          * query would cost another texture instruction. */
         if (!ddx) {
            ddx = nir_ddx(b, nir_channels(b, coord, 0x3));
            ddy = nir_ddy(b, nir_channels(b, coord, 0x3));
         }
         nir_def *size0 = nir_i2f32(b, nir_channels(b, &build_tex(b, tex, nir_texop_txs, NULL, nir_imm_int(b, 0),
                                                                   nir_type_int32, 0, false)->def, 0x3));
         nir_def *dx = nir_fmul(b, nir_channels(b, ddx, 0x3), size0);
         nir_def *dy = nir_fmul(b, nir_channels(b, ddy, 0x3), size0);
         nir_def *rho2 = nir_fmax(b, length2(b, dx), length2(b, dy));
         lod = nir_fmul_imm(b, nir_flog2(b, rho2), 0.5);
      } else {
         lod = nir_imm_float(b, 0.0f);
      }
      if (bias)
         lod = nir_fadd(b, lod, bias);
      {
         /* LOD_BIAS is word2 bits 0-13 (signed, 8 fraction bits); MIN_LOD and MAX_LOD are word1 bits
          * 0-11 and 12-23 (unsigned, 8 fraction bits). */
         lod = nir_fadd(b, lod, lod_bias);
         nir_def *min_lod = nir_fmul_imm(b, nir_u2f32(b, nir_ubitfield_extract_imm(b, word1, 0, 12)), 1.0 / 256);
         nir_def *max_lod = nir_fmul_imm(b, nir_u2f32(b, nir_ubitfield_extract_imm(b, word1, 12, 12)), 1.0 / 256);
         lod = nir_fmin(b, nir_fmax(b, lod, min_lod), max_lod);
      }
      /* The magnification filter applies at LOD <= 0, the minification filter above it. */
      nir_def *xy_linear = nir_ine_imm(b, nir_iand(b, word2, nir_bcsel(b, nir_fle_imm(b, lod, 0.0f),
                                                                       nir_imm_int(b, BITFIELD_BIT(20)),
                                                                       nir_imm_int(b, BITFIELD_BIT(22)))), 0);
      nir_def *last = nir_iadd_imm(b, levels, -1);
      lod = nir_fmin(b, nir_fmax(b, lod, nir_imm_float(b, 0.0f)), nir_i2f32(b, last));

      /* MIP_FILTER is word2 bits 26-27: 0 none (base level), 1 point, 2 linear. */
      nir_def *mip_filter = nir_ubitfield_extract_imm(b, word2, 26, 2);
      nir_def *mip_linear = nir_ieq_imm(b, mip_filter, 2);
      nir_def *base = nir_bcsel(b, mip_linear, nir_ffloor(b, lod), nir_ffloor(b, nir_fadd_imm(b, lod, 0.5)));
      base = nir_bcsel(b, nir_ieq_imm(b, mip_filter, 0), nir_imm_float(b, 0.0f), base);
      nir_def *frac = nir_bcsel(b, mip_linear, nir_fsub(b, lod, base), nir_imm_float(b, 0.0f));
      nir_def *level0 = nir_f2i32(b, base);

      /* Mode 2 skips the second level. */
      if (mode == 2)
         frac = nir_imm_float(b, 0.0f);
      nir_def *count = nir_bcsel(b, nir_fgt_imm(b, frac, 0.0f), nir_imm_int(b, 8), nir_imm_int(b, 4));

      nir_variable *acc_var = nir_local_variable_create(b->impl, glsl_vec4_type(), "xclipse_filter_acc");
      nir_variable *i_var = nir_local_variable_create(b->impl, glsl_uint_type(), "xclipse_filter_i");
      nir_store_var(b, acc_var, nir_imm_zero(b, 4, 32), 0xf);
      nir_store_var(b, i_var, nir_imm_int(b, 0), 0x1);
      nir_loop *loop = nir_push_loop(b);
      {
         nir_def *i = nir_load_var(b, i_var);
         nir_break_if(b, nir_uge(b, i, count));
         nir_store_var(b, acc_var, add_texel(b, tex, coord, i, level0, frac, lod_bias, nir_load_var(b, acc_var)), 0xf);
         nir_store_var(b, i_var, nir_iadd_imm(b, i, 1), 0x1);
      }
      nir_pop_loop(b, loop);
      emulated = nir_trim_vector(b, nir_load_var(b, acc_var), tex->def.num_components);

      /* A point filter at this LOD keeps the hardware sample, which point-samples correctly. */
      emulated = nir_bcsel(b, xy_linear, emulated, &tex->def);
      if (mode == 3) /* diagnostic: the check and branch alone */
         emulated = nir_imm_zero(b, tex->def.num_components, 32);
   }
   nir_pop_if(b, nif);

   /* The phi is in another block, so nir_def_rewrite_uses_after() doesn't apply. */
   nir_def *result = nir_if_phi(b, emulated, &tex->def);
   util_dynarray_foreach (&uses, nir_src *, src)
      nir_src_rewrite(*src, result);
   util_dynarray_fini(&uses);
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
radv_nir_lower_xclipse_filter(nir_shader *shader, unsigned mode)
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
         lower_tex(&b, *tex, mode);

      if (util_dynarray_num_elements(&texs, nir_tex_instr *)) {
         progress = true;
         nir_progress(true, impl, nir_metadata_none);
      } else {
         nir_no_progress(impl);
      }
      util_dynarray_fini(&texs);
   }

   if (progress) {
      NIR_PASS(_, shader, nir_lower_vars_to_ssa);
      shader->info.uses_resource_info_query = true;
   }
   return progress;
}
