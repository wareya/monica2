


/*
TODO:
- Support for multiple decks via folders; deck selection UI
- Font determined by formats instead of hardcoded
- Suspension (with forced tagging: delete, annoying, known)
- Manual burying?
- "Undo"
- Import notes?
*/

#include <SDL2/SDL.h>
#undef main

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <time.h> // Note: monica only supports implementations that return a number of seconds from time().

#include <string>
#include <map>
#include <set>
#include <vector>
#include <deque>
#include <algorithm>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "include/stb_truetype.h"

#define USE_SDL_EVERYWHERE 0

#ifdef max
#undef max
#endif
#define max(a,b) ((a) > (b) ? (a) : (b))

#ifdef min
#undef min
#endif
#define min(a,b) ((a) < (b) ? (a) : (b))


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
// nonconformant hacky garbage
bool allows_newline(uint32_t codepoint)
{
    if((codepoint >= 0x00003400 and codepoint < 0x00004DBF) or
       (codepoint >= 0x0000FE30 and codepoint < 0x0000FE4F) or
       (codepoint >= 0x0000F900 and codepoint < 0x0000FAFF) or
       (codepoint >= 0x00004E00 and codepoint < 0x00009FFF) or
       (codepoint >= 0x00020000 and codepoint < 0x0002A6DF) or
       (codepoint >= 0x0002a700 and codepoint < 0x0002b73f) or
       (codepoint >= 0x0002b740 and codepoint < 0x0002b81f) or
       (codepoint >= 0x0002b820 and codepoint < 0x0002ceaf) or
       (codepoint >= 0x0002f800 and codepoint < 0x0002fa1f) or
       (codepoint >= 0x00003040 and codepoint < 0x0000309f) or
       (codepoint >= 0x000030a0 and codepoint < 0x000030ff) or
       (codepoint >= 0x00002e80 and codepoint < 0x00002eff) or
       (codepoint >= 0x00003000 and codepoint < 0x0000303f) or
       (codepoint >= 0x00003300 and codepoint < 0x000033ff) or
       
       (codepoint <= 0xFF and !(
           (codepoint >= 0x30 and codepoint <= 0x39) or
           (codepoint >= 0x41 and codepoint <= 0x5A) or
           (codepoint >= 0x61 and codepoint <= 0x7A)
       )))
       return true;
   else
       return false;
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
    float string_width_pixels(const char * text, float size)
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
        return max_real_x;
    }
    // same but with a vector reference
    float string_width_pixels(std::vector<uint32_t> & text, float size)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        
        uint32_t lastindex = 0;
        float real_x = 0;
        float max_real_x = 0;
        for(uint32_t codepoint : text)
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
        if(real_x > max_real_x) max_real_x = real_x;
        return max_real_x;
    }
    
    // Renders unicode text, wrapping at the right edge of the window if necessary. Does not support right-to-left or vertical text or ligatures, but does support kerning and astral unicode.
    // Takes a bitmap cache because font rasterization is not actually that fast.
    void string(SDL_Surface * surface, std::map<uint64_t, crap> * cache, float x, float y, const char * text, uint8_t red, uint8_t green, uint8_t blue, float size)
    {
        string(surface, cache, x, y, surface->w, surface->h, text, red, green, blue, size);
    }
    void string(SDL_Surface * surface, std::map<uint64_t, crap> * cache, float x, float y, int x2, int y2, const char * text, uint8_t red, uint8_t green, uint8_t blue, float size)
    {
        string(surface, cache, x, y, x, y, x2, y2, text, red, green, blue, size, false, false);
    }
    // TODO: Make centering part of string() instead of done in the mainloop's render logic, so that multi-line justification works properly instead of being a hack
    // FIXME: Make (e.g. english) text with spaces wrap by the word, not by the character
    void string(SDL_Surface * surface, std::map<uint64_t, crap> * cache, float x, float y, int x1, int y1, int x2, int y2, const char * text, uint8_t red, uint8_t green, uint8_t blue, float size, bool center_x, bool center_y)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        
        size_t textlen = strlen(text);
        
        // stb_truetype's coordinate system renders at the baseline instead of the top left. That's fine but we want to render at the top left.
        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(&fontinfo, &ascent, &descent, &linegap);
        int linespan = ascent - descent + linegap;
        
        y1 = max(0, y1);
        x1 = max(0, x1);
        y2 = min(surface->h, y2);
        x2 = min(surface->w, x2);
        
        std::vector<std::vector<uint32_t>> lines;
        
        // scoping block
        {
            std::vector<uint32_t> currline;
            float real_x = x;
            if(center_x)
                real_x = x1;
            
            uint32_t lastindex = 0;
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
                            real_x = x1;
                            lines.push_back(currline);
                            currline = {};
                            continue;
                        }
                        
                        int advance, bearing, width, height, xoff, yoff;
                        
                        uint32_t index = glyph_lookup(codepoint);
                        
                        stbtt_GetGlyphHMetrics(&fontinfo, index, &advance, &bearing);
                        
                        if(lastindex != 0)
                            real_x += fontscale*stbtt_GetGlyphKernAdvance(&fontinfo, lastindex, index); // ?
                        float advancepixels = fontscale*advance;
                        
                        real_x += advancepixels;
                        lastindex = index;
                        
                        if(real_x > x2)
                        {
                            lastindex = 0;
                            
                            bool lastallowsnewline = false;
                            if(currline.size() > 0)
                                if(allows_newline(currline.back()))
                                    lastallowsnewline = true;
                            if(allows_newline(codepoint) or lastallowsnewline)
                            {
                                if(currline.size() > 0)
                                    if(currline.back() == ' ')
                                        currline.pop_back();
                                lines.push_back(currline);
                                currline = {codepoint};
                            }
                            else
                            {
                                std::deque<uint32_t> temp;
                                while(1)
                                {
                                    if(currline.size() == 0) break;
                                    if(allows_newline(currline.back())) break;
                                    temp.push_front(currline.back());
                                    currline.pop_back();
                                }
                                // if we emptied currline put it all back
                                if(currline.size() == 0)
                                {
                                    for(auto c : temp) currline.push_back(c);
                                    temp = {};
                                }
                                if(currline.size() > 0)
                                    if(currline.back() == ' ')
                                        currline.pop_back();
                                lines.push_back(currline);
                                currline = {};
                                for(auto c : temp) currline.push_back(c);
                                currline.push_back(codepoint);
                            }
                            
                            real_x = x1 + string_width_pixels(currline, size);
                        }
                        else
                            currline.push_back(codepoint);
                    }
                }
            }
            if(currline.size() != 0)
            {
                lines.push_back(currline);
                currline = {};
            }
        }
        if(center_y)
            y = y1 + (y2-y1)/2 - (linespan*lines.size()*fontscale)/2;
        
        y += ascent*fontscale;
        
        for(auto line : lines)
        {
            uint32_t lastindex = 0;
            if(center_x)
                x = x1 + (x2-x1)/2 - string_width_pixels(line, size)/2;
            float real_x = x;
            for(uint32_t codepoint : line)
            {
                if(codepoint == '\n')
                    continue;
                
                // black magic begin
                
                int advance, bearing, width, height, xoff, yoff;
                
                uint32_t index = glyph_lookup(codepoint);
                
                stbtt_GetGlyphHMetrics(&fontinfo, index, &advance, &bearing);
                if(lastindex != 0)
                    real_x += fontscale*stbtt_GetGlyphKernAdvance(&fontinfo, lastindex, index); // ?
                
                float advancepixels = fontscale*advance;
                
                if(y-ascent*fontscale > y2) break;
                
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
                            if(out_y < y1) continue;
                            if(out_y >= y2) break;
                            for(int j = 0; j < width; j++)
                            {
                                const int out_x = j + int_temp_x + xoff;
                                if (out_x < x1) continue;
                                if (out_x >= x2) break;
                                //if (out_x < 0) continue;
                                //if (out_x >= surface->w) break;
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
                                if(out_y < 0 or out_y >= y2 or out_x < 0 or out_x >= x2) continue;
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
            lastindex = 0;
            y += linespan*fontscale;
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
    bool textcenter_x = false;
    bool textcenter_y = false;
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
        
        this->textcenter_x = true;
        this->textcenter_y = true;
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
    uint64_t unique_id = 0; // real unique id, first field of card. MUST be persistent across edits.
    uint64_t i; // which note this is in the list of notes, used to order new cards only. MUST be in linear order in the note file.
    note(std::string line, uint64_t i)
    {
        this->i = i;
        
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
    scheduling s;
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
// returns a positive number when t2 is larger
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

// Get current time, plus whatever the offset is
int offset_hours = 0;
time_t time()
{
    return add_hours(time(NULL), offset_hours);
}

void schedule_minutes(time_t now, scheduling & schedule, int minutes)
{
    schedule.time_scheduled_from = now;
    schedule.time_scheduled_for = add_minutes(now, minutes);
    schedule.day_interval = 0;
    schedule.day_buried_until = 0;
}
void schedule_days(time_t now, scheduling & schedule, int days)
{
    schedule.time_scheduled_from = now;
    schedule.time_scheduled_for = add_days(now, days);
    schedule.day_interval = days;
    schedule.day_buried_until = 0;
}
void schedule_bury(time_t now, scheduling & schedule, int days)
{
    schedule.day_buried_until = add_days(time_floor_to_midnight(now), days);
}

// a deck is a collection of notes and cards
struct deck
{
    std::map<uint64_t, note *> notes;
    std::map<uint64_t, format *> formats;
    std::map<std::pair<uint64_t, uint64_t>, card *> cards;
    void add_note(note * n)
    {
        notes[n->unique_id] = n;
    }
    bool card_exists(uint64_t note_id, uint64_t format_id)
    {
        return cards.count(std::pair(note_id, format_id));
    }
    bool valid_possible_card(uint64_t note_id, uint64_t format_id)
    {
        return notes.count(note_id) != 0 and formats.count(format_id) != 0;
    }
    void card_insert(uint64_t note_id, uint64_t format_id, card * newcard)
    {
        cards[std::pair(note_id, format_id)] = newcard;
    }
    // arguments must be valid, check with valid_possible_card if you're calling with user data
    card make_default_card(uint64_t note_id, uint64_t format_id)
    {
        card c;
        c.n = notes[note_id];
        c.f = formats[format_id];
        c.s.time_added = 0;
        c.s.time_scheduled_from = 0;
        c.s.time_scheduled_for = 0;
        c.s.time_last_seen = 0;
        c.s.day_buried_until = 0;
        c.s.days_repped = 0;
        c.s.times_flunked = 0;
        c.s.times_passed = 0;
        return c;
    }
    card * try_insert_new_card(uint64_t note_id, uint64_t format_id)
    {
        if(card_exists(note_id, format_id)) return nullptr;
        if(!valid_possible_card(note_id, format_id)) return nullptr;
        
        auto c = make_default_card(note_id, format_id);
        
        auto newcard = new card;
        *newcard = c;
        card_insert(note_id, format_id, newcard);
        
        return newcard;
    }
    void add_missing_cards()
    {
        for(auto && [ k, f ] : formats)
        {
            for(auto && [ k, n ] : notes)
            {
                auto c = try_insert_new_card(n->unique_id, f->unique_id);
                if(c != nullptr) puts("Added a missing card");
            }
        }
    }
};

// TODO: Support multiple decks via folders

void load_notes(deck * mydeck)
{
    puts("loading note");
    std::ifstream file("notes.txt");
    std::string str;
    uint64_t i = 0;
    while (std::getline(file, str))
        mydeck->add_note(new note(str, i++));
}
posdata posdata_from_string(std::string str)
{
    std::vector<std::string> matches;
    std::string match = "";
    for(auto ch : str)
    {
        if(ch != ' ')
            match += ch;
        else if(match != "")
        {
            matches.push_back(match);
            match = "";
        }
    }
    if(match != "")
        matches.push_back(match);
    if(matches.size() == 0)
    {
        return posdata(0);
    }
    else if(matches.size() == 1)
    {
        return posdata(std::stoi(matches[0].data()));
    }
    else
    {
        reference ref = EARLY;
        if(matches.size() >= 2) // guaranteed to be true, but for code cleanliness reasons
        {
            if(matches[1] == "EARLY")
                ref = EARLY;
            else if(matches[1] == "CENTER")
                ref = CENTER;
            else if(matches[1] == "LATE")
                ref = LATE;
        }
        bool proportional = false;
        if(matches.size() >= 3)
        {
            if(matches[2] == "true")
                proportional = true;
            else if(matches[2] == "false")
                proportional = false;
        }
        return posdata(std::stoi(matches[0].data()), ref, proportional);
    }
}
void load_format(deck * mydeck)
{
    puts("loading formats");
    std::ifstream file("formats.txt");
    std::string last_string = "";
    try
    {
        std::string str;
        while (std::getline(file, str))
        {
            last_string = str;
            bool blank = true;
            for(auto ch : str) if(ch != ' ') blank = false;
            if(blank)
            {
                break;
            }
            
            if(str.back() != ':')
            {
                continue;
            }
            std::string id_s = "";
            bool leadingspace = true;
            for(auto ch : str)
            {
                if(leadingspace and ch == ' ')
                    continue;
                else
                    leadingspace = false;
                if(ch != ':')
                    id_s += ch;
            }
            
            uint64_t id = std::stoull(id_s);
            
            auto formatting_group = new format;
            formatting_group->unique_id = id;
            while (std::getline(file, str))
            {
                last_string = str;
                bool blank = true;
                for(auto ch : str) if(ch != ' ' and ch != '\n') blank = false;
                if(blank)
                {
                    break;
                }
                
                if(str.back() != ':')
                {
                    continue;
                }
                std::string name = "";
                bool leadingspace = true;
                for(auto ch : str)
                {
                    if(leadingspace and ch == ' ')
                        continue;
                    else
                        leadingspace = false;
                    if(ch != ':')
                        name += ch;
                }
                
                int type = -1;
                if(name == "common")
                    type = 0;
                else if(name == "front")
                    type = 1;
                else if(name == "answer")
                    type = 2;
                
                std::vector<std::string> values;
                while (std::getline(file, str))
                {
                    last_string = str;
                    bool blank = true;
                    for(auto ch : str) if(ch != ' ' and ch != '\n') blank = false;
                    if(blank)
                    {
                        break;
                    }
                    
                    std::string value = "";
                    bool leadingspace = true;
                    for(auto ch : str)
                    {
                        if(leadingspace and ch == ' ')
                            continue;
                        else
                            leadingspace = false;
                        value += ch;
                    }
                    
                    values.push_back(value);
                }
                if(values.size() != 10)
                    puts("Wrong number of values in a format element, ignoring format element.");
                else
                {
                    auto formatelement =
                        new formatting {
                            posdata_from_string(values[0]),
                            posdata_from_string(values[1]),
                            posdata_from_string(values[2]),
                            posdata_from_string(values[3]),
                            (uint8_t)std::stoi(values[4]), // r
                            (uint8_t)std::stoi(values[5]), // g
                            (uint8_t)std::stoi(values[6]), // b
                            values[7], // format string
                            std::stoi(values[8]), // text size
                            ((values[9] == "true") ? (true) : (false)), // centered
                            type
                        };
                    formatting_group->formatting.push_back(formatelement);
                }
            }
            mydeck->formats[id] = formatting_group;
        }
    }
    catch (const std::invalid_argument& e)
    {
        puts("ERROR: Number formatting problem in deck format. Exiting.");
        printf("Last string: '%s'\n", last_string.data());
        exit(0);
    }
    catch (const std::out_of_range& e)
    {
        puts("ERROR: Number size problem in deck format. Exiting.");
        printf("Last string: '%s'\n", last_string.data());
        exit(0);
    }
}

bool queue_save = false;

// user interface for the deck
struct deckui
{
    std::vector<std::pair<formatting *, element *>> associations;
    std::vector<element *> frontbuttons;
    std::vector<element *> backbuttons;
    
    card * currentcard;
    deck currentdeck;
    
    int notes_daily = 3;
    int new_notes_today = notes_daily;
    time_t note_day = time_floor_to_midnight(time());
    
    element * donefornow = nullptr;
    
    int displaystate = 0; // 0: stashed; 1: front; 2: back
    
    deckui()
    {
        load_format(&currentdeck);
        load_notes(&currentdeck);
        
        deserialize(&currentdeck);
        currentdeck.add_missing_cards();
        
        // make UI elements for all of the formatting elements in our card definition
        for(auto&& [ k, format ] : currentdeck.formats)
        {
            for(auto e : format->formatting)
            {
                element * myelement;
                if(e->format == "")
                    myelement = new element(e->x, e->y, e->w, e->h, true, false, "<uninitialized>", 0, 0, e->fontsize, {e->r, e->g, e->b}, {128,0,0} );
                else
                    myelement = new element(e->x, e->y, e->w, e->h, false, false, "<uninitialized>", 0, 0, e->fontsize, {128,0,0}, {e->r, e->g, e->b} );
                myelement->textcenter_x = e->centered;
                associations.push_back(std::pair<formatting *, element *> { e, myelement });
            }
        }
        
        donefornow = new element(
            posdata(20, EARLY, true), posdata(40, EARLY, true), posdata(60, EARLY, true), posdata(20, EARLY, true),
            false, false, "No cards left to study today", 0, 0, 32, {0, 0, 0}, {255, 255, 255});
        donefornow->textcenter_x = true;
        donefornow->textcenter_y = true;
    }
    void initialize_display()
    {
        auto now = time();
        auto cardlist = available(now);
        if(cardlist.size() > 0)
        {
            puts("setting up initial card");
            currentcard = cardlist[0];
            refresh(currentcard);
        }
        else
        {
            puts("no available cards to set up");
            currentcard = nullptr;
            stash();
        }
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
                else if(*text == 'n') // newline escape
                    current += '\n';
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
        displaystate = 0;
        puts("calling stash");
        stashed = true;
        for(auto p : associations)
        {
            auto e = p.second;
            e->text = "";
            e->active = false;
        }
        donefornow->active = true;
        for(auto b : frontbuttons)
        {
            puts("setting front button to inactive");
            b->active = false;
        }
        for(auto b : backbuttons)
            b->active = false;
        queue_save = true;
    }
    void unstash()
    {
        stashed = false;
        donefornow->active = false;
    }
    
    // shows the front of the given card
    void refresh(card * c)
    {
        displaystate = 1;
        currentcard = c;
        
        if(currentcard == nullptr)
        {
            puts("Refreshed card is nullptr");
            return;
        }
        
        unstash();
        for(auto b : frontbuttons)
            b->active = true;
        for(auto b : backbuttons)
            b->active = false;
        
        currentcard->s.time_last_seen = time();
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
        displaystate = 2;
        if(currentcard == nullptr) return;
        
        unstash();
        for(auto b : frontbuttons)
            b->active = false;
        for(auto b : backbuttons)
            b->active = true;
        
        currentcard->s.time_last_seen = time();
        for(auto p : associations)
        {
            auto f = p.first;
            auto e = p.second;
            e->active = true;
            if(f->type == 1) e->active = false;
        }
        
        queue_save = true;
    }
    
    // answers and reschedules the current note
    void answer(int rank = 0) // rank - 0: flunk; 1: good; (for now)
    {
        if(stashed) return;
        
        if(currentcard == nullptr) return;
        
        auto & schedule = currentcard->s;
        
        auto now = time();
        
        if(schedule.days_repped == 0)
        {
            schedule.days_repped++;
            schedule.time_added = now;
            new_notes_today--;
        }
        else if(days_between(now, schedule.time_scheduled_from) != 0)
            schedule.days_repped++;
        
        if(schedule.learning > 0)
        {
            // flunk: reset to first learning step
            if(rank == 0) 
            {
                schedule.learning = 2;
                schedule_minutes(now, schedule, 1);
                // If we're on the first learning stage...
                if(schedule.learning == 2)
                {
                    // check the consecutive flunk counter
                    if(schedule.consecutive_flunks > 0)
                    {
                        puts("learning flunk");
                        schedule.consecutive_flunks--;
                    }
                    // if we exhausted our consecutive flunks on first learning stage cards, we're leeching
                    // (the consecutive flunk count is only reset on graduation when buried)
                    else
                    {
                        puts("learning flunk (leech, bury)");
                        schedule.consecutive_flunks = 8;
                        schedule_bury(now, schedule, 1);
                    }
                }
            }
            // pass: depends on learning stage
            else
            {
                schedule.learning--;
                
                if(schedule.learning > 0)
                {
                    puts("learning step");
                    schedule_minutes(now, schedule, 5);
                }
                // was the last learning stage: graduated, one day
                else // graduation
                {
                    puts("learning graduation");
                    schedule_days(now, schedule, 1);
                    schedule.consecutive_flunks = 8;
                }
            }
        }
        // review queue card
        else
        {
            // failsafe
            schedule.consecutive_flunks = 8;
            // if this is a followup review
            if(schedule.day_interval < schedule.last_good_day_interval)
            {
                if(rank == 0)
                {
                    puts("followup flunk");
                    schedule.learning = 2;
                    schedule.times_flunked++;
                    schedule.last_good_day_interval = 1; // no longer a followup card
                    schedule_minutes(now, schedule, 1);
                }
                // 
                else
                {
                    puts("followup pass");
                    schedule.times_passed++;
                    schedule_days(now, schedule, schedule.last_good_day_interval); // reset to last recent good interval instead of using a low interval
                }
            }
            // if this is a normal review
            else
            {
                if(rank == 0)
                {
                    puts("rep flunk");
                    schedule.learning = 2;
                    schedule.times_flunked++;
                    // last_good_day_interval is whatever the *previous* interval was
                    // e.g. if this was a 2.5 ease 25 day interval we failed, the last good interval will be 10 days
                    schedule_minutes(now, schedule, 1);
                }
                else
                {
                    puts("rep pass");
                    schedule.times_passed++;
                    schedule.last_good_day_interval = schedule.day_interval;
                    schedule_days(now, schedule, schedule.day_interval*schedule.ease);
                }
            }
        }
        
        schedule.time_last_seen = now;
        
        queue_save = true;
        
        next(now);
    }
    
    // advances to the next note
    void next(time_t now)
    {
        auto cardlist = available(now);
        if(cardlist.size() > 0)
        {
            currentcard = cardlist[0];
            refresh(currentcard);
        }
        else
            stash();
    }
    
    void check_day_reset(time_t now)
    {
        auto day = time_floor_to_midnight(now);
        if(note_day != day)
        {
            new_notes_today = notes_daily;
            note_day = day;
        }
    }
    
    void sort_new(std::vector<card *> & cardlist)
    {
        std::sort(cardlist.begin(), cardlist.end(), [this](card * a, card * b)
        {
            if(a == nullptr and b == nullptr) return false;
            if(b == nullptr) return false;
            if(a == nullptr) return true;
            // sort new cards by their note's position in the notes file
            if(a->n->i != b->n->i)
                return a->n->i < b->n->i;
            // if same note, sort by their format's unique ID instead
            else
                return a->f->unique_id < b->f->unique_id;
                
        });
    }
    void sort_learn(std::vector<card *> & cardlist)
    {
        std::sort(cardlist.begin(), cardlist.end(), [this](card * a, card * b)
        {
            if(a == nullptr and b == nullptr) return false;
            if(b == nullptr) return false;
            if(a == nullptr) return true;
            // sort learning cards by time waited
            return a->s.time_scheduled_from < b->s.time_scheduled_from;
        });
    }
    void sort_review(std::vector<card *> & cardlist, time_t now)
    {
        std::sort(cardlist.begin(), cardlist.end(), [this, now](card * a, card * b)
        {
            if(a == nullptr and b == nullptr) return false;
            if(b == nullptr) return false;
            if(a == nullptr) return true;
            // sort review cards by proportional overdueness
            float a_overdueness = days_between(a->s.time_scheduled_for, now)/a->s.day_interval; // example: days_between(2, 5)/2 -> 3/2 -> 1.5 (mid importance)
            float b_overdueness = days_between(b->s.time_scheduled_for, now)/b->s.day_interval; // example: days_between(2, 5)/3 -> 3/3 -> 1.0 (low importance);  example: days_between(2, 6)/2 -> 4/2 -> 2.0 (high importance)
            if(a_overdueness != b_overdueness)
                return a_overdueness > b_overdueness; // inequality is inverted because small is less important here
            else
                return a->s.time_scheduled_from < b->s.time_scheduled_from;
        });
    }
    // find some available cards
    // sorted by preferred first
    // the vector is not the list of all available cards, only the ones in the queue that available() rolls dice to select
    std::vector<card *> available(time_t now)
    {
        check_day_reset(now);
        
        std::vector<card *> available_new;
        std::vector<card *> available_learning;
        std::vector<card *> available_review;
        std::vector<card *> learning_cards;
        puts("making list of available cards");
        for(auto && [k, card] : currentdeck.cards)
        {
            auto & schedule = card->s;
            
            // if buried, skip
            if((schedule.days_repped > 0 or schedule.learning == 0) and now < schedule.day_buried_until)
            {
                puts("Buried card");
                continue;
            }
            
            // for later
            if(schedule.learning > 0 and schedule.days_repped > 0)
                learning_cards.push_back(card);
            // learning card, ready
            if(schedule.learning > 0 and schedule.days_repped > 0 and now >= schedule.time_scheduled_for)
            {
                puts("learning card");
                available_learning.push_back(card);
            }
            // new card
            else if(schedule.learning > 0 and schedule.days_repped == 0 and new_notes_today > 0)
            {
                puts("new card");
                available_new.push_back(card);
            }
            // rep card
            else if(schedule.learning == 0 and days_between(now, schedule.time_scheduled_for) <= 0 and schedule.day_interval > 0)
            {
                puts("rep card");
                printf("scheduled for: %lld\n", schedule.time_scheduled_for);
                printf("now: %lld\n", now);
                available_review.push_back(card);
            }
        }
        // bring learning cards closer to now by the study ahead limit (FIXME: hardcoded 5 minutes) if we're out of stuff to study
        if(available_new.size() == 0 and available_learning.size() == 0 and available_review.size() == 0 and learning_cards.size() > 0)
        {
            for(auto card : learning_cards)
            {
                auto & schedule = card->s;
                auto oldtime = schedule.time_scheduled_for;
                schedule.time_scheduled_for = add_seconds(now, -5*60);
                // if we caused integer underflow (technically possible by standard C) set the scheduled_for time to 0 instead
                if(oldtime < schedule.time_scheduled_for) schedule.time_scheduled_for = 0;
                
                // finally, queue up available cards
                if(now >= schedule.time_scheduled_for)
                {
                    puts("unprepared card");
                    available_learning.push_back(card);
                }
                
            }
        }
        
        // pure laziness
        srand(now);
        rand(); // some rand() implementations are broken and seed after the next call
        float check_new      = available_new     .size()?(rand()/(float)RAND_MAX*available_new     .size()):-1;
        float check_learning = available_learning.size()?(rand()/(float)RAND_MAX*available_learning.size()):-1;
        float check_review   = available_review  .size()?(rand()/(float)RAND_MAX*available_review  .size()):-1;
        
        sort_new(available_new);
        sort_learn(available_learning);
        sort_review(available_review, now);
        
        //printf("%d %.2f; %d %.2f; %d %.2f\n", available_new.size(), check_new, available_learning.size(), check_learning, available_review.size(), check_review);
        
        if(check_review > check_new and check_review > check_learning)
            return available_review;
        else if(check_new > check_learning)
            return available_new;
        else
            return available_learning;
    }
    
    // informs the UI about our UI elements
    void insert_into(std::vector<element*> & elements)
    {
        for(auto p : associations)
        {
            auto e = p.second;
            elements.push_back(e);
        }
        elements.push_back(donefornow);
    }
    
    // Deserialize scheduling state
    void deserialize(deck * mydeck)
    {
        puts("loading scheduling");
        // read the current scheduling state in from disk
        std::ifstream file("cards.txt");
        std::string str;
        int line = 0;
        int version = 0;
        while (std::getline(file, str))
        {
            if(line == 0)
            {
                if(str == "version2")
                    version = 1;
            }
            else if(version == 1)
            {
                std::string match = "";
                std::vector<std::string> matches;
                for(auto ch : str)
                {
                    if(ch != ' ' and ch != '\n' and ch != '\0')
                        match += ch;
                    else if(match != "")
                    {
                        matches.push_back(match);
                        match = "";
                    }
                }
                if(match != "")
                {
                    matches.push_back(match);
                    match = "";
                }
                if(line == 1)
                {
                    if(matches.size() != 4)
                    {
                        puts("ERROR: Wrong number of scheduling metadata fields. Can't do anything.");
                        for(auto m : matches)
                            puts(m.data());
                        return;
                    }
                    else
                    {
                        try
                        {
                            offset_hours = std::stoi(matches[0]);
                            note_day = std::stoi(matches[1]);
                            notes_daily = std::stoi(matches[2]);
                            new_notes_today = std::stoi(matches[3]);
                        }
                        catch (const std::invalid_argument& e)
                        {
                            puts("ERROR: A field in the scheduling metadata is invalid. Can't do anything.");
                            return;
                        }
                        catch (const std::out_of_range& e)
                        {
                            puts("ERROR: A field in the scheduling metadata is too large. Can't do anything.");
                            return;
                        }
                    }
                }
                else
                {
                    if(matches.size() != 15)
                    {
                        puts("ERROR: A card's scheduling data has the wrong number of fields. The card will be discarded.");
                        for(auto m : matches)
                            puts(m.data());
                        continue;
                    }
                    else
                    {
                        try
                        {
                            int i = 0;
                            auto nid = std::stoull(matches[i++]);
                            auto fid = std::stoull(matches[i++]);
                            
                            auto c = mydeck->try_insert_new_card(nid, fid);
                            if(!c)
                                puts("ERROR: Tried to insert a card that either already exists or has an invalid note id or format id. The card will be discarded.");
                            else
                            {
                                scheduling s;
                                s.time_added = std::stoll(matches[i++]);
                                s.time_scheduled_from = std::stoll(matches[i++]);
                                s.time_scheduled_for = std::stoll(matches[i++]);
                                s.time_last_seen = std::stoll(matches[i++]);
                                s.day_buried_until = std::stoll(matches[i++]);
                                
                                s.consecutive_flunks = std::stoi(matches[i++]);
                                s.day_interval = std::stoi(matches[i++]);
                                s.last_good_day_interval = std::stoi(matches[i++]);
                                
                                s.days_repped = std::stoi(matches[i++]);
                                s.times_flunked = std::stoi(matches[i++]);
                                s.times_passed = std::stoi(matches[i++]);
                                
                                s.ease = std::stof(matches[i++]);
                                s.learning = std::stoi(matches[i++]);
                                
                                c->s = s;
                            }
                        }
                        catch (const std::invalid_argument& e)
                        {
                            puts("ERROR: A field in a card is invalid. The card will be discarded.");
                        }
                        catch (const std::out_of_range& e)
                        {
                            puts("ERROR: A field in a card is too large. The card will be discarded.");
                        }
                    }
                }
            }
            line++;
        }
    }

    // Serialize scheduling state
    void serialize(deck * mydeck)
    {
        auto & cards = mydeck->cards;
        // make a backup with a date-based name
        char newfname[128];
        auto now = time();
        auto tm_now = localtime(&now);
        auto err = strftime(newfname, 128, "cards_backup_%Y%m%d", tm_now);
        if(err != 0)
            rename("cards.txt", newfname);
        else
            rename("cards.txt", "cards_backup_dateless");
        // write the current scheduling state out to disk
        auto f = fopen("cards.txt", "wb");
        fprintf(f, "version2\n");
        fprintf(f, "%d %lld %d %d\n", offset_hours, note_day, notes_daily, new_notes_today);
        for(auto && [k, c] : cards)
        {
            fprintf(f, "%lld %lld %lld %lld %lld %lld %lld %d %d %d %d %d %d %f %d\n",
                c->n->unique_id, c->f->unique_id,
                c->s.time_added, c->s.time_scheduled_from, c->s.time_scheduled_for, c->s.time_last_seen, c->s.day_buried_until, 
                c->s.consecutive_flunks, c->s.day_interval, c->s.last_good_day_interval,
                c->s.days_repped, c->s.times_flunked, c->s.times_passed,
                c->s.ease,
                c->s.learning
            );
        }
        fclose(f);
        puts("saved schedule");
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
    mydeckui.frontbuttons.push_back(b_flip);
    mydeckui.backbuttons.push_back(b_flunk);
    mydeckui.backbuttons.push_back(b_good);
    
    mydeckui.initialize_display();
    
    // Pointless test text
    const char * prealphawarning = "Prealpha Software\nプレアルファ";
    int prealphasize = 16;
    elements.push_back(new element(posdata(-ceil(backend.string_width_pixels(prealphawarning, prealphasize)), LATE), posdata(-prealphasize*2, LATE), posdata(0, LATE), posdata(prealphasize*2, EARLY), false, false,
        prealphawarning,
        0, 0, prealphasize, {0,0,0}, {255,255,255}));
    
    // Main loop / mainloop
    while(1)
    {
        SDL_Event event;
        static element * pressedelement = nullptr;
        while (SDL_PollEvent(&event))
        {
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
                        mydeckui.flip();
                        break;
                    case FLUNK:
                        mydeckui.answer(0);
                        break;
                    case GOOD:
                        mydeckui.answer(1);
                        break;
                    }
                }
                pressedelement = nullptr;
            }
            if(event.type == SDL_KEYDOWN)
            {
                if(event.key.keysym.sym == SDLK_1)
                {
                    if(mydeckui.displaystate == 1)
                        mydeckui.flip();
                    else
                        mydeckui.answer(0);
                }
                if(event.key.keysym.sym == SDLK_2)
                {
                    if(mydeckui.displaystate == 1)
                        mydeckui.flip();
                    else
                        mydeckui.answer(1);
                }
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
                int x = element->x1(&backend);
                int y = element->y1(&backend);
                backend.string(backend.surface, &element->bitmapcache, x, y, element->x1(&backend), element->y1(&backend), element->x2(&backend)+1, element->y2(&backend)+1,
                    element->text.data(), element->foreground.r*factor, element->foreground.g*factor, element->foreground.b*factor, element->textsize,
                    element->textcenter_x, element->textcenter_y);
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
        
        if(queue_save)
        {
            mydeckui.serialize(&mydeckui.currentdeck);
            queue_save = false;
        }
        
        SDL_Delay(1);
    }
    
    return 0;
}
