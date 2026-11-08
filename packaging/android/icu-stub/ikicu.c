/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stands in for ICU (libicuuc, libicui18n and the ~31 MB libicudata) in the
 * Android package. Qt Core is the only user, through 22 functions: case
 * mapping in QLocale, QCollator (locale-aware sorting) and QTextCodec's
 * legacy encodings. prune-tablet-libs.py points Qt Core at this library.
 *
 * On Android 12 (API 31) and later the system's ICU is public (libicu.so,
 * unversioned names) and every call is forwarded to it: same behaviour as
 * bundling ICU. On older versions each call reports U_UNSUPPORTED_ERROR and
 * Qt falls back to its own code: Unicode default case mapping, code point
 * order for sorting, and its built-in UTF-8/UTF-16/UTF-32/Latin-1 codecs
 * (Krita reads and writes UTF-8 everywhere).
 *
 * Build with -DICU_MAJOR=<n>: the version suffix Qt Core was linked against
 * (u_strToUpper_72 -> 72), read from Qt Core by prune-tablet-libs.py.
 */

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef ICU_MAJOR
#error "define ICU_MAJOR"
#endif

#define IK_CAT2(a, b) a##_##b
#define IK_CAT(a, b) IK_CAT2(a, b)
#define SYM(name) IK_CAT(name, ICU_MAJOR)
#define EXPORT __attribute__((visibility("default")))

typedef uint16_t UChar;
typedef int32_t UChar32;
typedef int UErrorCode;
typedef int8_t UBool;
typedef struct UConverter UConverter;
typedef struct UCollator UCollator;
typedef void (*UConverterFromUCallback)(const void *context, void *args, const UChar *codeUnits, int32_t length,
                                        UChar32 codePoint, int reason, UErrorCode *err);

#define U_UNSUPPORTED_ERROR 16
#define U_FAILURE(x) ((x) > 0)

static void *system_icu(void)
{
    static void *handle;
    static int tried;
    if (!tried) {
        tried = 1;
        handle = dlopen("libicu.so", RTLD_NOW | RTLD_LOCAL);
    }
    return handle;
}

static void *lookup(const char *name)
{
    void *h = system_icu();
    return h ? dlsym(h, name) : NULL;
}

/* Resolves the system function once; NULL when there is none. */
#define FORWARD(type, name) \
    static __typeof__(type) fn; \
    static int resolved; \
    if (!resolved) { \
        fn = (type)lookup(#name); \
        resolved = 1; \
    }

static int unsupported(UErrorCode *err)
{
    if (err && !U_FAILURE(*err)) *err = U_UNSUPPORTED_ERROR;
    return 0;
}

/* ------------------------------------------------------------- utilities */

EXPORT const char *SYM(u_errorName)(UErrorCode code)
{
    FORWARD(const char *(*)(UErrorCode), u_errorName)
    if (fn) return fn(code);
    return code == U_UNSUPPORTED_ERROR ? "U_UNSUPPORTED_ERROR" : (code > 0 ? "U_ERROR" : "U_ZERO_ERROR");
}

typedef int32_t (*CaseFn)(UChar *, int32_t, const UChar *, int32_t, const char *, UErrorCode *);

EXPORT int32_t SYM(u_strToLower)(UChar *dest, int32_t cap, const UChar *src, int32_t len, const char *locale,
                                 UErrorCode *err)
{
    FORWARD(CaseFn, u_strToLower)
    if (fn) return fn(dest, cap, src, len, locale, err);
    return unsupported(err);
}

EXPORT int32_t SYM(u_strToUpper)(UChar *dest, int32_t cap, const UChar *src, int32_t len, const char *locale,
                                 UErrorCode *err)
{
    FORWARD(CaseFn, u_strToUpper)
    if (fn) return fn(dest, cap, src, len, locale, err);
    return unsupported(err);
}

/* ------------------------------------------------------------ converters */

EXPORT void SYM(UCNV_FROM_U_CALLBACK_SUBSTITUTE)(const void *context, void *args, const UChar *codeUnits,
                                                 int32_t length, UChar32 codePoint, int reason, UErrorCode *err)
{
    FORWARD(UConverterFromUCallback, UCNV_FROM_U_CALLBACK_SUBSTITUTE)
    if (fn) fn(context, args, codeUnits, length, codePoint, reason, err);
    else unsupported(err);
}

EXPORT UConverter *SYM(ucnv_open)(const char *name, UErrorCode *err)
{
    FORWARD(UConverter * (*)(const char *, UErrorCode *), ucnv_open)
    if (fn) return fn(name, err);
    unsupported(err);
    return NULL;
}

EXPORT void SYM(ucnv_close)(UConverter *cnv)
{
    FORWARD(void (*)(UConverter *), ucnv_close)
    if (fn && cnv) fn(cnv);
}

static int name_char(const char **p)
{
    /* ICU's rule: compare ignoring case, '-', '_' and spaces. */
    while (**p == '-' || **p == '_' || **p == ' ') ++*p;
    int c = (unsigned char)**p;
    if (c) ++*p;
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

EXPORT int SYM(ucnv_compareNames)(const char *a, const char *b)
{
    FORWARD(int (*)(const char *, const char *), ucnv_compareNames)
    if (fn) return fn(a, b);
    for (;;) {
        int ca = name_char(&a), cb = name_char(&b);
        if (ca != cb || !ca) return ca - cb;
    }
}

EXPORT uint16_t SYM(ucnv_countAliases)(const char *alias, UErrorCode *err)
{
    FORWARD(uint16_t(*)(const char *, UErrorCode *), ucnv_countAliases)
    if (fn) return fn(alias, err);
    return (uint16_t)unsupported(err);
}

EXPORT int32_t SYM(ucnv_countAvailable)(void)
{
    FORWARD(int32_t(*)(void), ucnv_countAvailable)
    return fn ? fn() : 0;
}

EXPORT const char *SYM(ucnv_getAlias)(const char *alias, uint16_t n, UErrorCode *err)
{
    FORWARD(const char *(*)(const char *, uint16_t, UErrorCode *), ucnv_getAlias)
    if (fn) return fn(alias, n, err);
    unsupported(err);
    return NULL;
}

EXPORT const char *SYM(ucnv_getAvailableName)(int32_t n)
{
    FORWARD(const char *(*)(int32_t), ucnv_getAvailableName)
    return fn ? fn(n) : NULL;
}

EXPORT int8_t SYM(ucnv_getMaxCharSize)(const UConverter *cnv)
{
    FORWARD(int8_t(*)(const UConverter *), ucnv_getMaxCharSize)
    return fn ? fn(cnv) : 1;
}

EXPORT const char *SYM(ucnv_getStandardName)(const char *name, const char *standard, UErrorCode *err)
{
    FORWARD(const char *(*)(const char *, const char *, UErrorCode *), ucnv_getStandardName)
    if (fn) return fn(name, standard, err);
    unsupported(err);
    return NULL;
}

EXPORT void SYM(ucnv_fromUnicode)(UConverter *cnv, char **target, const char *targetLimit, const UChar **source,
                                  const UChar *sourceLimit, int32_t *offsets, UBool flush, UErrorCode *err)
{
    FORWARD(void (*)(UConverter *, char **, const char *, const UChar **, const UChar *, int32_t *, UBool,
                     UErrorCode *),
            ucnv_fromUnicode)
    if (fn) fn(cnv, target, targetLimit, source, sourceLimit, offsets, flush, err);
    else unsupported(err);
}

EXPORT void SYM(ucnv_toUnicode)(UConverter *cnv, UChar **target, const UChar *targetLimit, const char **source,
                                const char *sourceLimit, int32_t *offsets, UBool flush, UErrorCode *err)
{
    FORWARD(void (*)(UConverter *, UChar **, const UChar *, const char **, const char *, int32_t *, UBool,
                     UErrorCode *),
            ucnv_toUnicode)
    if (fn) fn(cnv, target, targetLimit, source, sourceLimit, offsets, flush, err);
    else unsupported(err);
}

EXPORT void SYM(ucnv_setFromUCallBack)(UConverter *cnv, UConverterFromUCallback action, const void *context,
                                       UConverterFromUCallback *oldAction, const void **oldContext,
                                       UErrorCode *err)
{
    FORWARD(void (*)(UConverter *, UConverterFromUCallback, const void *, UConverterFromUCallback *,
                     const void **, UErrorCode *),
            ucnv_setFromUCallBack)
    if (!fn) {
        unsupported(err);
        return;
    }
    /* Qt passes our UCNV_FROM_U_CALLBACK_SUBSTITUTE: hand ICU its own. */
    if (action == (UConverterFromUCallback)SYM(UCNV_FROM_U_CALLBACK_SUBSTITUTE)) {
        UConverterFromUCallback real = (UConverterFromUCallback)lookup("UCNV_FROM_U_CALLBACK_SUBSTITUTE");
        if (real) action = real;
    }
    fn(cnv, action, context, oldAction, oldContext, err);
}

EXPORT void SYM(ucnv_setSubstChars)(UConverter *cnv, const char *chars, int8_t len, UErrorCode *err)
{
    FORWARD(void (*)(UConverter *, const char *, int8_t, UErrorCode *), ucnv_setSubstChars)
    if (fn) fn(cnv, chars, len, err);
    else unsupported(err);
}

/* ------------------------------------------------------------ collation */

EXPORT UCollator *SYM(ucol_open)(const char *locale, UErrorCode *err)
{
    FORWARD(UCollator * (*)(const char *, UErrorCode *), ucol_open)
    if (fn) return fn(locale, err);
    unsupported(err);
    return NULL;
}

EXPORT void SYM(ucol_close)(UCollator *coll)
{
    FORWARD(void (*)(UCollator *), ucol_close)
    if (fn && coll) fn(coll);
}

EXPORT void SYM(ucol_setAttribute)(UCollator *coll, int attr, int value, UErrorCode *err)
{
    FORWARD(void (*)(UCollator *, int, int, UErrorCode *), ucol_setAttribute)
    if (fn) fn(coll, attr, value, err);
    else unsupported(err);
}

EXPORT int SYM(ucol_strcoll)(const UCollator *coll, const UChar *a, int32_t alen, const UChar *b, int32_t blen)
{
    FORWARD(int (*)(const UCollator *, const UChar *, int32_t, const UChar *, int32_t), ucol_strcoll)
    if (fn) return fn(coll, a, alen, b, blen);
    return 0; /* not reached: Qt only collates with a collator it opened */
}

EXPORT int32_t SYM(ucol_getSortKey)(const UCollator *coll, const UChar *src, int32_t len, uint8_t *result,
                                    int32_t resultLen)
{
    FORWARD(int32_t(*)(const UCollator *, const UChar *, int32_t, uint8_t *, int32_t), ucol_getSortKey)
    return fn ? fn(coll, src, len, result, resultLen) : 0;
}
