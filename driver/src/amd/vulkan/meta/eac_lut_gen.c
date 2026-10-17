/*
 * SPDX-License-Identifier: MIT
 *
 * Generates eac_lut.h: for every BC4 endpoint pair (e0,e1), the EAC_R11 configuration
 * (base codeword, multiplier, modifier table) whose eight values best cover the BC4 palette,
 * and the nearest EAC index for each BC4 index. Not built by meson; run by hand:
 *
 *   cc -O2 -pthread -o /tmp/eac_lut_gen eac_lut_gen.c -lm
 *   /tmp/eac_lut_gen eac_lut.h.new eac_lut.h     # second argument: score an existing table too
 *
 * Objective, per pair and palette set, lexicographic: (1) the worst palette entry's distance to
 * its nearest EAC value (minimax), (2) the endpoints' error (indices 0 and 1, and the 0.0/1.0
 * entries when used), (3) the sum of squared errors. Errors are in 1/255 units of the decoded
 * value: BC4 v/255 against EAC_R11 unorm e/2047.
 *
 * EAC_R11 unsigned decode (Khronos Data Format spec, EAC): e = clamp(base*8 + 4 + mod*m, 0, 2047)
 * with m = mult*8, or m = 1 when mult == 0.
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int eac_mod[16][8] = {
   {-3, -6, -9, -15, 2, 5, 8, 14}, {-3, -7, -10, -13, 2, 6, 9, 12}, {-2, -5, -8, -13, 1, 4, 7, 12},
   {-2, -4, -6, -13, 1, 3, 5, 12}, {-3, -6, -8, -12, 2, 5, 7, 11}, {-3, -7, -9, -11, 2, 6, 8, 10},
   {-4, -7, -8, -11, 3, 6, 7, 10}, {-3, -5, -8, -11, 2, 4, 7, 10}, {-2, -6, -8, -10, 1, 5, 7, 9},
   {-2, -5, -8, -10, 1, 4, 7, 9},  {-2, -4, -8, -10, 1, 3, 7, 9},  {-2, -5, -7, -10, 1, 4, 6, 9},
   {-3, -4, -7, -10, 2, 3, 6, 9},  {-1, -2, -3, -10, 0, 1, 2, 9},  {-4, -6, -8, -9, 3, 5, 7, 8},
   {-3, -5, -7, -9, 2, 4, 6, 8},
};

static int
bc4val(int i, int e0, int e1)
{
   if (i == 0)
      return e0;
   if (i == 1)
      return e1;
   if (e0 > e1)
      return ((8 - i) * e0 + (i - 1) * e1) / 7;
   if (i == 6)
      return 0;
   if (i == 7)
      return 255;
   return ((6 - i) * e0 + (i - 1) * e1) / 5;
}

static inline int
eac_value(int base, int mult, int tab, int k)
{
   int e = base * 8 + 4 + eac_mod[tab][k] * (mult ? mult * 8 : 1);
   return e < 0 ? 0 : e > 2047 ? 2047 : e;
}

struct score {
   double worst, ends, sq;
};

static inline int
better(const struct score *a, const struct score *b)
{
   const double eps = 1e-9;
   if (a->worst < b->worst - eps)
      return 1;
   if (a->worst > b->worst + eps)
      return 0;
   if (a->ends < b->ends - eps)
      return 1;
   if (a->ends > b->ends + eps)
      return 0;
   return a->sq < b->sq - eps;
}

/* Scores a config on the first n palette entries (targets in 1/255 units). Fills idx with the
 * nearest EAC index per entry. ends: entries 0, 1 and, in 6-value mode, 6 and 7. */
static struct score
score_cfg(int base, int mult, int tab, const int *target, int n, int six_mode, int *idx)
{
   double vals[8];
   for (int k = 0; k < 8; k++)
      vals[k] = eac_value(base, mult, tab, k) * 255.0 / 2047.0;
   struct score s = {0, 0, 0};
   for (int i = 0; i < n; i++) {
      double best = 1e9;
      int bk = 0;
      for (int k = 0; k < 8; k++) {
         double d = fabs(vals[k] - target[i]);
         if (d < best) {
            best = d;
            bk = k;
         }
      }
      if (idx)
         idx[i] = bk;
      if (best > s.worst)
         s.worst = best;
      if (i < 2 || (six_mode && i >= 6))
         s.ends += best;
      s.sq += best * best;
   }
   return s;
}

static uint16_t
search(const int *target, int n, int six_mode)
{
   int lo = 255, hi = 0;
   for (int i = 0; i < n; i++) {
      lo = target[i] < lo ? target[i] : lo;
      hi = target[i] > hi ? target[i] : hi;
   }
   struct score best = {1e9, 1e9, 1e9};
   uint16_t cfg = 0;
   for (int mult = 0; mult < 16; mult++) {
      for (int tab = 0; tab < 16; tab++) {
         /* base*8+4 sits between the extremes' targets; search a window around their midpoint.
          * Clamping at 0/2047 lets far-out bases reach the edges, so the window always includes
          * 0 and 255 when the targets touch them. */
         const int mid = ((lo + hi) * 2047 / 255 / 2 - 4) / 8;
         const int span = 6 + (mult ? mult * 2 : 1);
         int b0 = mid - span, b1 = mid + span;
         if (b0 < 0)
            b0 = 0;
         if (b1 > 255)
            b1 = 255;
         for (int base = b0; base <= b1; base++) {
            struct score s = score_cfg(base, mult, tab, target, n, six_mode, NULL);
            if (better(&s, &best)) {
               best = s;
               cfg = (uint16_t)(base << 8 | mult << 4 | tab);
            }
         }
         for (int edge = 0; edge < 2; edge++) {
            const int base = edge ? 255 : 0;
            if (base >= b0 && base <= b1)
               continue;
            struct score s = score_cfg(base, mult, tab, target, n, six_mode, NULL);
            if (better(&s, &best)) {
               best = s;
               cfg = (uint16_t)(base << 8 | mult << 4 | tab);
            }
         }
      }
   }
   return cfg;
}

static uint32_t lut[65536];
static uint32_t remap[131072];

static void
palette(int e0, int e1, int *t)
{
   for (int i = 0; i < 8; i++)
      t[i] = bc4val(i, e0, e1);
}

static uint32_t
remap_word(uint16_t cfg, const int *t, int six_mode)
{
   int idx[8];
   score_cfg(cfg >> 8, (cfg >> 4) & 15, cfg & 15, t, 8, six_mode, idx);
   uint32_t w = 0;
   for (int i = 0; i < 8; i++)
      w |= (uint32_t)idx[i] << (3 * i);
   return w;
}

static void *
worker(void *arg)
{
   const int part = (int)(intptr_t)arg, parts = 16;
   for (int li = part; li < 65536; li += parts) {
      const int e0 = li >> 8, e1 = li & 255;
      int t[8];
      palette(e0, e1, t);
      const int six = e0 <= e1;
      /* Low half: the entries on the line (all 8 in 8-value mode, 0-5 in 6-value mode). High
       * half: all eight. */
      const uint16_t on = search(t, six ? 6 : 8, six);
      const uint16_t all = six ? search(t, 8, six) : on;
      lut[li] = (uint32_t)all << 16 | on;
      remap[li * 2 + 0] = remap_word(on, t, six);
      remap[li * 2 + 1] = remap_word(all, t, six);
   }
   return NULL;
}

/* Error statistics of a (lut, remap) pair: the decoded value at each BC4 index through its remap,
 * against the BC4 value. */
/* mode 0: 8-value blocks (e0 > e1); 1: 6-value blocks, on-line config; 2: 6-value, all eight. */
static void
stats_mode(const char *name, const uint32_t *l, const uint32_t *r, int mode)
{
   static const char *const mname[3] = {"8-value", "6-value", "6+0/255"};
   static const int edges[] = {0, 32, 64, 128, 255};
   for (int band = 0; band < 4; band++) {
      double worst = 0, sum = 0;
      long n = 0, over = 0;
      for (int li = 0; li < 65536; li++) {
         const int e0 = li >> 8, e1 = li & 255;
         const int spread = abs(e0 - e1);
         if (spread < edges[band] + (band ? 1 : 0) || spread > edges[band + 1])
            continue;
         int t[8];
         palette(e0, e1, t);
         const int six = e0 <= e1;
         if (six != (mode != 0))
            continue;
         for (int half = mode == 2; half < (mode == 1 ? 1 : 2) && (six || half == 0); half++) {
            const uint16_t cfg = (uint16_t)(half ? l[li] >> 16 : l[li] & 0xffff);
            const uint32_t rw = r[li * 2 + half];
            const int n_used = (six && !half) ? 6 : 8;
            double block_worst = 0;
            for (int i = 0; i < n_used; i++) {
               const int k = (rw >> (3 * i)) & 7;
               const double v = eac_value(cfg >> 8, (cfg >> 4) & 15, cfg & 15, k) * 255.0 / 2047.0;
               const double d = fabs(v - t[i]);
               block_worst = d > block_worst ? d : block_worst;
               sum += d;
               n++;
            }
            worst = block_worst > worst ? block_worst : worst;
            over += block_worst > 2.55;
         }
      }
      printf("%-4s %-8s spread %3d-%3d: worst %5.2f/255  mean %.2f/255  blocks over 1%%: %ld\n", name,
             mname[mode], edges[band] + (band ? 1 : 0), edges[band + 1], worst, n ? sum / n : 0, over);
   }
}

static void
stats(const char *name, const uint32_t *l, const uint32_t *r)
{
   for (int mode = 0; mode < 3; mode++)
      stats_mode(name, l, r, mode);
}

/* Reads the two arrays of an existing eac_lut.h. */
static int
load_old(const char *path, uint32_t *l, uint32_t *r)
{
   FILE *f = fopen(path, "r");
   if (!f)
      return 0;
   char line[4096];
   uint32_t *dst = NULL;
   size_t n = 0, cap = 0;
   while (fgets(line, sizeof(line), f)) {
      if (strstr(line, "eac_lut[")) {
         dst = l, n = 0, cap = 65536;
         continue;
      }
      if (strstr(line, "eac_remap[")) {
         dst = r, n = 0, cap = 131072;
         continue;
      }
      if (!dst)
         continue;
      for (char *p = line; (p = strstr(p, "0x")); p += 2)
         if (n < cap)
            dst[n++] = (uint32_t)strtoul(p, NULL, 16);
   }
   fclose(f);
   return 1;
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      fprintf(stderr, "usage: %s OUT.h [OLD.h]\n", argv[0]);
      return 1;
   }
   pthread_t th[16];
   for (int i = 0; i < 16; i++)
      pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
   for (int i = 0; i < 16; i++)
      pthread_join(th[i], NULL);

   if (argc > 2) {
      static uint32_t ol[65536], orr[131072];
      if (load_old(argv[2], ol, orr))
         stats("old", ol, orr);
   }
   stats("new", lut, remap);

   FILE *f = fopen(argv[1], "w");
   if (!f)
      return 1;
   fprintf(f, "/*\n * SPDX-License-Identifier: MIT\n */\n\n"
              "/* Generated by eac_lut_gen.c -- do not hand-edit.\n *\n"
              " * For every BC4 endpoint pair (e0,e1), the EAC_R11 configuration that covers that block's\n"
              " * palette best: base_codeword in bits 15..8, multiplier in 7..4, table_index in 3..0. Exhaustive\n"
              " * over multipliers and tables, minimax over the palette entries, ties broken by endpoint error and\n"
              " * then squared error.\n *\n"
              " * Indexed [e0 * 256 + e1]. 65536 entries of 32 bits: the LOW half is the configuration\n"
              " * fitted to the ON-LINE palette entries only, the HIGH half the one fitted to ALL\n"
              " * EIGHT, so BC4 mode-6 indices 6 and 7 (0.0 and 1.0) are reachable too. 256 KB.\n */\n");
   fprintf(f, "static const uint32_t eac_lut[65536] = {\n");
   for (int i = 0; i < 65536; i++)
      fprintf(f, "%s0x%08x,%s", i % 8 ? "" : "   ", lut[i], i % 8 == 7 ? "\n" : "");
   fprintf(f, "};\n\n/* Per [e0 * 256 + e1][half]: the nearest EAC index for each of the 8 BC4 indices, 3 bits\n"
              " * each (index i at bits 3i..3i+2), for the low (on-line) and high (all eight) configuration. */\n");
   fprintf(f, "static const uint32_t eac_remap[131072] = {\n");
   for (int i = 0; i < 131072; i++)
      fprintf(f, "%s0x%08x,%s", i % 8 ? "" : "   ", remap[i], i % 8 == 7 ? "\n" : "");
   fprintf(f, "};\n");
   fclose(f);
   return 0;
}
