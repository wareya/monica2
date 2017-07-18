# Monica srs

Monica is a spaced repetition flashcard program inspired by Anki.

Monica implements a modified version of Anki's modified SM2 spacing algorithm.

Monica only gives 2 choices for answering cards. Answering cards late doesn't change anything, the new interval is based on the intended interval. Ease is never modified. 

The first failure on a review card puts it into "followup" mode, which means that the last successfully answered interval will be recovered if you do not get it wrong the next time it comes up for reviwe outside of the learning queue. The learning queue does not leave "learning ahead" mode when you lapse a learning queue card (technically, entering the "learning ahead" mode brings every learning queue card's interval closer to now until one is available).

Finally, not SRS related, cards are automatically buried if they relapse from the first learning step 8 times in a single day.

Monica is prealpha software. **Please do not use monica for academic studies (e.g. medicine), it might be unstable and might get abandoned.**

# Compiling

Requires a C++17 compiler and SDL2.

## On unix-like platforms

Install SDL2 dev libraries from your package manager, modify compile.sh to hashbang your own shell and use the OS's SDL paths.

## Windows

Create the directory path depend/sdl2/ in the repository root, extract the contents of the SDL2 development library package to it.

![example](https://i.imgur.com/OrvPY93.png)

If necessary, modify compile.sh to correspond to your compilation target environment's architecture.

Recommended toolchain is Mingw-w64 from mingwbuilds. Msys2 is not recommended. Msys2 repackages libraries and monica has not been tested with Msys2's specific repackaging of SDL2, and Msys2 packages do not see extensive testing as linux distro packages do.

# Tech notes

Environment should return unix timestamps from time() for best compatibility, but this is not technically required. Using monica on a system where this is not the case will result in scheduling data that gives bogus dates when loaded on other platforms, because scheduling data is stored as a numeric timestamp.
