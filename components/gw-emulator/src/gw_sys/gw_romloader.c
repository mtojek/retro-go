/*

This program permits to load ROM generated by LCD-Game-Shrinker.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.
This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
this program. If not, see <http://www.gnu.org/licenses/>.

__author__ = "bzhxx"
__contact__ = "https://github.com/bzhxx"
__license__ = "GPLv3"

*/

//to be removed using arguments get rom pointer and size
#include "rom_manager.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define GW_ROM_LZ4_SUPPORT
// #define GW_ROM_ZOPFLI_SUPPORT
// #define GW_ROM_LZMA_SUPPORT
// #define GW_JPEG_SUPPORT

#ifdef GW_ROM_LZ4_SUPPORT
#include "lz4_depack.h"
#endif

#ifdef GW_ROM_ZOPFLI_SUPPORT
#include "miniz.h"
#endif

#ifdef GW_ROM_LZMA_SUPPORT
#include "lzma.h"
#endif

#include "gw_type_defs.h"
#include "gw_system.h"
#include "gw_romloader.h"
#ifdef GW_JPEG_SUPPORT
#include "hw_jpeg_decoder.h"


/* instances for JPEG decoder */
// Internal buffer for hardware JPEG decoder
#define JPEG_BUFFER_SIZE ((uint32_t)(GW_SCREEN_WIDTH * GW_SCREEN_HEIGHT * 3 / 2))

static uint8_t JPEG_Buffer[JPEG_BUFFER_SIZE] __attribute__((aligned(4)));
#endif

//ROM in RAM
// size is 2 or 4 images in 16bits format (1 screen or 2screen swapping)
// + 8kB rom+program
// + 8kB metadata
// = 4x320x240*2 + 2 x 4096
// = 622592 kB

// 100KB         uncompressed ROM file (considering no background)   100KB (segments < 50kB)
// 4x320x240     JPEG out buffer ARGB 8888                           307200
// background    RGB565 background from JPEG 2 x 320x240             153600

#define GW_ROM_SIZE_MAX (uint32_t)(400000)
unsigned char *GW_ROM;

unsigned short *gw_background = NULL;
unsigned char *gw_segments = NULL;
unsigned short *gw_segments_x = NULL;
unsigned short *gw_segments_y = NULL;
unsigned short *gw_segments_width = NULL;
unsigned short *gw_segments_height = NULL;
unsigned int *gw_segments_offset = NULL;
unsigned char *gw_program = NULL;
unsigned char *gw_melody = NULL;
unsigned int *gw_keyboard = NULL;

gwromheader_t gw_head;

/**************** Background *******************/
/*
	Background extracted and adapted to GW LCD from BackgroundNS.png file
	RGB565 16bits pixel format
*/
// unsigned short gw_background_data[320*240];

/**************** segments *******************/
/*
segments extracted and adapted to GW LCD from .svg file
RGB565 16bits pixel format
the data segments are smaller than the reserved memory
*/

//unsigned short gw_segments_data[320*240];
// unsigned short gw_segments_x[NB_SEGS];
// unsigned short gw_segments_y[NB_SEGS];
// unsigned short gw_segments_width[NB_SEGS];
// unsigned short gw_segments_height[NB_SEGS];
// unsigned int   gw_segments_offset[NB_SEGS];

/**************** program file *******************/
// unsigned char gw_program[4096];

/**************** melody file *******************/
// unsigned char gw_melody[4096];

/*** G&W Buttons to CPU inputs Mapping ***/
/*
S1..S8 x K1..4 keyboard matrix input on SM5xx emulated CPU
BA,B direct input on SM5xx emulated CPU
8 buttons on G&W SM

Buttons to keyboards mapping MSB..LSB defined on 8 bits
LEFT | RIGHT | UP | DOWN |A | GAME | SELECT


keyboard[0] is S1 K4..K1
keyboard[1] is S2 K4..K1
.
keyboard[7] is S2 K4..K1
keyboard[8] is BA (8 bits lsb)
keyboard[9] is B   (8 bits lsb)
*/
// unsigned int gw_keyboard[10];

bool gw_romloader_rom2ram()
{
   GW_ROM = malloc(GW_ROM_SIZE_MAX);

   /* src pointer to the ROM data in the external flash (raw or LZ4) */
   const unsigned char *src = (unsigned char *)ROM_DATA;

   /* dest pointer to the ROM data in the internal RAM (raw) */
   unsigned char *dest = (unsigned char *)GW_ROM;

   /* variable used to compare the size to detect error, uncompressed  */
   unsigned int rom_size_src  = ROM_DATA_LENGTH;
   unsigned int rom_size_dest = ROM_DATA_LENGTH;

   /* 1st part on FLASH before JPEG */
   unsigned int rom_size_compressed_src  = ROM_DATA_LENGTH;

   /* cleanup destination memory with white color (in case of no background) */
   memset(dest, 0xffff, GW_ROM_SIZE_MAX);

   /* Check it by testing 3 first characters == SM5 */
   if (memcmp(src, ROM_CPU_SM510, 3) == 0)
   {
      printf("Not compressed : header OK\n");

      memcpy(dest, src, ROM_DATA_LENGTH);
      printf("ROM2RAM done\n");

      rom_size_src = ROM_DATA_LENGTH;

#ifdef GW_ROM_LZ4_SUPPORT

      /* Check if it's compressed */
   }
   else if (memcmp(src, LZ4_MAGIC, 4) == 0)
   {
      printf("ROM LZ4 detected\n");
      rom_size_compressed_src = lz4_get_file_size(src);

      rom_size_src = lz4_uncompress(src, dest);

      if ((memcmp(dest, ROM_CPU_SM510, 3) == 0))
      {
         printf("ROM LZ4 : header OK\n");
      }
      else
      {
         printf("ROM LZ4 : header KO\n");
         return false;
      }
#endif

#ifdef GW_ROM_ZOPFLI_SUPPORT
   }
   else if (memcmp(src, ZLIB_MAGIC,4) == 0)
   {

      /* DEFLATE decompression */
      printf("ROM ZLIB detected.\n");
      memcpy(&rom_size_compressed_src, &src[4], sizeof(rom_size_compressed_src));

      size_t n_decomp_bytes;
      int flags = 0;
      flags |= TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;

      n_decomp_bytes = tinfl_decompress_mem_to_mem(dest, GW_ROM_SIZE_MAX, &src[8], rom_size_compressed_src, flags);
      assert(n_decomp_bytes != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED);
      rom_size_src = (uint32_t) n_decomp_bytes;

      if ((memcmp(dest, ROM_CPU_SM510, 3) == 0))
      {
         printf("ROM ZLIB : header OK\n");
      }
      else
      {
         printf("ROM ZLIB : header KO\n");
         return false;
      }
#endif

#ifdef GW_ROM_LZMA_SUPPORT
   }
   else if (memcmp(src, LZMA_MAGIC,4) == 0)
   {

      /* DEFLATE decompression */
      printf("ROM LZMA detected.\n");
      memcpy(&rom_size_compressed_src, &src[4], sizeof(rom_size_compressed_src));

      size_t n_decomp_bytes;
      n_decomp_bytes = lzma_inflate(dest, GW_ROM_SIZE_MAX, &src[8], rom_size_compressed_src);
      rom_size_src = (uint32_t) n_decomp_bytes;

      if ((memcmp(dest, ROM_CPU_SM510, 3) == 0))
      {
         printf("ROM LZMA : header OK\n");
      }
      else
      {
         printf("ROM LZMA : header KO\n");
         return false;
      }

#endif
      /* Something wrong in the ROM detection... */
   }
   else
   {
      printf("Unknow ROM format\n");
      return false;
   }

   /* Read in the ROM header */
   memcpy(&gw_head, dest, sizeof(gw_head));

   /* check size */
   /*Check if the data size matches. based on the last object in the ROM header (keyboard) */
   rom_size_dest = gw_head.keyboard + gw_head.keyboard_size;

   if (rom_size_src != rom_size_dest)
   {
      printf("CPU_name=%s\n", gw_head.cpu_name);
      printf("signature:%s\n", gw_head.rom_signature);
      printf("ROM ERROR,size=%u,expected=%u\n", rom_size_src, rom_size_dest);
      return false;
   }
   else
   {
      printf("ROM size: OK\n");
   }

   /* Manage the background */

   // check if there is a uncompressed background inside
   if (gw_head.background_pixel_size != 0)
   {
      printf("RGB565 background\n");
      gw_background = (unsigned short *)&GW_ROM[gw_head.background_pixel];

      (void)rom_size_compressed_src; // unused
   }
#ifdef GW_JPEG_SUPPORT
   // otherwise we get the background from JPEG file
   else if((rom_size_compressed_src+8) != ROM_DATA_LENGTH)
   {
      printf("JPEG background?\n");

      /* JPEG decoder : from Flash to RAM */
      uint32_t JpegSrc;
      uint32_t FrameDst;

      JpegSrc = (uint32_t)&ROM_DATA[rom_size_compressed_src+8];

      /*set destination RGB image, 32 bits aligned */
      FrameDst = (uint32_t)&GW_ROM[rom_size_src + 4 - (rom_size_src % 4)];

      /* cleanup Frame buffer with black color (in case of background cropped) */
      memset((unsigned char *)FrameDst, 0x0, GW_SCREEN_HEIGHT*GW_SCREEN_WIDTH*2);

      assert(JPEG_DecodeToFrameInit((uint32_t)&JPEG_Buffer,JPEG_BUFFER_SIZE) == 0);

      // get jpeg image size

      //determine center position
      uint32_t xImg=0, yImg=0, wImg=0,hImg=0;
      assert (0 == JPEG_DecodeGetSize(JpegSrc, &wImg, &hImg));

      xImg = ( GW_SCREEN_WIDTH - wImg )/2;
      yImg = ( GW_SCREEN_HEIGHT - hImg )/2;

      // decode background and copy it in the righ place in the frame buffer
      assert( 0 == JPEG_DecodeToFrame(JpegSrc, FrameDst, xImg, yImg, 0xFF));

      assert(JPEG_DecodeDeInit() == 0);

      /* set the address of RGB background  */
      gw_background = (unsigned short *)(FrameDst);
   }
#endif
   /* Set up pointers to objects base */
   gw_segments = (unsigned char *)&GW_ROM[gw_head.segments_pixel];

   gw_segments_x = (unsigned short *)&GW_ROM[gw_head.segments_x];
   gw_segments_y = (unsigned short *)&GW_ROM[gw_head.segments_y];
   gw_segments_width = (unsigned short *)&GW_ROM[gw_head.segments_width];
   gw_segments_height = (unsigned short *)&GW_ROM[gw_head.segments_height];
   gw_segments_offset = (unsigned int *)&GW_ROM[gw_head.segments_offset];

   gw_program = (unsigned char *)&GW_ROM[gw_head.program];

   if (gw_head.melody_size != 0)
      gw_melody = (unsigned char *)&GW_ROM[gw_head.melody];

   gw_keyboard = (unsigned int *)&GW_ROM[gw_head.keyboard];

   return true;
}

/* Load a ROM image into memory */
bool gw_romloader()
{
   printf("gw_romloader\n");

   bool rom_status = gw_romloader_rom2ram();

   //debug
   if (!rom_status)
      assert(false);

   return rom_status;
}