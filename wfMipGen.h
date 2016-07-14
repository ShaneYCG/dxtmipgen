#pragma once
#ifndef WF_MIPGEN_H
#define WF_MIPGEN_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
	#if _MSC_VER < 1300
	   typedef signed   char  int8_t;
	   typedef unsigned char  uint8_t;
	   typedef signed   short int16_t;
	   typedef unsigned short uint16_t;
	   typedef signed   int   int32_t;
	   typedef unsigned int   uint32_t;
	#else
	   typedef signed   __int8  int8_t;
	   typedef unsigned __int8  uint8_t;
	   typedef signed   __int16 int16_t;
	   typedef unsigned __int16 uint16_t;
	   typedef signed   __int32 int32_t;
	   typedef unsigned __int32 uint32_t;
	#endif
	typedef signed   __int64 int64_t;
	typedef unsigned __int64 uint64_t;
#else
	#include <stdint.h>
#endif
	
#define WF_MIPGEN_DXT1 (1<<0)
#define WF_MIPGEN_DXT5 (1<<1)

//! wfMipGen_SquishImage()
/*!
TODO: restrict! (these can overlap, but we don't care if they do!)
*/
void wfMipGen_SquishImage( void* pSrc, void* pDst, const uint32_t width, const uint32_t height, const uint32_t flags );

//! wfMipGen_SquishDxt1()
/*!
Combines four DXT1 blocks into one.

TODO: restrict! (these can overlap, but we don't care if they do!)
*/
void wfMipGen_SquishDxt1( void* pSrc[4], void* pDst );

//! wfMipGen_SquishDxt5()
/*!
Combines four DXT5 blocks into one.

TODO: restrict! (these can overlap, but we don't care if they do!)
*/
void wfMipGen_SquishDxt5( void* pSrc[4], void* pDst );

#ifdef __cplusplus
}
#endif

#endif // WF_MIPGEN_H
