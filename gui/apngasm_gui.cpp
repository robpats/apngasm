/* APNG Assembler 2.9
 *
 * This program creates APNG animation from PNG/TGA image sequence.
 *
 * http://apngasm.sourceforge.net/
 *
 * Copyright (c) 2009-2014 Max Stepin
 * maxst at users.sourceforge.net
 *
 * zlib license
 * ------------
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "apngasm_gui.h"
#include "resource.h"
#include "png.h"     /* original (unpatched) libpng is ok */
#include "zlib.h"
#include "7z.h"
extern "C" {
#include "zopfli.h"
}

typedef struct { unsigned char * p; unsigned int size; int x, y, w, h, valid, filters; } OP;
typedef struct { unsigned int num; unsigned char r, g, b, a; } COLORS;
typedef struct { unsigned char r, g, b; } rgb;
typedef struct { unsigned char * p, ** rows, t; unsigned int w, h; int ps, ts; rgb pl[256]; unsigned char tr[256]; int num, den; } image_info;

OP      op[6];
COLORS  col[256];

unsigned char * op_zbuf1;
unsigned char * op_zbuf2;
z_stream        op_zstream1;
z_stream        op_zstream2;

unsigned int    next_seq_num;
unsigned char * row_buf;
unsigned char * sub_row;
unsigned char * up_row;
unsigned char * avg_row;
unsigned char * paeth_row;
unsigned char   png_sign[8] = {137,  80,  78,  71,  13,  10,  26,  10};
unsigned char   png_Software[27] = { 83, 111, 102, 116, 119, 97, 114, 101, '\0',
                                     65,  80,  78,  71,  32, 65, 115, 115, 101,
                                    109,  98, 108, 101, 114, 32,  50,  46,  57};

extern HWND        hMainDlg;
extern FILE_INFO * pInputFiles;

int cmp_colors( const void *arg1, const void *arg2 )
{
  if ( ((COLORS*)arg1)->a != ((COLORS*)arg2)->a )
    return (int)(((COLORS*)arg1)->a) - (int)(((COLORS*)arg2)->a);

  if ( ((COLORS*)arg1)->num != ((COLORS*)arg2)->num )
    return (int)(((COLORS*)arg2)->num) - (int)(((COLORS*)arg1)->num);

  if ( ((COLORS*)arg1)->r != ((COLORS*)arg2)->r )
    return (int)(((COLORS*)arg1)->r) - (int)(((COLORS*)arg2)->r);

  if ( ((COLORS*)arg1)->g != ((COLORS*)arg2)->g )
    return (int)(((COLORS*)arg1)->g) - (int)(((COLORS*)arg2)->g);

  return (int)(((COLORS*)arg1)->b) - (int)(((COLORS*)arg2)->b);
}

unsigned int load_png(wchar_t * szImage, image_info * pInfo)
{
  FILE * f;
  unsigned int res = 0;

  if ((f = _wfopen(szImage, L"rb")) != 0)
  {
    unsigned char sig[8];

    if (fread(sig, 1, 8, f) == 8 && png_sig_cmp(sig, 0, 8) == 0)
    {
      png_structp png_ptr  = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
      png_infop   info_ptr = png_create_info_struct(png_ptr);
      if (png_ptr && info_ptr)
      {
        png_byte       depth;
        png_uint_32    rowbytes, i;
        png_colorp     palette;
        png_color_16p  trans_color;
        png_bytep      trans_alpha;

        if (setjmp(png_jmpbuf(png_ptr)))
        {
          png_destroy_read_struct(&png_ptr, &info_ptr, 0);
          fclose(f);
          return 4;
        }

        png_init_io(png_ptr, f);
        png_set_sig_bytes(png_ptr, 8);
        png_read_info(png_ptr, info_ptr);
        pInfo->w = png_get_image_width(png_ptr, info_ptr);
        pInfo->h = png_get_image_height(png_ptr, info_ptr);
        pInfo->t = png_get_color_type(png_ptr, info_ptr);
        depth    = png_get_bit_depth(png_ptr, info_ptr);
        if (depth < 8)
        {
          if (pInfo->t == PNG_COLOR_TYPE_PALETTE)
            png_set_packing(png_ptr);
          else
            png_set_expand(png_ptr);
        }
        else
        if (depth > 8)
        {
          png_set_expand(png_ptr);
          png_set_strip_16(png_ptr);
        }
        (void)png_set_interlace_handling(png_ptr);
        png_read_update_info(png_ptr, info_ptr);
        pInfo->t = png_get_color_type(png_ptr, info_ptr);
        rowbytes = png_get_rowbytes(png_ptr, info_ptr);
        memset(pInfo->pl, 255, sizeof(pInfo->pl));
        memset(pInfo->tr, 255, sizeof(pInfo->tr));

        if (png_get_PLTE(png_ptr, info_ptr, &palette, &pInfo->ps))
          memcpy(pInfo->pl, palette, pInfo->ps * 3);
        else
          pInfo->ps = 0;

        if (png_get_tRNS(png_ptr, info_ptr, &trans_alpha, &pInfo->ts, &trans_color))
        {
          if (pInfo->ts > 0)
          {
            if (pInfo->t == PNG_COLOR_TYPE_GRAY)
            {
              pInfo->tr[0] = 0;
              pInfo->tr[1] = trans_color->gray & 0xFF;
              pInfo->ts = 2;
            }
            else
            if (pInfo->t == PNG_COLOR_TYPE_RGB)
            {
              pInfo->tr[0] = 0;
              pInfo->tr[1] = trans_color->red & 0xFF;
              pInfo->tr[2] = 0;
              pInfo->tr[3] = trans_color->green & 0xFF;
              pInfo->tr[4] = 0;
              pInfo->tr[5] = trans_color->blue & 0xFF;
              pInfo->ts = 6;
            }
            else
            if (pInfo->t == PNG_COLOR_TYPE_PALETTE)
              memcpy(pInfo->tr, trans_alpha, pInfo->ts);
            else
              pInfo->ts = 0;
          }
        }
        else
          pInfo->ts = 0;

        pInfo->p = new unsigned char[pInfo->h * rowbytes];
        pInfo->rows  = new png_bytep[pInfo->h * sizeof(png_bytep)];

        for (i=0; i<pInfo->h; i++)
          pInfo->rows[i] = pInfo->p + i*rowbytes;

        png_read_image(png_ptr, pInfo->rows);
        png_read_end(png_ptr, NULL);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
      }
      else
        res = 3;

      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    }
    else
      res = 2;

    fclose(f);
  }
  else
    res = 1;

  return res;
}

unsigned int load_tga(wchar_t * szImage, image_info * pInfo)
{
  FILE * f;
  unsigned int res = 0;

  if ((f = _wfopen(szImage, L"rb")) != 0)
  {
    unsigned int  i, j, y, compr;
    unsigned int  k, n, rowbytes;
    unsigned char c;
    unsigned char col[4];
    unsigned char fh[18];

    if (fread(&fh, 1, 18, f) != 18) return 2;

    pInfo->w = fh[12] + fh[13]*256;
    pInfo->h = fh[14] + fh[15]*256;
    pInfo->t = -1;
    pInfo->ps = 0;
    pInfo->ts = 0;

    rowbytes = pInfo->w;
    if ((fh[2] & 7) == 3)
      pInfo->t = 0;
    else
    if (((fh[2] & 7) == 2) && (fh[16] == 24))
    {
      pInfo->t = 2;
      rowbytes = pInfo->w * 3;
    }
    else
    if (((fh[2] & 7) == 2) && (fh[16] == 32))
    {
      pInfo->t = 6;
      rowbytes = pInfo->w * 4;
    }
    else
    if (((fh[2] & 7) == 1) && (fh[1] == 1) && (fh[7] == 24))
      pInfo->t = 3;

    compr = fh[2] & 8;

    if (pInfo->t >= 0)
    {
      pInfo->p = new unsigned char[pInfo->h * rowbytes];
      pInfo->rows  = new png_bytep[pInfo->h * sizeof(png_bytep)];

      for (i=0; i<pInfo->h; i++)
        pInfo->rows[i] = pInfo->p + i*rowbytes;

      if (fh[0] != 0)
        fseek(f, fh[0], SEEK_CUR);

      if (fh[1] == 1)
      {
        unsigned int start = fh[3] + fh[4]*256;
        unsigned int size  = fh[5] + fh[6]*256;

        for (i=start; i<start+size && i<256; i++)
        {
          if (fread(&col, 1, 3, f) != 3) return 5;
          pInfo->pl[i].r = col[2];
          pInfo->pl[i].g = col[1];
          pInfo->pl[i].b = col[0];
        }
        pInfo->ps = i;

        if (start+size > 256)
          fseek(f, (start+size-256)*3, SEEK_CUR);
      }

      if ((fh[17] & 0x20) == 0)
        y = pInfo->h - 1;
      else
        y = 0;

      for (j=0; j<pInfo->h; j++)
      {
        if (compr == 0)
        {
          if (pInfo->t == 6)
          {
            for (i=0; i<pInfo->w; i++)
            {
              if (fread(&col, 1, 4, f) != 4) return 5;
              pInfo->rows[y][i*4]   = col[2];
              pInfo->rows[y][i*4+1] = col[1];
              pInfo->rows[y][i*4+2] = col[0];
              pInfo->rows[y][i*4+3] = col[3];
            }
          }
          else
          if (pInfo->t == 2)
          {
            for (i=0; i<pInfo->w; i++)
            {
              if (fread(&col, 1, 3, f) != 3) return 5;
              pInfo->rows[y][i*3]   = col[2];
              pInfo->rows[y][i*3+1] = col[1];
              pInfo->rows[y][i*3+2] = col[0];
            }
          }
          else
            if (fread(pInfo->rows[y], 1, rowbytes, f) != rowbytes) return 5;
        }
        else
        {
          i = 0;
          while (i<pInfo->w)
          {
            if (fread(&c, 1, 1, f) != 1) return 5;
            n = (c & 0x7F)+1;

            if ((c & 0x80) != 0)
            {
              if (pInfo->t == 6)
              {
                if (fread(&col, 1, 4, f) != 4) return 5;
                for (k=0; k<n; k++)
                {
                  pInfo->rows[y][(i+k)*4]   = col[2];
                  pInfo->rows[y][(i+k)*4+1] = col[1];
                  pInfo->rows[y][(i+k)*4+2] = col[0];
                  pInfo->rows[y][(i+k)*4+3] = col[3];
                }
              }
              else
              if (pInfo->t == 2)
              {
                if (fread(&col, 1, 3, f) != 3) return 5;
                for (k=0; k<n; k++)
                {
                  pInfo->rows[y][(i+k)*3]   = col[2];
                  pInfo->rows[y][(i+k)*3+1] = col[1];
                  pInfo->rows[y][(i+k)*3+2] = col[0];
                }
              }
              else
              {
                if (fread(&col, 1, 1, f) != 1) return 5;
                memset(pInfo->rows[y]+i, col[0], n);
              }
            }
            else
            {
              if (pInfo->t == 6)
              {
                for (k=0; k<n; k++)
                {
                  if (fread(&col, 1, 4, f) != 4) return 5;
                  pInfo->rows[y][(i+k)*4]   = col[2];
                  pInfo->rows[y][(i+k)*4+1] = col[1];
                  pInfo->rows[y][(i+k)*4+2] = col[0];
                  pInfo->rows[y][(i+k)*4+3] = col[3];
                }
              }
              else
              if (pInfo->t == 2)
              {
                for (k=0; k<n; k++)
                {
                  if (fread(&col, 1, 3, f) != 3) return 5;
                  pInfo->rows[y][(i+k)*3]   = col[2];
                  pInfo->rows[y][(i+k)*3+1] = col[1];
                  pInfo->rows[y][(i+k)*3+2] = col[0];
                }
              }
              else
                if (fread(pInfo->rows[y]+i, 1, n, f) != n) return 5;
            }
            i+=n;
          }
        }
        if ((fh[17] & 0x20) == 0)
          y--;
        else
          y++;
      }
    }
    else
      res = 3;

    fclose(f);
  }
  else
    res = 1;

  return res;
}

void write_chunk(FILE * f, const char * name, unsigned char * data, unsigned int length)
{
  unsigned char buf[4];
  unsigned int crc = crc32(0, Z_NULL, 0);

  png_save_uint_32(buf, length);
  fwrite(buf, 1, 4, f);
  fwrite(name, 1, 4, f);
  crc = crc32(crc, (const Bytef *)name, 4);

  if (memcmp(name, "fdAT", 4) == 0)
  {
    png_save_uint_32(buf, next_seq_num++);
    fwrite(buf, 1, 4, f);
    crc = crc32(crc, buf, 4);
    length -= 4;
  }

  if (data != NULL && length > 0)
  {
    fwrite(data, 1, length, f);
    crc = crc32(crc, data, length);
  }

  png_save_uint_32(buf, crc);
  fwrite(buf, 1, 4, f);
}

void write_IDATs(FILE * f, int frame, unsigned char * data, unsigned int length, unsigned int idat_size)
{
  unsigned int z_cmf = data[0];
  if ((z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70)
  {
    if (length >= 2)
    {
      unsigned int z_cinfo = z_cmf >> 4;
      unsigned int half_z_window_size = 1 << (z_cinfo + 7);
      while (idat_size <= half_z_window_size && half_z_window_size >= 256)
      {
        z_cinfo--;
        half_z_window_size >>= 1;
      }
      z_cmf = (z_cmf & 0x0f) | (z_cinfo << 4);
      if (data[0] != (unsigned char)z_cmf)
      {
        data[0] = (unsigned char)z_cmf;
        data[1] &= 0xe0;
        data[1] += (unsigned char)(0x1f - ((z_cmf << 8) + data[1]) % 0x1f);
      }
    }
  }

  while (length > 0)
  {
    unsigned int ds = length;
    if (ds > 32768)
      ds = 32768;

    if (frame == 0)
      write_chunk(f, "IDAT", data, ds);
    else
      write_chunk(f, "fdAT", data, ds+4);

    data += ds;
    length -= ds;
  }
}

void process_rect(unsigned char * row, int rowbytes, int bpp, int stride, int h, unsigned char * rows)
{
  int i, j, v;
  int a, b, c, pa, pb, pc, p;
  unsigned char * prev = NULL;
  unsigned char * dp  = rows;
  unsigned char * out;

  for (j=0; j<h; j++)
  {
    unsigned int    sum = 0;
    unsigned char * best_row = row_buf;
    unsigned int    mins = ((unsigned int)(-1)) >> 1;

    out = row_buf+1;
    for (i=0; i<rowbytes; i++)
    {
      v = out[i] = row[i];
      sum += (v < 128) ? v : 256 - v;
    }
    mins = sum;

    sum = 0;
    out = sub_row+1;
    for (i=0; i<bpp; i++)
    {
      v = out[i] = row[i];
      sum += (v < 128) ? v : 256 - v;
    }
    for (i=bpp; i<rowbytes; i++)
    {
      v = out[i] = row[i] - row[i-bpp];
      sum += (v < 128) ? v : 256 - v;
      if (sum > mins) break;
    }
    if (sum < mins)
    {
      mins = sum;
      best_row = sub_row;
    }

    if (prev)
    {
      sum = 0;
      out = up_row+1;
      for (i=0; i<rowbytes; i++)
      {
        v = out[i] = row[i] - prev[i];
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins) break;
      }
      if (sum < mins)
      {
        mins = sum;
        best_row = up_row;
      }

      sum = 0;
      out = avg_row+1;
      for (i=0; i<bpp; i++)
      {
        v = out[i] = row[i] - prev[i]/2;
        sum += (v < 128) ? v : 256 - v;
      }
      for (i=bpp; i<rowbytes; i++)
      {
        v = out[i] = row[i] - (prev[i] + row[i-bpp])/2;
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins) break;
      }
      if (sum < mins)
      {
        mins = sum;
        best_row = avg_row;
      }

      sum = 0;
      out = paeth_row+1;
      for (i=0; i<bpp; i++)
      {
        v = out[i] = row[i] - prev[i];
        sum += (v < 128) ? v : 256 - v;
      }
      for (i=bpp; i<rowbytes; i++)
      {
        a = row[i-bpp];
        b = prev[i];
        c = prev[i-bpp];
        p = b - c;
        pc = a - c;
        pa = abs(p);
        pb = abs(pc);
        pc = abs(p + pc);
        p = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;
        v = out[i] = row[i] - p;
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins) break;
      }
      if (sum < mins)
      {
        best_row = paeth_row;
      }
    }

    if (rows == NULL)
    {
      // deflate_rect_op()
      op_zstream1.next_in = row_buf;
      op_zstream1.avail_in = rowbytes + 1;
      deflate(&op_zstream1, Z_NO_FLUSH);

      op_zstream2.next_in = best_row;
      op_zstream2.avail_in = rowbytes + 1;
      deflate(&op_zstream2, Z_NO_FLUSH);
    }
    else
    {
      // deflate_rect_fin()
      memcpy(dp, best_row, rowbytes+1);
      dp += rowbytes+1;
    }

    prev = row;
    row += stride;
  }
}

void deflate_rect_fin(int deflate_method, int iter, unsigned char * zbuf, unsigned int * zsize, int bpp, int stride, unsigned char * rows, int zbuf_size, int n)
{
  unsigned char * row  = op[n].p + op[n].y*stride + op[n].x*bpp;
  int rowbytes = op[n].w*bpp;

  if (op[n].filters == 0)
  {
    unsigned char * dp  = rows;
    for (int j=0; j<op[n].h; j++)
    {
      *dp++ = 0;
      memcpy(dp, row, rowbytes);
      dp += rowbytes;
      row += stride;
    }
  }
  else
    process_rect(row, rowbytes, bpp, stride, op[n].h, rows);

  if (deflate_method == 2)
  {
    ZopfliOptions opt_zopfli;
    unsigned char* data = 0;
    size_t size = 0;
    ZopfliInitOptions(&opt_zopfli);
    opt_zopfli.numiterations = iter;
    ZopfliCompress(&opt_zopfli, ZOPFLI_FORMAT_ZLIB, rows, op[n].h*(rowbytes + 1), &data, &size);
    if (size < (size_t)zbuf_size)
    {
      memcpy(zbuf, data, size);
      *zsize = size;
    }
    free(data);
  }
  else
  if (deflate_method == 1)
  {
    unsigned size = zbuf_size;
    compress_rfc1950_7z(rows, op[n].h*(rowbytes + 1), zbuf, size, iter<100 ? iter : 100, 255);
    *zsize = size;
  }
  else
  {
    z_stream fin_zstream;

    fin_zstream.data_type = Z_BINARY;
    fin_zstream.zalloc = Z_NULL;
    fin_zstream.zfree = Z_NULL;
    fin_zstream.opaque = Z_NULL;
    deflateInit2(&fin_zstream, Z_BEST_COMPRESSION, 8, 15, 8, op[n].filters ? Z_FILTERED : Z_DEFAULT_STRATEGY);

    fin_zstream.next_out = zbuf;
    fin_zstream.avail_out = zbuf_size;
    fin_zstream.next_in = rows;
    fin_zstream.avail_in = op[n].h*(rowbytes + 1);
    deflate(&fin_zstream, Z_FINISH);
    *zsize = fin_zstream.total_out;
    deflateEnd(&fin_zstream);
  }
}

void deflate_rect_op(unsigned char *pdata, int x, int y, int w, int h, int bpp, int stride, int zbuf_size, int n)
{
  unsigned char * row  = pdata + y*stride + x*bpp;
  int rowbytes = w * bpp;

  op_zstream1.data_type = Z_BINARY;
  op_zstream1.next_out = op_zbuf1;
  op_zstream1.avail_out = zbuf_size;

  op_zstream2.data_type = Z_BINARY;
  op_zstream2.next_out = op_zbuf2;
  op_zstream2.avail_out = zbuf_size;

  process_rect(row, rowbytes, bpp, stride, h, NULL);

  deflate(&op_zstream1, Z_FINISH);
  deflate(&op_zstream2, Z_FINISH);
  op[n].p = pdata;

  if (op_zstream1.total_out < op_zstream2.total_out)
  {
    op[n].size = op_zstream1.total_out;
    op[n].filters = 0;
  }
  else
  {
    op[n].size = op_zstream2.total_out;
    op[n].filters = 1;
  }
  op[n].x = x;
  op[n].y = y;
  op[n].w = w;
  op[n].h = h;
  op[n].valid = 1;
  deflateReset(&op_zstream1);
  deflateReset(&op_zstream2);
}

void get_rect(unsigned int w, unsigned int h, unsigned char *pimage1, unsigned char *pimage2, unsigned char *ptemp, unsigned int bpp, unsigned int stride, int zbuf_size, unsigned int has_tcolor, unsigned int tcolor, int n)
{
  unsigned int   i, j, x0, y0, w0, h0;
  unsigned int   x_min = w-1;
  unsigned int   y_min = h-1;
  unsigned int   x_max = 0;
  unsigned int   y_max = 0;
  unsigned int   diffnum = 0;
  unsigned int   over_is_possible = 1;

  if (!has_tcolor)
    over_is_possible = 0;

  if (bpp == 1)
  {
    unsigned char *pa = pimage1;
    unsigned char *pb = pimage2;
    unsigned char *pc = ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      unsigned char c = *pb++;
      if (*pa++ != c)
      {
        diffnum++;
        if (has_tcolor && c == tcolor) over_is_possible = 0;
        if (i<x_min) x_min = i;
        if (i>x_max) x_max = i;
        if (j<y_min) y_min = j;
        if (j>y_max) y_max = j;
      }
      else
        c = tcolor;

      *pc++ = c;
    }
  }
  else
  if (bpp == 2)
  {
    unsigned short *pa = (unsigned short *)pimage1;
    unsigned short *pb = (unsigned short *)pimage2;
    unsigned short *pc = (unsigned short *)ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      unsigned int c1 = *pa++;
      unsigned int c2 = *pb++;
      if ((c1 != c2) && ((c1>>8) || (c2>>8)))
      {
        diffnum++;
        if ((c2 >> 8) != 0xFF) over_is_possible = 0;
        if (i<x_min) x_min = i;
        if (i>x_max) x_max = i;
        if (j<y_min) y_min = j;
        if (j>y_max) y_max = j;
      }
      else
        c2 = 0;

      *pc++ = c2;
    }
  }
  else
  if (bpp == 3)
  {
    unsigned char *pa = pimage1;
    unsigned char *pb = pimage2;
    unsigned char *pc = ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      unsigned int c1 = (pa[2]<<16) + (pa[1]<<8) + pa[0];
      unsigned int c2 = (pb[2]<<16) + (pb[1]<<8) + pb[0];
      if (c1 != c2)
      {
        diffnum++;
        if (has_tcolor && c2 == tcolor) over_is_possible = 0;
        if (i<x_min) x_min = i;
        if (i>x_max) x_max = i;
        if (j<y_min) y_min = j;
        if (j>y_max) y_max = j;
      }
      else
        c2 = tcolor;

      memcpy(pc, &c2, 3);
      pa += 3;
      pb += 3;
      pc += 3;
    }
  }
  else
  if (bpp == 4)
  {
    unsigned int *pa = (unsigned int *)pimage1;
    unsigned int *pb = (unsigned int *)pimage2;
    unsigned int *pc = (unsigned int *)ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      unsigned int c1 = *pa++;
      unsigned int c2 = *pb++;
      if ((c1 != c2) && ((c1>>24) || (c2>>24)))
      {
        diffnum++;
        if ((c2 >> 24) != 0xFF) over_is_possible = 0;
        if (i<x_min) x_min = i;
        if (i>x_max) x_max = i;
        if (j<y_min) y_min = j;
        if (j>y_max) y_max = j;
      }
      else
        c2 = 0;

      *pc++ = c2;
    }
  }

  if (diffnum == 0)
  {
    x0 = y0 = 0;
    w0 = h0 = 1;
  }
  else
  {
    x0 = x_min;
    y0 = y_min;
    w0 = x_max-x_min+1;
    h0 = y_max-y_min+1;
  }

  deflate_rect_op(pimage2, x0, y0, w0, h0, bpp, stride, zbuf_size, n*2);

  if (over_is_possible)
    deflate_rect_op(ptemp, x0, y0, w0, h0, bpp, stride, zbuf_size, n*2+1);
}

DWORD WINAPI CreateAPNG(FILE_INFO * pInputFiles, UINT frames, wchar_t * szOut, int deflate_method, int iter, int keep_palette, int keep_coltype, int first, int loops)
{
  FILE_INFO      * pFileInfo;
  unsigned int     i, j, k;
  unsigned int     width, height, size;
  unsigned int     x0, y0, w0, h0;
  unsigned int     bpp, rowbytes, imagesize;
  unsigned int     idat_size, zbuf_size, zsize;
  unsigned int     has_tcolor, tcolor, colors;
  unsigned char    coltype, dop, bop, r, g, b, a;
  int              c;
  rgb              palette[256];
  unsigned char    trns[256];
  unsigned int     palsize, trnssize;
  unsigned char    cube[4096];
  unsigned char    gray[256];
  unsigned char  * zbuf;
  unsigned char  * sp;
  unsigned char  * dp;
  FILE           * f;
  unsigned char  * over1;
  unsigned char  * over2;
  unsigned char  * over3;
  unsigned char  * temp;
  unsigned char  * rest;
  unsigned char  * rows;
  DWORD            res = 1;
  wchar_t          szBuf[MAX_PATH+1];

  coltype = 6;

  image_info * img = new image_info[frames * sizeof(image_info)];

  memset(&cube, 0, sizeof(cube));
  memset(&gray, 0, sizeof(gray));

  for (i=0; i<256; i++)
  {
    col[i].num = 0;
    col[i].r = col[i].g = col[i].b = i;
    col[i].a = trns[i] = 255;
  }

  pFileInfo = pInputFiles;
  for (i=0; i<frames; ++i, ++pFileInfo)
  {
    if (pFileInfo->is_png_tga == 1)
      res = load_png(pFileInfo->szPath, &img[i]);
    else
      res = load_tga(pFileInfo->szPath, &img[i]);

    if (res)
    {
      swprintf(szBuf, MAX_PATH, L"load_%s(%s) failed    ", (pFileInfo->is_png_tga == 1) ? "png" : "tga", pFileInfo->szPath);
      MessageBox(hMainDlg, szBuf, 0, MB_ICONINFORMATION);
      return res;
    }

    for (c=img[i].ps; c<256; c++)
      img[i].pl[c].r = img[i].pl[c].g = img[i].pl[c].b = c;

    for (c=img[i].ts; c<256; c++)
      img[i].tr[c] = 255;

    img[i].num = pFileInfo->delay_num;
    img[i].den = pFileInfo->delay_den;

    if (img[0].w != img[i].w || img[0].h != img[i].h)
    {
      swprintf(szBuf, MAX_PATH, L"Error: all frames are expected to be %dx%d    \n\n%s%s is %dx%d    \n ", img[0].w, img[0].h, pFileInfo->szName, pFileInfo->szExt, img[i].w, img[i].h);
      MessageBox(hMainDlg, szBuf, 0, MB_ICONINFORMATION);
      return 1;
    }

    if (i == 0)
      coltype = img[0].t;
    else
    if (img[0].ps != img[i].ps || memcmp(img[0].pl, img[i].pl, img[0].ps*3) != 0)
      coltype = 6;
    else
    if (img[0].ts != img[i].ts || memcmp(img[0].tr, img[i].tr, img[0].ts) != 0)
      coltype = 6;
    else
    if (img[i].t != 3)
    {
      if (coltype != 3)
        coltype |= img[i].t;
      else
        coltype = 6;
    }
    else
      if (coltype != 3)
        coltype = 6;
  }

  width = img[0].w;
  height = img[0].h;
  size = width * height;

  /* Upconvert to common coltype - start */
  for (i=0; i<frames; i++)
  {
    if (coltype == 6 && img[i].t != 6)
    {
      unsigned char * dst = new unsigned char[size * 4];
      if (img[i].t == 0)
      {
        sp = img[i].p;
        dp = dst;
        if (img[i].ts == 0)
        for (j=0; j<size; j++)
        {
          *dp++ = *sp;
          *dp++ = *sp;
          *dp++ = *sp++;
          *dp++ = 255;
        }
        else
        for (j=0; j<size; j++)
        {
          g = *sp++;
          *dp++ = g;
          *dp++ = g;
          *dp++ = g;
          *dp++ = (img[i].tr[1] == g) ? 0 : 255;
        }
      }
      else
      if (img[i].t == 2)
      {
        sp = img[i].p;
        dp = dst;
        if (img[i].ts == 0)
        for (j=0; j<size; j++)
        {
          *dp++ = *sp++;
          *dp++ = *sp++;
          *dp++ = *sp++;
          *dp++ = 255;
        }
        else
        for (j=0; j<size; j++)
        {
          r = *sp++;
          g = *sp++;
          b = *sp++;
          *dp++ = r;
          *dp++ = g;
          *dp++ = b;
          *dp++ = (img[i].tr[1] == r && img[i].tr[3] == g && img[i].tr[5] == b) ? 0 : 255;
        }
      }
      else
      if (img[i].t == 3)
      {
        sp = img[i].p;
        dp = dst;
        for (j=0; j<size; j++, sp++)
        {
          *dp++ = img[i].pl[*sp].r;
          *dp++ = img[i].pl[*sp].g;
          *dp++ = img[i].pl[*sp].b;
          *dp++ = img[i].tr[*sp];
        }
      }
      else
      if (img[i].t == 4)
      {
        sp = img[i].p;
        dp = dst;
        for (j=0; j<size; j++)
        {
          *dp++ = *sp;
          *dp++ = *sp;
          *dp++ = *sp++;
          *dp++ = *sp++;
        }
      }
      delete[] img[i].p;
      img[i].p = dst;
      for (j=0; j<height; j++, dst+=width*4)
        img[i].rows[j] = dst;
    }
    else
    if (coltype == 4 && img[i].t == 0)
    {
      unsigned char * dst = new unsigned char[size * 2];
      sp = img[i].p;
      dp = dst;
      for (j=0; j<size; j++)
      {
        *dp++ = *sp++;
        *dp++ = 255;
      }
      delete[] img[i].p;
      img[i].p = dst;
      for (j=0; j<height; j++, dst+=width*2)
        img[i].rows[j] = dst;
    }
    else
    if (coltype == 2 && img[i].t == 0)
    {
      unsigned char * dst = new unsigned char[size * 3];
      sp = img[i].p;
      dp = dst;
      for (j=0; j<size; j++)
      {
        *dp++ = *sp;
        *dp++ = *sp;
        *dp++ = *sp++;
      }
      delete[] img[i].p;
      img[i].p = dst;
      for (j=0; j<height; j++, dst+=width*3)
        img[i].rows[j] = dst;
    }
  }
  /* Upconvert to common coltype - end */

  /* Dirty transparency optimization - start */
  if (coltype == 6)
  {
    for (i=0; i<frames; i++)
    {
      sp = img[i].p;
      for (j=0; j<size; j++, sp+=4)
        if (sp[3] == 0)
           sp[0] = sp[1] = sp[2] = 0;
    }
  }
  else
  if (coltype == 4)
  {
    for (i=0; i<frames; i++)
    {
      sp = img[i].p;
      for (j=0; j<size; j++, sp+=2)
        if (sp[1] == 0)
          sp[0] = 0;
    }
  }
  /* Dirty transparency optimization - end */

  /* Identical frames optimization - start */
  bpp = (coltype == 6) ? 4 : (coltype == 2) ? 3 : (coltype == 4) ? 2 : 1;
  imagesize = width * height * bpp;
  i = first;

  while (++i < frames)
  {
    if (memcmp(img[i-1].p, img[i].p, imagesize) != 0)
      continue;

    i--;
    delete[] img[i].p;
    delete[] img[i].rows;
    int num = img[i].num;
    int den = img[i].den;
      
    for (j=i; j<frames-1; j++)
      memcpy(&img[j], &img[j+1], sizeof(image_info));

    if (img[i].den == den)
      img[i].num += num;
    else
    {
      img[i].num = num = num*img[i].den + den*img[i].num;
      img[i].den = den = den*img[i].den;
      while (num && den)
      {
        if (num > den)
          num = num % den;
        else
          den = den % num;
      }
      num += den;
      img[i].num /= num;
      img[i].den /= num;
    }
      
    frames--;
  }
  /* Identical frames optimization - end */

  /* Downconvert optimizations - start */
  has_tcolor = 0;
  palsize = trnssize = 0;
  colors = 0;

  if (coltype == 6 && !keep_coltype)
  {
    int transparent = 255;
    int simple_trans = 1;
    int grayscale = 1;

    for (i=0; i<frames; i++)
    {
      sp = img[i].p;
      for (j=0; j<size; j++)
      {
        r = *sp++;
        g = *sp++;
        b = *sp++;
        a = *sp++;
        transparent &= a;

        if (a != 0)
        {
          if (a != 255)
            simple_trans = 0;
          else
            if (((r | g | b) & 15) == 0)
              cube[(r<<4) + g + (b>>4)] = 1;

          if (r != g || g != b)
            grayscale = 0;
          else
            gray[r] = 1;
        }

        if (colors <= 256)
        {
          int found = 0;
          for (k=0; k<colors; k++)
          if (col[k].r == r && col[k].g == g && col[k].b == b && col[k].a == a)
          {
            found = 1;
            col[k].num++;
            break;
          }
          if (found == 0)
          {
            if (colors < 256)
            {
              col[colors].num++;
              col[colors].r = r;
              col[colors].g = g;
              col[colors].b = b;
              col[colors].a = a;
              if (a == 0) has_tcolor = 1;
            }
            colors++;
          }
        }
      }
    }

    if (grayscale && simple_trans && colors<=256) /* 6 -> 0 */
    {
      coltype = 0;

      for (i=0; i<256; i++)
      if (gray[i] == 0)
      {
        trns[0] = 0;
        trns[1] = i;
        trnssize = 2;
        break;
      }

      for (i=0; i<frames; i++)
      {
        sp = dp = img[i].p;
        for (j=0; j<size; j++, sp+=4)
        {
          if (sp[3] == 0)
            *dp++ = trns[1];
          else
            *dp++ = sp[0];
        }
      }
    }
    else
    if (colors<=256)   /* 6 -> 3 */
    {
      coltype = 3;

      if (has_tcolor==0 && colors<256)
        col[colors++].a = 0;

      qsort(&col[0], colors, sizeof(COLORS), cmp_colors);

      palsize = colors;
      for (i=0; i<colors; i++)
      {
        palette[i].r = col[i].r;
        palette[i].g = col[i].g;
        palette[i].b = col[i].b;
        trns[i]      = col[i].a;
        if (trns[i] != 255) trnssize = i+1;
      }

      for (i=0; i<frames; i++)
      {
        sp = dp = img[i].p;
        for (j=0; j<size; j++)
        {
          r = *sp++;
          g = *sp++;
          b = *sp++;
          a = *sp++;
          for (k=0; k<colors; k++)
            if (col[k].r == r && col[k].g == g && col[k].b == b && col[k].a == a)
              break;
          *dp++ = k;
        }
      }
    }
    else
    if (grayscale)     /* 6 -> 4 */
    {
      coltype = 4;
      for (i=0; i<frames; i++)
      {
        sp = dp = img[i].p;
        for (j=0; j<size; j++, sp+=4)
        {
          *dp++ = sp[2];
          *dp++ = sp[3];
        }
      }
    }
    else
    if (simple_trans)  /* 6 -> 2 */
    {
      for (i=0; i<4096; i++)
      if (cube[i] == 0)
      {
        trns[0] = 0;
        trns[1] = (i>>4)&0xF0;
        trns[2] = 0;
        trns[3] = i&0xF0;
        trns[4] = 0;
        trns[5] = (i<<4)&0xF0;
        trnssize = 6;
        break;
      }
      if (transparent == 255)
      {
        coltype = 2;
        for (i=0; i<frames; i++)
        {
          sp = dp = img[i].p;
          for (j=0; j<size; j++)
          {
            *dp++ = *sp++;
            *dp++ = *sp++;
            *dp++ = *sp++;
            sp++;
          }
        }
      }
      else
      if (trnssize != 0)
      {
        coltype = 2;
        for (i=0; i<frames; i++)
        {
          sp = dp = img[i].p;
          for (j=0; j<size; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            a = *sp++;
            if (a == 0)
            {
              *dp++ = trns[1];
              *dp++ = trns[3];
              *dp++ = trns[5];
            }
            else
            {
              *dp++ = r;
              *dp++ = g;
              *dp++ = b;
            }
          }
        }
      }
    }
  }
  else
  if (coltype == 2)
  {
    int grayscale = 1;

    for (i=0; i<frames; i++)
    {
      sp = img[i].p;
      for (j=0; j<size; j++)
      {
        r = *sp++;
        g = *sp++;
        b = *sp++;

        if (img[i].ts == 0)
          if (((r | g | b) & 15) == 0)
            cube[(r<<4) + g + (b>>4)] = 1;

        if (r != g || g != b)
          grayscale = 0;
        else
          gray[r] = 1;

        if (colors <= 256)
        {
          int found = 0;
          for (k=0; k<colors; k++)
          if (col[k].r == r && col[k].g == g && col[k].b == b)
          {
            found = 1;
            col[k].num++;
            break;
          }
          if (found == 0)
          {
            if (colors < 256)
            {
              col[colors].num++;
              col[colors].r = r;
              col[colors].g = g;
              col[colors].b = b;
              if (img[i].ts == 6 && img[i].tr[1] == r && img[i].tr[3] == g && img[i].tr[5] == b)
              {
                col[colors].a = 0;
                has_tcolor = 1;
              }
            }
            colors++;
          }
        }
      }
    }

    if (grayscale && colors<=256 && !keep_coltype) /* 2 -> 0 */
    {
      for (i=0; i<256; i++)
      if (gray[i] == 0)
      {
        trns[0] = 0;
        trns[1] = i;
        trnssize = 2;
        break;
      }
      if (img[0].ts == 0)
      {
        coltype = 0;
        for (i=0; i<frames; i++)
        {
          sp = dp = img[i].p;
          for (j=0; j<size; j++, sp+=3)
            *dp++ = *sp;
        }
      }
      else
      if (trnssize != 0)
      {
        coltype = 0;
        for (i=0; i<frames; i++)
        {
          sp = dp = img[i].p;
          for (j=0; j<size; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            if (img[i].tr[1] == r && img[i].tr[3] == g && img[i].tr[5] == b)
              *dp++ = trns[1];
            else
              *dp++ = g;
          }
        }
      }
    }
    else
    if (colors<=256 && !keep_coltype)   /* 2 -> 3 */
    {
      coltype = 3;

      if (has_tcolor==0 && colors<256)
        col[colors++].a = 0;

      qsort(&col[0], colors, sizeof(COLORS), cmp_colors);

      palsize = colors;
      for (i=0; i<colors; i++)
      {
        palette[i].r = col[i].r;
        palette[i].g = col[i].g;
        palette[i].b = col[i].b;
        trns[i]      = col[i].a;
        if (trns[i] != 255) trnssize = i+1;
      }

      for (i=0; i<frames; i++)
      {
        sp = dp = img[i].p;
        for (j=0; j<size; j++)
        {
          r = *sp++;
          g = *sp++;
          b = *sp++;

          for (k=0; k<colors; k++)
            if (col[k].r == r && col[k].g == g && col[k].b == b)
              break;
          *dp++ = k;
        }
      }
    }
    else /* 2 -> 2 */
    {
      if (img[0].ts != 0)
      {
        memcpy(trns, img[0].tr, img[0].ts);
        trnssize = img[0].ts;
      }
      else
      for (i=0; i<4096; i++)
      if (cube[i] == 0)
      {
        trns[0] = 0;
        trns[1] = (i>>4)&0xF0;
        trns[2] = 0;
        trns[3] = i&0xF0;
        trns[4] = 0;
        trns[5] = (i<<4)&0xF0;
        trnssize = 6;
        break;
      }
    }
  }
  else
  if (coltype == 4 && !keep_coltype)
  {
    int simple_trans = 1;

    for (i=0; i<frames; i++)
    {
      sp = img[i].p;
      for (j=0; j<size; j++)
      {
        g = *sp++;
        a = *sp++;

        if (a != 0)
        {
          if (a != 255)
            simple_trans = 0;
          else
            gray[g] = 1;
        }

        if (colors <= 256)
        {
          int found = 0;
          for (k=0; k<colors; k++)
          if (col[k].g == g && col[k].a == a)
          {
            found = 1;
            col[k].num++;
            break;
          }
          if (found == 0)
          {
            if (colors < 256)
            {
              col[colors].num++;
              col[colors].r = g;
              col[colors].g = g;
              col[colors].b = g;
              col[colors].a = a;
              if (a == 0) has_tcolor = 1;
            }
            colors++;
          }
        }
      }
    }

    if (simple_trans && colors<=256)   /* 4 -> 0 */
    {
      coltype = 0;

      for (i=0; i<256; i++)
      if (gray[i] == 0)
      {
        trns[0] = 0;
        trns[1] = i;
        trnssize = 2;
        break;
      }

      for (i=0; i<frames; i++)
      {
        sp = dp = img[i].p;
        for (j=0; j<size; j++)
        {
          g = *sp++;
          if (*sp++ == 0)
            *dp++ = trns[1];
          else
            *dp++ = g;
        }
      }
    }
    else
    if (colors<=256)   /* 4 -> 3 */
    {
      coltype = 3;

      if (has_tcolor==0 && colors<256)
        col[colors++].a = 0;

      qsort(&col[0], colors, sizeof(COLORS), cmp_colors);

      palsize = colors;
      for (i=0; i<colors; i++)
      {
        palette[i].r = col[i].r;
        palette[i].g = col[i].g;
        palette[i].b = col[i].b;
        trns[i]      = col[i].a;
        if (trns[i] != 255) trnssize = i+1;
      }

      for (i=0; i<frames; i++)
      {
        sp = dp = img[i].p;
        for (j=0; j<size; j++)
        {
          g = *sp++;
          a = *sp++;
          for (k=0; k<colors; k++)
            if (col[k].g == g && col[k].a == a)
              break;
          *dp++ = k;
        }
      }
    }
  }
  else
  if (coltype == 3)
  {
    int simple_trans = 1;
    int grayscale = 1;

    for (c=0; c<img[0].ps; c++)
    {
      col[c].r = img[0].pl[c].r;
      col[c].g = img[0].pl[c].g;
      col[c].b = img[0].pl[c].b;
      col[c].a = img[0].tr[c];
    }

    for (i=0; i<frames; i++)
    {
      sp = img[i].p;
      for (j=0; j<size; j++)
        col[*sp++].num++;
    }

    for (i=0; i<256; i++)
    if (col[i].num != 0)
    {
      if (col[i].a != 0)
      {
        if (col[i].a != 255)
          simple_trans = 0;
        else
        if (col[i].r != col[i].g || col[i].g != col[i].b)
          grayscale = 0;
        else
          gray[col[i].g] = 1;
      }
      else
        has_tcolor = 1;
    }

    if (grayscale && simple_trans && !keep_coltype) /* 3 -> 0 */
    {
      for (i=0; i<256; i++)
      if (gray[i] == 0)
      {
        trns[0] = 0;
        trns[1] = i;
        trnssize = 2;
        break;
      }
      if (!has_tcolor)
      {
        coltype = 0;
        for (i=0; i<frames; i++)
        {
          sp = img[i].p;
          for (j=0; j<size; j++, sp++)
            *sp = img[i].pl[*sp].g;
        }
      }
      else
      if (trnssize != 0)
      {
        coltype = 0;
        for (i=0; i<frames; i++)
        {
          sp = img[i].p;
          for (j=0; j<size; j++, sp++)
          {
            if (col[*sp].a == 0)
              *sp = trns[1];
            else
              *sp = img[i].pl[*sp].g;
          }
        }
      }
    }
    else
    if (!keep_palette)                 /* 3 -> 3 */
    {
      for (i=0; i<256; i++)
      if (col[i].num == 0)
      {
        col[i].a = 255;
        if (!has_tcolor)
        {
          col[i].a = 0;
          has_tcolor = 1;
        }
      }

      qsort(&col[0], 256, sizeof(COLORS), cmp_colors);

      for (i=0; i<256; i++)
      {
        palette[i].r = col[i].r;
        palette[i].g = col[i].g;
        palette[i].b = col[i].b;
        trns[i]      = col[i].a;
        if (col[i].num != 0)
          palsize = i+1;
        if (trns[i] != 255)
          trnssize = i+1;
      }

      for (i=0; i<frames; i++)
      {
        sp = img[i].p;
        for (j=0; j<size; j++)
        {
          r = img[i].pl[*sp].r;
          g = img[i].pl[*sp].g;
          b = img[i].pl[*sp].b;
          a = img[i].tr[*sp];

          for (k=0; k<palsize; k++)
            if (col[k].r == r && col[k].g == g && col[k].b == b && col[k].a == a)
              break;
          *sp++ = k;
        }
      }
    }
    else
    {
      palsize = img[0].ps;
      trnssize = img[0].ts;
      for (i=0; i<palsize; i++)
      {
        palette[i].r = col[i].r;
        palette[i].g = col[i].g;
        palette[i].b = col[i].b;
      }
      for (i=0; i<trnssize; i++)
        trns[i] = col[i].a;
    }
  }
  else
  if (coltype == 0)  /* 0 -> 0 */
  {
    if (img[0].ts != 0)
    {
      memcpy(trns, img[0].tr, img[0].ts);
      trnssize = img[0].ts;
    }
    else
    {
      for (i=0; i<frames; i++)
      {
        sp = img[i].p;
        for (j=0; j<size; j++)
          gray[*sp++] = 1;
      }
      for (i=0; i<256; i++)
      if (gray[i] == 0)
      {
        trns[0] = 0;
        trns[1] = i;
        trnssize = 2;
        break;
      }
    }
  }
  /* Downconvert optimizations - end */

  bpp = (coltype == 6) ? 4 : (coltype == 2) ? 3 : (coltype == 4) ? 2 : 1;
  has_tcolor = 0;
  if (coltype == 0)
  {
    if (trnssize)
    {
      has_tcolor = 1;
      tcolor = trns[1];
    }
  }
  else
  if (coltype == 2)
  {
    if (trnssize)
    {
      has_tcolor = 1;
      tcolor = (((trns[5]<<8)+trns[3])<<8)+trns[1];
    }
  }
  else
  if (coltype == 3)
  {
    for (i=0; i<trnssize; i++)
    if (trns[i] == 0)
    {
      has_tcolor = 1;
      tcolor = i;
      break;
    }
  }
  else
  {
    has_tcolor = 1;
    tcolor = 0;
  }

  rowbytes  = width * bpp;
  imagesize = rowbytes * height;

  temp  = new unsigned char[imagesize];
  over1 = new unsigned char[imagesize];
  over2 = new unsigned char[imagesize];
  over3 = new unsigned char[imagesize];
  rest  = new unsigned char[imagesize];
  rows  = new unsigned char[(rowbytes + 1) * height];

  SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETRANGE, 0, MAKELPARAM(0, 48*frames));
  SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, frames, 0);
  SetDlgItemText(hMainDlg, IDC_PERCENT, L"2 %");

  /* APNG encoding - start */
  if ((f = _wfopen(szOut, L"wb")) != 0)
  {
    unsigned char buf_IHDR[13];
    unsigned char buf_acTL[8];
    unsigned char buf_fcTL[26];

    png_save_uint_32(buf_IHDR, width);
    png_save_uint_32(buf_IHDR + 4, height);
    buf_IHDR[8] = 8;
    buf_IHDR[9] = coltype;
    buf_IHDR[10] = 0;
    buf_IHDR[11] = 0;
    buf_IHDR[12] = 0;

    png_save_uint_32(buf_acTL, frames-first);
    png_save_uint_32(buf_acTL + 4, loops);

    fwrite(png_sign, 1, 8, f);

    write_chunk(f, "IHDR", buf_IHDR, 13);

    if (frames > 1)
      write_chunk(f, "acTL", buf_acTL, 8);
    else
      first = 0;

    if (palsize > 0)
      write_chunk(f, "PLTE", (unsigned char *)(&palette), palsize*3);

    if (trnssize > 0)
      write_chunk(f, "tRNS", trns, trnssize);

    op_zstream1.data_type = Z_BINARY;
    op_zstream1.zalloc = Z_NULL;
    op_zstream1.zfree = Z_NULL;
    op_zstream1.opaque = Z_NULL;
    deflateInit2(&op_zstream1, Z_BEST_SPEED+1, 8, 15, 8, Z_DEFAULT_STRATEGY);

    op_zstream2.data_type = Z_BINARY;
    op_zstream2.zalloc = Z_NULL;
    op_zstream2.zfree = Z_NULL;
    op_zstream2.opaque = Z_NULL;
    deflateInit2(&op_zstream2, Z_BEST_SPEED+1, 8, 15, 8, Z_FILTERED);

    idat_size = (rowbytes + 1) * height;
    zbuf_size = idat_size + ((idat_size + 7) >> 3) + ((idat_size + 63) >> 6) + 11;

    zbuf = new unsigned char[zbuf_size];
    op_zbuf1 = new unsigned char[zbuf_size];
    op_zbuf2 = new unsigned char[zbuf_size];
    row_buf = new unsigned char[rowbytes + 1];
    sub_row = new unsigned char[rowbytes + 1];
    up_row = new unsigned char[rowbytes + 1];
    avg_row = new unsigned char[rowbytes + 1];
    paeth_row = new unsigned char[rowbytes + 1];

    row_buf[0] = 0;
    sub_row[0] = 1;
    up_row[0] = 2;
    avg_row[0] = 3;
    paeth_row[0] = 4;

    x0 = 0;
    y0 = 0;
    w0 = width;
    h0 = height;
    bop = 0;
    dop = 0;
    next_seq_num = 0;

    for (j=0; j<6; j++)
      op[j].valid = 0;
    deflate_rect_op(img[0].p, x0, y0, w0, h0, bpp, rowbytes, zbuf_size, 0);
    deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, 0);

    if (first)
    {
      write_IDATs(f, 0, zbuf, zsize, idat_size);

      for (j=0; j<6; j++)
        op[j].valid = 0;
      deflate_rect_op(img[1].p, x0, y0, w0, h0, bpp, rowbytes, zbuf_size, 0);
      deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, 0);
    }

    for (i=first; i<frames-1; i++)
    {
      unsigned int op_min;
      int          op_best;

      for (j=0; j<6; j++)
        op[j].valid = 0;

      /* dispose = none */
      get_rect(width, height, img[i].p, img[i+1].p, over1, bpp, rowbytes, zbuf_size, has_tcolor, tcolor, 0);

      /* dispose = background */
      if (has_tcolor)
      {
        memcpy(temp, img[i].p, imagesize);
        if (coltype == 2)
          for (j=0; j<h0; j++)
            for (k=0; k<w0; k++)
              memcpy(temp + ((j+y0)*width + (k+x0))*3, &tcolor, 3);
        else
          for (j=0; j<h0; j++)
            memset(temp + ((j+y0)*width + x0)*bpp, tcolor, w0*bpp);

        get_rect(width, height, temp, img[i+1].p, over2, bpp, rowbytes, zbuf_size, has_tcolor, tcolor, 1);
      }

      /* dispose = previous */
      if (i > first)
        get_rect(width, height, rest, img[i+1].p, over3, bpp, rowbytes, zbuf_size, has_tcolor, tcolor, 2);

      op_min = op[0].size;
      op_best = 0;
      for (j=1; j<6; j++)
      if (op[j].valid)
      {
        if (op[j].size < op_min)
        {
          op_min = op[j].size;
          op_best = j;
        }
      }

      dop = op_best >> 1;

      png_save_uint_32(buf_fcTL, next_seq_num++);
      png_save_uint_32(buf_fcTL + 4, w0);
      png_save_uint_32(buf_fcTL + 8, h0);
      png_save_uint_32(buf_fcTL + 12, x0);
      png_save_uint_32(buf_fcTL + 16, y0);
      png_save_uint_16(buf_fcTL + 20, img[i].num);
      png_save_uint_16(buf_fcTL + 22, img[i].den);
      buf_fcTL[24] = dop;
      buf_fcTL[25] = bop;
      write_chunk(f, "fcTL", buf_fcTL, 26);

      write_IDATs(f, i, zbuf, zsize, idat_size);

      /* process apng dispose - begin */
      if (dop != 2)
        memcpy(rest, img[i].p, imagesize);

      if (dop == 1)
      {
        if (coltype == 2)
          for (j=0; j<h0; j++)
            for (k=0; k<w0; k++)
              memcpy(rest + ((j+y0)*width + (k+x0))*3, &tcolor, 3);
        else
          for (j=0; j<h0; j++)
            memset(rest + ((j+y0)*width + x0)*bpp, tcolor, w0*bpp);
      }
      /* process apng dispose - end */

      x0 = op[op_best].x;
      y0 = op[op_best].y;
      w0 = op[op_best].w;
      h0 = op[op_best].h;
      bop = op_best & 1;

      deflate_rect_fin(deflate_method, iter, zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, op_best);

      SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, frames+46*i, 0);
      _snwprintf(szBuf, 8, L"%d %%\0", (frames+46*i)*100/(48*frames));
      SetDlgItemText(hMainDlg, IDC_PERCENT, szBuf);
    }

    if (frames > 1)
    {
      png_save_uint_32(buf_fcTL, next_seq_num++);
      png_save_uint_32(buf_fcTL + 4, w0);
      png_save_uint_32(buf_fcTL + 8, h0);
      png_save_uint_32(buf_fcTL + 12, x0);
      png_save_uint_32(buf_fcTL + 16, y0);
      png_save_uint_16(buf_fcTL + 20, img[frames-1].num);
      png_save_uint_16(buf_fcTL + 22, img[frames-1].den);
      buf_fcTL[24] = 0;
      buf_fcTL[25] = bop;
      write_chunk(f, "fcTL", buf_fcTL, 26);
    }

    write_IDATs(f, frames-1, zbuf, zsize, idat_size);

    write_chunk(f, "tEXt", png_Software, 27);
    write_chunk(f, "IEND", 0, 0);
    fclose(f);

    res = 0;
    SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 48*frames, 0);
    SetDlgItemText(hMainDlg, IDC_PERCENT, L"100 %");

    delete[] zbuf;
    delete[] op_zbuf1;
    delete[] op_zbuf2;
    delete[] row_buf;
    delete[] sub_row;
    delete[] up_row;
    delete[] avg_row;
    delete[] paeth_row;

    deflateEnd(&op_zstream1);
    deflateEnd(&op_zstream2);
  }
  else
  {
    swprintf(szBuf, MAX_PATH, L"Error: can't open file for writing:    \n\n%s  \n ", szOut);
    MessageBox(hMainDlg, szBuf, 0, MB_ICONINFORMATION);
    SendDlgItemMessage(hMainDlg, IDC_PROGRESS1, PBM_SETPOS, 0, 0);
  }
  /* APNG encoding - end */

  delete[] temp;
  delete[] over1;
  delete[] over2;
  delete[] over3;
  delete[] rest;
  delete[] rows;

  for (i=0; i<frames; i++)
  {
    delete[] img[i].p;
    delete[] img[i].rows;
  }

  delete[] img;

  return res;
}
