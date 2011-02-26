/* APNG Assembler 2.3
 *
 * Copyright (c) 2009,2010 Max Stepin
 * maxst at users.sourceforge.net
 *
 * GNU LGPL information
 * --------------------
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#define FIREFOX_BUG_546272_WORKAROUND

#define PNG_ZBUF_SIZE  32768

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"     /* original (unpatched) libpng is ok */
#include "zlib.h"

#if defined(_MSC_VER) && _MSC_VER >= 1300
#define swap16(data) _byteswap_ushort(data)
#define swap32(data) _byteswap_ulong(data)
#elif defined(__linux__)
#include <byteswap.h>
#define swap16(data) bswap_16(data)
#define swap32(data) bswap_32(data)
#elif defined(__FreeBSD__)
#include <sys/endian.h>
#define swap16(data) bswap16(data)
#define swap32(data) bswap32(data)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define swap16(data) OSSwapInt16(data)
#define swap32(data) OSSwapInt32(data)
#else
unsigned short swap16(unsigned short data) {return((data & 0xFF) << 8) | ((data >> 8) & 0xFF);}
unsigned int swap32(unsigned int data) {return((data & 0xFF) << 24) | ((data & 0xFF00) << 8) | ((data >> 8) & 0xFF00) | ((data >> 24) & 0xFF);}
#endif

typedef struct { z_stream zstream; unsigned char * zbuf; int x, y, w, h, valid; } OP;
typedef struct { unsigned int i, num, trans; } STATS;
typedef struct rgb_struct { unsigned char r, g, b; } rgb;

OP    op[12];
STATS stats[256];

unsigned int next_seq_num = 0;
unsigned char * row_buf;
unsigned char * sub_row;
unsigned char * up_row;
unsigned char * avg_row;
unsigned char * paeth_row;
unsigned char png_sign[8] = {137,  80,  78,  71,  13,  10,  26,  10};
unsigned char png_Software[27] = { 83, 111, 102, 116, 119, 97, 114, 101, '\0', 
                                   65,  80,  78,  71,  32, 65, 115, 115, 101, 
                                  109,  98, 108, 101, 114, 32,  50,  46,  51};

int cmp_stats( const void *arg1, const void *arg2 )
{
  if ( ((STATS*)arg2)->trans == ((STATS*)arg1)->trans )
    return ((STATS*)arg2)->num - ((STATS*)arg1)->num;
  else
    return ((STATS*)arg2)->trans - ((STATS*)arg1)->trans;
}

unsigned char * LoadPNG(char * szImage, int *pWidth, int *pHeight, int *pDepth, int *pType, rgb *pPal, int *pPsize, unsigned char *pTrns, int *pTsize, int *pRes)
{
  FILE          * f;
  unsigned char * image_data = NULL;

  *pRes = 0;

  if ((f = fopen(szImage, "rb")) != 0)
  {
    png_structp     png_ptr;
    png_infop       info_ptr;
    png_bytepp      row_pointers = NULL;
    png_uint_32     width, height, i, rowbytes;
    int             depth, coltype;
    unsigned char   sig[8];

    if (fread(sig, 1, 8, f) != 8) return NULL;

    if (png_sig_cmp(sig, 0, 8) == 0)
    {
      png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

      if (png_ptr != NULL)
      {
        info_ptr = png_create_info_struct(png_ptr);

        if (info_ptr != NULL) 
        {
          if (setjmp(png_jmpbuf(png_ptr)) == 0)
          {
            png_init_io(png_ptr, f);
            png_set_sig_bytes(png_ptr, 8);
            png_read_info(png_ptr, info_ptr);
            png_get_IHDR(png_ptr, info_ptr, &width, &height, &depth, &coltype, NULL, NULL, NULL);
            *pWidth  = width;
            *pHeight = height;
            *pDepth  = depth;
            *pType   = coltype;

            if (png_get_valid(png_ptr, info_ptr, PNG_INFO_PLTE))
            {
              png_colorp     palette;

              png_get_PLTE(png_ptr, info_ptr, &palette, pPsize);
              memcpy(pPal, palette, *pPsize * 3);
            }
            else
              *pPsize = 0;

            if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
            {
              png_color_16p  trans_color;
              png_bytep      trans_alpha;

              png_get_tRNS(png_ptr, info_ptr, &trans_alpha, pTsize, &trans_color);

              if (coltype == PNG_COLOR_TYPE_GRAY)
              {
                if (depth == 16)
                  trans_color->gray >>= 8;

                pTrns[0] = 0;
                pTrns[1] = trans_color->gray & 0xFF;
                *pTsize = 2;
              }
              else
              if (coltype == PNG_COLOR_TYPE_RGB)
              {
                if (depth == 16)
                {
                  trans_color->red >>= 8;
                  trans_color->green >>= 8;
                  trans_color->blue >>= 8;
                }
                pTrns[0] = 0;
                pTrns[1] = trans_color->red & 0xFF;
                pTrns[2] = 0;
                pTrns[3] = trans_color->green & 0xFF;
                pTrns[4] = 0;
                pTrns[5] = trans_color->blue & 0xFF;
                *pTsize = 6;
              }
              else
                memcpy(pTrns, trans_alpha, *pTsize);
            }
            else
              *pTsize = 0;

            if (depth > 8)
              png_set_strip_16(png_ptr);

            if (depth < 8)
            {
              if (coltype == PNG_COLOR_TYPE_GRAY)
                png_set_expand_gray_1_2_4_to_8(png_ptr);
              else
                png_set_packing(png_ptr);
            }

            png_read_update_info(png_ptr, info_ptr);
            *pDepth  = png_get_bit_depth(png_ptr, info_ptr);

            rowbytes = png_get_rowbytes(png_ptr, info_ptr);

            if ((image_data = (unsigned char *)malloc(rowbytes*height)) != NULL) 
            {
              if ((row_pointers = (png_bytepp)malloc(height*sizeof(png_bytep))) != NULL) 
              {
                for (i=0; i<height; i++)
                  row_pointers[i] = image_data + i*rowbytes;

                png_read_image(png_ptr, row_pointers);
                free(row_pointers);
                png_read_end(png_ptr, NULL);
                png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
              }
              else
              {
                png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
                free(image_data);
                *pRes = 7;
              }
            }
            else
            {
              png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
              *pRes = 6;
            }
          }
          else
          {
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            *pRes = 5;
          }
        }
        else
        {
          png_destroy_read_struct(&png_ptr, NULL, NULL);
          *pRes = 4;
        }
      }
      else
        *pRes = 3;
    }
    else
      *pRes = 2;

    fclose(f);
  }
  else
    *pRes = 1;

  return image_data;
}

unsigned char * LoadTGA(char * szImage, int *pWidth, int *pHeight, int *pDepth, int *pType, rgb *pPal, int *pPsize, unsigned char *pTrns, int *pTsize, int *pRes)
{
  FILE          * f;
  unsigned char * image_data = NULL;

  *pRes = 0;

  if ((f = fopen(szImage, "rb")) != 0)
  {
    int          i, j, compr;
    unsigned int k, n, rowbytes;
    unsigned char c;
    unsigned char col[4];
    unsigned char fh[18];
    unsigned char * pRow;

    if (fread(&fh, 1, 18, f) != 18) return NULL;

    *pWidth  = fh[12] + fh[13]*256;
    *pHeight = fh[14] + fh[15]*256;
    *pDepth  = 8;
    *pType   = -1;
    *pPsize  = 0;
    *pTsize  = 0;

    rowbytes = *pWidth;
    if ((fh[2] & 7) == 3)
      *pType = 0;
    else
    if (((fh[2] & 7) == 2) && (fh[16] == 24))
    {
      *pType = 2;
      rowbytes = *pWidth * 3;
    }
    else
    if (((fh[2] & 7) == 2) && (fh[16] == 32))
    {
      *pType = 6;
      rowbytes = *pWidth * 4;
    }
    else
    if (((fh[2] & 7) == 1) && (fh[1] == 1) && (fh[7] == 24))
      *pType = 3;

    compr = fh[2] & 8;

    if (*pType >= 0)
    {
      if ((image_data = (unsigned char *)malloc(*pHeight * rowbytes)) != NULL)
      {
        if (fh[0] != 0)
          fseek( f, fh[0], SEEK_CUR );

        if (fh[1] == 1)
        {
          int start = fh[3] + fh[4]*256;
          int size  = fh[5] + fh[6]*256;

          for (i=start; i<start+size && i<256; i++)
          {
            if (fread(&col, 1, 3, f) != 3) return NULL;
            pPal[i].r = col[2];
            pPal[i].g = col[1];
            pPal[i].b = col[0];
          }
          *pPsize = i;

          if (start+size > 256)
            fseek( f, (start+size-256)*3, SEEK_CUR );
        }

        if ((fh[17] & 0x20) == 0)
          pRow = image_data+(*pHeight-1)*rowbytes;
        else
          pRow = image_data;

        for (j=0; j<*pHeight; j++)
        {
          if (compr == 0)
          {
            if (*pType == 6)
            {
              for (i=0; i<*pWidth; i++)
              {
                if (fread(&col, 1, 4, f) != 4) return NULL;
                pRow[i*4]   = col[2];
                pRow[i*4+1] = col[1];
                pRow[i*4+2] = col[0];
                pRow[i*4+3] = col[3];
              }
            }
            else
            if (*pType == 2)
            {
              for (i=0; i<*pWidth; i++)
              {
                if (fread(&col, 1, 3, f) != 3) return NULL;
                pRow[i*3]   = col[2];
                pRow[i*3+1] = col[1];
                pRow[i*3+2] = col[0];
              }
            }
            else
              if (fread(pRow, 1, rowbytes, f) != rowbytes) return NULL;
          }
          else
          {
            i = 0;
            while (i<*pWidth)
            {
              if (fread(&c, 1, 1, f) != 1) return NULL;
              n = (c & 0x7F)+1;

              if ((c & 0x80) != 0)
              {
                if (*pType == 6)
                {
                  if (fread(&col, 1, 4, f) != 4) return NULL;
                  for (k=0; k<n; k++)
                  {
                    pRow[(i+k)*4]   = col[2];
                    pRow[(i+k)*4+1] = col[1];
                    pRow[(i+k)*4+2] = col[0];
                    pRow[(i+k)*4+3] = col[3];
                  }
                }
                else
                if (*pType == 2)
                {
                  if (fread(&col, 1, 3, f) != 3) return NULL;
                  for (k=0; k<n; k++)
                  {
                    pRow[(i+k)*3]   = col[2];
                    pRow[(i+k)*3+1] = col[1];
                    pRow[(i+k)*3+2] = col[0];
                  }
                }
                else
                {
                  if (fread(&col, 1, 1, f) != 1) return NULL;
                  memset(pRow+i, col[0], n);
                }
              }
              else
              {
                if (*pType == 6)
                {
                  for (k=0; k<n; k++)
                  {
                    if (fread(&col, 1, 4, f) != 4) return NULL;
                    pRow[(i+k)*4]   = col[2];
                    pRow[(i+k)*4+1] = col[1];
                    pRow[(i+k)*4+2] = col[0];
                    pRow[(i+k)*4+3] = col[3];
                  }
                }
                else
                if (*pType == 2)
                {
                  for (k=0; k<n; k++)
                  {
                    if (fread(&col, 1, 3, f) != 3) return NULL;
                    pRow[(i+k)*3]   = col[2];
                    pRow[(i+k)*3+1] = col[1];
                    pRow[(i+k)*3+2] = col[0];
                  }
                }
                else
                  if (fread(pRow+i, 1, n, f) != n) return NULL;
              }
              i+=n;
            }
          }
          if ((fh[17] & 0x20) == 0)
            pRow -= rowbytes;
          else
            pRow += rowbytes;
        }
      }
      else
        *pRes = 3;
    }
    else
      *pRes = 2;

    fclose(f);
  }
  else
    *pRes = 1;

  return image_data;
}

void write_chunk(FILE * f, const char * name, unsigned char * data, unsigned int length)
{
  unsigned int crc = crc32(0, Z_NULL, 0);
  unsigned int len = swap32(length);

  fwrite(&len, 1, 4, f);
  fwrite(name, 1, 4, f);
  crc = crc32(crc, (const Bytef *)name, 4);

  if (memcmp(name, "fdAT", 4) == 0)
  {
    unsigned int seq = swap32(next_seq_num++);
    fwrite(&seq, 1, 4, f);
    crc = crc32(crc, (const Bytef *)(&seq), 4);
    length -= 4;
  }

  if (data != NULL && length > 0)
  {
    fwrite(data, 1, length, f);
    crc = crc32(crc, data, length);
  }

  crc = swap32(crc);
  fwrite(&crc, 1, 4, f);
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
    if (ds > PNG_ZBUF_SIZE)
      ds = PNG_ZBUF_SIZE;

    if (frame == 0)
      write_chunk(f, "IDAT", data, ds);
    else
      write_chunk(f, "fdAT", data, ds+4);

    data += ds;
    length -= ds;
  }
}

int get_rect(int w, int h, unsigned char *pimg1, unsigned char *pimg2, unsigned char *ptemp, int *px, int *py, int *pw, int *ph, int bpp, unsigned int has_tcolor, unsigned int tcolor)
{
  int   i, j;
  int   x_min = w-1;
  int   y_min = h-1;
  int   x_max = 0;
  int   y_max = 0;
  int   diffnum = 0;
  int   over_is_possible = 1;

  if (!has_tcolor)
    over_is_possible = 0;

  if (bpp == 1)
  {
    unsigned char *pa = pimg1;
    unsigned char *pb = pimg2;
    unsigned char *pc = ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      unsigned char c = *pb++;
      if (*pa++ != c)
      {
        diffnum++;
        if ((has_tcolor) && (c == tcolor)) over_is_possible = 0;
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
    unsigned short *pa = (unsigned short *)pimg1;
    unsigned short *pb = (unsigned short *)pimg2;
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
    unsigned char *pa = pimg1;
    unsigned char *pb = pimg2;
    unsigned char *pc = ptemp;

    for (j=0; j<h; j++)
    for (i=0; i<w; i++)
    {
      unsigned int c1 = (((pa[2]<<8)+pa[1])<<8)+pa[0];
      unsigned int c2 = (((pb[2]<<8)+pb[1])<<8)+pb[0];
      if (c1 != c2)
      {
        diffnum++;
        if ((has_tcolor) && (c2 == tcolor)) over_is_possible = 0;
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
    unsigned int *pa = (unsigned int *)pimg1;
    unsigned int *pb = (unsigned int *)pimg2;
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
    *px = *py = 0;
    *pw = *ph = 1; 
  }
  else
  {
    *px = x_min;
    *py = y_min;
    *pw = x_max-x_min+1;
    *ph = y_max-y_min+1;
  }

  return over_is_possible;
}

void deflate_rect(unsigned char *pdata, int x, int y, int w, int h, int bpp, int stride, int zbuf_size, int n)
{
  int i, j, v;
  int a, b, c, pa, pb, pc, p;
  int rowbytes = w * bpp;
  unsigned char * prev = NULL;
  unsigned char * row  = pdata + y*stride + x*bpp;
  unsigned char * out;

  op[n*2].valid = 1;
  op[n*2].zstream.next_out = op[n*2].zbuf;
  op[n*2].zstream.avail_out = zbuf_size;

  op[n*2+1].valid = 1;
  op[n*2+1].zstream.next_out = op[n*2+1].zbuf;
  op[n*2+1].zstream.avail_out = zbuf_size;

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

    op[n*2].zstream.next_in = row_buf;
    op[n*2].zstream.avail_in = rowbytes + 1;
    deflate(&op[n*2].zstream, Z_NO_FLUSH);

    op[n*2+1].zstream.next_in = best_row;
    op[n*2+1].zstream.avail_in = rowbytes + 1;
    deflate(&op[n*2+1].zstream, Z_NO_FLUSH);

    prev = row;
    row += stride;
  }

  deflate(&op[n*2].zstream, Z_FINISH);
  deflate(&op[n*2+1].zstream, Z_FINISH);

  op[n*2].x = op[n*2+1].x = x;
  op[n*2].y = op[n*2+1].y = y;
  op[n*2].w = op[n*2+1].w = w;
  op[n*2].h = op[n*2+1].h = h;
}

int main(int argc, char** argv)
{
  char  * szOutput;
  char  * szImage;
  char  * szOpt;
  char    szFormat[256];
  char    szNext[256];
  int     i, j, k, len, n, c;
  int     bpp, rowbytes, imagesize, idat_size, zbuf_size, zsize;
  int     x0, y0, w0, h0, x1, y1, w1, h1, try_over;
  int     has_tcolor, tcolor;
  int     input_ext = 0;
  int     width = 0;
  int     height = 0;
  int     depth = 0;
  int     coltype = 0;
  int     cur = 0;
  int     first = 0;
  int     frames = 0;
  int     loops = 0;
  int     palsize = 0;
  int     trnssize = 0;
  int     keep_palette = 0;
  int     keep_coltype = 0;
  short   delay_num = -1;
  short   delay_den = -1;
  rgb              palette[256];
  unsigned char    trns[256];
  unsigned char    bits[256];
  unsigned char    dop, bop, r, g, b;
  unsigned char  * zbuf;
  unsigned char  * imagetemp;
  char           * szExt;
  unsigned char ** images;
  unsigned char  * bigcube;
  unsigned char  * sp;
  unsigned char  * dp;
  FILE * f;

  printf("\nAPNG Assembler 2.3\n\n");

  if (argc <= 2)
  {
    printf("Usage   : apngasm output.png frame001.png [options]\n"
           "          apngasm output.png frame*.png   [options]\n\n"
           "Options :\n"
           "1 10    : frame delay is 1/10 sec. (default)\n"
           "/l2     : 2 loops (default is 0, forever)\n"
           "/f      : skip the first frame\n"
           "/kp     : keep palette\n"
           "/kc     : keep color type\n");
    return 1;
  }

  szOutput = argv[1];
  szImage  = argv[2];

  for (i=3; i<argc; i++)
  {
    szOpt = argv[i];

    if (szOpt[0] == '/' || szOpt[0] == '-')
    {
      if (szOpt[1] == 'f' || szOpt[1] == 'F')
        first = 1;
      else
      if (szOpt[1] == 'l' || szOpt[1] == 'L')
        loops = atoi(szOpt+2);
      else
      if (szOpt[1] == 'k' || szOpt[1] == 'K')
      {
        if (szOpt[2] == 'p' || szOpt[2] == 'P')
          keep_palette = 1;
        else
        if (szOpt[2] == 'c' || szOpt[2] == 'C')
          keep_coltype = 1;
      }
    }
    else
    {
      short n = atoi(szOpt);
      if ((n != 0) || (strcmp(szOpt, "0") == 0))
      {
        if (delay_num == -1) delay_num = n;
        else
        if (delay_den == -1) delay_den = n;
      }
    }
  }

  if (delay_num <= 0) delay_num = 1;
  if (delay_den <= 0) delay_den = 10;

  len = strlen(szImage);
  szExt = szImage + len - 4;

  if ((len>4) && (szExt[0]=='.') && (szExt[1]=='p' || szExt[1]=='P') && (szExt[2]=='n' || szExt[2]=='N') && (szExt[3]=='g' || szExt[3]=='G'))
    input_ext = 1;

  if ((len>4) && (szExt[0]=='.') && (szExt[1]=='t' || szExt[1]=='T') && (szExt[2]=='g' || szExt[2]=='G') && (szExt[3]=='a' || szExt[3]=='A'))
    input_ext = 2;

  if (input_ext == 0)
  {
    printf( "Error: '.png' or '.tga' extension expected\n" );
    return 1;
  }

  if (*(szExt-1) == '*')
  {
    f = 0;
    for (i=1; i<6; i++)
    {
      strcpy(szFormat, szImage);
      sprintf(szFormat+len-5, "%%0%dd%s", i, szExt);
      cur = 0;
      sprintf(szNext, szFormat, cur);
      if ((f = fopen(szNext, "rb")) != 0) break;
      cur = 1;
      sprintf(szNext, szFormat, cur);
      if ((f = fopen(szNext, "rb")) != 0) break;
    }

    if (f != 0)
      fclose(f);
    else
    {
      printf( "Error: *%s sequence not found\n", szExt );
      return 1;
    }
  }
  else
  {
    for (i=1; i<6; i++)
    {
      if (*(szExt-i) < '0') break;
      if (*(szExt-i) > '9') break;
      if (szImage == szExt-i) break;
    }

    if (i == 1)
    {
      printf( "Error: *%s sequence not found\n", szExt );
      return 1;
    }
    cur = atoi(szExt-i+1);
    strcpy(szFormat, szImage);
    sprintf(szFormat+len-3-i, "%%0%dd%s", i-1, szExt);
    strcpy(szNext, szImage);
  }

  frames = 0;

  for (i=0; i<256; i++)
  {
    palette[i].r = palette[i].g = palette[i].b = i;
    trns[i] = 255;
    stats[i].i = i;
    stats[i].num = 0;
    stats[i].trans = 0;
  }

  if ((f = fopen(szNext, "rb")) == 0)
  {
    printf("Error: can't open the file '%s'", szNext);
    return 1;
  }

  do
  {
    frames++;
    fclose(f);
    sprintf(szNext, szFormat, cur+frames);
    f = fopen(szNext, "rb");
  } 
  while (f != 0);

  images = (unsigned char **)malloc(frames*sizeof(unsigned char *));
  bigcube = (unsigned char *)malloc(256*256*32);
  if (!images || !bigcube)
  {
    printf( "Error: not enough memory\n" );
    return 1;
  }

  memset(bigcube, 0, 256*256*32);

  for (i=0; i<256; i++)
    bits[i] = ((i>>7)&1) + ((i>>6)&1) + ((i>>5)&1) + ((i>>4)&1) + ((i>>3)&1) + ((i>>2)&1) + ((i>>1)&1) + (i&1);

  for (i=0; i<frames; i++)
  {
    int w, h, d, t, res;
    int ps, ts;
    rgb           pl[256];
    unsigned char tr[256];

    sprintf(szNext, szFormat, cur+i);
    printf("reading %s (%d of %d)\n", szNext, i-first+1, frames-first);

    if (input_ext == 1)
      images[i] = LoadPNG(szNext, &w, &h, &d, &t, &pl[0], &ps, &tr[0], &ts, &res);
    else
      images[i] = LoadTGA(szNext, &w, &h, &d, &t, &pl[0], &ps, &tr[0], &ts, &res);

    if (images[i] == NULL)
    {
      printf("Error: Load%s(%s) failed\n", (input_ext == 1) ? "PNG" : "TGA", szNext);
      return res;
    }

    if (i == 0)
    {
      width = w;
      height = h;
      depth = d;
      coltype = t;
      palsize = ps;
      trnssize = ts;
      memset(trns, 255, 256);
      if (ps) memcpy(palette, pl, ps*3);
      if (ts) memcpy(trns, tr, ts);
    }
    else
    {
      if (width != w || height != h)
      {
        printf("Error at %s: different image size\n", szNext);
        return 1;
      }
      if (depth != d || coltype != t)
      {
        printf("Error at %s: different image type\n", szNext);
        return 1;
      }
      if (palsize < ps || memcmp(palette, pl, ps*3) != 0)
      {
        printf("Error at %s: different palette\n", szNext);
        return 1;
      }
      if (trnssize != ts || memcmp(trns, tr, ts) != 0)
      {
        printf("Error at %s: different tRNS chunk\n", szNext);
        return 1;
      }
    }
  }

  bpp = 1;
  if (coltype == 2)
    bpp = 3;
  else
  if (coltype == 4)
    bpp = 2;
  else
  if (coltype == 6)
    bpp = 4;

  rowbytes  = width * bpp;
  imagesize = rowbytes * height;
  idat_size = (rowbytes + 1) * height;
  zbuf_size = idat_size + ((idat_size + 7) >> 3) + ((idat_size + 63) >> 6) + 11;

  /* Optimizations  - start */
  has_tcolor = 0;
  tcolor = 0;

  if (coltype == 6)
  {
    int transparency = 0;
    int partial_transparency = 0;

    has_tcolor = 1;
    for (i=0; i<frames; i++)
    {
      sp = images[i];
      for (j=0; j<width*height; j++, sp+=4)
      {
        if (sp[3] != 255)
        {
          transparency = 1;
          if (sp[3] == 0)
           sp[0] = sp[1] = sp[2] = 0;
          else
            partial_transparency = 1;
        }
      }
    }

    if (!keep_coltype)
    {
      if (transparency == 0)
      {
        has_tcolor = 0;
        coltype = 2;
        bpp = 3;
        rowbytes  = width * bpp;
        imagesize = rowbytes * height;
        for (i=0; i<frames; i++)
        {
          sp = dp = images[i];
          for (j=0; j<width*height; j++)
          {
            *dp++ = *sp++;
            *dp++ = *sp++;
            *dp++ = *sp++;
            sp++;
          }
        }
      }
      else
      if (partial_transparency == 0)
      {
        int colors = 0;
        for (i=0; i<frames; i++)
        {
          sp = images[i];
          for (j=0; j<width*height; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            c = (b<<16) + (g<<8) + r;
            if (*sp++ != 0)
              bigcube[c>>3] |= (1<<(r & 7));
          }
        }
        sp = bigcube;
        for (i=0; i<256*256*32; i++)
          colors += bits[*sp++];

        if (colors + has_tcolor <= 256)
        {
          palette[0].r = palette[0].g = palette[0].b = trns[0] = 0;
          palsize = trnssize = 1;
          for (i=0; i<256*256*32; i++)
          if ((c = bigcube[i]) != 0)
          {
            for (n=0; n<8; n++, c>>=1)
            if (c&1)
            {
              palette[palsize].b = i>>13;
              palette[palsize].g = (i>>5)&255;
              palette[palsize].r = ((i&31)<<3) + n;
              palsize++;
            }
          }
          coltype = 3;
          bpp = 1;
          rowbytes  = width;
          imagesize = rowbytes * height;
          for (i=0; i<frames; i++)
          {
            sp = dp = images[i];
            for (j=0; j<width*height; j++)
            {
              r = *sp++;
              g = *sp++;
              b = *sp++;
              if (*sp++ != 0)
              {
                for (c=1; c<palsize; c++)
                  if (r==palette[c].r && g==palette[c].g && b==palette[c].b)
                    break;
                *dp++ = c;
              }
              else
                *dp++ = 0;
            }
          }
        }
      }
    }
  }

  if (coltype == 2)
  {
    int colors = 0;
    if (trnssize)
    {
      has_tcolor = 1;
      tcolor = (((trns[5]<<8)+trns[3])<<8)+trns[1];
    }

    for (i=0; i<frames; i++)
    {
      sp = images[i];
      for (j=0; j<width*height; j++)
      {
        r = *sp++;
        g = *sp++;
        b = *sp++;
        c = (b<<16) + (g<<8) + r;
        if (has_tcolor == 0 || c != tcolor)
          bigcube[c>>3] |= (1<<(r & 7));
      }
    }

    sp = bigcube;
    for (i=0; i<256*256*32; i++)
    {
      c = *sp++;
      if (has_tcolor == 0 && c == 0)
      {
        has_tcolor = 1;
        tcolor = i<<3;
        trnssize = 6;
        trns[0] = 0; trns[1] = (i&31)<<3;
        trns[2] = 0; trns[3] = (i>>5)&255;
        trns[4] = 0; trns[5] = i>>13;
      } 
      colors += bits[c];
    }

    if (!keep_coltype)
    {
      if (colors + has_tcolor <= 256)
      {
        palsize = trnssize = 0;
        trns[0] = trns[1] = trns[2] = trns[3] = trns[4] = trns[5] = 255;
        if (has_tcolor == 1)
        {
          palette[0].r = tcolor&255;
          palette[0].g = (tcolor>>8)&255;
          palette[0].b = (tcolor>>16)&255;
          trns[0] = 0;
          palsize = trnssize = 1;
        }
        for (i=0; i<256*256*32; i++)
        if ((c = bigcube[i]) != 0)
        {
          for (n=0; n<8; n++, c>>=1)
          if (c&1)
          {
            palette[palsize].b = i>>13;
            palette[palsize].g = (i>>5)&255;
            palette[palsize].r = ((i&31)<<3) + n;
            palsize++;
          }
        }
        coltype = 3;
        bpp = 1;
        rowbytes  = width;
        imagesize = rowbytes * height;
        for (i=0; i<frames; i++)
        {
          sp = dp = images[i];
          for (j=0; j<width*height; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            for (c=0; c<palsize; c++)
              if (r==palette[c].r && g==palette[c].g && b==palette[c].b)
                break;
            *dp++ = c;
          }
        }
      }
    }
  }

  if (coltype == 3)
  {
    for (i=0; i<trnssize; i++)
    if (trns[i] == 0)
    {
      stats[i].trans = 1;
      has_tcolor = 1;
      tcolor = i;
      break;
    }

    for (i=0; i<frames; i++)
    {
      sp = images[i];
      for (j=0; j<imagesize; j++)
        stats[*sp++].num++;
    }

    if (!has_tcolor)
    for (i=0; i<256; i++)
    if (stats[i].num == 0)
    {
      trns[i] = 0;
      stats[i].trans = 1;
      has_tcolor = 1;
      tcolor = i;
      break;
    }

    if (!keep_palette)
    {
      rgb           pl[256];
      unsigned char tr[256];
      unsigned int  sh[256];

      /* sort the palette, by color usage */
      qsort(&stats[0], 256, sizeof(STATS), cmp_stats);
      tcolor = 0;

      palsize = trnssize = 0;
      for (i=0; i<256; i++)
      {
        pl[i].r = palette[stats[i].i].r;
        pl[i].g = palette[stats[i].i].g;
        pl[i].b = palette[stats[i].i].b;
        tr[i]   = trns[stats[i].i];

        if (stats[i].num != 0)
          palsize = i+1;

        if ((tr[i] != 255) && (stats[i].num != 0 || i==0))
          trnssize = i+1;

        sh[stats[i].i] = i;
      }

      if (palsize) memcpy(palette, pl, palsize*3);
      if (trnssize) memcpy(trns, tr, trnssize);

      for (i=0; i<frames; i++)
      {
        sp = images[i];
        for (j=0; j<imagesize; j++)
          sp[j] = sh[sp[j]];
      }
    }
  }

  if (coltype == 4)
  {
    has_tcolor = 1;
    for (i=0; i<frames; i++)
    {
      sp = images[i];
      for (j=0; j<width*height; j++, sp+=2)
        if (sp[1] == 0)
          sp[0] = 0;
    }
  }

  if (coltype == 0)
  {
    if (trnssize)
    {
      has_tcolor = 1;
      tcolor = trns[1];
    }
  }
  /* Optimizations  - end */

  for (i=0; i<12; i++)
  {
    op[i].zstream.data_type = Z_BINARY;
    op[i].zstream.zalloc = Z_NULL;
    op[i].zstream.zfree = Z_NULL;
    op[i].zstream.opaque = Z_NULL;

    if (i & 1)
      deflateInit2(&op[i].zstream, Z_BEST_COMPRESSION, 8, 15, 8, Z_FILTERED);
    else
      deflateInit2(&op[i].zstream, Z_BEST_COMPRESSION, 8, 15, 8, Z_DEFAULT_STRATEGY);

    op[i].zbuf = (unsigned char *)malloc(zbuf_size);
    if (op[i].zbuf == NULL)
    {
      printf( "Error: not enough memory\n" );
      return 1;
    }
  }

  imagetemp = (unsigned char *)malloc(imagesize);
  zbuf = (unsigned char *)malloc(zbuf_size);
  row_buf = (unsigned char *)malloc(rowbytes + 1);
  sub_row = (unsigned char *)malloc(rowbytes + 1);
  up_row = (unsigned char *)malloc(rowbytes + 1);
  avg_row = (unsigned char *)malloc(rowbytes + 1);
  paeth_row = (unsigned char *)malloc(rowbytes + 1);

  if (imagetemp && zbuf && row_buf && sub_row && up_row && avg_row && paeth_row)
  {
    row_buf[0] = 0;
    sub_row[0] = 1;
    up_row[0] = 2;
    avg_row[0] = 3;
    paeth_row[0] = 4;
  }
  else
  {
    printf( "Error: not enough memory\n" );
    return 1;
  }

  if ((f = fopen(szOutput, "wb")) != 0)
  {
    struct IHDR 
    {
      unsigned int    mWidth;
      unsigned int    mHeight;
      unsigned char   mDepth;
      unsigned char   mColorType;
      unsigned char   mCompression;
      unsigned char   mFilterMethod;
      unsigned char   mInterlaceMethod;
    } ihdr = { swap32(width), swap32(height), depth, coltype, 0, 0, 0 };

    struct acTL 
    {
      unsigned int    mFrameCount;
      unsigned int    mLoopCount;
    } actl = { swap32(frames-first), swap32(loops) };

    struct fcTL 
    {
      unsigned int    mSeq;
      unsigned int    mWidth;
      unsigned int    mHeight;
      unsigned int    mXOffset;
      unsigned int    mYOffset;
      unsigned short  mDelayNum;
      unsigned short  mDelayDen;
      unsigned char   mDisposeOp;
      unsigned char   mBlendOp;
    } fctl;

    fwrite(png_sign, 1, 8, f);

    write_chunk(f, "IHDR", (unsigned char *)(&ihdr), 13);

    if (frames > 1)
      write_chunk(f, "acTL", (unsigned char *)(&actl), 8);
    else
      first = 0;

    if (palsize > 0)
      write_chunk(f, "PLTE", (unsigned char *)(&palette), palsize*3);

    if (trnssize > 0)
      write_chunk(f, "tRNS", trns, trnssize);

    x0 = 0;
    y0 = 0;
    w0 = width;
    h0 = height;
    bop = 0;

    printf("saving frame %d of %d\n", 1-first, frames-first);
    deflate_rect(images[0], x0, y0, w0, h0, bpp, rowbytes, zbuf_size, 0);

    if (op[0].zstream.total_out <= op[1].zstream.total_out)
    {
      zsize = op[0].zstream.total_out;
      memcpy(zbuf, op[0].zbuf, zsize);
    }
    else
    {
      zsize = op[1].zstream.total_out;
      memcpy(zbuf, op[1].zbuf, zsize);
    }

    deflateReset(&op[0].zstream);
    op[0].zstream.data_type = Z_BINARY;
    deflateReset(&op[1].zstream);
    op[1].zstream.data_type = Z_BINARY;

    if (first)
    {
      write_IDATs(f, 0, zbuf, zsize, idat_size);

      printf("saving frame %d of %d\n", 1, frames-first);
      deflate_rect(images[1], x0, y0, w0, h0, bpp, rowbytes, zbuf_size, 0);

      if (op[0].zstream.total_out <= op[1].zstream.total_out)
      {
        zsize = op[0].zstream.total_out;
        memcpy(zbuf, op[0].zbuf, zsize);
      }
      else
      {
        zsize = op[1].zstream.total_out;
        memcpy(zbuf, op[1].zbuf, zsize);
      }

      deflateReset(&op[0].zstream);
      op[0].zstream.data_type = Z_BINARY;
      deflateReset(&op[1].zstream);
      op[1].zstream.data_type = Z_BINARY;
    }

    for (i=first; i<frames-1; i++)
    {
      unsigned int op_min;
      int          op_best;

      printf("saving frame %d of %d\n", i-first+2, frames-first);
      for (j=0; j<12; j++)
        op[j].valid = 0;

      /* dispose = none */
      try_over = get_rect(width, height, images[i], images[i+1], imagetemp, &x1, &y1, &w1, &h1, bpp, has_tcolor, tcolor);
      deflate_rect(images[i+1], x1, y1, w1, h1, bpp, rowbytes, zbuf_size, 0);
      if (try_over)
        deflate_rect(imagetemp, x1, y1, w1, h1, bpp, rowbytes, zbuf_size, 1);

      /* dispose = background */
      if (has_tcolor)
      {
        memcpy(imagetemp, images[i], imagesize);
        if (coltype == 2)
          for (j=0; j<h0; j++)
            for (k=0; k<w0; k++)
              memcpy(imagetemp + ((j+y0)*width + (k+x0))*3, &tcolor, 3);
        else
          for (j=0; j<h0; j++)
            memset(imagetemp + ((j+y0)*width + x0)*bpp, tcolor, w0*bpp);

        try_over = get_rect(width, height, imagetemp, images[i+1], imagetemp, &x1, &y1, &w1, &h1, bpp, has_tcolor, tcolor);

#ifdef FIREFOX_BUG_546272_WORKAROUND
        if (i == first) { w1 += x1; h1 += y1; x1 = y1 = 0; }
#endif
        deflate_rect(images[i+1], x1, y1, w1, h1, bpp, rowbytes, zbuf_size, 2);
        if (try_over)
          deflate_rect(imagetemp, x1, y1, w1, h1, bpp, rowbytes, zbuf_size, 3);
      }

      if (i>first)
      {
        /* dispose = previous */
        try_over = get_rect(width, height, images[i-1], images[i+1], imagetemp, &x1, &y1, &w1, &h1, bpp, has_tcolor, tcolor);
        deflate_rect(images[i+1], x1, y1, w1, h1, bpp, rowbytes, zbuf_size, 4);
        if (try_over)
          deflate_rect(imagetemp, x1, y1, w1, h1, bpp, rowbytes, zbuf_size, 5);
      }

      op_min = op[0].zstream.total_out;
      op_best = 0;
      for (j=1; j<12; j++)
      {
        if (op[j].valid)
        {
          if (op[j].zstream.total_out < op_min)
          {
            op_min = op[j].zstream.total_out;
            op_best = j;
          }
        }
      }

      dop = op_best >> 2;

      fctl.mSeq       = swap32(next_seq_num++);
      fctl.mWidth     = swap32(w0);
      fctl.mHeight    = swap32(h0);
      fctl.mXOffset   = swap32(x0);
      fctl.mYOffset   = swap32(y0);
      fctl.mDelayNum  = swap16(delay_num);
      fctl.mDelayDen  = swap16(delay_den);
      fctl.mDisposeOp = dop;
      fctl.mBlendOp   = bop;
      write_chunk(f, "fcTL", (unsigned char *)(&fctl), 26);

      write_IDATs(f, i, zbuf, zsize, idat_size);

      /* process apng dispose - begin */
      if (dop == 1)
      {
        if (coltype == 2)
          for (j=0; j<h0; j++)
            for (k=0; k<w0; k++)
              memcpy(images[i] + ((j+y0)*width + (k+x0))*3, &tcolor, 3);
        else
          for (j=0; j<h0; j++)
            memset(images[i] + ((j+y0)*width + x0)*bpp, tcolor, w0*bpp);
      }
      else
      if (dop == 2)
      {
        for (j=0; j<h0; j++)
          memcpy(images[i] + ((j+y0)*width + x0)*bpp, images[i-1] + ((j+y0)*width + x0)*bpp, w0*bpp);
      }
      /* process apng dispose - end */

      x0 = op[op_best].x;
      y0 = op[op_best].y;
      w0 = op[op_best].w;
      h0 = op[op_best].h;
      bop = (op_best >> 1) & 1;

      zsize = op[op_best].zstream.total_out;
      memcpy(zbuf, op[op_best].zbuf, zsize);

      for (j=0; j<12; j++)
      {
        deflateReset(&op[j].zstream);
        op[j].zstream.data_type = Z_BINARY;
      }
    }

    if (frames > 1)
    {
      fctl.mSeq       = swap32(next_seq_num++);
      fctl.mWidth     = swap32(w0);
      fctl.mHeight    = swap32(h0);
      fctl.mXOffset   = swap32(x0);
      fctl.mYOffset   = swap32(y0);
      fctl.mDelayNum  = swap16(delay_num);
      fctl.mDelayDen  = swap16(delay_den);
      fctl.mDisposeOp = 0;
      fctl.mBlendOp   = bop;
      write_chunk(f, "fcTL", (unsigned char *)(&fctl), 26);
    }

    write_IDATs(f, i, zbuf, zsize, idat_size);

    write_chunk(f, "tEXt", png_Software, 27); 
    write_chunk(f, "IEND", 0, 0);
    fclose(f);
  }
  else
  {
    printf( "Error: couldn't open file for writing\n" );
    return 1;
  }

  free(imagetemp);
  free(zbuf);
  free(row_buf);
  free(sub_row);
  free(up_row);
  free(avg_row);
  free(paeth_row);

  for (i=0; i<12; i++)
  {
    deflateEnd(&op[i].zstream);
    if (op[i].zbuf != NULL)
      free(op[i].zbuf);
  }

  for (i=0; i<frames; i++)
  {
    if (images[i] != NULL)
      free(images[i]);
  }
  free(images);
  free(bigcube);

  printf("all done\n");

  return 0;
}
