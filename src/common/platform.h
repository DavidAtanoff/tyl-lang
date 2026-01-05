// Tyl Compiler - Platform compatibility header
#ifndef TYL_PLATFORM_H
#define TYL_PLATFORM_H

// Windows-specific defines
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#endif // TYL_PLATFORM_H
