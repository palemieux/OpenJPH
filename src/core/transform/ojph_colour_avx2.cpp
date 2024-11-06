//***************************************************************************/
// This software is released under the 2-Clause BSD license, included
// below.
//
// Copyright (c) 2019, Aous Naman 
// Copyright (c) 2019, Kakadu Software Pty Ltd, Australia
// Copyright (c) 2019, The University of New South Wales, Australia
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//***************************************************************************/
// This file is part of the OpenJPH software implementation.
// File: ojph_colour_avx2.cpp
// Author: Aous Naman
// Date: 11 October 2019
//***************************************************************************/

#include <climits>
#include <cmath>

#include "ojph_defs.h"
#include "ojph_arch.h"
#include "ojph_mem.h"
#include "ojph_colour.h"

#include <immintrin.h>

namespace ojph {
  namespace local {

    /////////////////////////////////////////////////////////////////////////
    // https://github.com/seung-lab/dijkstra3d/blob/master/libdivide.h
    static inline 
    __m256i avx2_mm256_srai_epi64(__m256i a, int amt, __m256i m) 
    {
      // note than m must be obtained using
      // __m256i m = _mm256_set1_epi64x(1ULL << (63 - amt));
      __m256i x = _mm256_srli_epi64(a, amt);
      x = _mm256_xor_si256(x, m);
      __m256i result = _mm256_sub_epi64(x, m);
      return result;
    }


    //////////////////////////////////////////////////////////////////////////
    void avx2_cnvrt_si32_to_si32_shftd(const si32 *sp, si32 *dp, int shift,
                                       ui32 width)
    {
      __m256i sh = _mm256_set1_epi32(shift);
      for (int i = (width + 7) >> 3; i > 0; --i, sp+=8, dp+=8)
      {
        __m256i s = _mm256_loadu_si256((__m256i*)sp);
        s = _mm256_add_epi32(s, sh);
        _mm256_storeu_si256((__m256i*)dp, s);
      }
    }

    //////////////////////////////////////////////////////////////////////////
    void avx2_cnvrt_si32_to_si32_nlt_type3(const si32* sp, si32* dp,
                                           int shift, ui32 width)
    {
      __m256i sh = _mm256_set1_epi32(-shift);
      __m256i zero = _mm256_setzero_si256();
      for (int i = (width + 7) >> 3; i > 0; --i, sp += 8, dp += 8)
      {
        __m256i s = _mm256_loadu_si256((__m256i*)sp);
        __m256i c = _mm256_cmpgt_epi32(s, zero);  // 0xFFFFFFFF for +ve value
        __m256i z = _mm256_cmpeq_epi32(s, zero);  // 0xFFFFFFFF for 0
        c = _mm256_or_si256(c, z);                // 0xFFFFFFFF for +ve and 0

        __m256i v_m_sh = _mm256_sub_epi32(sh, s); // - shift - value 
        v_m_sh = _mm256_andnot_si256(c, v_m_sh);  // keep only - shift - value
        s = _mm256_and_si256(c, s);               // keep only +ve or 0
        s = _mm256_or_si256(s, v_m_sh);           // combine
        _mm256_storeu_si256((__m256i*)dp, s);
      }
    }

    //
    // _mm256_cvtepi32_epi64 
    //

    // //////////////////////////////////////////////////////////////////////////
    // void avx2_rct_forward(const si32 *r, const si32 *g, const si32 *b,
    //                       si32 *y, si32 *cb, si32 *cr, ui32 repeat)
    // {
    //   for (int i = (repeat + 7) >> 3; i > 0; --i)
    //   {
    //     __m256i mr = _mm256_load_si256((__m256i*)r);
    //     __m256i mg = _mm256_load_si256((__m256i*)g);
    //     __m256i mb = _mm256_load_si256((__m256i*)b);
    //     __m256i t = _mm256_add_epi32(mr, mb);
    //     t = _mm256_add_epi32(t, _mm256_slli_epi32(mg, 1));
    //     _mm256_store_si256((__m256i*)y, _mm256_srai_epi32(t, 2));
    //     t = _mm256_sub_epi32(mb, mg);
    //     _mm256_store_si256((__m256i*)cb, t);
    //     t = _mm256_sub_epi32(mr, mg);
    //     _mm256_store_si256((__m256i*)cr, t);

    //     r += 8; g += 8; b += 8;
    //     y += 8; cb += 8; cr += 8;
    //   }
    // }

    //////////////////////////////////////////////////////////////////////////
    void avx2_rct_backward(const line_buf *y, 
                           const line_buf *cb, 
                           const line_buf *cr,
                           line_buf *r, line_buf *g, line_buf *b, 
                           ui32 repeat)
    {
      assert((y->flags  & line_buf::LFT_REVERSIBLE) &&
             (cb->flags & line_buf::LFT_REVERSIBLE) && 
             (cr->flags & line_buf::LFT_REVERSIBLE) &&
             (r->flags  & line_buf::LFT_REVERSIBLE) &&
             (g->flags  & line_buf::LFT_REVERSIBLE) && 
             (b->flags  & line_buf::LFT_REVERSIBLE));

      if (y->flags & line_buf::LFT_32BIT)
      {
        assert((y->flags  & line_buf::LFT_32BIT) &&
               (cb->flags & line_buf::LFT_32BIT) && 
               (cr->flags & line_buf::LFT_32BIT) &&
               (r->flags  & line_buf::LFT_32BIT) &&
               (g->flags  & line_buf::LFT_32BIT) && 
               (b->flags  & line_buf::LFT_32BIT));
        const si32 *yp = y->i32, *cbp = cb->i32, *crp = cr->i32;
        si32 *rp = r->i32, *gp = g->i32, *bp = b->i32;
        for (int i = (repeat + 7) >> 3; i > 0; --i)
        {
          __m256i my  = _mm256_load_si256((__m256i*)yp);
          __m256i mcb = _mm256_load_si256((__m256i*)cbp);
          __m256i mcr = _mm256_load_si256((__m256i*)crp);

          __m256i t = _mm256_add_epi32(mcb, mcr);
          t = _mm256_sub_epi32(my, _mm256_srai_epi32(t, 2));
          _mm256_store_si256((__m256i*)gp, t);
          __m256i u = _mm256_add_epi32(mcb, t);
          _mm256_store_si256((__m256i*)bp, u);
          u = _mm256_add_epi32(mcr, t);
          _mm256_store_si256((__m256i*)rp, u);

          yp += 8; cbp += 8; crp += 8;
          rp += 8; gp += 8; bp += 8;
        }        
      }
      else
      {
        assert((y->flags  & line_buf::LFT_64BIT) &&
               (cb->flags & line_buf::LFT_64BIT) && 
               (cr->flags & line_buf::LFT_64BIT) &&
               (r->flags  & line_buf::LFT_32BIT) &&
               (g->flags  & line_buf::LFT_32BIT) && 
               (b->flags  & line_buf::LFT_32BIT));
        __m256i v2 = _mm256_set1_epi64x(1ULL << (63 - 2));
        __m256i low_bits = _mm256_set_epi64x(0, (si64)ULLONG_MAX, 0, (si64)ULLONG_MAX);
        const si64 *yp = y->i64, *cbp = cb->i64, *crp = cr->i64;
        si32 *rp = r->i32, *gp = g->i32, *bp = b->i32;
        for (int i = (repeat + 7) >> 3; i > 0; --i)
        {
          __m256i my, mcb, mcr, tr, tg, tb;          
          my  = _mm256_load_si256((__m256i*)yp);
          mcb = _mm256_load_si256((__m256i*)cbp);
          mcr = _mm256_load_si256((__m256i*)crp);

          tg = _mm256_add_epi64(mcb, mcr);
          tg = _mm256_sub_epi64(my, avx2_mm256_srai_epi64(tg, 2, v2));
          tb = _mm256_add_epi64(mcb, tg);
          tr = _mm256_add_epi64(mcr, tg);

          __m256i mr, mg, mb;
          mr = _mm256_shuffle_epi32(tr, _MM_SHUFFLE(0, 0, 2, 0));
          mr = _mm256_and_si256(low_bits, mr);
          mg = _mm256_shuffle_epi32(tg, _MM_SHUFFLE(0, 0, 2, 0));
          mg = _mm256_and_si256(low_bits, mg);
          mb = _mm256_shuffle_epi32(tb, _MM_SHUFFLE(0, 0, 2, 0));
          mb = _mm256_and_si256(low_bits, mb);

          yp += 4; cbp += 4; crp += 4;

          my  = _mm256_load_si256((__m256i*)yp);
          mcb = _mm256_load_si256((__m256i*)cbp);
          mcr = _mm256_load_si256((__m256i*)crp);

          tg = _mm256_add_epi64(mcb, mcr);
          tg = _mm256_sub_epi64(my, avx2_mm256_srai_epi64(tg, 2, v2));
          tb = _mm256_add_epi64(mcb, tg);
          tr = _mm256_add_epi64(mcr, tg);

          tr = _mm256_shuffle_epi32(tr, _MM_SHUFFLE(2, 0, 0, 0));
          tr = _mm256_andnot_si256(low_bits, tr);
          mr = _mm256_or_si256(mr, tr);
          tg = _mm256_shuffle_epi32(tg, _MM_SHUFFLE(2, 0, 0, 0));
          tg = _mm256_andnot_si256(low_bits, tg);
          mg = _mm256_or_si256(mg, tg);
          tb = _mm256_shuffle_epi32(tb, _MM_SHUFFLE(2, 0, 0, 0));
          tb = _mm256_andnot_si256(low_bits, tb);
          mb = _mm256_or_si256(mb, tb);

          _mm256_store_si256((__m256i*)rp, mr);
          _mm256_store_si256((__m256i*)gp, mg);
          _mm256_store_si256((__m256i*)bp, mb);

          yp += 4; cbp += 4; crp += 4;
          rp += 8; gp += 8; bp += 8;
        }        
      }
    }


    // //////////////////////////////////////////////////////////////////////////
    // void avx2_rct_backward(const si32 *y, const si32 *cb, const si32 *cr,
    //                        si32 *r, si32 *g, si32 *b, ui32 repeat)
    // {
    //   for (int i = (repeat + 7) >> 3; i > 0; --i)
    //   {
    //     __m256i my  = _mm256_load_si256((__m256i*)y);
    //     __m256i mcb = _mm256_load_si256((__m256i*)cb);
    //     __m256i mcr = _mm256_load_si256((__m256i*)cr);

    //     __m256i t = _mm256_add_epi32(mcb, mcr);
    //     t = _mm256_sub_epi32(my, _mm256_srai_epi32(t, 2));
    //     _mm256_store_si256((__m256i*)g, t);
    //     __m256i u = _mm256_add_epi32(mcb, t);
    //     _mm256_store_si256((__m256i*)b, u);
    //     u = _mm256_add_epi32(mcr, t);
    //     _mm256_store_si256((__m256i*)r, u);

    //     y += 8; cb += 8; cr += 8;
    //     r += 8; g += 8; b += 8;
    //   }
    // }

  }
}
