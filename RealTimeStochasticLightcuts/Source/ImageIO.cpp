#include <cstdlib>
#include <cstdio>
#include <cmath>
#include "..\include\FreeImage.h"
#include "ImageIO.h"
#include <iostream>

using namespace std;

/////////////////////////////////////////////////////////////////////////////
// Deallocate the memory allocated to (*imageData) returned by 
// the function ReadImageFile().
// (*imageData) will be set to NULL.
/////////////////////////////////////////////////////////////////////////////

void ImageIO::DeallocateImageData( uchar **imageData )
{
    free( *imageData );
    (*imageData) = NULL;
}





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

int ImageIO::ReadImageFile( const char *filename, uchar **imageData,
                   int *imageWidth, int *imageHeight, int *numComponents,
				   int flags, bool forceRGBA)
{
// Determine image format.
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType( filename, 0 );
    if( fif == FIF_UNKNOWN ) fif = FreeImage_GetFIFFromFilename( filename );
    if( fif == FIF_UNKNOWN )
    {
        printf( "Error: Cannot determine image format of %s.\n", filename );
        return 0;
    }

// Read image data from file.
    FIBITMAP *dib = NULL;
    if( FreeImage_FIFSupportsReading( fif ) )
        dib = FreeImage_Load( fif, filename, flags );

    if( !dib )
    {
        printf( "Error: Cannot read image file %s.\n", filename );
        return 0;
    }

// Check image type.
    FREE_IMAGE_TYPE fit = FreeImage_GetImageType( dib );
    if ( fit != FIT_BITMAP )
    {
        FreeImage_Unload( dib );
        printf( "Error: Only 8-bits-per-component standard bitmap is supported.\n" );
        return 0;
    }

// Check bits per pixel.
    int bits_per_pixel = FreeImage_GetBPP( dib );
    if ( bits_per_pixel != 8 && bits_per_pixel != 16 && bits_per_pixel != 24 && bits_per_pixel != 32 )
    {
        FreeImage_Unload( dib );
        printf( "Error: Only 8, 16, 24, 32 bits per pixel are supported.\n" );
        return 0;
    }

	if (bits_per_pixel < 24)
	{
		FIBITMAP *temp;
		temp = FreeImage_ConvertTo24Bits(dib);
		FreeImage_Unload(dib);
		dib = temp;
		bits_per_pixel = 24;
	}

    int _numComponents = bits_per_pixel / 8;
    int _imageWidth = FreeImage_GetWidth( dib );
    int _imageHeight = FreeImage_GetHeight( dib );

	int outputNumComponents = (forceRGBA || _numComponents == 3) ? 4 : _numComponents;

    uchar *_imageData = (uchar *) malloc( _imageWidth * _imageHeight * outputNumComponents );
    if ( _imageData == NULL )
    {
        FreeImage_Unload( dib );
        printf( "Error: Not enough memory.\n" );
        return 0;
    }

// Copy image in FIBITMAP to user image data.
    int imageDataCount = 0;

    if ( _numComponents == 1 )
    {
        for( int y = 0; y < _imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < _imageWidth; x++) 
            {
                _imageData[imageDataCount++] = dibData[x];
				if (forceRGBA)
				{
					_imageData[imageDataCount++] = 255;
					_imageData[imageDataCount++] = 255;
					_imageData[imageDataCount++] = 255;
				}
            }
        }
    }
    else if ( _numComponents == 2 )
    {
        for( int y = 0; y < _imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < _imageWidth; x++) 
            {
                _imageData[imageDataCount++] = dibData[0];
                _imageData[imageDataCount++] = dibData[1];
				if (forceRGBA)
				{
					_imageData[imageDataCount++] = 255;
					_imageData[imageDataCount++] = 255;
				}
                dibData += _numComponents;
            }
        }
    }
    else if ( _numComponents == 3 )
    {
        for( int y = 0; y < _imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < _imageWidth; x++) 
            {
                _imageData[imageDataCount++] = dibData[FI_RGBA_RED];
                _imageData[imageDataCount++] = dibData[FI_RGBA_GREEN];
                _imageData[imageDataCount++] = dibData[FI_RGBA_BLUE];
				if (forceRGBA) _imageData[imageDataCount++] = 255;
                dibData += _numComponents;
            }
        }
    }
    else if ( _numComponents == 4 )
    {
        for( int y = 0; y < _imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < _imageWidth; x++) 
            {
                _imageData[imageDataCount++] = dibData[FI_RGBA_RED];
                _imageData[imageDataCount++] = dibData[FI_RGBA_GREEN];
                _imageData[imageDataCount++] = dibData[FI_RGBA_BLUE];
                _imageData[imageDataCount++] = dibData[FI_RGBA_ALPHA];
                dibData += _numComponents;
            }
        }
    }

    FreeImage_Unload( dib );

    (*numComponents) = outputNumComponents;
    (*imageWidth) = _imageWidth;
    (*imageHeight) = _imageHeight;
    (*imageData) = _imageData;
    return 1; 
}





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

int ImageIO::SaveImageFile( const char *filename, const uchar *imageData,
                   int imageWidth, int imageHeight, int numComponents,
				   int flags )
{
// Try to guess the file format from the file extension.
	FREE_IMAGE_FORMAT fif = FreeImage_GetFIFFromFilename( filename );
    if( fif == FIF_UNKNOWN )
    {
        printf( "Error: Cannot determine output image format of %s.\n", filename );
        return 0;
    }

    int bits_per_pixel = numComponents * 8;
    if ( bits_per_pixel != 8 && bits_per_pixel != 16 && bits_per_pixel != 24 && bits_per_pixel != 32 )
    {
        printf( "Error: Only 8, 16, 24, 32 bits per pixel are supported.\n" );
        return 0;
    }

// Check whether user image data can be supported by output image format.
	if( !( FreeImage_FIFSupportsWriting( fif ) && 
		   FreeImage_FIFSupportsExportBPP( fif, bits_per_pixel ) ) )
    {
        printf( "Error: Output image format not supported.\n" );
        return 0;
    }

// Create a FIBITMAP for storing the user image data before writing it to file.
	FIBITMAP *dib = FreeImage_AllocateT( FIT_BITMAP, imageWidth, imageHeight, bits_per_pixel );
	if( !dib )
    {
        FreeImage_Unload( dib );
        printf( "Error: Cannot allocate internal bitmap.\n" );
        return 0;
    }


// Copy user image data to the FIBITMAP.
    int imageDataCount = 0;

    if ( numComponents == 1 )
    {
        for( int y = 0; y < imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < imageWidth; x++) 
            {
                 dibData[x] = imageData[imageDataCount++];
            }
        }
    }
    else if ( numComponents == 2 )
    {
        for( int y = 0; y < imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < imageWidth; x++) 
            {
                dibData[0] = imageData[imageDataCount++];
                dibData[1] = imageData[imageDataCount++];
                dibData += numComponents;
            }
        }
    }
    else if ( numComponents == 3 )
    {
        for( int y = 0; y < imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < imageWidth; x++) 
            {
                dibData[FI_RGBA_RED] = imageData[imageDataCount++];
                dibData[FI_RGBA_GREEN] = imageData[imageDataCount++];
                dibData[FI_RGBA_BLUE] = imageData[imageDataCount++];
                dibData += numComponents;
            }
        }
    }
    else if ( numComponents == 4 )
    {
        for( int y = 0; y < imageHeight; y++ )
        {
            BYTE *dibData = FreeImage_GetScanLine( dib, y );

            for( int x = 0; x < imageWidth; x++) 
            {
                dibData[FI_RGBA_RED] = imageData[imageDataCount++];
                dibData[FI_RGBA_GREEN] = imageData[imageDataCount++];
                dibData[FI_RGBA_BLUE] = imageData[imageDataCount++];
                dibData[FI_RGBA_ALPHA] = imageData[imageDataCount++];
                dibData += numComponents;
            }
        }
    }
	
// Write image in FIBITMAP to file.
	if ( !FreeImage_Save( fif, dib, filename, flags ) )
    {
        FreeImage_Unload( dib );
        printf( "Error: Cannot save image file %s.\n", filename );
        return 0;
    }

    FreeImage_Unload( dib );
    return 1; 
}

int ImageIO::ReadImageFile(const char * filename, float ** imageData, int * imageWidth, int * imageHeight, int * numComponents, int flags)
{
	// Determine image format.
	FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);
	if (fif == FIF_UNKNOWN) fif = FreeImage_GetFIFFromFilename(filename);
	if (fif == FIF_UNKNOWN)
	{
		printf("Error: Cannot determine image format of %s.\n", filename);
		return 0;
	}
	else
	{
		printf("Type code: %d\n", fif);
	}

	// Read image data from file.
	FIBITMAP *dib = NULL;
	if (FreeImage_FIFSupportsReading(fif))
		dib = FreeImage_Load(fif, filename, flags);

	if (!dib)
	{
		printf("Error: Cannot read image file %s.\n", filename);
		return 0;
	}

	// Check bits per pixel.
	int bits_per_pixel = FreeImage_GetBPP(dib);

	int _numComponents = bits_per_pixel / 32;
	int _imageWidth = FreeImage_GetWidth(dib);
	int _imageHeight = FreeImage_GetHeight(dib);
	float *_imageData = (float *)malloc(_imageWidth * _imageHeight * _numComponents * 4);
	if (_imageData == NULL)
	{
		FreeImage_Unload(dib);
		printf("Error: Not enough memory.\n");
		return 0;
	}

	// Copy image in FIBITMAP to user image data.
	int imageDataCount = 0;

	if (_numComponents == 1)
	{
		for (int y = 0; y < _imageHeight; y++)
		{
			float *dibData = (float *)FreeImage_GetScanLine(dib, y);
			for (int x = 0; x < _imageWidth; x++)
			{
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_BLUE], sizeof(float));
			}
		}
	}
	else if (_numComponents == 2)
	{
		for (int y = 0; y < _imageHeight; y++)
		{
			float *dibData = (float *)FreeImage_GetScanLine(dib, y);
			for (int x = 0; x < _imageWidth; x++)
			{
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_GREEN], sizeof(float));
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_BLUE], sizeof(float));
				dibData += _numComponents;
			}
		}
	}
	else if (_numComponents == 3)
	{
		for (int y = 0; y < _imageHeight; y++)
		{
			float *dibData = (float *)FreeImage_GetScanLine(dib, y);
			for (int x = 0; x < _imageWidth; x++)
			{
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_RED], sizeof(float));
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_GREEN], sizeof(float));
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_BLUE], sizeof(float));
				dibData += _numComponents;
			}
		}
	}
	else if (_numComponents == 4)
	{
		for (int y = 0; y < _imageHeight; y++)
		{
			float *dibData = (float *)FreeImage_GetScanLine(dib, y);
			for (int x = 0; x < _imageWidth; x++)
			{
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_BLUE], sizeof(float));
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_GREEN], sizeof(float));
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_RED], sizeof(float));
				memcpy(&_imageData[imageDataCount++], &dibData[FI_RGBA_ALPHA], sizeof(float));
				//if (_imageData[imageDataCount - 4] > 1)
				//printf("pixel %d, r: %f, g: %f, b: %f a: %f\n", x + y*_imageWidth, _imageData[imageDataCount - 4], _imageData[imageDataCount - 3], _imageData[imageDataCount - 2], _imageData[imageDataCount - 1]);
				dibData += _numComponents;
			}
		}
	}

	FreeImage_Unload(dib);

	(*numComponents) = _numComponents;
	(*imageWidth) = _imageWidth;
	(*imageHeight) = _imageHeight;
	(*imageData) = _imageData;
	return 1;
}
