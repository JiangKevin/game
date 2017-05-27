#pragma once

#if defined _WIN32 || defined __CYGWIN__
#ifdef __GNUC__
#define SYMBOL_EXPORT __attribute__((dllexport))
#define SYMBOL_IMPORT __attribute__((dllimport))
#else // __GNUC__
#define SYMBOL_EXPORT __declspec(dllexport)
#define SYMBOL_IMPORT __declspec(dllimport)
#endif // __GNUC__
#else  // _WIN32
#define SYMBOL_EXPORT __attribute__((visibility("default")))
#define SYMBOL_IMPORT
#endif // _WIN32