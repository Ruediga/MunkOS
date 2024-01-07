/*
 * MIT License
 *
 * Copyright (c) 2024 Ruediga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "std/kprintf.h"

// defines for output formatting
#define KPRINTF_FLAGS_JUSTIFY_LEFT (1u << 0u)
#define KPRINTF_FLAGS_PERCEDE_SIGN (1u << 1u)
#define KPRINTF_FLAGS_PERCEDE_BLANK (1u << 2u)
#define KPRINTF_FLAGS_PRECEDE_RADIX_X_PREFIX (1u << 3u)
#define KPRINTF_KPRINTF_FLAGS_JUSTIFY_LEFT_PAD_ZEROES (1u << 4u)
// types
#define KPRINTF_FLAGS_TYPE_CHAR (1u << 5u)
#define KPRINTF_FLAGS_TYPE_SHORT (1u << 6u)
#define KPRINTF_FLAGS_TYPE_LONG (1u << 7u)
#define KPRINTF_FLAGS_TYPE_LONG_LONG (1u << 8u)
// extra flags
#define KPRINTF_FLAGS_CUSTOM_PRECISION (1u << 9u)
#define KPRINTF_FLAGS_UPPERCASE (1u << 10u)
#define KPRINTF_FLAGS_ADAPT_EXPONENT (1u << 11u)

#define KPRINTF_GET_FLAG_LONG_OR_LONG_LONG(T) (sizeof(T) == sizeof(long) ? KPRINTF_FLAGS_TYPE_LONG : KPRINTF_FLAGS_TYPE_LONG_LONG)

#ifndef KPRINTF_MAX_BUFFER_SIZE
#define KPRINTF_MAX_BUFFER_SIZE 32
#endif

#include "flanterm/flanterm.h"
extern struct flanterm_context *ft_ctx;
static inline void putc_(char c)
{
    flanterm_write(ft_ctx, &c, 1);
}

/*
 * Thanks to https://github.com/mpaland/printf for
 * providing some of the internal functionality.
 */

static void _out_rev(int *total, const char *buf, size_t len, unsigned int width, unsigned int flags)
{
    const size_t start_idx = *total;

    // pad spaces up to given width
    if (!(flags & KPRINTF_FLAGS_JUSTIFY_LEFT) && !(flags & KPRINTF_KPRINTF_FLAGS_JUSTIFY_LEFT_PAD_ZEROES))
    {
        for (size_t i = len; i < width; i++)
        {
            putc_(' ');
            (*total)++;
        }
    }

    // reverse string
    while (len)
    {
        putc_(buf[--len]);
        (*total)++;
    }

    // append pad spaces up to given width
    if (flags & KPRINTF_FLAGS_JUSTIFY_LEFT)
    {
        while ((*total) - start_idx < width)
        {
            putc_(' ');
            (*total)++;
        }
    }
}

static inline unsigned int _strnlen_s(const char *str, size_t maxsize)
{
    const char *s;
    for (s = str; *s && maxsize--; ++s)
        ;
    return (unsigned int)(s - str);
}

static void _ntoa_format(int *total, char *buf, size_t len, bool negative, unsigned int radix, unsigned int prec, unsigned int width, unsigned int flags)
{
    // pad leading zeros
    if (!(flags & KPRINTF_FLAGS_JUSTIFY_LEFT))
    {
        if (width && (flags & KPRINTF_KPRINTF_FLAGS_JUSTIFY_LEFT_PAD_ZEROES) && (negative || (flags & (KPRINTF_FLAGS_PERCEDE_SIGN | KPRINTF_FLAGS_PERCEDE_BLANK))))
        {
            width--;
        }
        while ((len < prec) && (len < KPRINTF_MAX_BUFFER_SIZE))
        {
            buf[len++] = '0';
        }
        while ((flags & KPRINTF_KPRINTF_FLAGS_JUSTIFY_LEFT_PAD_ZEROES) && (len < width) && (len < KPRINTF_MAX_BUFFER_SIZE))
        {
            buf[len++] = '0';
        }
    }

    // handle hash
    if (flags & KPRINTF_FLAGS_PRECEDE_RADIX_X_PREFIX)
    {
        if (!(flags & KPRINTF_FLAGS_CUSTOM_PRECISION) && len && ((len == prec) || (len == width)))
        {
            len--;
            if (len && (radix == 16U))
            {
                len--;
            }
        }
        if ((radix == 16U) && !(flags & KPRINTF_FLAGS_UPPERCASE) && (len < KPRINTF_MAX_BUFFER_SIZE))
        {
            buf[len++] = 'x';
        }
        else if ((radix == 16U) && (flags & KPRINTF_FLAGS_UPPERCASE) && (len < KPRINTF_MAX_BUFFER_SIZE))
        {
            buf[len++] = 'X';
        }
        else if ((radix == 2U) && (len < KPRINTF_MAX_BUFFER_SIZE))
        {
            buf[len++] = 'b';
        }
        if (len < KPRINTF_MAX_BUFFER_SIZE)
        {
            buf[len++] = '0';
        }
    }

    if (len < KPRINTF_MAX_BUFFER_SIZE)
    {
        if (negative)
        {
            buf[len++] = '-';
        }
        else if (flags & KPRINTF_FLAGS_PERCEDE_SIGN)
        {
            buf[len++] = '+'; // ignore the space if the '+' exists
        }
        else if (flags & KPRINTF_FLAGS_PERCEDE_BLANK)
        {
            buf[len++] = ' ';
        }
    }

    _out_rev(total, buf, len, width, flags);
}

static void _ntoa_long_long(int *total, unsigned long long value, bool negative, unsigned long long radix, unsigned int prec, unsigned int width, unsigned int flags)
{
    char buf[KPRINTF_MAX_BUFFER_SIZE];
    size_t len = 0U;

    // no hash for 0 values
    if (!value)
    {
        flags &= ~KPRINTF_FLAGS_PRECEDE_RADIX_X_PREFIX;
    }

    // write if precision != 0 and value is != 0
    if (!(flags & KPRINTF_FLAGS_CUSTOM_PRECISION) || value)
    {
        do
        {
            const char digit = (char)(value % radix);
            buf[len++] = digit < 10 ? '0' + digit : (flags & KPRINTF_FLAGS_UPPERCASE ? 'A' : 'a') + digit - 10;
            value /= radix;
        } while (value && (len < KPRINTF_MAX_BUFFER_SIZE));
    }

    _ntoa_format(total, buf, len, negative, (unsigned int)radix, prec, width, flags);
}

static void _ntoa_long(int *total, unsigned long value, bool negative, unsigned long radix, unsigned int prec, unsigned int width, unsigned int flags)
{
    char buf[KPRINTF_MAX_BUFFER_SIZE];
    size_t len = 0;

    // no hash for 0 values
    if (!value)
    {
        flags &= ~KPRINTF_FLAGS_PRECEDE_RADIX_X_PREFIX;
    }

    // write if precision != 0 and value is != 0
    if (!(flags & KPRINTF_FLAGS_CUSTOM_PRECISION) || value)
    {
        do
        {
            const char digit = (char)(value % radix);
            buf[len++] = digit < 10 ? '0' + digit : (flags & KPRINTF_FLAGS_CUSTOM_PRECISION ? 'A' : 'a') + digit - 10;
            value /= radix;
        } while (value && (len < KPRINTF_MAX_BUFFER_SIZE));
    }

    _ntoa_format(total, buf, len, negative, (unsigned int)radix, prec, width, flags);
}

int kprintf(const char *format, ...)
{
    va_list var_args;
    va_start(var_args, format);

    int total_chars_printed = 0;
    unsigned int flags = 0, width = 0, precision = 0;

    while (*format)
    {
        // if not %
        if (*format != '%')
        {
            putc_(*format);
            format++;
            total_chars_printed++;
            continue;
        }
        format++;

        // flags
        int brk = 0;
        while (!brk)
        {
            switch (*format)
            {
            case ('-'):
                flags |= KPRINTF_FLAGS_JUSTIFY_LEFT;
                format++;
                break;
            case ('+'):
                flags |= KPRINTF_FLAGS_PERCEDE_SIGN;
                format++;
                break;
            case (' '):
                flags |= KPRINTF_FLAGS_PERCEDE_BLANK;
                format++;
                break;
            case ('#'):
                flags |= KPRINTF_FLAGS_PRECEDE_RADIX_X_PREFIX;
                format++;
                break;
            case ('0'):
                flags |= KPRINTF_KPRINTF_FLAGS_JUSTIFY_LEFT_PAD_ZEROES;
                format++;
                break;
            default:
                brk = 1;
                break;
            }
        }

        // width
        while (*format >= '0' && *format <= '9')
        {
            width = width * 10 + (unsigned int)(*(format++) - '0');
        }
        if (*format == '*')
        {
            width = va_arg(var_args, int);
            format++;
        }

        // precision
        if (*format == '.')
        {
            format++;
            // we want some precision
            flags |= KPRINTF_FLAGS_CUSTOM_PRECISION;

            while (*format >= '0' && *format <= '9')
            {
                precision = precision * 10 + (unsigned int)(*(format++) - '0');
            }
            if (*format == '*')
            {
                precision = va_arg(var_args, int);
                format++;
            }
        }

        // length specifier
        switch (*format)
        {
        case ('h'):
            flags |= KPRINTF_FLAGS_TYPE_SHORT;
            format++;
            if (*format == 'h')
            {
                flags |= KPRINTF_FLAGS_TYPE_CHAR;
                format++;
            }
            break;
        case ('l'):
            flags |= KPRINTF_FLAGS_TYPE_LONG;
            format++;
            if (*format == 'l')
            {
                flags |= KPRINTF_FLAGS_TYPE_LONG_LONG;
                format++;
            }
            break;
        case ('j'):
            flags |= KPRINTF_GET_FLAG_LONG_OR_LONG_LONG(intmax_t);
            format++;
            break;
        case ('z'):
            flags |= KPRINTF_GET_FLAG_LONG_OR_LONG_LONG(size_t);
            format++;
            break;
        case ('t'):
            flags |= KPRINTF_GET_FLAG_LONG_OR_LONG_LONG(ptrdiff_t);
            format++;
            break;
        default:
            break;
        }

        // specifier
        switch (*format)
        {
        case 'b':
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            unsigned int radix, isSigned = 0;
            if (*format == 'b')
            {
                radix = 2;
            }
            else if (*format == 'd' || *format == 'i' || *format == 'u')
            {
                if (*format != 'u')
                {
                    isSigned = 1;
                }
                radix = 10;
                flags &= ~KPRINTF_FLAGS_PRECEDE_RADIX_X_PREFIX;
            }
            else if (*format == 'x')
            {
                radix = 16;
            }
            else if (*format == 'X')
            {
                radix = 16;
                flags |= KPRINTF_FLAGS_UPPERCASE;
            }
            else
            { // if *format == 'o'
                radix = 8U;
            }

            // no plus or space flag for b, u, o, x, X
            if ((*format != 'i') && (*format != 'd'))
            {
                flags &= ~(KPRINTF_FLAGS_PERCEDE_SIGN | KPRINTF_FLAGS_PERCEDE_BLANK);
            }

            // ignore '0' flag when precision is given
            if (flags & KPRINTF_FLAGS_CUSTOM_PRECISION)
            {
                flags &= ~KPRINTF_KPRINTF_FLAGS_JUSTIFY_LEFT_PAD_ZEROES;
            }

            if (isSigned)
            {
                if (flags & KPRINTF_FLAGS_TYPE_LONG_LONG)
                {
                    const long long value = va_arg(var_args, long long);
                    _ntoa_long_long(&total_chars_printed, (unsigned long long)(value > 0 ? value : 0 - value), value < 0, radix, precision, width, flags);
                }
                else if (flags & KPRINTF_FLAGS_TYPE_LONG)
                {
                    const long value = va_arg(var_args, long);
                    _ntoa_long(&total_chars_printed, (unsigned long)(value > 0 ? value : 0 - value), value < 0, radix, precision, width, flags);
                }
                else
                {
                    const int value = (flags & KPRINTF_FLAGS_TYPE_CHAR) ? (char)va_arg(var_args, int) : (flags & KPRINTF_FLAGS_TYPE_SHORT) ? (short int)va_arg(var_args, int)
                                                                                                                                           : va_arg(var_args, int);
                    _ntoa_long(&total_chars_printed, (unsigned int)(value > 0 ? value : 0 - value), value < 0, radix, precision, width, flags);
                }
            }
            else
            {
                // unsigned
                if (flags & KPRINTF_FLAGS_TYPE_LONG_LONG)
                {
                    _ntoa_long_long(&total_chars_printed, va_arg(var_args, unsigned long long), false, radix, precision, width, flags);
                }
                else if (flags & KPRINTF_FLAGS_TYPE_LONG)
                {
                    _ntoa_long(&total_chars_printed, va_arg(var_args, unsigned long), false, radix, precision, width, flags);
                }
                else
                {
                    const unsigned int value = (flags & KPRINTF_FLAGS_TYPE_CHAR) ? (unsigned char)va_arg(var_args, unsigned int) :
                        (flags & KPRINTF_FLAGS_TYPE_SHORT) ? (unsigned short int)va_arg(var_args, unsigned int) : va_arg(var_args, unsigned int);
                    _ntoa_long(&total_chars_printed, value, false, radix, precision, width, flags);
                }
            }
            format++;
            break;
        case 'c':
        {
            unsigned int l = 1U;
            // pre padding
            if (!(flags & KPRINTF_FLAGS_JUSTIFY_LEFT))
            {
                while (l++ < width)
                {
                    putc_(' ');
                    total_chars_printed++;
                }
            }
            // char output
            putc_((char)va_arg(var_args, int));
            total_chars_printed++;
            // post padding
            if (flags & KPRINTF_FLAGS_JUSTIFY_LEFT)
            {
                while (l++ < width)
                {
                    putc_(' ');
                    total_chars_printed++;
                }
            }
            format++;
            break;
        }
        case 's':
        {
            const char *p = va_arg(var_args, char *);
            unsigned int l = _strnlen_s(p, precision ? precision : (size_t)-1);
            // pre padding
            if (flags & KPRINTF_FLAGS_CUSTOM_PRECISION)
            {
                l = (l < precision ? l : precision);
            }
            if (!(flags & KPRINTF_FLAGS_JUSTIFY_LEFT))
            {
                while (l++ < width)
                {
                    putc_(' ');
                    total_chars_printed++;
                }
            }
            // string output
            while ((*p != 0) && (!(flags & KPRINTF_FLAGS_CUSTOM_PRECISION) || precision--))
            {
                putc_(*(p++));
                total_chars_printed++;
            }
            // post padding
            if (flags & KPRINTF_FLAGS_JUSTIFY_LEFT)
            {
                while (l++ < width)
                {
                    putc_(' ');
                    total_chars_printed++;
                }
            }
            format++;
            break;
        }

        case 'p':
        {
            width = sizeof(void *) * 2U;
            flags |= KPRINTF_KPRINTF_FLAGS_JUSTIFY_LEFT_PAD_ZEROES | KPRINTF_FLAGS_UPPERCASE;
            const bool is_ll = sizeof(uintptr_t) == sizeof(long long);
            if (is_ll)
            {
                _ntoa_long_long(&total_chars_printed, (uintptr_t)va_arg(var_args, void *), false, 16U, precision, width, flags);
            }
            else
            {
                _ntoa_long(&total_chars_printed, (unsigned long)((uintptr_t)va_arg(var_args, void *)), false, 16U, precision, width, flags);
            }
            format++;
            break;
        }

        case '%':
            putc_('%');
            total_chars_printed++;
            format++;
            break;

        default:
            putc_(*format);
            total_chars_printed++;
            format++;
            break;
        }
    }

    va_end(var_args);

    return 0;
}