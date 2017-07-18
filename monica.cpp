#include <SDL2/SDL.h>
#undef main

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <time.h> // Note: monica only supports implementations that return a number of seconds from time().

#include <string>
#include <map>
#include <vector>
#include <algorithm>

#ifdef max
#undef max
#endif
#define max(a,b) ((a) > (b) ? (a) : (b))

#ifdef min
#undef min
#endif
#define min(a,b) ((a) < (b) ? (a) : (b))

#define STB_TRUETYPE_IMPLEMENTATION
#include "include/stb_truetype.h"

#define USE_SDL_EVERYWHERE 0

// Returns a utf-32 codepoint
// Returns 0xFFFFFFFF if there was any error decoding the codepoint. There are two error types: decoding errors (invalid utf-8) and buffer overruns (the buffer cuts off mid-codepoint).
// Advance is set to the number of bytes (utf-8 code units, which are 8 bits long) consumed if no error occured
// Advance is set to 1 if a decoding error occured
// Advance is set to 0 if a buffer overrun occured
uint32_t utf8_pull(const unsigned char * text, size_t len, int * advance)
{
    if(len < 1)
    {
        *advance = 0;
        return 0xFFFFFFFF;
    }
    
    int i = 0;
    uint32_t c = text[i++]; // type used is large in order to do bit shifting
    
    if((c&0b1100'0000) == 0b1000'0000) // continuation byte when initial byte expected
    {
        return 0xFFFFFFFF;
    }
    else if ((c&0b1000'0000) == 0) // ascii
    {
        *advance = i;
        return c;
    }
    else if((c&0b0010'0000) == 0) // two-byte
    {
        if(len < 2)
        {
            *advance = 0;
            return 0xFFFFFFFF;
        }
        
        uint32_t c2 = text[i++];
        if((c2&0b1100'0000) != 0b1000'0000)
        {
            *advance = 1;
            return 0xFFFFFFFF;
        }
        
        uint32_t p = 0;
        p |=  c2&0b0011'1111;
        p |= (c &0b0001'1111)<<6;
        
        *advance = i;
        return p;
    }
    else if((c&0b0001'0000) == 0) // three-byte
    {
        if(len < 3)
        {
            *advance = 0;
            return 0xFFFFFFFF;
        }
        
        uint32_t c2 = text[i++];
        if((c2&0b1100'0000) != 0b1000'0000)
        {
            *advance = 1;
            return 0xFFFFFFFF;
        }
        uint32_t c3 = text[i++];
        if((c3&0b1100'0000) != 0b1000'0000)
        {
            *advance = 1;
            return 0xFFFFFFFF;
        }
        
        uint32_t p = 0;
        p |=  c3&0b0011'1111;
        p |= (c2&0b0011'1111)<<6;
        p |= (c &0b0001'1111)<<12;
        
        *advance = i;
        return p;
    }
    else if((c&0b0000'1000) == 0) // four-byte
    {
        if(len < 4)
        {
            *advance = 0;
            return 0xFFFFFFFF;
        }
        
        uint32_t c2 = text[i++];
        if((c2&0b1100'0000) != 0b1000'0000)
        {
            *advance = 1;
            return 0xFFFFFFFF;
        }
        uint32_t c3 = text[i++];
        if((c3&0b1100'0000) != 0b1000'0000)
        {
            *advance = 1;
            return 0xFFFFFFFF;
        }
        uint32_t c4 = text[i++];
        if((c4&0b1100'0000) != 0b1000'0000)
        {
            *advance = 1;
            return 0xFFFFFFFF;
        }
        
        uint32_t p = 0;
        p |=  c4&0b0011'1111;
        p |= (c3&0b0011'1111)<<6;
        p |= (c2&0b0011'1111)<<12;
        p |= (c &0b0000'0111)<<18;
        
        *advance = i;
        return p;
    }
    return 0xFFFFFFFF;
}
uint32_t utf8_pull(const char * text, long long len, int * advance)
{
    return utf8_pull((const unsigned char *)text, len, advance);
}

// SDL_MapRGB is written in a way that makes compilers not optimize it well for the most common surface format. This wrapper is more optimization-friendly.
uint32_t MapRGB(const SDL_PixelFormat * format, Uint8 r, Uint8 g, Uint8 b)
{
    if(format->format == SDL_PIXELFORMAT_RGB888)
        return (r << 16) | (g << 8) | (b << 0);
    else
        return SDL_MapRGB(format, r, g, b);
}

// Because for some reason std::fill doesn't let you specify the number of bytes
// (We actually use this elsewhere anyway, so all's well that ends well)
struct triad
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

bool render = false;

// Rasterization is slow, so we cache glyphs inside the element that's rendering them. These are just definitions.
bool cache = true;
struct crap // bitmap cache metadata related
{
    uint8_t * data;
    int width, height, xoff, yoff;
    crap()
    {
        width = 0;
        height = 0;
        xoff = 0;
        yoff = 0;
    }
};
crap bitmap_lookup(stbtt_fontinfo * fontinfo, std::map<uint64_t, crap> * bitmapcache, uint32_t index, float offset, float fontscale)
{
    // We don't do subpixel AA, but we do do subpixel offset rendering.
    // We keep track of their subpixel offset when we cache glyphs.
    // If this had full floating point resolution it would make the cache fill up with extremely similar glyphs, so we round subpixel advancement to to 1/3 pixel resolution.
    // This is not subpixel AA, this is subpixel advancement.
    int subpixel = floor(offset*3);
    if(subpixel == 4) subpixel = 3;
    uint64_t key = (uint64_t(index) << 2) + (subpixel & 0b11);
    if(bitmapcache)
        if(bitmapcache->count(key) > 0 and cache)
            return (*bitmapcache)[key];
    
    crap data;
    data.data = stbtt_GetGlyphBitmapSubpixel(fontinfo, fontscale, fontscale, subpixel/3.0, 0, index, &data.width, &data.height, &data.xoff, &data.yoff);
    if(bitmapcache and cache) (*bitmapcache)[key] = data;
    return data;
}

struct graphics
{
    SDL_Window* win;
    SDL_Surface* surface;
    SDL_Rect rect_;
    stbtt_fontinfo fontinfo;
    
    void init()
    {
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0"); // We software render everything and uploading it to the GPU every frame takes time. The compositor does it itself anyway if it needs to.
        win = SDL_CreateWindow("monica srs", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_RESIZABLE); // TODO: Handle failure
        surface = SDL_GetWindowSurface(win); // TODO: Handle NULL (is it possible?)
        
        auto fontfile = fopen("NotoSansCJKjp-Regular.otf", "rb"); // TODO: don't assume a fixed font
        if(!fontfile) puts("failed to open font file"); // TODO: Handle failure
        fseek(fontfile, 0, SEEK_END);
        uint64_t fontsize = ftell(fontfile);
        unsigned char * buffer = (unsigned char*)malloc(fontsize);
        fseek(fontfile, 0, SEEK_SET);
        if(fread(buffer, 1, fontsize, fontfile) != fontsize) // TODO: Handle failure
            puts("failed to read font file");
        fclose(fontfile);
        
        if(stbtt_InitFont(&fontinfo, buffer, stbtt_GetFontOffsetForIndex(buffer, 0)) == 0) // TODO: Handle failure
            puts("Something happened initializing the font");
    }
    
    // Cache glyph index lookup for the active font.
    // TODO: Don't assume there's only one font
    std::map<uint32_t, uint32_t> indexcache;
    uint32_t glyph_lookup(uint32_t codepoint)
    {
        if(indexcache.count(codepoint) > 0 and cache)
            return indexcache[codepoint];
        else
        {
            uint32_t index = stbtt_FindGlyphIndex(&fontinfo, codepoint);
            if(cache) indexcache[codepoint] = index;
            return index;
        }
    }
    
    // Gets how tall the font's ascent and descent area is if rendered at the given size.
    int font_height_pixels(float size)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        int ascent, descent;
        stbtt_GetFontVMetrics(&fontinfo, &ascent, &descent, nullptr);
        return (ascent-descent)*fontscale;
    }
    
    // Gets how wide in pixels the string would be with no wrapping if rendered at the given size. Measures virtual cursor advancement, not pixel coverage area.
    int string_width_pixels(const char * text, float size)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        
        float real_x = 0;
        
        size_t textlen = strlen(text);
        uint32_t lastindex = 0;
        
        float max_real_x = 0;
        while(textlen > 0)
        {
            int advance = 0;
            uint32_t codepoint = utf8_pull(text, textlen, &advance);
            
            if (advance == 0 or advance > textlen)
                break;
            else
            {
                textlen -= advance;
                text += advance;
                if (codepoint != 0xFFFFFFFF)
                {
                    if(codepoint == '\n')
                    {
                        lastindex = 0;
                        if(real_x > max_real_x) max_real_x = real_x;
                        real_x = 0;
                        continue;
                    }
                    int advance, bearing;
                    
                    uint32_t index = glyph_lookup(codepoint);
                    
                    stbtt_GetGlyphHMetrics(&fontinfo, index, &advance, &bearing);
                    if(lastindex != 0)
                        real_x += fontscale*stbtt_GetGlyphKernAdvance(&fontinfo, lastindex, index); // ?
                    
                    float advancepixels = fontscale*advance;
                    
                    real_x += advancepixels;
                    lastindex = index;
                }
            }
        }
        if(real_x > max_real_x) max_real_x = real_x;
        return ceil(max_real_x);
    }
    
    // Renders unicode text, wrapping at the right edge of the window if necessary. Does not support right-to-left or vertical text or ligatures, but does support kerning and astral unicode.
    // Takes a bitmap cache because font rasterization is not actually that fast.
    void string(SDL_Surface * surface, std::map<uint64_t, crap> * cache, int x, int y, const char * text, uint8_t red, uint8_t green, uint8_t blue, float size)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        
        float real_x = x;
        
        size_t textlen = strlen(text);
        uint32_t lastindex = 0;
        
        // stb_truetype's coordinate system renders at the baseline instead of the top left. That's fine but we want to render at the top left.
        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(&fontinfo, &ascent, &descent, &linegap);
        int linespan = ascent - descent + linegap;
        
        y += ascent*fontscale;
        
        while(textlen > 0)
        {
            int advance = 0;
            uint32_t codepoint = utf8_pull(text, textlen, &advance);
            
            if (advance == 0 or advance > textlen)
                break;
            else
            {
                textlen -= advance;
                text += advance;
                if (codepoint != 0xFFFFFFFF)
                {
                    if(codepoint == '\n')
                    {
                        lastindex = 0;
                        y += linespan*fontscale;
                        real_x = x;
                        continue;
                    }
                    
                    // black magic begin
                    
                    int advance, bearing, width, height, xoff, yoff;
                    
                    uint32_t index = glyph_lookup(codepoint);
                    
                    stbtt_GetGlyphHMetrics(&fontinfo, index, &advance, &bearing);
                    if(lastindex != 0)
                        real_x += fontscale*stbtt_GetGlyphKernAdvance(&fontinfo, lastindex, index); // ?
                    
                    float advancepixels = fontscale*advance;
                    
                    if(advancepixels + real_x > surface->w)
                    {
                        lastindex = 0;
                        y += linespan*fontscale;
                        real_x = x;
                    }
                    
                    if(y-ascent*fontscale > surface->h) break;
                    
                    //float bearingpixels = fontscale*bearing; // Not needed when using stbtt_GetCodepointBitmapSubpixel, it seems
                    float temp_x = real_x;// + bearingpixels; // target x to draw at
                    int int_temp_x = floor(temp_x);
                    crap stuff = bitmap_lookup(&fontinfo, cache, index, temp_x-int_temp_x, fontscale);
                    width = stuff.width;
                    height = stuff.height;
                    xoff = stuff.xoff;
                    yoff = stuff.yoff;
                    uint8_t * data = stuff.data;
                    
                    // black magic end
                    
                    // alpha-mixing glyph bitmap blitter
                    if(render)
                    {
                        // Avoid SDL_GetRGB/MapRGB if we're using the common trivial pixel format
                        if(surface->format->format == SDL_PIXELFORMAT_RGB888)
                        {
                            for(int i = 0; i < height; i++)
                            {
                                const int out_y = i + y + yoff;
                                if(out_y < 0) continue;
                                if(out_y >= surface->h) break;
                                for(int j = 0; j < width; j++)
                                {
                                    const int out_x = j + int_temp_x + xoff;
                                    if (out_x < 0) continue;
                                    if (out_x >= surface->w) break;
                                    const float alpha = data[i*width + j]/255.0f;
                                    if(alpha > 0.5f/255.0f) // skip trivial blank case
                                    {
                                        uint8_t * const pointer = ((uint8_t *)surface->pixels) + (out_y*surface->pitch + out_x*4);
                                        if(alpha >= 1.0f) // optimize trivial opaque case
                                        {
                                            pointer[2] = red;
                                            pointer[1] = green;
                                            pointer[0] = blue;
                                        }
                                        else
                                        {
                                            const uint8_t r = (1-alpha)*pointer[2] + alpha*red;
                                            const uint8_t g = (1-alpha)*pointer[1] + alpha*green;
                                            const uint8_t b = (1-alpha)*pointer[0] + alpha*blue;
                        
                                            pointer[2] = r;
                                            pointer[1] = g;
                                            pointer[0] = b;
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            for(int i = 0; i < height; i++)
                            {
                                for(int j = 0; j < width; j++)
                                {
                                    const float alpha = data[i*width + j]/255.0f;
                                    
                                    int out_y = i + y + yoff;
                                    int out_x = j + int_temp_x + xoff;
                                    if(out_y < 0 or out_y >= surface->h or out_x < 0 or out_x >= surface->w) continue;
                                    uint8_t * const pointer = ((uint8_t *)surface->pixels) + (out_y*surface->pitch + out_x*surface->format->BytesPerPixel);
                                    
                                    uint32_t output;
                                    memcpy(&output, pointer, surface->format->BytesPerPixel); // FIXME: probably wrong on big endian
                                    
                                    uint8_t r, g, b;
                                    SDL_GetRGB(output, surface->format, &r, &g, &b);
                                    
                                    r = (1-alpha)*r + alpha*red;
                                    g = (1-alpha)*g + alpha*green;
                                    b = (1-alpha)*b + alpha*blue;
                
                                    const uint32_t color = MapRGB(surface->format, r, g, b);
                                    
                                    memcpy(pointer, &color, surface->format->BytesPerPixel);
                                }
                            }
                        }
                    }
                    
                    real_x += advancepixels;
                    lastindex = index;
                }
            }
        }
    }
    
    // SDL surface pointers become invalid if the screen is resized, which we want to support. It turns out this is downright trivial to support, you just grab the surface pointer again.
    void resurface()
    {
        surface = SDL_GetWindowSurface(win);
    }
    
    // Basically just a flip queue
    void update()
    {
        SDL_UpdateWindowSurface(win);
        fill(surface, 0,0,0);
    }
    
    // Fill the screen with a given color; optimized for four-byte surface formats, otherwise uses rect rendering
    void fill(SDL_Surface * surface, float r, float g, float b)
    {
        const int bytes = surface->format->BytesPerPixel;
        if(bytes == 4)
        {
            uint8_t * const pixels = (uint8_t *)surface->pixels;
            uint32_t const color = MapRGB(surface->format, r, g, b);
            auto total = surface->w*surface->h;
            std::fill((uint32_t*)pixels, ((uint32_t*)pixels)+total, color);
            return;
        }
        else
            rect(surface, r, g, b, 0, 0, surface->w, surface->h);
        return;
    }
    
    // Rect coordinates are inclusive (0,0,0,0 fills in a full pixel, 0,0,1,1 fills in four pixels) and must be ordered (x1 <= x2, y1 <= y2)
    void rect(SDL_Surface * surface, float r, float g, float b, int ax1, int ay1, int ax2, int ay2)
    {
        #if USE_SDL_EVERYWHERE
        rect_.x = ax1;
        rect_.y = ay1;
        rect_.w = ax2-ax1+1;
        rect_.h = ay2-ay1+1;
        SDL_FillRect(surface, &rect_, MapRGB(surface->format, r, g, b));
        #else
        // Using std::fill is slightly faster in some situations, plus I don't want to depend too much on SDL in case I want to switch away
        uint32_t const color = MapRGB(surface->format, r, g, b);
        
        // Clip rect start and end 
        int y1 = ay1;
        int x1 = ax1;
        if(y1 >= surface->h) y1 = surface->h-1;
        else if(y1 < 0) y1 = 0;
        if(x1 >= surface->w) x1 = surface->w-1;
        else if(x1 < 0) x1 = 0;
        
        int y2 = ay2;
        int x2 = ax2;
        if(y2 >= surface->h) y2 = surface->h-1;
        else if(y2 < 0) y2 = 0;
        if(x2 >= surface->w) x2 = surface->w-1;
        else if(x2 < 0) x2 = 0;
        
        int h = y2 - y1 + 1;
        int w = x2 - x1 + 1;
        
        const int bytes = surface->format->BytesPerPixel;
        uint8_t * const start = (uint8_t *)surface->pixels + y1*surface->pitch + x1*bytes;
        
        // Ｗｈｙ does SDL allow formats where the pitch is not just the width times the number of bytes per pixel. God damn it.
        if(bytes == 4)
        {
            for(uint8_t * pointer = start; pointer < start + h*surface->pitch; pointer += surface->pitch)
                std::fill((uint32_t *)pointer, ((uint32_t *)pointer) + w, color);
            return;
        }
        else if(bytes == 3)
        {
            for(uint8_t * pointer = start; pointer < start + h*surface->pitch; pointer += surface->pitch)
                std::fill((triad *)pointer, ((triad *)pointer) + w, *(triad*)&color);
        }
        else if(bytes == 2)
        {
            for(uint8_t * pointer = start; pointer < start + h*surface->pitch; pointer += surface->pitch)
                std::fill((uint16_t*)pointer, (uint16_t*)(pointer+surface->pitch), *(uint16_t*)&color);
        }
        else if(bytes == 1)
        {
            for(uint8_t * pointer = start; pointer < start + h*surface->pitch; pointer += surface->pitch)
                memset(pointer, surface->pitch, color);
                //std::fill(pointer, pointer+surface->pitch, color;
        }
        #endif
    }
    
    // An outline is just four pixel-wide rects
    void rect_outline(SDL_Surface * surface, float r, float g, float b, int x1, int y1, int x2, int y2)
    {
        rect(surface, r, g, b, x1, y1, x2, y1);
        rect(surface, r, g, b, x1, y1, x1, y2);
        rect(surface, r, g, b, x1, y2, x2, y2);
        rect(surface, r, g, b, x2, y1, x2, y2);
    }
};

// Positions can be relative to the top, equator, or bottom; or left, meridian, or right side of the screen, and might be in screenspace percentile units instead of pixels
// height/width data uses raw positions and are further relative to another position, but that relativeness is handled in whatever uses those height/width values
enum reference {
    EARLY,
    CENTER,
    LATE
};
struct posdata {
    reference position; // topleft, center, bottomright
    bool proportional; // false: pixels; true: screenspace percentages
    int offset;
    posdata(int offset = 0, reference position = EARLY, bool proportional = false)
    {
        this->position = position;
        this->offset = offset;
        this->proportional = proportional;
    }
};

// A UI element is a bunch of visual data, and might be a button of some kind
enum action {
    NONE,
    FLIP,
    FLUNK,
    GOOD
};
struct element {
    posdata x, y, w, h;
    bool drawbg, outline;
    std::string text;
    posdata textx, texty;
    int textsize;
    bool textcenter = false;
    triad background, foreground;
    int clickaction = 0;
    bool active = true;
    
    std::map<uint64_t, crap> bitmapcache;
    
    element(posdata x, posdata y, posdata w, posdata h, bool drawbg, bool outline, std::string text, posdata textx, posdata texty, int textsize, triad background, triad foreground, int clickaction = 0, bool active = true)
    {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        this->drawbg = drawbg;
        this->outline = outline;
        this->text = text;
        this->textx = textx;
        this->texty = texty;
        this->textsize = textsize;
        this->background = background;
        this->foreground = foreground;
        this->clickaction = clickaction;
        this->active = active;
    }
    
    element(posdata x, posdata y, posdata w, posdata h, std::string text, int textsize, triad background, triad foreground, int clickaction)
    {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        this->drawbg = true;
        this->outline = false;
        this->text = text;
        this->textx = posdata(0);
        this->texty = posdata(0);
        this->textsize = textsize;
        this->background = background;
        this->foreground = foreground;
        this->clickaction = clickaction;
        this->active = true;
        
        this->textcenter = true;
    }
    
    element(posdata x, posdata y, posdata w, posdata h, triad background, triad foreground)
    {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        this->drawbg = true;
        this->outline = false;
        this->text = "";
        this->textx = posdata(0);
        this->texty = posdata(0);
        this->textsize = 0;
        this->background = background;
        this->foreground = foreground;
        this->clickaction = NONE;
        this->active = true;
    }
    
    ~element()
    {
        for (const auto & pair : bitmapcache)
        {
            crap value = pair.second;
            free(value.data);
        }
    }
    
    // Get the given coordinate according to the graphics backend given
    int y1(graphics * backend)
    {
        int y1 = y.offset;
        if(y.proportional) y1 = roundf(y1*backend->surface->h/100.0f);
        if(y.position == CENTER) y1 += backend->surface->h/2;
        if(y.position == LATE) y1 += backend->surface->h;
        return y1;
    }
    int x1(graphics * backend)
    {
        int x1 = x.offset;
        if(x.proportional) x1 = roundf(x1*backend->surface->w/100.0f);
        if(x.position == CENTER) x1 += backend->surface->w/2;
        if(x.position == LATE) x1 += backend->surface->w;
        return x1;
    }
    
    int y2(graphics * backend)
    {
        int y2 = h.offset;
        if(h.proportional) y2 = roundf(y2*backend->surface->h/100.0f);
        if(h.position == CENTER) y2 += backend->surface->h/2;
        if(h.position == LATE) y2 += backend->surface->h;
        return y2 + y1(backend);
    }
    int x2(graphics * backend)
    {
        int x2 = w.offset;
        if(w.proportional) x2 = roundf(x2*backend->surface->w/100.0f);
        if(w.position == CENTER) x2 += backend->surface->w/2;
        if(w.position == LATE) x2 += backend->surface->w;
        return x2 + x1(backend);
    }
};

// a note is a collection of fields, loaded here from a TSV line
struct note {
    std::vector<std::string> fields;
    uint64_t unique_id = 0;
    note(std::string line)
    {
        const char * text = line.data();
        const char * start = text;
        
        bool quoted = false; // -1: unknown (start state); 0: false; 1: true
        bool escaping = false;
        
        std::string current;
        
        size_t textlen = strlen(start);
        // Allows quote-encapsulated fields.
        // Quotes must be adjacent to the relevant tab to be treated as encapculation quotes. String [asdf\t"gjrfger"gjafg"awefsdg"\t"fdhjdfh"] is three fields.
        // Quotes can be escaped with a \.
        // \ can be escaped with a \ but does not have to be if it's not ambiguous.
        // Newlines are \n. 0x20 in the byte stream triggers the end of the line, just like 0x00.
        while(text - start < textlen)
        {
            if(!quoted and *text == '"')
            {
                quoted = true;
            }
            else if(*text == '\t' and !quoted)
            {
                fields.push_back(current);
                current = "";
            }
            else if(*text == '"' and quoted and !escaping)
            {
                text++;
                if(text - start >= textlen)
                {
                    fields.push_back(current);
                    current = "";
                }
                // FIXME: There's a pointer advancement bug here. Can you find it?
                else if(*text == '\t' or *text == '\n' or *text == '\0') // only " that are adjecant with a tab are the end " to a field
                {
                    fields.push_back(current);
                    current = "";
                }
                else
                    current += '"';
            }
            else if(*text == '\\' and !escaping)
            {
                escaping = true;
            }
            else if(*text == '\n' or *text == '\0')
            {
                fields.push_back(current);
                break;
            }
            else if (escaping)
            {
                if(*text == '\\') // escaped backslash
                    current += '\\';
                else if(*text == 'n') // newline escape
                    current += '\n';
                else if(*text == '"') // escaped quote
                    current += '"';
                else // false escape, just a backslash followed by the current character
                {
                    current += '\\';
                    current += *text;
                }
                escaping = false;
            }
            else
                current += *text;
            
            text++;
        }
        if(current != "")
            fields.push_back(current);
        if(fields.size() > 0)
        {
            try
            {
                unique_id = std::stoll(fields[0]);
                fields.erase(fields.begin());
            }
            catch (const std::invalid_argument& e)
            {
                puts("ERROR: A note has an invalid Unique ID field. This is very bad!");
            }
            catch (const std::out_of_range& e)
            {
                puts("ERROR: A note has an invalid Unique ID field. This is very bad!");
            }
        }
    }
};

// a formatting element is a virtual version of a UI element
struct formatting
{
    posdata x, y, w, h;
    uint8_t r, g, b;
    std::string format;
    int fontsize;
    bool centered;
    int type; // 0: common; 1: front only; 2: answer only
};

// a card is just a list of formatting elements
struct format
{
    uint64_t unique_id = 0;
    std::vector<formatting *> formatting;
    ~format()
    {
        for(auto i : formatting)
            delete i;
        formatting = {};
    }
};

struct scheduling
{
    time_t time_added, time_scheduled_from, time_scheduled_for, time_last_seen, day_buried_until; // day_scheduled_for is only used for buried cards
    int consecutive_flunks = 8;
    int day_interval = 1;
    int last_good_day_interval = 1;
    int days_repped, times_flunked, times_passed;
    float ease = 2.5;
    int learning = 2; // learning step, 2 is first, 1 is second, 0 is graduated
};

struct card
{
    note * n = nullptr;
    format * f = nullptr;
    scheduling * s = nullptr;
};


// FIXME: might be backwards (1/x)
const double seconds_per_time = (60.0*60.0)/difftime(60*60, 0);

time_t time_floor_to_midnight(time_t t)
{
    auto local = *localtime(&t);
    int seconds_into_day = local.tm_sec + local.tm_min*60 + local.tm_hour*60*60;
    t -= seconds_into_day/seconds_per_time;
    return t;
}
int days_between(time_t t1, time_t t2)
{
    double diff_seconds = difftime(time_floor_to_midnight(t2), time_floor_to_midnight(t1));
    int between = floor(diff_seconds/60/60/24);
    return between;
}
time_t add_seconds(time_t start, int seconds)
{
    return start+seconds*seconds_per_time;
}
time_t add_minutes(time_t start, int minutes)
{
    return add_seconds(start, minutes*60);
}
time_t add_hours(time_t start, int hours)
{
    return add_minutes(start, hours*60);
}
time_t add_days(time_t start, int days)
{
    return add_hours(start, days*24);
}

void schedule_minutes(time_t now, scheduling * schedule, int minutes)
{
    schedule->time_scheduled_from = now;
    schedule->time_scheduled_for = add_minutes(now, minutes);
    schedule->day_interval = 0;
    schedule->day_buried_until = 0;
}
void schedule_days(time_t now, scheduling * schedule, int days)
{
    schedule->time_scheduled_from = now;
    schedule->time_scheduled_for = add_days(now, days);
    schedule->day_interval = days;
    schedule->day_buried_until = 0;
}
void schedule_bury(time_t now, scheduling * schedule, int days)
{
    schedule->day_buried_until = add_days(time_floor_to_midnight(now), days);
}

// a deck is a collection of notes and cards
struct deck
{
    // TODO: Change to maps?
    std::vector<note *> notes;
    std::vector<format *> formats;
    std::vector<card *> cards;
    void add_note(note * n)
    {
        for(auto f : formats)
        {
            notes.push_back(n);
            
            auto c = make_card(n->unique_id, f->unique_id);
            
            auto schedule = new scheduling();
            schedule->days_repped = 0;
            schedule->times_flunked = 0;
            schedule->times_passed = 0;
            c->s = schedule;
            
            cards.push_back(c);
        }
    }
    void add_note(note * n, scheduling * schedule)
    {
        for(auto f : formats)
        {
            notes.push_back(n);
            auto c = make_card(n->unique_id, f->unique_id);
            c->s = schedule;
            cards.push_back(c);
        }
    }
    card * make_card(uint64_t note_id, uint64_t format_id)
    {
        card * c = new card;
        for(auto note : notes)
            if(note->unique_id == note_id) c->n = note;
        for(auto format : formats)
            if(format->unique_id == format_id) c->f = format;
        if(c->n != nullptr and c->f != nullptr)
            return c;
        else
            return nullptr;
    }
};

// user interface for the deck
struct deckui
{
    std::vector<std::pair<formatting *, element *>> associations;
    card * currentcard;
    deck currentdeck;
    
    int new_notes_today = 3;
    
    deckui()
    {
        // Modern C++ :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^) :^)
        auto common = new formatting {posdata(20, EARLY, true), posdata(20, EARLY, true), posdata(60, EARLY, true), posdata(20, EARLY, true), 255, 255, 255, "{0}", 64, true, 0};
        auto front  = new formatting {posdata(20, EARLY, true), posdata(40, EARLY, true), posdata(60, EARLY, true), posdata(20, EARLY, true), 255, 255, 255, "({1})", 64, true, 1};
        auto answer = new formatting {posdata(20, EARLY, true), posdata(50, EARLY, true), posdata(60, EARLY, true), posdata(20, EARLY, true), 255, 255, 255, "Meaning: {2}", 32, true, 2};
        currentdeck.formats.push_back(new format { 1, std::vector<formatting *> {common, front, answer} });
        
        // test data, gonna implement deck loading properly later
        currentdeck.add_note(new note("0\t日本\tにほん\\nにっぽん\t\"japan\t/\tjapan (traditional)\""));
        currentdeck.add_note(new note("1\t犬\tいぬ\tdog"));
        currentdeck.add_note(new note("2\t𠂇\t\thand"));
        
        // make UI elements for all of the formatting elements in our card definition
        for(auto format : currentdeck.formats)
        {
            for(auto e : format->formatting)
            {
                element * myelement;
                if(e->format == "")
                    myelement = new element(e->x, e->y, e->w, e->h, true, false, "<uninitialized>", 0, 0, e->fontsize, {e->r, e->g, e->b}, {128,0,0} );
                else
                    myelement = new element(e->x, e->y, e->w, e->h, false, false, "<uninitialized>", 0, 0, e->fontsize, {128,0,0}, {e->r, e->g, e->b} );
                myelement->textcenter = e->centered;
                associations.push_back(std::pair<formatting *, element *> { e, myelement });
            }
        }
        
        auto cardlist = available(time(NULL));
        if(cardlist.size() > 0)
        {
            puts("setting up initial card");
            cardsort(cardlist);
            currentcard = cardlist[0];
            refresh(currentcard);
        }
        else
        {
            puts("no available cards to set up");
            currentcard = nullptr;
        }
    }
    
    void cardsort(std::vector<card *> & cardlist)
    {
        std::sort(cardlist.begin(), cardlist.end(), [this](card * a, card * b)
        {
            if(a == nullptr and b == nullptr) return false;
            if(b == nullptr) return false;
            if(a == nullptr) return true;
            // highest priority different is note ID difference
            if(a->n->unique_id != b->n->unique_id) return (a->n->unique_id < b->n->unique_id);
            // second is format ID difference
            if(a->f->unique_id != b->f->unique_id) return (a->f->unique_id < b->f->unique_id);
            // you should never have multiple card with both the same format and note id
            // but supposing a bit flips somewhere, the different should be based on time instead of crashing the program
            // this is only for runtime; when loading the db, one of the cards will be ignored
            return a->s->time_last_seen < b->s->time_last_seen;
        });
    }
    
    // takes a card formatting string and formats in a note
    std::string do_format(std::string & format, card * c)
    {
        std::string formatted = "";
        
        const char * text = format.data();
        const char * start = text;
        
        bool capsulated = 0; // 0: false; 1: true
        bool escaping = false;
        
        std::string current;
        std::string capsule;
        
        size_t textlen = strlen(start);
        while(text - start < textlen)
        {
            if(!capsulated and *text == '{' and !escaping)
            {
                capsulated = true;
            }
            else if(capsulated and *text == '}')
            {
                try
                {
                    int num = std::stoi(capsule);
                    current += c->n->fields[num];
                }
                catch (const std::invalid_argument& e)
                { } // don't care
                catch (const std::out_of_range& e)
                { } // don't care
                capsule = "";
                capsulated = false;
            }
            else if(*text == '\\' and !escaping)
            {
                escaping = true;
            }
            else if(*text == '\0')
            {
                break;
            }
            else if (escaping)
            {
                if(*text == '\\') // escaped backslash
                    current += '\\';
                else if(*text == '{') // brace escape
                    current += '{';
                else // false escape, just a backslash followed by the current character
                {
                    current += '\\';
                    current += *text;
                }
                escaping = false;
            }
            else if (capsulated)
            {
                capsule += *text;
            }
            else
            {
                current += *text;
            }
            
            text++;
        }
        return current;
    }
    
    // hides the current card
    bool stashed = true;
    void stash()
    {
        stashed = true;
        for(auto p : associations)
        {
            auto e = p.second;
            e->text = "";
            e->active = false;
        }
    }
    
    // shows the front of the given card
    void refresh(card * c)
    {
        currentcard = c;
        
        if(currentcard == nullptr)
        {
            puts("Refreshed card is nullptr");
            return;
        }
        
        stashed = false;
        
        currentcard->s->time_last_seen = time(NULL);
        for(auto p : associations)
        {
            puts("doing formatting");
            auto f = p.first;
            auto e = p.second;
            e->text = do_format(f->format, currentcard);
            e->active = true;
            if(f->type == 2) e->active = false;
        }
    }
    
    // shows the back of the current card
    void flip()
    {
        if(currentcard == nullptr) return;
        
        stashed = false;
        
        currentcard->s->time_last_seen = time(NULL);
        for(auto p : associations)
        {
            auto f = p.first;
            auto e = p.second;
            e->active = true;
            if(f->type == 1) e->active = false;
        }
    }
    
    // answers and reschedules the current note
    // TODO: add virtual time adjustment for debugging and to tolerate sleep cycle disorders or disruptions
    void answer(int rank = 0) // rank - 0: flunk; 1: good; (for now)
    {
        if(stashed) return;
        
        if(currentcard == nullptr) return;
        
        auto schedule = currentcard->s;
        
        auto now = time(NULL);
        
        if(schedule->days_repped == 0)
        {
            schedule->days_repped++;
            schedule->time_added = now;
            new_notes_today--;
        }
        else if(days_between(now, schedule->time_scheduled_from) != 0)
            schedule->days_repped++;
        
        // TODO:
        // 1: Automatically bury cards that relapse too many times (5? 8?)
        if(schedule->learning > 0)
        {
            // flunk: reset to first learning step
            if(rank == 0) 
            {
                schedule->learning = 2;
                schedule_minutes(now, schedule, 1);
                // If we're on the first learning stage...
                if(schedule->learning == 2)
                {
                    // check the consecutive flunk counter
                    if(schedule->consecutive_flunks > 0)
                    {
                        puts("learning flunk");
                        schedule->consecutive_flunks--;
                    }
                    // if we exhausted our consecutive flunks on first learning stage cards, we're leeching
                    // (the consecutive flunk count is only reset on graduation when buried)
                    else
                    {
                        puts("learning flunk (leech, bury)");
                        schedule->consecutive_flunks = 8;
                        schedule_bury(now, schedule, 1);
                    }
                }
            }
            // pass: depends on learning stage
            else
            {
                schedule->learning--;
                
                if(schedule->learning > 0)
                {
                    puts("learning step");
                    schedule_minutes(now, schedule, 5);
                }
                // was the last learning stage: graduated, one day
                else // graduation
                {
                    puts("learning graduation");
                    schedule_days(now, schedule, 1);
                    schedule->consecutive_flunks = 8;
                }
            }
        }
        // review queue card
        else
        {
            // failsafe
            schedule->consecutive_flunks = 8;
            // if this is a followup review
            if(schedule->day_interval < schedule->last_good_day_interval)
            {
                if(rank == 0)
                {
                    puts("followup flunk");
                    schedule->learning = 2;
                    schedule->times_flunked++;
                    schedule->last_good_day_interval = 1; // no longer a followup card
                    schedule_minutes(now, schedule, 1);
                }
                // 
                else
                {
                    puts("followup pass");
                    schedule->times_passed++;
                    schedule_days(now, schedule, schedule->last_good_day_interval); // reset to last recent good interval instead of using a low interval
                }
            }
            // if this is a normal review
            else
            {
                if(rank == 0)
                {
                    puts("rep flunk");
                    schedule->learning = 2;
                    schedule->times_flunked++;
                    // last_good_day_interval is whatever the *previous* interval was
                    // e.g. if this was a 2.5 ease 25 day interval we failed, the last good interval will be 10 days
                    schedule_minutes(now, schedule, 1);
                }
                else
                {
                    puts("rep pass");
                    schedule->times_passed++;
                    schedule->last_good_day_interval = schedule->day_interval;
                    schedule_days(now, schedule, schedule->day_interval*schedule->ease);
                }
            }
        }
        
        schedule->time_last_seen = now;
        
        next(now);
    }
    
    // advances to the next note
    void next(time_t now)
    {
        auto cardlist = available(now);
        cardsort(cardlist);
        if(cardlist.size() > 0)
        {
            currentcard = cardlist[0];
            refresh(currentcard);
        }
        else
            stash();
    }
    
    // find available cards
    std::vector<card *> available(time_t now)
    {
        std::vector<card *> ret;
        std::vector<card *> learning_cards;
        puts("making list of available cards");
        for(auto card : currentdeck.cards)
        {
            auto schedule = card->s;
            
            // if buried, skip
            if((schedule->days_repped > 0 or schedule->learning == 0) and now < schedule->day_buried_until)
            {
                puts("Buried card");
                continue;
            }
            
            // for later
            if(schedule->learning > 0 and schedule->days_repped > 0)
                learning_cards.push_back(card);
            // learning card, ready
            if(schedule->learning > 0 and schedule->days_repped > 0 and now >= schedule->time_scheduled_for)
            {
                puts("learning card");
                ret.push_back(card);
            }
            // new card
            else if(schedule->learning > 0 and schedule->days_repped == 0 and new_notes_today > 0)
            {
                puts("new card");
                ret.push_back(card);
            }
            // rep card
            else if(schedule->learning == 0 and days_between(now, schedule->time_scheduled_for) <= 0)
            {
                puts("rep card");
                printf("scheduled for: %lld\n", schedule->time_scheduled_for);
                printf("now: %lld\n", now);
                ret.push_back(card);
            }
        }
        // bring learning cards closer to now by the study ahead limit (FIXME: hardcoded 5 minutes) if we're out of stuff to study
        if(ret.size() == 0 and learning_cards.size() > 0)
        {
            for(auto card : learning_cards)
            {
                auto schedule = card->s;
                auto oldtime = schedule->time_scheduled_for;
                schedule->time_scheduled_for = add_seconds(now, -5*60);
                // if we caused integer underflow (technically possible by standard C) set the scheduled_for time to 0 instead
                if(oldtime < schedule->time_scheduled_for) schedule->time_scheduled_for = 0;
                
                // finally, queue up available cards
                if(now >= schedule->time_scheduled_for)
                {
                    puts("unprepared card");
                    ret.push_back(card);
                }
                
            }
        }
        return ret;
    }
    
    // informs the UI about our UI elements
    void insert_into(std::vector<element*> & elements)
    {
        for(auto p : associations)
        {
            auto e = p.second;
            elements.push_back(e);
        }
    }
};

int main()
{
    // Initialize
    std::vector<element*> elements;
    
    graphics backend;
    
    backend.init();
    
    auto start = SDL_GetTicks();
    float smoothtime = 0;
    
    // UI buttons
    auto b_flip = new element(posdata(0), posdata(-32, LATE), posdata(0, LATE), posdata(32, EARLY), "flip", 24, {127,127,127}, {255,255,255}, FLIP);
    elements.push_back(b_flip);
    
    auto b_flunk = new element(posdata(0), posdata(-32, LATE), posdata(50, EARLY, true), posdata(32, EARLY), "flunk", 24, {255,0,0}, {255,255,255}, FLUNK);
    b_flunk->active = false;
    elements.push_back(b_flunk);
    
    auto b_good = new element(posdata(50, EARLY, true), posdata(-32, LATE), posdata(50, EARLY, true), posdata(32, EARLY), "good", 24, {0,200,0}, {255,255,255}, GOOD);
    b_good->active = false;
    elements.push_back(b_good);
    
    // Debug deck
    deckui mydeckui;
    mydeckui.insert_into(elements);
    
    // Pointless test text
    const char * prealphawarning = "Prealpha Software\nプレアルファ";
    int prealphasize = 16;
    elements.push_back(new element(posdata(-backend.string_width_pixels(prealphawarning, prealphasize), LATE), posdata(-prealphasize*2, LATE), posdata(0, LATE), posdata(prealphasize, EARLY), false, false,
        prealphawarning,
        0, 0, prealphasize, {0,0,0}, {255,255,255}));
    
    // Main loop / mainloop
    while(1)
    {
        SDL_Event event;
        static element * pressedelement = nullptr;
        while (SDL_PollEvent(&event))
        {
            // TODO: hook in database saving stuff
            if (event.type == SDL_QUIT)
                exit(0);
            
            // Re-grab rendering surface if the window is resized
            if (event.type == SDL_WINDOWEVENT)
            {
                if(event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    backend.resurface();
            }
            // We use the "must press and release on the same element to trigger an input" button model, and dim buttons while they're held down
            if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                element * foundelement = nullptr;
                for(auto element : elements)
                {
                    if(!element) continue;
                    if(!element->active) continue;
                    int x = event.button.x;
                    int y = event.button.y;
                    if (element->x1(&backend) <= x and x <= element->x2(&backend) and
                        element->y1(&backend) <= y and y <= element->y2(&backend) and
                        element->clickaction != NONE)
                    {
                        foundelement = element;
                    }
                }
                if(foundelement) pressedelement = foundelement;
            }
            if (event.type == SDL_MOUSEBUTTONUP)
            {
                element * foundelement = nullptr;
                for(auto element : elements)
                {
                    if(!element) continue;
                    if(!element->active) continue;
                    int x = event.button.x;
                    int y = event.button.y;
                    if (element->x1(&backend) <= x and x <= element->x2(&backend) and
                        element->y1(&backend) <= y and y <= element->y2(&backend) and
                        element->clickaction != NONE)
                        foundelement = element;
                }
                if(foundelement == pressedelement and foundelement != nullptr)
                {
                    switch(foundelement->clickaction)
                    {
                    case FLIP:
                        b_flunk->active = true;
                        b_good->active = true;
                        b_flip->active = false;
                        mydeckui.flip();
                        break;
                    case FLUNK:
                        b_flunk->active = false;
                        b_good->active = false;
                        b_flip->active = true;
                        mydeckui.answer(0);
                        break;
                    case GOOD:
                        b_flunk->active = false;
                        b_good->active = false;
                        b_flip->active = true;
                        mydeckui.answer(1);
                        break;
                    }
                    if(mydeckui.stashed)
                    {
                        b_flunk->active = false;
                        b_good->active = false;
                        b_flip->active = false;
                    }
                }
                pressedelement = nullptr;
            }
        }
        
        // Render everything in painter's order. There is no depth information, everything is rendered in the order in which it was all added to the element list.
        for (auto element : elements)
        {
            if(!element) continue;
            if(!element->active) continue;
            float factor = (element==pressedelement)?(0.8):(1.0);
            bool debugui = false;
            if(debugui)
            {
                backend.rect_outline(backend.surface, 255, 0, 255, element->x1(&backend), element->y1(&backend), element->x2(&backend), element->y2(&backend));
            }
            if(element->drawbg)
            {
                if(!element->outline)
                    backend.rect(backend.surface, element->background.r*factor, element->background.g*factor, element->background.b*factor, element->x1(&backend), element->y1(&backend), element->x2(&backend), element->y2(&backend));
                else
                    backend.rect_outline(backend.surface, element->background.r*factor, element->background.g*factor, element->background.b*factor, element->x1(&backend), element->y1(&backend), element->x2(&backend), element->y2(&backend));
            }
            if(element->text != "")
            {
                int x;
                int y;
                if(!element->textcenter)
                {
                    x = element->x1(&backend);
                    y = element->y1(&backend);
                }
                else
                {
                    x = (element->x1(&backend)+element->x2(&backend))/2;
                    x -= backend.string_width_pixels(element->text.data(), element->textsize)/2;
                    
                    y = (element->y1(&backend)+element->y2(&backend))/2;
                    y -= backend.font_height_pixels(element->textsize)/2;
                }
                backend.string(backend.surface, &element->bitmapcache, x, y, element->text.data(), element->foreground.r*factor, element->foreground.g*factor, element->foreground.b*factor, element->textsize);
            }
        }
        
        auto time = SDL_GetTicks();
        //float constant = 0.9999f;
        //float constant = 0.99;
        float constant = 0.9;
        smoothtime = constant*smoothtime + (1-constant)*(time-start);
        start = time;
        
        auto s = std::to_string(int(round(1000/smoothtime)))+"fps";
        render = true;
        backend.string(backend.surface, nullptr, backend.surface->w-backend.string_width_pixels(s.data(), 24)-5, 5, s.data(), 255, 255, 255, 24);
        
        backend.update();
        
        //SDL_Delay(16);
    }
    
    return 0;
}
