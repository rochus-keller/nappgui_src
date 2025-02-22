/*
 * NAppGUI Cross-platform C SDK
 * 2015-2022 Francisco Garcia Collado
 * MIT Licence
 * https://nappgui.com/en/legal/license.html
 *
 * File: osimage.cpp
 *
 */

/* Images */

#include "image.inl"
#include "osimage.inl"
#include "dctx.inl"
#include "dctx_win.inl"
#include "imgutils.inl"
#include "bmem.h"
#include "buffer.h"
#include "cassert.h"
#include "color.h"
#include "heap.h"
#include "palette.h"
#include "pixbuf.h"
#include "ptr.h"
#include "stream.h"
#include "unicode.h"

#if !defined(__WINDOWS__)
#error This file is only for WINDOWS
#endif

#include "shlwapi.h"
#include "draw2d_win.ixx"
#pragma comment (lib, "gdiplus.lib")
#pragma comment (lib, "Shlwapi.lib")

typedef IStream* (__stdcall *fnSHCreateMemStream)(const BYTE *pInit, UINT cbInit);
static fnSHCreateMemStream i_kSHCreateMemStream = NULL;
static ULONG_PTR i_GDIPLUSTOKEN = 0L;

struct _osimage_t
{
    Gdiplus::Bitmap *bitmap;
    HBITMAP hbitmap;
    COLORREF hbitmap_background;
    LONG hbitmap_width;
    LONG hbitmap_height;
};

/*---------------------------------------------------------------------------*/

void osimage_alloc_globals(void)
{
	// The SHCreateMemStream function has existed since Windows 2000, but was only added to the header files
	// in Windows Vista. To use this function in Windows 2000 and Windows XP, we need to explicitly load it
	// from the DLL. SHCreateMemStream creates a COM stream object from an array of bytes in local memory.
    // To use it on earlier systems, you must call it directly from the Shlwapi.dll file as ordinal 12.
	HMODULE libShlWapi;
	libShlWapi = LoadLibrary(L"shlwapi.dll");
	cassert_no_null(libShlWapi);
    Gdiplus::GdiplusStartupInput startup;
    Gdiplus::GdiplusStartup(&i_GDIPLUSTOKEN, &startup, NULL);
    #pragma warning(disable:4191)
	i_kSHCreateMemStream = (fnSHCreateMemStream)GetProcAddress(libShlWapi, (LPCSTR)12);
    #pragma warning(default:4191)
	cassert_no_null(i_kSHCreateMemStream);
	FreeLibrary(libShlWapi);
}

/*---------------------------------------------------------------------------*/

void osimage_dealloc_globals(void)
{
    Gdiplus::GdiplusShutdown(i_GDIPLUSTOKEN);
    i_GDIPLUSTOKEN = 0L;
}

/*---------------------------------------------------------------------------*/

typedef struct _argb_t
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} _ARGB;

#define ARGB(r,g,b,a)\
    ((uint32_t)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))

#define ABGR(r,g,b,a)\
    ((uint32_t)((((a)&0xff)<<24)|(((b)&0xff)<<16)|(((g)&0xff)<<8)|((r)&0xff)))

/*---------------------------------------------------------------------------*/

OSImage *osimage_create_from_pixels(const uint32_t width, const uint32_t height, const pixformat_t format, const byte_t *pixel_data)
{
    Gdiplus::Bitmap *bitmap = NULL;
    Gdiplus::BitmapData bdata;
    byte_t *dest_data = NULL;
    uint32_t i, j, stride;
    
    switch (format) {
    case ekGRAY8:
        bitmap = new Gdiplus::Bitmap((INT)width, (INT)height, PixelFormat8bppIndexed);
        bitmap->LockBits(NULL, Gdiplus::ImageLockModeWrite, PixelFormat8bppIndexed, &bdata);
        stride = bdata.Stride - width;
        dest_data = (byte_t*)bdata.Scan0;
        for (j = 0; j < height; ++j)
        {
            for (i = 0; i < width; ++i)
            {
                dest_data[0] = pixel_data[0];
                dest_data += 1;
                pixel_data += 1;
            }

            dest_data += stride;
        }
            
        bitmap->UnlockBits(&bdata);
        bitmap->SetPalette(dctx_8bpp_grayscale_palette());
        break;

    case ekRGB24:
        bitmap = new Gdiplus::Bitmap((INT)width, (INT)height, PixelFormat24bppRGB);
        bitmap->LockBits(NULL, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &bdata);
        stride = bdata.Stride - (width * 3);
        dest_data = (byte_t*)bdata.Scan0;
        for (j = 0; j < height; ++j)
        {
            for (i = 0; i < width; ++i)
            {
                dest_data[0] = pixel_data[2];
                dest_data[1] = pixel_data[1];
                dest_data[2] = pixel_data[0];
                dest_data += 3;
                pixel_data += 3;
            }

            dest_data += stride;
        }
            
        bitmap->UnlockBits(&bdata);
        break;

    case ekRGBA32:
        bitmap = new Gdiplus::Bitmap((INT)width, (INT)height, PixelFormat32bppARGB);
        bitmap->LockBits(NULL, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bdata);
        stride = bdata.Stride - (width * 4);
        dest_data = (byte_t*)bdata.Scan0;
        for (j = 0; j < height; ++j)
        {
            for (i = 0; i < width; ++i)
            {
                *(uint32_t*)dest_data = ARGB(pixel_data[0], pixel_data[1], pixel_data[2], pixel_data[3]);
                dest_data += 4;
                pixel_data += 4;
            }

            dest_data += stride;
        }
            
        bitmap->UnlockBits(&bdata);
        break;

    case ekINDEX1:
    case ekINDEX2:
    case ekINDEX4:
    case ekINDEX8:
    case ekFIMAGE:
    cassert_default();
    }

    {
        OSImage *image = heap_new0(OSImage);
        image->bitmap = bitmap;
        return image;
    }
}

/*---------------------------------------------------------------------------*/

OSImage *osimage_create_from_data(const byte_t *data, const uint32_t size_in_bytes)
{
    IStream *stream = NULL;
    Gdiplus::Bitmap *bitmap = NULL;
	cassert_no_null(data);
    cassert(size_in_bytes > 0);
    stream = i_kSHCreateMemStream((const BYTE*)data, (UINT)size_in_bytes);
    bitmap = Gdiplus::Bitmap::FromStream(stream, TRUE);
    stream->Release();

    {
        OSImage *image = heap_new0(OSImage);
        image->bitmap = bitmap;
        return image;
    }
}

/*---------------------------------------------------------------------------*/

OSImage *osimage_create_from_type(const char_t *file_type)
{
    WCHAR wextension[64];
    DWORD dwFileAttributes;
    uint32_t num_bytes;

    if (strcmp(file_type, ".") == 0)
    {
        num_bytes = unicode_convers("\fdsfg", (char_t*)wextension, ekUTF8, ekUTF16, sizeof(wextension));
        dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
        num_bytes = unicode_convers(file_type, (char_t*)wextension, ekUTF8, ekUTF16, sizeof(wextension));
        dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    }

    if (num_bytes < sizeof(wextension))
    {
        SHFILEINFO psfi;
        DWORD_PTR ok;
        Gdiplus::Bitmap *bitmap = NULL;
        ok = SHGetFileInfo(wextension, dwFileAttributes, &psfi, sizeof(psfi), SHGFI_ICON | SHGFI_SMALLICON /*SHGFI_ICON | SHGFI_LARGEICON */| SHGFI_USEFILEATTRIBUTES);
        cassert(ok != 0);
        bitmap = Gdiplus::Bitmap::FromHICON(psfi.hIcon);

        {
            OSImage *image = heap_new0(OSImage);
            image->bitmap = bitmap;
            return image;
        }
    }
    else
    {
        cassert(FALSE);
        return NULL;
    }
}

/*---------------------------------------------------------------------------*/

OSImage *osimage_create_scaled(const OSImage *image, const uint32_t new_width, const uint32_t new_height)
{
    UINT current_width, current_height;
    real32_t scale_factor_x, scale_factor_y;
    Gdiplus::Bitmap *bitmap = NULL;
    cassert_no_null(image);
    cassert_no_null(image->bitmap);
    current_width = image->bitmap->GetWidth();
    current_height = image->bitmap->GetHeight();
    scale_factor_x = (real32_t)new_width / (real32_t)current_width;
    scale_factor_y = (real32_t)new_height / (real32_t)current_height;
    bitmap = new Gdiplus::Bitmap((INT)new_width, (INT)new_height, image->bitmap->GetPixelFormat());
    Gdiplus::Graphics g(bitmap);
    g.ScaleTransform(scale_factor_x, scale_factor_y);
    g.DrawImage(image->bitmap, (INT)0, (INT)0, (INT)current_width, (INT)current_height);

    {
        OSImage *nimage = heap_new0(OSImage);
        nimage->bitmap = bitmap;
        return nimage;
    }
}

/*---------------------------------------------------------------------------*/

OSImage *osimage_from_context(DCtx **ctx)
{
    Gdiplus::Bitmap *bitmap;
    cassert_no_null(ctx);
    cassert_no_null(*ctx);
    bitmap = (*ctx)->bitmap;
    cassert_no_null(bitmap);

    //if (ctx->format == ekGRAY8)
    //    ctx->bitmap->SetPalette(_dctx_8bpp_grayscale_palette());

    // Optimal grayscale
    //if (/*(*ctx)->format == ekGRAY4 ||*/ (*ctx)->format == ekGRAY8)
    //{
    //    INT psize = (*ctx)->bitmap->GetPaletteSize();
    //    Gdiplus::ColorPalette *palette = (Gdiplus::ColorPalette*)heap_malloc((uint32_t)psize, "ImageImpPalette");
    //    (*ctx)->bitmap->GetPalette(palette, psize);
    //    //cassert(palette->Count == 16);

    //    for (UINT i = 0; i < palette->Count; ++i)
    //    {
    //        _ARGB *color = (_ARGB*)(palette->Entries + i);
    //        color->r = (uint8_t)((77 * (uint32_t)color->r + 148 * (uint32_t)color->g + 30 * (uint32_t)color->b) / 255);
    //        //float c = .299f * ((float)color->r / 255.f) + .587f * ((float)color->g / 255.f) + .114f * ((float)color->b / 255.f);
    //        //color->r = (uint8_t)(c * 255.f);
    //        color->g = color->r;

    //        color->b = color->r;
    //    }

    //    (*ctx)->bitmap->SetPalette(palette);

    //    if (palette != NULL)
    //        heap_free((byte_t**)&palette, (uint32_t)psize, "ImageImpPalette");
    //}

    dctx_destroy(ctx);

    {
        OSImage *image = heap_new0(OSImage);
        image->bitmap = bitmap;
        return image;
    }
}

/*---------------------------------------------------------------------------*/

void osimage_destroy(OSImage **image)
{
    cassert_no_null(image);
    cassert_no_null(*image);

    delete (*image)->bitmap;
    if ((*image)->hbitmap != NULL)
    {
        BOOL ret = DeleteObject((*image)->hbitmap);
        cassert_unref(ret != 0, ret);
    }

    heap_delete(image, OSImage);
}

/*---------------------------------------------------------------------------*/

static bool_t i_is_gray_palette(const Gdiplus::ColorPalette *palette)
{
    for (UINT i = 0; i < palette->Count; ++i)
    {
        _ARGB *color = (_ARGB*)(palette->Entries + i);
        // Has alpha
        if (color->a != 255)
            return FALSE;
        // Gray i-tone
        if (color->r != color->g || color->r != color->b)
            return FALSE;
    }

    return TRUE;
}

/*---------------------------------------------------------------------------*/

static bool_t i_is_alpha_palette(const Gdiplus::ColorPalette *palette)
{
    for (UINT i = 0; i < palette->Count; ++i)
    {
        _ARGB *color = (_ARGB*)(palette->Entries + i);
        if (color->a != 255)
            return TRUE;
    }

    return FALSE;
}

/*---------------------------------------------------------------------------*/

static void i_indexed_info(Gdiplus::Bitmap *bitmap, pixformat_t *format, Pixbuf **pixels)
{
    uint32_t psize = (uint32_t)bitmap->GetPaletteSize();
    Gdiplus::ColorPalette *pal = (Gdiplus::ColorPalette*)heap_malloc(psize, "ImageImpPalette");
    cassert_no_null(format);
    bitmap->GetPalette(pal, (INT)psize);

    if (i_is_gray_palette(pal) == TRUE)
        *format = ekGRAY8;
    else if (i_is_alpha_palette(pal) == TRUE)
        *format = ekRGBA32;
    else
        *format = ekRGB24;

    if (pixels != NULL)
    {
        Palette *palette = palette_create((uint32_t)pal->Count);
        color_t *color = palette_colors(palette);
        Gdiplus::PixelFormat pf = bitmap->GetPixelFormat();
        Gdiplus::BitmapData data;
        register uint32_t i;
        uint32_t ibpp = 8;

        for (i = 0; i < pal->Count; ++i)
        {
            _ARGB *argb = (_ARGB*)(pal->Entries + i);
            color[i] = color_rgba(argb->r, argb->g, argb->b, argb->a);
        }

        switch(pf) {
        case PixelFormat1bppIndexed: ibpp = 1; break;            
        case PixelFormat4bppIndexed: ibpp = 4; break;
        case PixelFormat8bppIndexed: ibpp = 8; break;
        cassert_default();
        }

        bitmap->LockBits(NULL, Gdiplus::ImageLockModeRead, pf, &data);

        switch(*format) {
        case ekGRAY8:
            *pixels = imgutil_indexed_to_gray((uint32_t)data.Width, (uint32_t)data.Height, (const byte_t*)data.Scan0, (uint32_t)data.Stride, ibpp, color);
            break;
        case ekRGB24:
            *pixels = imgutil_indexed_to_rgb((uint32_t)data.Width, (uint32_t)data.Height, (const byte_t*)data.Scan0, (uint32_t)data.Stride, ibpp, color);
            break;
        case ekRGBA32:
            *pixels = imgutil_indexed_to_rgba((uint32_t)data.Width, (uint32_t)data.Height, (const byte_t*)data.Scan0, (uint32_t)data.Stride, ibpp, color);
            break;

        case ekINDEX1:
        case ekINDEX2:
        case ekINDEX4:
        case ekINDEX8:
        case ekFIMAGE:
        cassert_default();
        }

        bitmap->UnlockBits(&data);
        palette_destroy(&palette);
    }

    heap_free((byte_t**)&pal, (uint32_t)psize, "ImageImpPalette");
}

/*---------------------------------------------------------------------------*/

static bool_t i_rgb_is_gray(Gdiplus::Bitmap *bitmap, const uint32_t bpp)
{
    bool_t gray = TRUE;
    Gdiplus::PixelFormat pf = bitmap->GetPixelFormat();
    Gdiplus::BitmapData data;
    const byte_t *src = NULL;
    UINT i, j;

    bitmap->LockBits(NULL, Gdiplus::ImageLockModeRead, pf, &data);
    src = (const byte_t*)data.Scan0;

    for (j = 0; j < data.Height && gray == TRUE; ++j)
    {
        for (i = 0; i < data.Width; ++i)
        {
            if (src[0] != src[1] || src[0] != src[2])
            {
                gray = FALSE;
                break;
            }

            src += bpp;
        }

        src += data.Stride - (data.Width * bpp);
    }

    bitmap->UnlockBits(&data);
    return gray;
}

/*---------------------------------------------------------------------------*/

static bool_t i_rgba_has_alpha(Gdiplus::Bitmap *bitmap)
{
    bool_t alpha = FALSE;
    const uint32_t bpp = 4;
    Gdiplus::PixelFormat pf = bitmap->GetPixelFormat();
    Gdiplus::BitmapData data;
    const byte_t *src = NULL;
    UINT i, j;

    cassert(pf == PixelFormat32bppARGB);
    bitmap->LockBits(NULL, Gdiplus::ImageLockModeRead, pf, &data);
    src = (const byte_t*)data.Scan0;

    for (j = 0; j < data.Height && alpha == FALSE; ++j)
    {
        for (i = 0; i < data.Width; ++i)
        {
            if (src[3] != 255)
            {
                alpha = TRUE;
                break;
            }

            src += bpp;
        }

        src += data.Stride - (data.Width * bpp);
    }

    bitmap->UnlockBits(&data);
    return alpha;
}

/*---------------------------------------------------------------------------*/

static Pixbuf *i_rgb24_pixels(Gdiplus::Bitmap *bitmap, const pixformat_t format)
{
    Pixbuf *pixels = NULL;
    Gdiplus::PixelFormat pf = bitmap->GetPixelFormat();
    Gdiplus::BitmapData data;
    uint32_t bpp = (uint32_t)((pf == PixelFormat24bppRGB) ? 3 : 4);
    const byte_t *src = NULL;
    byte_t *dest = NULL;
    UINT i, j;

    cassert(format == ekGRAY8 || format == ekRGB24);
    bitmap->LockBits(NULL, Gdiplus::ImageLockModeRead, pf, &data);
    pixels = pixbuf_create((uint32_t)data.Width, (uint32_t)data.Height, format);
    src = (const byte_t*)data.Scan0;
    dest = pixbuf_data(pixels);

    if (format == ekRGB24)
    {
        for (j = 0; j < data.Height; ++j)
        {
            for (i = 0; i < data.Width; ++i)
            {
                dest[0] = src[2];
                dest[1] = src[1];
                dest[2] = src[0];
                dest += 3;
                src += bpp;
            }

            src += data.Stride - (data.Width * bpp);
        }
    }
    else
    {
        cassert(format == ekGRAY8);
        for (j = 0; j < data.Height; ++j)
        {
            for (i = 0; i < data.Width; ++i)
            {
                dest[0] = src[0];
                cassert(dest[0] == src[1]);
                cassert(dest[0] == src[2]);
                dest += 1;
                src += bpp;
            }

            src += data.Stride - (data.Width * bpp);
        }
    }

    bitmap->UnlockBits(&data);
    return pixels;
}

/*---------------------------------------------------------------------------*/

static Pixbuf *i_rgba32_pixels(Gdiplus::Bitmap *bitmap, const pixformat_t format)
{
    Pixbuf *pixels = NULL;
    Gdiplus::PixelFormat pf = bitmap->GetPixelFormat();
    Gdiplus::BitmapData data;
    const byte_t *src = NULL;
    byte_t *dest = NULL;
    UINT i, j;

    bitmap->LockBits(NULL, Gdiplus::ImageLockModeRead, pf, &data);
    pixels = pixbuf_create((uint32_t)data.Width, (uint32_t)data.Height, format);
    src = (const byte_t*)data.Scan0;
    dest = pixbuf_data(pixels);

    if (format == ekRGBA32)
    {
        for (j = 0; j < data.Height; ++j)
        {
            for (i = 0; i < data.Width; ++i)
            {
                *((uint32_t*)dest) = ABGR(src[2], src[1], src[0], src[3]);
                src += 4;
                dest += 4;
            }

            src += data.Stride - (data.Width * 4);
        }
    }
    else if (format == ekRGB24)
    {
        for (j = 0; j < data.Height; ++j)
        {
            for (i = 0; i < data.Width; ++i)
            {
                dest[0] = src[2];
                dest[1] = src[1];
                dest[2] = src[0];
                dest += 3;
                src += 4;
            }

            src += data.Stride - (data.Width * 4);
        }
    }
    else
    {
        cassert(format == ekGRAY8);
        for (j = 0; j < data.Height; ++j)
        {
            for (i = 0; i < data.Width; ++i)
            {
                dest[0] = src[0];
                cassert(dest[0] == src[1]);
                cassert(dest[0] == src[2]);
                dest += 1;
                src += 4;
            }

            src += data.Stride - (data.Width * 4);
        }
    }

    bitmap->UnlockBits(&data);
    return pixels;
}

/*---------------------------------------------------------------------------*/

void osimage_info(const OSImage *image, uint32_t *width, uint32_t *height, pixformat_t *format, Pixbuf **pixels)
{
    Gdiplus::Bitmap *bitmap = NULL;
    cassert_no_null(image);
    bitmap = image->bitmap;

    if (width != NULL)
        *width = (uint32_t)bitmap->GetWidth();

    if (height != NULL)
        *height = (uint32_t)bitmap->GetHeight();

    if (format != NULL || pixels != NULL)
    {
        Gdiplus::PixelFormat pf = bitmap->GetPixelFormat();
        pixformat_t lformat = ENUM_MAX(pixformat_t);

        switch (pf) {
        case PixelFormat1bppIndexed:
        case PixelFormat4bppIndexed:
        case PixelFormat8bppIndexed:
            i_indexed_info(bitmap, &lformat, pixels);
            break;

        case PixelFormat24bppRGB:
        case PixelFormat32bppRGB:
            if (i_rgb_is_gray(bitmap, (uint32_t)((pf == PixelFormat24bppRGB) ? 3 : 4)) == TRUE)
                lformat = ekGRAY8;
            else
                lformat = ekRGB24;

            if (pixels != NULL)
                *pixels = i_rgb24_pixels(bitmap, lformat);

            break;

        case PixelFormat32bppARGB:
            if (i_rgba_has_alpha(bitmap) == TRUE)
                lformat = ekRGBA32;
            else if (i_rgb_is_gray(bitmap, 4) == TRUE)
                lformat = ekGRAY8;
            else
                lformat = ekRGB24;

            if (pixels != NULL)
                *pixels = i_rgba32_pixels(bitmap, lformat);

            break;

        cassert_default();
        }

        if (format != NULL)
            *format = lformat;
    }
}

/*---------------------------------------------------------------------------*/

static bool_t i_get_encoder(const codec_t codec, CLSID *clsid)
{
    const WCHAR *encoder_mime = NULL;
    bool_t found = FALSE;

    switch (codec) {
    case ekJPG:
        encoder_mime = L"image/jpeg";
        break;
    case ekPNG:
        encoder_mime = L"image/png";
        break; 
    case ekBMP:
        encoder_mime = L"image/bmp";
        break;
    case ekGIF:
        encoder_mime = L"image/gif";
        break;
    cassert_default();
    }

    if (encoder_mime != NULL)
    {
        UINT num = 0;
        UINT size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size != 0)
        {
            Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)heap_malloc((uint32_t)size, "ImageImpEncoder");
            Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
            for (UINT i = 0; i < num; ++i)
            {
                if (wcscmp(pImageCodecInfo[i].MimeType, encoder_mime) == 0)
                {
                    *clsid = pImageCodecInfo[i].Clsid;
                    found = TRUE;
                    break;
                }    
            }
                 
            heap_free((byte_t**)&pImageCodecInfo, (uint32_t)size, "ImageImpEncoder");
        }
    }

    return found;
}

/*---------------------------------------------------------------------------*/

bool_t osimage_available_codec(const OSImage *image, const codec_t codec)
{
    CLSID clsid;
    unref(image);
    return i_get_encoder(codec, &clsid);
}

/*---------------------------------------------------------------------------*/

void osimage_write(const OSImage *image, const codec_t codec, Stream *stm)
{
    Gdiplus::Bitmap *bitmap = NULL;
    CLSID clsid;
    IStream *stream = NULL;
    Gdiplus::Status status = Gdiplus::NotImplemented;
    uint32_t size = 0;
    byte_t *data = NULL;
    cassert_no_null(image);
    bitmap = image->bitmap;

    if (i_get_encoder(codec, &clsid) == FALSE)
        return;

#if _MSC_VER > 1400
    // Check with euroval 'testfit'
    stream = SHCreateMemStream(NULL, 0);
#else
    stream = i_kSHCreateMemStream(NULL, 0);
#endif

    status = bitmap->Save(stream, &clsid);
    cassert(status == Gdiplus::Ok);

    {
        STATSTG stats = {0};
        stream->Stat(&stats, 0);
        cassert(stats.cbSize.QuadPart <= 0xFFFFFFFF);
        size = (uint32_t)stats.cbSize.QuadPart;
    }

    data = heap_malloc(size, "ImgEncoded");

    {
        LARGE_INTEGER offset;
        HRESULT res;
        offset.QuadPart = 0L;
        res = stream->Seek(offset, SEEK_SET, NULL);
        cassert(res == S_OK);
    }

    {
        ULONG readed;
        HRESULT res;
        res = stream->Read((void*)data, (ULONG)size, &readed);
        cassert(res == S_OK);
        cassert(readed == (ULONG)size);
    }

    stm_write(stm, data, size);
    heap_free(&data, size, "ImgEncoded");
    stream->Release();
}

/*---------------------------------------------------------------------------*/

void osimage_frames(const OSImage *image, uint32_t *num_frames, uint32_t *num_loops)
{
    Gdiplus::Bitmap *bitmap = NULL;
    GUID dimension;
    cassert_no_null(image);
    cassert_no_null(num_frames);
    unref(num_loops);
    bitmap = image->bitmap;
    bitmap->GetFrameDimensionsList(&dimension, 1);
    *num_frames = (uint32_t)bitmap->GetFrameCount(&dimension);
}

/*---------------------------------------------------------------------------*/

void osimage_frame(const OSImage *image, const uint32_t frame_index, real32_t *frame_length)
{
    Gdiplus::Bitmap *bitmap = NULL;
    UINT property_size;
    Gdiplus::PropertyItem *property_item = NULL;
    cassert_no_null(image);
    cassert_no_null(frame_length);
    bitmap = image->bitmap;
    property_size = bitmap->GetPropertyItemSize(PropertyTagFrameDelay);
    property_item = (Gdiplus::PropertyItem*)heap_malloc(property_size, "ImgImpPropItem");
    bitmap->GetPropertyItem(PropertyTagFrameDelay, property_size, property_item);
    cassert(property_item->type == PropertyTagTypeLong);
    *frame_length = 0.01f * (real32_t)(((long*)property_item->value)[frame_index]);
    heap_free((byte_t**)&property_item, property_size, "ImgImpPropItem");
}

/*---------------------------------------------------------------------------*/

void *osimage_bitmap(const OSImage *osimage)
{
    cassert_no_null(osimage);
    return osimage->bitmap;
}

/*---------------------------------------------------------------------------*/

HBITMAP osimage_hbitmap(const Image *image, COLORREF background)
{
    OSImage *osimage = (OSImage*)image_native(image);
    Gdiplus::Color c;
    HBITMAP hbitmap;
    cassert_no_null(osimage);
    c.SetFromCOLORREF(background);
    Gdiplus::Status status = osimage->bitmap->GetHBITMAP(c, &hbitmap);
    cassert_unref(status == Gdiplus::Ok, status);
    return hbitmap;
}

/*---------------------------------------------------------------------------*/

HBITMAP osimage_hbitmap_cache(const Image *image, COLORREF background, LONG *width, LONG *height)
{
    OSImage *osimage = (OSImage*)image_native(image);
    cassert_no_null(osimage);

    if (osimage->hbitmap != NULL && osimage->hbitmap_background != background)
    {
        BOOL ret = DeleteObject(osimage->hbitmap);
        cassert_unref(ret != 0, ret);
        osimage->hbitmap = NULL;
    }

    if (osimage->hbitmap == NULL)
    {
        BITMAP bm;
        HBITMAP hbitmap = osimage_hbitmap(image, background);
        osimage->hbitmap = hbitmap;
        GetObject(hbitmap, sizeof(bm), &bm);
        osimage->hbitmap_width = bm.bmWidth;
        osimage->hbitmap_height = bm.bmHeight;
        osimage->hbitmap_background = background;
    }

    *width = osimage->hbitmap_width;
    *height = osimage->hbitmap_height;
    return osimage->hbitmap;
}

/*---------------------------------------------------------------------------*/

HBITMAP osimage_transparent_hbitmap(const uint32_t width, const uint32_t height)
{
    HBITMAP hbitmap = NULL;
    register uint32_t size = 4 * width * height;
    BYTE *pixel_data = (BYTE*)heap_malloc(size, "OSImageTHBitmap");
    Gdiplus::Bitmap *bitmap = NULL;
    register uint32_t i;

    for (i = 0; i < size; i += 4)
        *((uint32_t*)(pixel_data + i)) = 0x000000FF;

    bitmap = new Gdiplus::Bitmap((INT)width, (INT)height, (INT)width * 4, PixelFormat32bppARGB, pixel_data);
    cassert_no_null(bitmap);
    heap_free((byte_t**)&pixel_data, size, "OSImageTHBitmap");

    {
        Gdiplus::Status status = bitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbitmap);
        cassert_unref(status == Gdiplus::Ok, status);
    }

    delete bitmap;
    return hbitmap;
}

/*---------------------------------------------------------------------------*/

//static void i_masks(HBITMAP bitmap, int cx, int cy, COLORREF transparent, HBITMAP *and_mask, HBITMAP *xor_mask)
//{
//    HDC hdc = GetDC(NULL);
//    HDC hmain = CreateCompatibleDC(hdc); 
//    HDC hand = CreateCompatibleDC(hdc); 
//    HDC hxor = CreateCompatibleDC(hdc); 
//  
//    *and_mask = CreateCompatibleBitmap(hdc, cx, cy);
//    *xor_mask = CreateCompatibleBitmap(hdc, cx, cy);
//
//    HBITMAP old1 = (HBITMAP)SelectObject(hmain, bitmap);
//    HBITMAP old2 = (HBITMAP)SelectObject(hand, *and_mask);
//    HBITMAP old3 = (HBITMAP)SelectObject(hxor, *xor_mask);
//
//    //Scan each pixel of the souce bitmap and create the masks
//    COLORREF pixel;
//    for(int x = 0; x < cx; ++x)
//    {
//        for(int y = 0; y < cy; ++y)
//        {
//            pixel = GetPixel(hmain, x, y);
//            if(pixel == transparent)
//            {
//                SetPixel(hand, x, y, RGB(255,255,255));
//                SetPixel(hxor, x, y, RGB(0,0,0));
//            }
//            else
//            {
//                SetPixel(hand, x, y, RGB(0,0,0));
//                SetPixel(hxor, x, y, pixel);
//            }
//        }
//    }
//  
//    SelectObject(hmain, old1);
//    SelectObject(hand, old2);
//    SelectObject(hxor, old3);
//
//    DeleteDC(hxor);
//    DeleteDC(hand);
//    DeleteDC(hmain);
//    ReleaseDC(NULL, hdc);
//}

/*---------------------------------------------------------------------------*/

HCURSOR osimage_hcursor(const Image *image, const uint32_t hot_x, const uint32_t hot_y)
{
    // HCURSOR direct from Gdiplus::Bitmap ;-)
    // http://csharphelper.com/blog/2017/01/convert-a-bitmap-into-a-cursor-in-c/
    OSImage *osimage = (OSImage*)image_native(image);
    HICON icon = NULL;
    ICONINFO info;
    HCURSOR hcursor = NULL;
    BOOL ret = FALSE;
    Gdiplus::Status st = osimage->bitmap->GetHICON(&icon);
    cassert_unref(st == Gdiplus::Ok, st);
    GetIconInfo(icon, &info);
    info.xHotspot = (DWORD)hot_x;
    info.yHotspot = (DWORD)hot_y;
    info.fIcon = FALSE;
    hcursor = CreateIconIndirect(&info);
    ret = DeleteObject(info.hbmColor);
    cassert_unref(ret != 0, ret);
    ret = DeleteObject(info.hbmMask);
    cassert_unref(ret != 0, ret);
    ret = DestroyIcon(icon);
    cassert_unref(ret != 0, ret);
    return hcursor;
}

/*---------------------------------------------------------------------------*/

void osimage_draw(const Image *image, HDC hdc, const uint32_t frame_index, const real32_t x, const real32_t y, const real32_t width, const real32_t height, const BOOL gray)
{
    OSImage *osimage = (OSImage*)image_native(image);
    Gdiplus::Graphics graphics(hdc);

    if (frame_index != UINT32_MAX)
    {
        Gdiplus::Status status = osimage->bitmap->SelectActiveFrame(&Gdiplus::FrameDimensionTime, (UINT)frame_index);
        cassert_unref(status == Gdiplus::Ok, status);
    }

    // HBITMAP to Grayscale
//    https://blogs.msdn.microsoft.com/oldnewthing/20090714-00/?p=17503/
//void
//PaintContent(HWND hwnd, PAINTSTRUCT *pps)
//{
// if (g_hbm) {
//  BITMAP bm;
//  if (GetObject(g_hbm, sizeof(bm), &bm) == sizeof(bm) &&
//                bm.bmBits != NULL &&
//                bm.bmPlanes * bm.bmBitsPixel <= 8) {
//   struct BITMAPINFO256 {
//    BITMAPINFOHEADER bmiHeader;
//    RGBQUAD bmiColors[256];
//   } bmiGray;
//   ZeroMemory(&bmiGray, sizeof(bmiGray));
//   HDC hdc = CreateCompatibleDC(NULL);
//   if (hdc) {
//    HBITMAP hbmPrev = SelectBitmap(hdc, g_hbm);
//    UINT cColors = GetDIBColorTable(hdc, 0, 256, bmiGray.bmiColors);
//    for (UINT iColor = 0; iColor < cColors; iColor++) {
//     BYTE b = (BYTE)((30 * bmiGray.bmiColors[iColor].rgbRed +
//                      59 * bmiGray.bmiColors[iColor].rgbGreen +
//                      11 * bmiGray.bmiColors[iColor].rgbBlue) / 100);
//     bmiGray.bmiColors[iColor].rgbRed   = b;
//     bmiGray.bmiColors[iColor].rgbGreen = b;
//     bmiGray.bmiColors[iColor].rgbBlue  = b;
//    }
//    bmiGray.bmiHeader.biSize        = sizeof(bmiGray.bmiHeader);
//    bmiGray.bmiHeader.biWidth       = bm.bmWidth;
//    bmiGray.bmiHeader.biHeight      = bm.bmHeight;
//    bmiGray.bmiHeader.biPlanes      = bm.bmPlanes;
//    bmiGray.bmiHeader.biBitCount    = bm.bmBitsPixel;
//    bmiGray.bmiHeader.biCompression = BI_RGB;
//    bmiGray.bmiHeader.biClrUsed     = cColors;
//    SetDIBitsToDevice(pps->hdc, 0, 0,
//                      bmiGray.bmiHeader.biWidth,
//                      bmiGray.bmiHeader.biHeight, 0, 0,
//                      0, bmiGray.bmiHeader.biHeight,
//                      bm.bmBits,
//                     (BITMAPINFO*)&bmiGray, DIB_RGB_COLORS);
//
//    BitBlt(pps->hdc, bm.bmWidth, 0, bm.bmWidth, bm.bmHeight,
//           hdc, 0, 0, SRCCOPY);
//    SelectBitmap(hdc, hbmPrev);
//    DeleteDC(hdc);
//   }
//  }
// }


    if (gray == TRUE)
    {    
        // Gray scale conversion (.4 = Alpha)
        Gdiplus::ColorMatrix matrix =
        {
            .299f, .299f, .299f,    0,   0,
            .587f, .587f, .587f,    0,   0,
            .114f, .114f, .114f,    0,   0,
                0,     0,     0,   .4,   0,
                0,     0,     0,    0,   1};
        Gdiplus::ImageAttributes attr;
        Gdiplus::RectF rect((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)width, (Gdiplus::REAL)height);
        attr.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
        graphics.DrawImage(osimage->bitmap, rect, (Gdiplus::REAL)0, (Gdiplus::REAL)0, (Gdiplus::REAL)width, (Gdiplus::REAL)height, Gdiplus::UnitPixel, &attr);
    }
    else
    {
        graphics.DrawImage(osimage->bitmap, (Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)width, (Gdiplus::REAL)height);
    }
}
