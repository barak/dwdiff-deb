/* Copyright (C) 2008-2010 G.P. Halkes
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

#ifndef UNICODE_H
#define UNICODE_H

#ifdef USE_UNICODE
#include "definitions.h"

#define UEOF -1
#define UEOVERLONG -2
#define UEINVALID -3
#define UETOOLARGE -4

/* Officially there should be no break between CR and LF. However, on the
command line one would commonly write \n to match a line end. On Unix systems
it is customary to simply use LF as the line break, so not allowing a break
between CR and LF would break expected behaviour if the user were to write
\r\n on the command line. Conversely, if the user writes \n on the command
line, and the text contains CRLF, the user would expect the \n to match the
line feed.

The problem here is that there is hardly a good solution, that will satisfy
all cases in an intuitive manner. Therefore, I'm going to stick with the
Unix EOL convention instead of using the official Unicode way. Users can run
dos2unix to convert their documents if need be. */
#define CRLF_GRAPHEME_CLUSTER_BREAK 0

#include <unicode/uchar.h>
#include <unicode/unorm.h>

typedef struct {
	UChar *data;
	int32_t allocated;
	int32_t length;
} UTF16Buffer;

typedef struct {
	UTF16Buffer *list;
	int allocated;
	int used;
} CharList;

bool getCluster(Stream *stream, UTF16Buffer *buffer);
bool getBackspaceCluster(Stream *stream, UTF16Buffer *buffer);
int convertToUTF8(UChar32 c, char *buffer);
int filteredConvertToUTF8(UChar32 c, char *buffer, UChar32 *highSurrogate);
int putuc(Stream *stream, UChar32 c);

void decomposeChar(CharData *c);
void casefoldChar(CharData *c);

void UTF16BufferInit(UTF16Buffer *buffer);
void freeUTF16Buffer(UTF16Buffer *buffer);
int compareUTF16Buffer(const UTF16Buffer *a, const UTF16Buffer *b);
bool isUTF16Punct(UTF16Buffer *buffer);
bool isUTF16Whitespace(UTF16Buffer *buffer);
#endif
#endif
