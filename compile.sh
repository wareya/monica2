#!bash
g++ -fno-strict-aliasing -msse -msse2 monica.cpp -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -pedantic -Wno-sign-compare -Ldepend/sdl2/x86_64-w64-mingw32/lib -Idepend/sdl2/x86_64-w64-mingw32/include -static -lSDL2 $(depend/sdl2/x86_64-w64-mingw32/bin/sdl2-config --static-libs) -O3 -mconsole -fdata-sections -ffunction-sections -fwhole-program -Wl,--gc-sections -Wl,--strip-all
