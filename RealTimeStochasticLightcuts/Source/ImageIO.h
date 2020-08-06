//ImageIO from CS4247 GRAPHICS RENDERING TECHNIQUES taught by Dr Kok-Lim LOW at 
//National University of Singapore

#pragma once
#include "FreeImage.h"
typedef unsigned char uchar;


class ImageIO
{
public:

	/////////////////////////////////////////////////////////////////////////////
	// Deallocate the memory allocated to (*imageData) returned by 
	// the function ReadImageFile().
	// (*imageData) will be set to NULL.
	/////////////////////////////////////////////////////////////////////////////

	static void DeallocateImageData( uchar **imageData );


	/////////////////////////////////////////////////////////////////////////////
	// Read an image from the input filename. 
	// Returns 1 if successful or 0 if unsuccessful.
	// The returned image data will be pointed to by (*imageData).
	// The image width, image height, and number of components (color channels) 
	// per pixel will be returned in (*imageWidth), (*imageHeight),
	// and (*numComponents).
	// The value of (*numComponents) can be 1, 2, 3 or 4.
	// The returned image data is always packed tightly with red, green, blue,
	// and alpha arranged from lower to higher memory addresses. 
	// Each color channel take one byte.
	// The first pixel (origin of the image) is at the bottom-left of the image.
	/////////////////////////////////////////////////////////////////////////////

	static int ReadImageFile( const char *filename, uchar **imageData,
					   int *imageWidth, int *imageHeight, int *numComponents,
					   int flags = 0, bool forceRGBA = false );


	/////////////////////////////////////////////////////////////////////////////
	// Save an image to the output filename. 
	// Returns 1 if successful or 0 if unsuccessful.
	// The input image data is pointed to by imageData.
	// The image width, image height, and number of components (color channels) 
	// per pixel are provided in imageWidth, imageHeight, numComponents.
	// The value of numComponents can be 1, 2, 3 or 4.
	// Note that some numComponents cannot be supported by some image file formats. 
	// The input image data is assumed packed tightly with red, green, blue,
	// and alpha arranged from lower to higher memory addresses. 
	// Each color channel take one byte.
	// The first pixel (origin of the image) is at the bottom-left of the image.
	/////////////////////////////////////////////////////////////////////////////

	static int SaveImageFile( const char *filename, const uchar *imageData,
					   int imageWidth, int imageHeight, int numComponents,
					   int flags = 0 );


	static int ReadImageFile(const char *filename, float **imageData,
		int *imageWidth, int *imageHeight, int *numComponents,
		int flags = 0);

};

