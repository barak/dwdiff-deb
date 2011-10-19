/* Copyright (C) 2006-2010 G.P. Halkes
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3, as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DEFINITIONS_H
#define DEFINITIONS_H
#include <stdio.h>
#include <limits.h>

#if defined(USE_GETTEXT) || defined(USE_UNICODE)
#	include <locale.h>
#endif

#if defined(USE_UNICODE) && defined(USE_NL_LANGINFO)
#	include <langinfo.h>
#endif

#ifdef USE_GETTEXT
#	include <libintl.h>
#	define _(String) gettext(String)
#else
#	define _(String) (String)
#endif
#define N_(String) String

#ifdef USE_UNICODE
#define ONLY_UNICODE(_x) _x
#define SWITCH_UNICODE(a, b) a
#else
#define ONLY_UNICODE(_x)
#define SWITCH_UNICODE(a, b) b
#endif

/*==== Misc definitions ====*/
/* Define a bool type if not already defined (C++ and C99 do)*/
#if !(defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 19990601L))
/*@-incondefs@*/
typedef enum {false, true} bool;
/*@+incondefs@*/
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 19990601L
#include <stdbool.h>
#endif

#ifndef NO_STRDUP
#define strdupA strdup
#endif

#define VERSION_STRING "1.8.2"

/*==== Configuration definitions ====*/
#ifndef DIFF
#	define DIFF "diff"
#endif

/* Solaris diff does not provide -a, but does distinguish between binary and
   text files. Therefore, we need to take special measures to allow diffs on
   all kinds of semi-binary files. */
#ifdef NO_MINUS_A
#	define MINUS_A ""
#else
#	define MINUS_A " -a"
#endif

#define DIFF_COMMAND DIFF MINUS_A

typedef union CharData CharData;

#include "stream.h"
#include "tempfile.h"
#include "unicode.h"
#include "buffer.h"

typedef struct {
	const char *name;
	Stream *input;
	TempFile *diffTokens;
	TempFile *tokens;
	TempFile *whitespace;
	int lastPrinted;
	Context whitespaceBuffer;
	bool whitespaceBufferUsed;
} InputFile;

typedef struct {
	int added,
		deleted,
		oldChanged,
		newChanged,
		oldTotal,
		newTotal;
} Statistics;
extern Statistics statistics;
extern int differences;

#define SET_BIT(x, b) do { (x)[(b)>>3] |= 1 << ((b) & 0x7); } while (0);
#define RESET_BIT(x, b) do { (x)[(b)>>3] &= ~(1 << ((b) & 0x7)); } while (0);
#define TEST_BIT(x, b) ((x)[(b)>>3] & (1 << ((b) & 0x7)))

union CharData {
	int singleChar;

#ifdef USE_UNICODE
	struct {
		UTF16Buffer original; /* UTF-16 encoded original input after clean-up */
		UTF16Buffer converted; /* UTF-16 encoded string for comparison purposes. */
		UTF16Buffer casefolded; /* UTF-16 encoded string for comparison purposes, case folded version. */
	} UTF8Char;
#endif
};

#ifdef USE_UNICODE
extern bool UTF8Mode;
#endif
extern CharData charData;

void doDiff(void);

#endif
