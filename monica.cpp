#include <SDL2/SDL.h>
#undef main

#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include <string>
#include <map>
#include <vector>

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

uint32_t MapRGB(const SDL_PixelFormat * format, Uint8 r, Uint8 g, Uint8 b)
{
    if(format->format == SDL_PIXELFORMAT_RGB888)
        return (r << 16) | (g << 8) | (b << 0);
    else
        return SDL_MapRGB(format, r, g, b);
}

// Because for some reason std::fill doesn't let you specify the number of bytes
struct triad
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

bool render = false;

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
    int subpixel = floor(offset*3);
    if(subpixel == 4) subpixel = 3;
    uint64_t key = (uint64_t(index) << 2) + (subpixel & 0b11);
    if(bitmapcache)
        if(bitmapcache->count(key) > 0 and cache)
            return (*bitmapcache)[key];
    
    crap data;
    data.data = stbtt_GetGlyphBitmapSubpixel(fontinfo, fontscale, fontscale, subpixel/3.0, 0, index, &data.width, &data.height, &data.xoff, &data.yoff);
    if(bitmapcache and cache) bitmapcache->insert(std::pair<uint64_t, crap>(key, data));
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
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
        win = SDL_CreateWindow("monica srs", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_RESIZABLE);
        surface = SDL_GetWindowSurface(win);
        
        auto fontfile = fopen("NotoSansCJKjp-Regular.otf", "rb");
        if(!fontfile) puts("failed to open font file");
        fseek(fontfile, 0, SEEK_END);
        uint64_t fontsize = ftell(fontfile);
        unsigned char * buffer = (unsigned char*)malloc(fontsize);
        fseek(fontfile, 0, SEEK_SET);
        if(fread(buffer, 1, fontsize, fontfile) != fontsize)
            puts("failed to read font file");
        fclose(fontfile);
        
        if(stbtt_InitFont(&fontinfo, buffer, stbtt_GetFontOffsetForIndex(buffer, 0)) == 0)
            puts("Something happened initializing the font");
    }
    
    std::map<uint32_t, uint32_t> indexcache;
    uint32_t glyph_lookup(uint32_t codepoint)
    {
        if(indexcache.count(codepoint) > 0 and cache)
            return indexcache[codepoint];
        else
        {
            uint32_t index = stbtt_FindGlyphIndex(&fontinfo, codepoint);
            if(cache) indexcache.insert(std::pair<uint32_t, uint32_t>(codepoint, index));
            return index;
        }
    }
    
    int font_height_pixels(float size)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        int ascent, descent;
        stbtt_GetFontVMetrics(&fontinfo, &ascent, &descent, nullptr);
        return (ascent-descent)*fontscale;
    }
    
    int string_width_pixels(const char * text, float size)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        
        float real_x = 0;
        
        size_t textlen = strlen(text);
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
        return ceil(real_x);
    }
    
    void string(SDL_Surface * surface, std::map<uint64_t, crap> * cache, int x, int y, const char * text, uint8_t red, uint8_t green, uint8_t blue, float size)
    {
        float fontscale = stbtt_ScaleForPixelHeight(&fontinfo, size);
        
        float real_x = x;
        
        size_t textlen = strlen(text);
        uint32_t lastindex = 0;
        
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
                    // reinsert 
                    
                    if(render)
                    {
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
                                    if(alpha > 0.5f/255.0f)
                                    {
                                        uint8_t * const pointer = ((uint8_t *)surface->pixels) + (out_y*surface->pitch + out_x*4);
                                        if(alpha >= 1.0f)
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
                                    memcpy(&output, pointer, surface->format->BytesPerPixel); // fixme: probably wrong on big endian
                                    
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
    
    void resurface()
    {
        surface = SDL_GetWindowSurface(win);
    }
    
    std::map<uint64_t, crap> bitmapcache;
    void update()
    {
        SDL_UpdateWindowSurface(win);
        fill(surface, 0,0,0);
    }
    
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
    
    void rect(SDL_Surface * surface, float r, float g, float b, int ax1, int ay1, int ax2, int ay2)
    {
        #if USE_SDL_EVERYWHERE
        rect_.x = ax1;
        rect_.y = ay1;
        rect_.w = ax2-ax1+1;
        rect_.h = ay2-ay1+1;
        SDL_FillRect(surface, &rect_, MapRGB(surface->format, r, g, b));
        #else
        
        uint32_t const color = MapRGB(surface->format, r, g, b);
        
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
    
    void rect_outline(SDL_Surface * surface, float r, float g, float b, int x1, int y1, int x2, int y2)
    {
        rect(surface, r, g, b, x1, y1, x2, y1);
        rect(surface, r, g, b, x1, y1, x1, y2);
        rect(surface, r, g, b, x1, y2, x2, y2);
        rect(surface, r, g, b, x2, y1, x2, y2);
    }
};

enum action {
    NONE,
    FLIP,
    FLUNK,
    GOOD
};

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

std::vector<element*> elements;

int main()
{
    graphics backend;
    
    backend.init();
    
    auto start = SDL_GetTicks();
    float smoothtime = 0;
    
    auto b_flip = new element(posdata(0), posdata(-32, LATE), posdata(0, LATE), posdata(32, EARLY), "flip", 24, {127,127,127}, {255,255,255}, FLIP);
    elements.push_back(b_flip);
    
    auto b_flunk = new element(posdata(0), posdata(-32, LATE), posdata(50, EARLY, true), posdata(32, EARLY), "flunk", 24, {255,0,0}, {255,255,255}, FLUNK);
    b_flunk->active = false;
    elements.push_back(b_flunk);
    
    auto b_good = new element(posdata(50, EARLY, true), posdata(-32, LATE), posdata(50, EARLY, true), posdata(32, EARLY), "good", 24, {0,200,0}, {255,255,255}, GOOD);
    b_good->active = false;
    elements.push_back(b_good);
    
    elements.push_back(new element(posdata(-backend.string_width_pixels("つづく", 32)-10, LATE), posdata(-40, LATE), posdata(0, LATE), posdata(32, EARLY), false, false,
        "つづく",
        0, 0, 32, {0,0,0}, {255,255,255}));
    
    while(1)
    {
        SDL_Event event;
        static element * pressedelement = nullptr;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                exit(0);
            
            if (event.type == SDL_WINDOWEVENT)
            {
                if(event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    backend.resurface();
            }
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
                        break;
                    case FLUNK:
                        b_flunk->active = false;
                        b_good->active = false;
                        b_flip->active = true;
                        break;
                    case GOOD:
                        b_flunk->active = false;
                        b_good->active = false;
                        b_flip->active = true;
                        break;
                    }
                }
                pressedelement = nullptr;
            }
        }
        
        for (auto element : elements)
        {
            if(!element) continue;
            if(!element->active) continue;
            float factor = (element==pressedelement)?(0.8):(1.0);
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
