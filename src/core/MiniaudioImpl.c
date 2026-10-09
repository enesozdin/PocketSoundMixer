// The single translation unit that compiles miniaudio. Feature-trim macros live in CMake
// so every file that includes miniaudio.h sees the same configuration.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
