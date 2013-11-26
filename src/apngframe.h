#ifndef _APNGFRAME_H_
#define _APNGFRAME_H_

#include <png.h>
#include <cstdlib>
#include <string>

namespace apngasm {

  const unsigned DEFAULT_FRAME_NUMERATOR = 100;
  const unsigned DEFAULT_FRAME_DENOMINATOR = 1000;

  typedef struct { unsigned char r, g, b; } rgb;
  typedef struct { unsigned char r, g, b, a; } rgba;

  //Individual APNG frame
  class APNGFrame
  {
  public:
    // Raw pixel data
    unsigned char* pixels(unsigned char* setPixels = NULL);
    unsigned char* _pixels;
    
    // Width and Height
    unsigned int width(unsigned int setWidth = 0);
    unsigned int height(unsigned int setHeight = 0);
    unsigned int _width;
    unsigned int _height;

    // PNG color type
    unsigned char colorType(unsigned char setColorType = 255);
    unsigned char _colorType;
    
    // Palette into
    rgb _palette[256];
    
    //Transparency info
    unsigned char _transparency[256];
    
    //Sizes for palette and transparency records
    int _palleteSize;
    int _transparencySize;
    
    //Delay is numerator/denominator ratio, in seconds
    unsigned int _delayNum;
    unsigned int _delayDen;

    unsigned char ** _rows;

    // Init empty frame for filling in manually
    APNGFrame();
    // Init frame from a PNG image file
    APNGFrame(const std::string &filePath, unsigned delayNum = DEFAULT_FRAME_NUMERATOR, unsigned delayDen = DEFAULT_FRAME_DENOMINATOR);
    // Init frame 
    APNGFrame(rgb *pixels, unsigned char tr[] = NULL, unsigned delayNum = DEFAULT_FRAME_NUMERATOR, unsigned delayDen = DEFAULT_FRAME_DENOMINATOR);
    APNGFrame(rgba *pixels, unsigned delayNum = DEFAULT_FRAME_NUMERATOR, unsigned delayDen = DEFAULT_FRAME_DENOMINATOR);

  private:
  };  // class APNGFrame

} // namespace apngasm

#endif /* _APNGFRAME_H_ */