#include "wfMipGen.h"

//#define WF_MIPGEN_USE_ASSERTS

#ifdef WF_MIPGEN_USE_ASSERTS
	#include <assert.h>
	#define wfAssert( x ) assert( x )
#else
	#define wfAssert( x )
#endif

#define WF_MIPGEN_GETFMTBLOCKSIZEBYTES( flags ) (((flags&0x3)-1)*8+8) // get the dxt block size based on conversion flags
#define WF_MIPGEN_DXTBLOCKPIXELWIDTH            4                     // pixel width/height of a dxt block
#define WF_MIPGEN_DOWNSCALEBLOCKS               4                     // number of dxt blocks we are combining into one

// how far to shift into a u32 of the pixel data to get at the 2 bit palette idx, _xy
#define WF_MIPGEN_COLOR_SHIFT_00 0
#define WF_MIPGEN_COLOR_SHIFT_10 2
#define WF_MIPGEN_COLOR_SHIFT_01 8
#define WF_MIPGEN_COLOR_SHIFT_11 10

#define WF_MIPGEN_COLOR_SHIFT_20 4
#define WF_MIPGEN_COLOR_SHIFT_30 6
#define WF_MIPGEN_COLOR_SHIFT_21 12
#define WF_MIPGEN_COLOR_SHIFT_31 14

#define WF_MIPGEN_COLOR_SHIFT_02 16
#define WF_MIPGEN_COLOR_SHIFT_12 18
#define WF_MIPGEN_COLOR_SHIFT_03 24
#define WF_MIPGEN_COLOR_SHIFT_13 26

#define WF_MIPGEN_COLOR_SHIFT_22 20
#define WF_MIPGEN_COLOR_SHIFT_32 22
#define WF_MIPGEN_COLOR_SHIFT_23 28
#define WF_MIPGEN_COLOR_SHIFT_33 30

// how far to shift into a u64 of alpha data to get at the 3 bit pallete idx, _xy
// note: there is only 48 bits of pixel data, so this must skip over the 2 u8s of alpha values!
#define WF_MIPGEN_ALPHA_SHIFT_00 ((WF_MIPGEN_COLOR_SHIFT_00*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_10 ((WF_MIPGEN_COLOR_SHIFT_10*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_01 ((WF_MIPGEN_COLOR_SHIFT_01*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_11 ((WF_MIPGEN_COLOR_SHIFT_11*3)/2 + 16)

#define WF_MIPGEN_ALPHA_SHIFT_20 ((WF_MIPGEN_COLOR_SHIFT_20*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_30 ((WF_MIPGEN_COLOR_SHIFT_30*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_21 ((WF_MIPGEN_COLOR_SHIFT_21*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_31 ((WF_MIPGEN_COLOR_SHIFT_31*3)/2 + 16)

#define WF_MIPGEN_ALPHA_SHIFT_02 ((WF_MIPGEN_COLOR_SHIFT_02*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_12 ((WF_MIPGEN_COLOR_SHIFT_12*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_03 ((WF_MIPGEN_COLOR_SHIFT_03*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_13 ((WF_MIPGEN_COLOR_SHIFT_13*3)/2 + 16)

#define WF_MIPGEN_ALPHA_SHIFT_22 ((WF_MIPGEN_COLOR_SHIFT_22*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_32 ((WF_MIPGEN_COLOR_SHIFT_32*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_23 ((WF_MIPGEN_COLOR_SHIFT_23*3)/2 + 16)
#define WF_MIPGEN_ALPHA_SHIFT_33 ((WF_MIPGEN_COLOR_SHIFT_33*3)/2 + 16)

typedef struct _wfMipGen_DxtPaletteBlock
{
	uint16_t color0;
	uint16_t color1;
} wfMipGen_DxtPaletteBlock;

void wfMipGen_Alpha_BuildTable4( const uint32_t a0, const uint32_t a1, uint32_t* dst )
{
	*dst = a0;                  ++dst;
	*dst = a1;                  ++dst;
	*dst = ( a0*4 + a1*1 ) / 5; ++dst;
	*dst = ( a0*3 + a1*2 ) / 5; ++dst;
	*dst = ( a0*2 + a1*3 ) / 5; ++dst;
	*dst = ( a0*1 + a1*4 ) / 5; ++dst;
	*dst = 0;                   ++dst;
	*dst = 255;
}

void wfMipGen_Alpha_BuildTable6( const uint32_t a0, const uint32_t a1, uint32_t* dst )
{
	*dst = a0;                  ++dst;
	*dst = a1;                  ++dst;
	*dst = ( a0*6 + a1*1 ) / 7; ++dst;
	*dst = ( a0*5 + a1*2 ) / 7; ++dst;
	*dst = ( a0*4 + a1*3 ) / 7; ++dst;
	*dst = ( a0*3 + a1*4 ) / 7; ++dst;
	*dst = ( a0*2 + a1*5 ) / 7; ++dst;
	*dst = ( a0*1 + a1*6 ) / 7;
}

int32_t wfMipgen_Alpha_DistSq( const uint32_t a0, const uint32_t a1 )
{
	int32_t a = *((int32_t*)&a0) - *((int32_t*)&a1);
	return a*a;
}

typedef struct _wfMipGen_Color
{
	int32_t r, g, b; // stored in int but green is still higher precision, r = 0-31, g = 0-63, b = 0-31
} wfMipGen_Color;

void wfMipGen_Color_FromDxtColor( wfMipGen_Color* out, const uint16_t in )
{
	out->r = ( in & 0xF800 ) >> 11;
	out->g = ( in & 0x7E0  ) >> 5;
	out->b = ( in & 0x1F   );
}

void wfMipGen_Color_ToDxtColor( uint16_t* out, const wfMipGen_Color* in )
{
	uint16_t r = ( uint16_t )in->r;
	uint16_t g = ( uint16_t )in->g;
	uint16_t b = ( uint16_t )in->b;
	*out = ( r << 11 ) | ( g << 5 ) | b;
}

void wfMipGen_Color_Blend( const wfMipGen_Color* color0, const wfMipGen_Color* color1, const uint32_t bf0, const uint32_t bf1, wfMipGen_Color* out )
{
	out->r = ( color0->r*bf0 + color1->r*bf1 ) / 24;
	out->g = ( color0->g*bf0 + color1->g*bf1 ) / 24;
	out->b = ( color0->b*bf0 + color1->b*bf1 ) / 24;
}

int32_t wfMipgen_Color_DistSq( const wfMipGen_Color* color0, const wfMipGen_Color* color1 )
{
	int32_t r, g, b;
	r = color0->r - color1->r;
	g = color0->g - color1->g; // storing green in higher precision gives it more weight in color distance calculations -- a good thing
	b = color0->b - color1->b;
	return r*r + g*g + b*b;
}

typedef struct _wfMipGen_PaletteBuilder
{
	wfMipGen_Color  colors[ WF_MIPGEN_DOWNSCALEBLOCKS*4 ];
	uint32_t        numColors;
	int32_t         maxColorDist;
	wfMipGen_Color* color0;
	wfMipGen_Color* color1;
} wfMipGen_PaletteBuilder;

void wfMipGen_PaletteBuilder_Init( wfMipGen_PaletteBuilder* builder )
{
	builder->numColors    = 0;
	builder->maxColorDist = -1;
	builder->color0       = &builder->colors[ 0 ];
	builder->color1       = &builder->colors[ 1 ];
}

void wfMipGen_PaletteBuilder_AddColor( wfMipGen_PaletteBuilder* builder, const wfMipGen_Color* color )
{
	wfMipGen_Color* srcColor;
	const uint32_t dstColorIdx = builder->numColors;
	wfMipGen_Color* dstColor = &builder->colors[ dstColorIdx ];
	*dstColor = *color;

	// do not add the same color multiple times
	for( srcColor = builder->colors; srcColor != dstColor; ++srcColor )
	{
		if( srcColor->r == dstColor->r && srcColor->g == dstColor->g && srcColor->b == srcColor->b )
		{
			return;
		}
	}

	// add the color to the palette, and mark it if it is one of the extremes
	for( srcColor = builder->colors; srcColor != dstColor; ++srcColor ) 
	{
		const int32_t dist = wfMipgen_Color_DistSq( srcColor, dstColor );
		if( dist >= builder->maxColorDist )
		{
			builder->color0       = srcColor;
			builder->color1       = dstColor;
			builder->maxColorDist = dist;
		}
	}

	++builder->numColors;
}

//! wfMipGen_SquishImage()

void wfMipGen_SquishImage( void* pSrc, void* pDst, const uint32_t width, const uint32_t height, const uint32_t flags )
{
	uint32_t srcBlockWidth, srcBlockHeight, srcStrideBytes;
	uint32_t dstBlockWidth, dstBlockHeight, dstStrideBytes;
	const uint32_t fmtBlockSize = WF_MIPGEN_GETFMTBLOCKSIZEBYTES( flags );

	wfAssert( width  % WF_MIPGEN_DXTBLOCKPIXELWIDTH == 0 );
	wfAssert( height % WF_MIPGEN_DXTBLOCKPIXELWIDTH == 0 );
	wfAssert( (flags&WF_MIPGEN_DXT1) ^ (flags&WF_MIPGEN_DXT5) );

	srcBlockWidth  = width         / WF_MIPGEN_DXTBLOCKPIXELWIDTH;
	srcBlockHeight = height        / WF_MIPGEN_DXTBLOCKPIXELWIDTH;
	srcStrideBytes = srcBlockWidth * fmtBlockSize;
	
	dstBlockWidth  = srcBlockWidth  / 2;
	dstBlockHeight = srcBlockHeight / 2;
	dstStrideBytes = srcStrideBytes / 2;

	{
		uint32_t dstBlockY, dstBlockX;
		uint8_t* dst     = ( uint8_t* )pDst;
		uint8_t* srcRow0 = ( uint8_t* )pSrc;
		uint8_t* srcRow1 = srcRow0 + srcStrideBytes;
		for( dstBlockY = 0; dstBlockY != dstBlockHeight; ++dstBlockY )
		{
			for( dstBlockX = 0; dstBlockX != dstBlockWidth; ++dstBlockX )
			{
				void* srcBlocks[ WF_MIPGEN_DOWNSCALEBLOCKS ];
				srcBlocks[ 0 ] = srcRow0; srcRow0 += fmtBlockSize;
				srcBlocks[ 1 ] = srcRow0; srcRow0 += fmtBlockSize;
				srcBlocks[ 2 ] = srcRow1; srcRow1 += fmtBlockSize;
				srcBlocks[ 3 ] = srcRow1; srcRow1 += fmtBlockSize;
				
				if( flags&WF_MIPGEN_DXT1 )
				{
					wfMipGen_SquishDxt1( srcBlocks, dst );
				}
				else
				{
					wfMipGen_SquishDxt5( srcBlocks, dst );
				}

				dst += fmtBlockSize;
			}
			srcRow0 += srcStrideBytes;
			srcRow1 += srcStrideBytes;
		}
	}
}

void wfMipGen_SquishDxt1( void* pSrc[4], void* pDst )
{
	wfMipGen_Color          outColors[4];                      // the final four colors we will output
	wfMipGen_Color          blendedColors[16];                 // the 16 incoming colors
	wfMipGen_Color*         dstBlendedColor = blendedColors;   // current incoming color we are reading or writing
	wfMipGen_PaletteBuilder paletteBuilder;                    // takes input palettes and builds a new one for the single output block
	uint32_t                numFinalColors;                    // number of output colors; 4 if opaque, 3 if transparency is included
	uint32_t                srcBlockIdx;                       // the current source block
	uint32_t                dstBlendFactors[16];               // the blend factors for all incoming pixels
	uint32_t*               dstBlendFactor = dstBlendFactors;  // the current blend factor we are reading or writing
	uint32_t                blendTable[ 4 ] = { 6, 0, 0, 0 };  // base blend table, will be altered depending in blocks contain transparency
	uint32_t                hasAlpha = 0;                      // 1 if the output block will have transparency
	wfMipGen_PaletteBuilder_Init( &paletteBuilder );

	// create dst palette and blend input colors
	for( srcBlockIdx = 0; srcBlockIdx != WF_MIPGEN_DOWNSCALEBLOCKS; ++srcBlockIdx )
	{
		wfMipGen_Color srcColors[ 2 ];
		wfMipGen_DxtPaletteBlock* srcPalette = ( wfMipGen_DxtPaletteBlock* )pSrc[ srcBlockIdx ];
		const uint32_t srcPixels = *( ( uint32_t* )( srcPalette+1 ) );

		wfMipGen_Color_FromDxtColor( &srcColors[ 0 ], srcPalette->color0 );
		wfMipGen_Color_FromDxtColor( &srcColors[ 1 ], srcPalette->color1 );

		// opaque block
		if( srcPalette->color0 > srcPalette->color1 )
		{
			blendTable[ 2 ] = 4;
			blendTable[ 3 ] = 2;
		}
		// 1-bit alpha block (maybe -- it might not actually use the alpha, but is capable of it)
		else
		{
			blendTable[ 2 ] = 3;
			blendTable[ 3 ] = 25;	// max blend factor is 24 for normal color, so if a single pixel is transparent the entire quad's blend factor will go >24
		}
		{
			uint32_t i;
			uint32_t blendFactors[ 4 ];
			uint32_t* blendFactor = blendFactors;
				
			#define WF_MIPGEN_BF_COLOR( shift ) blendTable[ (srcPixels>>shift)&0x3 ]
			#define WF_MIPGEN_BF4_COLOR( shift0, shift1, shift2, shift3 ) WF_MIPGEN_BF_COLOR( shift0 ) + WF_MIPGEN_BF_COLOR( shift1 ) + WF_MIPGEN_BF_COLOR( shift2 ) + WF_MIPGEN_BF_COLOR( shift3 )

			blendFactors[ 0 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_00, WF_MIPGEN_COLOR_SHIFT_10, WF_MIPGEN_COLOR_SHIFT_01, WF_MIPGEN_COLOR_SHIFT_11 );
			blendFactors[ 1 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_20, WF_MIPGEN_COLOR_SHIFT_30, WF_MIPGEN_COLOR_SHIFT_21, WF_MIPGEN_COLOR_SHIFT_31 );
			blendFactors[ 2 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_02, WF_MIPGEN_COLOR_SHIFT_12, WF_MIPGEN_COLOR_SHIFT_03, WF_MIPGEN_COLOR_SHIFT_13 );
			blendFactors[ 3 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_22, WF_MIPGEN_COLOR_SHIFT_32, WF_MIPGEN_COLOR_SHIFT_23, WF_MIPGEN_COLOR_SHIFT_33 );

			#undef WF_MIPGEN_BF_COLOR
			#undef WF_MIPGEN_BF4_COLOR

			for( i = 0; i != 4; ++i )
			{
				if( *blendFactor <= 24 )
				{
					wfMipGen_Color_Blend( &srcColors[ 0 ], &srcColors[ 1 ], *blendFactor, 24-*blendFactor, dstBlendedColor );
					wfMipGen_PaletteBuilder_AddColor( &paletteBuilder, dstBlendedColor );
					*dstBlendFactor = 0;	// will be calculated later once we have a palette
				}
				else
				{
					hasAlpha = 1;
					*dstBlendFactor = 0x3;	// no palette necessary, transparent
				}
				++blendFactor;
				++dstBlendedColor;
				++dstBlendFactor;
			}
		}
	}
	{ // write the palette
		wfMipGen_DxtPaletteBlock* dstPalette = ( wfMipGen_DxtPaletteBlock* )pDst;
		wfMipGen_Color_ToDxtColor( &dstPalette->color0, paletteBuilder.color0 );
		wfMipGen_Color_ToDxtColor( &dstPalette->color1, paletteBuilder.color1 );

		// swap colors if necessary
		if( ( dstPalette->color0 <= dstPalette->color1 && hasAlpha == 0 ) || ( dstPalette->color0 > dstPalette->color1 && hasAlpha == 1 ) )
		{
			{ const uint16_t  tmp = dstPalette->color0;    dstPalette->color0    = dstPalette->color1;    dstPalette->color1    = tmp; }
			{ wfMipGen_Color* tmp = paletteBuilder.color0; paletteBuilder.color0 = paletteBuilder.color1; paletteBuilder.color1 = tmp; }
		}

		outColors[ 0 ] = *paletteBuilder.color0;
		outColors[ 1 ] = *paletteBuilder.color1;
		if( dstPalette->color0 > dstPalette->color1 )
		{
			wfMipGen_Color_Blend( &outColors[ 0 ], &outColors[ 1 ], 4*4, 2*4, &outColors[ 2 ] );
			wfMipGen_Color_Blend( &outColors[ 0 ], &outColors[ 1 ], 2*4, 4*4, &outColors[ 3 ] );
			numFinalColors = 4;
		}
		else
		{
			wfMipGen_Color_Blend( &outColors[ 0 ], &outColors[ 1 ], 3*4, 3*4, &outColors[ 2 ] );
			numFinalColors = 3;
		}
	}

	// find closest colors in the palette
	{
		wfMipGen_Color* dstBlendedColor = blendedColors;
		dstBlendFactor = dstBlendFactors;
		for( srcBlockIdx = 0; srcBlockIdx != WF_MIPGEN_DOWNSCALEBLOCKS; ++srcBlockIdx )
		{
			// functionize me!
			uint32_t i, j;
			for( i = 0; i != 4; ++i, ++dstBlendFactor )
			{
				if( *dstBlendFactor != 0x3 ) // skip pixels we've already determined are transparent
				{
					int32_t minDist = wfMipgen_Color_DistSq( dstBlendedColor, &outColors[ 0 ] );
					uint32_t closestIdx = 0;
					for( j = 1; j != numFinalColors; ++j )
					{
						int32_t dist = wfMipgen_Color_DistSq( dstBlendedColor, &outColors[ j ] );
						if( dist < minDist )
						{
							closestIdx = j;
							minDist = dist;
						}
					}
					*dstBlendFactor = closestIdx;
				}
				++dstBlendedColor;
			}
		}
		{ // write pixel data
			uint32_t dstPixels;
			const uint32_t* dstBlendFactor = dstBlendFactors;

			dstPixels  = *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_00; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_10; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_01; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_11; ++dstBlendFactor;

			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_20; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_30; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_21; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_31; ++dstBlendFactor;

			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_02; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_12; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_03; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_13; ++dstBlendFactor;

			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_22; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_32; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_23; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_33; ++dstBlendFactor;
			
			*( (uint32_t*)pDst + 3 ) = dstPixels;
		}
	}
}

void wfMipGen_SquishDxt5( void* pSrc[4], void* pDst )
{
	wfMipGen_Color          outColors[4];                    // the final four colors we will output
	uint32_t                blendedAlpha[16];                // blended alpha values for the 16 output pixels
	uint32_t*               dstBlendAlpha = blendedAlpha;    // current blended alpha value
	wfMipGen_Color          blendedColors[16];               // the 16 incoming colors
	wfMipGen_Color*         dstBlendedColor = blendedColors; // current incoming color we are reading or writing
	wfMipGen_PaletteBuilder paletteBuilder;                  // takes input palettes and builds a new one for the single output block
	uint32_t                numFinalColors;                  // number of output colors; 4 if opaque, 3 if transparency is included
	uint32_t                srcBlockIdx;                     // the current source block
	const uint32_t          colorBlendTable[ 4 ] = { 6, 0, 4, 2 }; // 

	wfMipGen_PaletteBuilder_Init( &paletteBuilder );

	// create dst palette and blend input colors and alpha
	for( srcBlockIdx = 0; srcBlockIdx != WF_MIPGEN_DOWNSCALEBLOCKS; ++srcBlockIdx )
	{
		wfMipGen_Color srcColors[ 2 ];
		wfMipGen_DxtPaletteBlock* srcPalette = ( wfMipGen_DxtPaletteBlock* )( (uint32_t*)(pSrc[ srcBlockIdx ]) + 2 );
		const uint32_t srcPixels = *( ( uint32_t* )( srcPalette+1 ) );

		wfMipGen_Color_FromDxtColor( &srcColors[ 0 ], srcPalette->color0 );
		wfMipGen_Color_FromDxtColor( &srcColors[ 1 ], srcPalette->color1 );

		{
			uint32_t i;
			uint32_t blendFactors[ 4 ];
			uint32_t* blendFactor = blendFactors;

			#define WF_MIPGEN_BF_COLOR( shift ) colorBlendTable[ (srcPixels>>shift)&0x3 ]
			#define WF_MIPGEN_BF4_COLOR( shift0, shift1, shift2, shift3 ) WF_MIPGEN_BF_COLOR( shift0 ) + WF_MIPGEN_BF_COLOR( shift1 ) + WF_MIPGEN_BF_COLOR( shift2 ) + WF_MIPGEN_BF_COLOR( shift3 )

			blendFactors[ 0 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_00, WF_MIPGEN_COLOR_SHIFT_10, WF_MIPGEN_COLOR_SHIFT_01, WF_MIPGEN_COLOR_SHIFT_11 );
			blendFactors[ 1 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_20, WF_MIPGEN_COLOR_SHIFT_30, WF_MIPGEN_COLOR_SHIFT_21, WF_MIPGEN_COLOR_SHIFT_31 );
			blendFactors[ 2 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_02, WF_MIPGEN_COLOR_SHIFT_12, WF_MIPGEN_COLOR_SHIFT_03, WF_MIPGEN_COLOR_SHIFT_13 );
			blendFactors[ 3 ] = WF_MIPGEN_BF4_COLOR( WF_MIPGEN_COLOR_SHIFT_22, WF_MIPGEN_COLOR_SHIFT_32, WF_MIPGEN_COLOR_SHIFT_23, WF_MIPGEN_COLOR_SHIFT_33 );

			#undef WF_MIPGEN_BF_COLOR
			#undef WF_MIPGEN_BF4_COLOR

			for( i = 0; i != 4; ++i )
			{
				wfMipGen_Color_Blend( &srcColors[ 0 ], &srcColors[ 1 ], *blendFactor, 24-*blendFactor, dstBlendedColor );
				wfMipGen_PaletteBuilder_AddColor( &paletteBuilder, dstBlendedColor );
				++blendFactor;
				++dstBlendedColor;
			}

			{
				uint8_t* srcAlpha = ( uint8_t* )pSrc[ srcBlockIdx ];
				const uint64_t srcAlphaPixels = *( ( uint64_t* )srcAlpha );
				uint32_t alphaTable[8];
				if( srcAlpha[0] > srcAlpha[1] ) { wfMipGen_Alpha_BuildTable6( srcAlpha[0], srcAlpha[1], alphaTable ); }
				else                            { wfMipGen_Alpha_BuildTable4( srcAlpha[0], srcAlpha[1], alphaTable ); }
				
				#define WF_MIPGEN_BF_ALPHA( shift ) alphaTable[ (srcAlphaPixels>>shift)&0x7 ]
				#define WF_MIPGEN_BF4_ALPHA( shift0, shift1, shift2, shift3 ) ( WF_MIPGEN_BF_ALPHA( shift0 ) + WF_MIPGEN_BF_ALPHA( shift1 ) + WF_MIPGEN_BF_ALPHA( shift2 ) + WF_MIPGEN_BF_ALPHA( shift3 ) ) / 4

				*dstBlendAlpha = WF_MIPGEN_BF4_ALPHA( WF_MIPGEN_ALPHA_SHIFT_00, WF_MIPGEN_ALPHA_SHIFT_10, WF_MIPGEN_ALPHA_SHIFT_01, WF_MIPGEN_ALPHA_SHIFT_11 ); ++dstBlendAlpha;
				*dstBlendAlpha = WF_MIPGEN_BF4_ALPHA( WF_MIPGEN_ALPHA_SHIFT_20, WF_MIPGEN_ALPHA_SHIFT_30, WF_MIPGEN_ALPHA_SHIFT_21, WF_MIPGEN_ALPHA_SHIFT_31 ); ++dstBlendAlpha;
				*dstBlendAlpha = WF_MIPGEN_BF4_ALPHA( WF_MIPGEN_ALPHA_SHIFT_02, WF_MIPGEN_ALPHA_SHIFT_12, WF_MIPGEN_ALPHA_SHIFT_03, WF_MIPGEN_ALPHA_SHIFT_13 ); ++dstBlendAlpha;
				*dstBlendAlpha = WF_MIPGEN_BF4_ALPHA( WF_MIPGEN_ALPHA_SHIFT_22, WF_MIPGEN_ALPHA_SHIFT_32, WF_MIPGEN_ALPHA_SHIFT_23, WF_MIPGEN_ALPHA_SHIFT_33 ); ++dstBlendAlpha;

				#undef WF_MIPGEN_BF_ALPHA
				#undef WF_MIPGEN_BF4_ALPHA
			}
		}
	}
	{ // write the palette
		wfMipGen_DxtPaletteBlock* dstPalette = ( wfMipGen_DxtPaletteBlock* )( (uint32_t*)pDst + 2 );
		wfMipGen_Color_ToDxtColor( &dstPalette->color0, paletteBuilder.color0 );
		wfMipGen_Color_ToDxtColor( &dstPalette->color1, paletteBuilder.color1 );

		// swap colors if necessary
		if( dstPalette->color0 <= dstPalette->color1 )
		{
			{ const uint16_t  tmp = dstPalette->color0;    dstPalette->color0    = dstPalette->color1;    dstPalette->color1    = tmp; }
			{ wfMipGen_Color* tmp = paletteBuilder.color0; paletteBuilder.color0 = paletteBuilder.color1; paletteBuilder.color1 = tmp; }
		}

		outColors[ 0 ] = *paletteBuilder.color0;
		outColors[ 1 ] = *paletteBuilder.color1;
		if( dstPalette->color0 > dstPalette->color1 )
		{
			wfMipGen_Color_Blend( &outColors[ 0 ], &outColors[ 1 ], 4*4, 2*4, &outColors[ 2 ] );
			wfMipGen_Color_Blend( &outColors[ 0 ], &outColors[ 1 ], 2*4, 4*4, &outColors[ 3 ] );
			numFinalColors = 4;
		}
		else
		{
			wfMipGen_Color_Blend( &outColors[ 0 ], &outColors[ 1 ], 3*4, 3*4, &outColors[ 2 ] );
			numFinalColors = 3;
		}
	}

	// find closest colors in the palette
	{
		uint32_t dstBlendFactors[16];               // the blend factors for all incoming pixels
		uint32_t* dstBlendFactor = dstBlendFactors; // the current blend factor we are reading or writing
		wfMipGen_Color* dstBlendedColor = blendedColors;
		for( srcBlockIdx = 0; srcBlockIdx != WF_MIPGEN_DOWNSCALEBLOCKS; ++srcBlockIdx )
		{
			// functionize me!
			uint32_t i, j;
			for( i = 0; i != 4; ++i, ++dstBlendFactor )
			{
				int32_t minDist = wfMipgen_Color_DistSq( dstBlendedColor, &outColors[ 0 ] );
				uint32_t closestIdx = 0;
				for( j = 1; j != numFinalColors; ++j )
				{
					int32_t dist = wfMipgen_Color_DistSq( dstBlendedColor, &outColors[ j ] );
					if( dist < minDist )
					{
						closestIdx = j;
						minDist = dist;
					}
				}
				*dstBlendFactor = closestIdx;
				++dstBlendedColor;
			}
		}
		{ // write pixel data
			uint32_t dstPixels;
			const uint32_t* dstBlendFactor = dstBlendFactors;

			dstPixels  = *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_00; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_10; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_01; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_11; ++dstBlendFactor;

			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_20; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_30; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_21; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_31; ++dstBlendFactor;

			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_02; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_12; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_03; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_13; ++dstBlendFactor;

			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_22; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_32; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_23; ++dstBlendFactor;
			dstPixels |= *dstBlendFactor << WF_MIPGEN_COLOR_SHIFT_33; ++dstBlendFactor;

			*( (uint32_t*)pDst + 3 ) = dstPixels;
		}
	}

	// alpha!
	{
		uint32_t dstAlphaTable[8];
		uint32_t dstBlendAlphaFactors[16];
		uint32_t* dstBlendAlphaFactor = dstBlendAlphaFactors;
		{ // build palette and find closest matches for each pixel
			uint32_t hasOpaqueOrTransparent = 0;
			uint32_t minAlpha, maxAlpha;
			{ // this should be in the first loop, don't iterate here again...
				uint32_t i;
				dstBlendAlpha = blendedAlpha;
				minAlpha = 255;
				maxAlpha = 0;
				for( i = 0; i != 16; ++i, ++dstBlendAlpha )
				{
					if(      *dstBlendAlpha == 0 || *dstBlendAlpha == 255 ) { hasOpaqueOrTransparent = 1; continue; }
					if(      *dstBlendAlpha < minAlpha                    ) { minAlpha = *dstBlendAlpha; }
					else if( *dstBlendAlpha > maxAlpha                    ) { maxAlpha = *dstBlendAlpha; }
				}
			}
			dstBlendAlpha = blendedAlpha;
			if( hasOpaqueOrTransparent == 1 )
			{
				int32_t i, j, minDist;
				if( maxAlpha > minAlpha ) { wfMipGen_Alpha_BuildTable4( minAlpha, maxAlpha, dstAlphaTable ); }
				else                      { wfMipGen_Alpha_BuildTable4( maxAlpha, minAlpha, dstAlphaTable ); }
				for( i = 0; i != 16; ++i, ++dstBlendAlphaFactor )
				{
					const uint32_t curAlpha = *dstBlendAlpha; ++dstBlendAlpha;
					if( curAlpha == 0   ) { *dstBlendAlphaFactor = 6; continue; }
					if( curAlpha == 255 ) { *dstBlendAlphaFactor = 7; continue; }
					minDist = wfMipgen_Alpha_DistSq( curAlpha, dstAlphaTable[ 0 ] );
					*dstBlendAlphaFactor = 0;
					for( j = 1; j != 8; ++j )
					{
						const int32_t dist = wfMipgen_Alpha_DistSq( curAlpha, dstAlphaTable[ j ] );
						if( dist < minDist )
						{
							minDist = dist;
							*dstBlendAlphaFactor = j;
						}
					}
				}
			}
			else
			{
				int32_t i, j, minDist;
				if( minAlpha > maxAlpha) { wfMipGen_Alpha_BuildTable6( minAlpha, maxAlpha, dstAlphaTable ); }
				else                     { wfMipGen_Alpha_BuildTable6( maxAlpha, minAlpha, dstAlphaTable ); }
				for( i = 0; i != 16; ++i, ++dstBlendAlphaFactor )
				{
					const uint32_t curAlpha = *dstBlendAlpha; ++dstBlendAlpha;
					minDist = wfMipgen_Alpha_DistSq( curAlpha, dstAlphaTable[ 0 ] );
					*dstBlendAlphaFactor = 0;
					for( j = 1; j != 8; ++j )
					{
						const int32_t dist = wfMipgen_Alpha_DistSq( curAlpha, dstAlphaTable[ j ] );
						if( dist < minDist )
						{
							minDist = dist;
							*dstBlendAlphaFactor = j;
						}
					}
				}
			}
		}
		{ // write alpha
			uint64_t dstAlpha = (uint64_t)(dstAlphaTable[0] << 0 ) | (uint64_t)(dstAlphaTable[1] << 8 ) ;

			dstBlendAlphaFactor = dstBlendAlphaFactors;

			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_00; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_10; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_01; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_11; ++dstBlendAlphaFactor;

			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_20; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_30; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_21; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_31; ++dstBlendAlphaFactor;

			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_02; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_12; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_03; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_13; ++dstBlendAlphaFactor;

			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_22; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_32; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_23; ++dstBlendAlphaFactor;
			dstAlpha |= ( uint64_t )( *dstBlendAlphaFactor ) << WF_MIPGEN_ALPHA_SHIFT_33; ++dstBlendAlphaFactor;

			*( (uint64_t*)pDst ) = dstAlpha;
		}
	}
}
