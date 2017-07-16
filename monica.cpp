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
    
};

struct element {
    int x, y, w, h;
    bool drawbg, outline;
    std::string text;
    int textx, texty, textsize;
    triad background, foreground;
    int clickaction = 0;
    bool active = true;
    
    std::map<uint64_t, crap> bitmapcache;
    
    element(int x, int y, int w, int h, bool drawbg, bool outline, std::string text, int textx, int texty, int textsize, triad background, triad foreground, int clickaction = 0, bool active = true)
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
        int y1 = y;
        if(y1 < 0) y1 += backend->surface->h;
        return y1;
    }
    int x1(graphics * backend)
    {
        int x1 = x;
        if(x1 < 0) x1 += backend->surface->w;
        return x1;
    }
    int y2(graphics * backend)
    {
        int y2 = h;
        if(y2 <= 0) y2 += backend->surface->h;
        else y2 += y1(backend);
        return y2;
    }
    int x2(graphics * backend)
    {
        int x2 = w;
        if(x2 <= 0) x2 += backend->surface->w;
        else x2 += x1(backend);
        return x2;
    }
};

std::vector<element*> elements;

int main()
{
    graphics backend;
    
    backend.init();
    
    auto start = SDL_GetTicks();
    float smoothtime = 0;
    
    elements.push_back(new element(-backend.string_width_pixels("つづく", 32)-10, -40, 0, 0, false, false,
        "つづく",
        0, 0, 32, {0,0,0}, {255,255,255}));
    
    elements.push_back(new element(10, 10,-10,-10, true, false,
        "\n"
        "私は結果もしこういう落第学という訳の時を教えました。かく事実を影響方ももとよりその撲殺ないななりを散らかすから致しなには留学来ないませて、たったにはなっなかろなけれないた。\n"
        "\n"
        "支を払っだ事はいよいよ時間へいよいよたませう。ちょうど久原さんで安心習慣あまり注文を上っです念その連中どこか相違へというご試験だないですませから、その当時はあなたか権力"
        "態度へ当てるから、嘉納さんのはずに秋刀魚の私にぷんぷんご注文と云っとあなた主義にご関係を考えようにもし実議論を云いだなけれども、ちゃんと無論所有を調っますといるやのをきまっなけれで。\n"
        "\n"
        "そうしてしかしご比喩の供する旨もどう曖昧としだて、その文芸をも致さですがという心をして得るですた。そのため傍点の中こういう教師も私中を困るないかと大森さんに上げるたない、性"
        "質の事実ですとしてご安心ですたたが、主命の中に別で今までの魚籃に九月して出しが、こうのその間を上るてその以上にもうあっだですと認めたものうて、多いますだろがなぜお人着るま"
        "せものたたな。つまり靄か便宜か所有を解るたて、ほか中火事に掘りからくるん上よりご発展のほかが至っだだ。時間がはどうしても飽いが始めありたいでしょうて、いよいよ余計入れて所有"
        "もとても多いですのない。ところがご意味にできるとはしまいごとくのなて、知人にも、けっしてそれかさていれますな引き離すられるないましとなれて、説はするがみありでしょ。\n"
        "\n"
        "そうしてしかしご比喩の供する旨もどう曖昧としだて、その文芸をも致さですがという心をして得るですた。そのため傍点の中こういう教師も私中を困るないかと大森さんに上げるたない、性"
        "質の事実ですとしてご安心ですたたが、主命の中に別で今までの魚籃に九月して出しが、こうのその間を上るてその以上にもうあっだですと認めたものうて、多いますだろがなぜお人着るま"
        "せものたたな。つまり靄か便宜か所有を解るたて、ほか中火事に掘りからくるん上よりご発展のほかが至っだだ。時間がはどうしても飽いが始めありたいでしょうて、いよいよ余計入れて所有"
        "もとても多いですのない。ところがご意味にできるとはしまいごとくのなて、知人にも、けっしてそれかさていれますな引き離すられるないましとなれて、説はするがみありでしょ。\n"
        "\n"
        "至極何しろはいくら方面においてくるたが、あれをは今上なりそれのごらくはえらいしいるんた。私はいくら承諾のもので不推薦は参りてみるですなんだが、一一の程度がしっかり聞えるまいと"
        "して講演でて、すなわちその傍点の人を返っられて、私かからよその他よりお話しを掴みで行くます事たたと学習するて自失する切らましだ。\n"
        "\n"
        "一般にまたは岡田さんへしかしいっそ聴いでものうでです。大森君はとても春を考えが分りんのないましう。（しかしがたのし時たいたましがありもしなますて、）こう進みう模範に、朝日新聞の"
        "兄かも立ち行かのでやりという、男の安心は今日の所まで済んさものの儲けないて学習ら立ちて得るなって大新うのまし。どこもむしろ一筋が思いませようにするがみるましのだがたとえばそう"
        "吉利がた出かけでしょない。またあまり一カ所も間際をかけ合わから、その間をまあ見るななとなっが、強くましますてもしくはご推察が接しでた。\n"
        "\n"
        "人の今に、その風俗を今が思っじゃ、時分上にちょっと事実幾幾十度が失っなりの書物を、どこか考えべく附随にさまし十月はいよいよいうられのたて、いよいよこう当人からないて、どんな気"
        "が引張っものに重ませ面白いありでした。すると同時に前二十三人をあるまではなりでという容易ない［＃「に見えると、人をそんな所いわゆる中のなりからしまっうものな。\n"
        "\n"
        "とうとうに手数から自分くれた一十年結果に洗わて、ここかするうていあるという事で実際生れなのなと、つい取りつかれのに妙ますて、今に本国からしからできているなけれん。\n"
        "\n"
        "人間をいうと握るばどこかなしものがしように過ぎでもいうですでしから、けれども問題は偉くのに突き抜けて、私に先生がよししまいて三本を一人は十杯もけっしてもっけれどもなら"
        "までですものない。ほかたでか至る自己が解らて、そうした金は高等憂非常ないとしでものますは立ち竦んだない、ない仲の日に云わたその道なかれと云ってしまいたのないべき。し"
        "たがって私は非常だので述べるだ方ではやかましい、危険なて取次いたらのますと思うて皆の外国の個性にその大勢が承諾見せるてみなけれた。人身がも普通たいやしくもしてい"
        "れれです今日に同年輩になるや、主義が生れとか、また貧民がしとかし市街が描い落、平穏なで、何しろしゃべっがたまらなく人をやるでとするから、世の中と纏めて中学でも何者"
        "までで困ら本領は這入りな。\n",    
        0, 0, 32, {0,128,255}, {255,255,255}));
    
    elements.push_back(new element(5, 5, 50, 20, true, false, "test", 5, 5, 16, {32,64,128}, {255,127,127}));
        
    elements.push_back(new element(0, 0,-0,-0, true, true, "", 0, 0, 0, {0,0,0}, {0,0,0}));
    elements.push_back(new element(1, 1,-1,-1, true, true, "", 0, 0, 0, {255,255,255}, {0,0,0}));
    elements.push_back(new element(2, 2,-2,-2, true, true, "", 0, 0, 0, {0,0,0}, {0,0,0}));
    elements.push_back(new element(3, 3,-3,-3, true, true, "", 0, 0, 0, {255,255,255}, {0,0,0}));
    elements.push_back(new element(4, 4,-4,-4, true, true, "", 0, 0, 0, {0,0,0}, {0,0,0}));
    
    while(1)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                exit(0);
            
            if (event.type == SDL_WINDOWEVENT)
            {
                if(event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    backend.resurface();
            }
        }
        
        for (auto element : elements)
        {
            if(!element) continue;
            if(element->drawbg)
            {
                if(!element->outline)
                    backend.rect(backend.surface, element->background.r, element->background.g, element->background.b, element->x1(&backend), element->y1(&backend), element->x2(&backend), element->y2(&backend));
                else
                    backend.rect_outline(backend.surface, element->background.r, element->background.g, element->background.b, element->x1(&backend), element->y1(&backend), element->x2(&backend), element->y2(&backend));
            }
            if(element->text != "")
            {
                backend.string(backend.surface, &element->bitmapcache, element->x1(&backend), element->y1(&backend), element->text.data(), element->foreground.r, element->foreground.g, element->foreground.b, element->textsize);
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
