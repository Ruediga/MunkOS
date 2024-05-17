#pragma once

#include <stddef.h>

// cpy src to dest, return dest, null delimit
char *strcpy (char *dest, const char *src);
// cpy max n characters of src to dest, return dest,
// don't explicitly null delimit, null - fill up to n
char *strncpy (char *dest, const char *src, size_t n);

// append src to dest, overwriting src' null delimiter, return dest
char *strcat (char *dest, const char *src);
// append max n characters of src to dest, overwriting src' null delimiter,
// properly null delimits, return dest
char *strncat (char *dest, const char *src, size_t n);

// compare s1 and s2, return lexograhic order difference
int strcmp (const char *s1, const char *s2);
// compare max n characters of s1 and s2, return lexograhic order difference
int strncmp (const char *s1, const char *s2, size_t n);

// find the first occurence of c in s, return string at first occurence, else NULL
char *strchr (const char *s, int c);
// find the lasr occurence of c in s, return string at last occurence, else NULL
char *strrchr (const char *s, int c);

// return the first occurence of needle in haystack, else NULL
char *strstr (const char *haystack, const char *needle);

// divide s into tokens seperated by characters in delim
char *strtok (char *s, const char *delim);

// return len of string s w/o null delimiter
size_t strlen (const char *s);