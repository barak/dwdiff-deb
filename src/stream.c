/* Copyright (C) 2008 G.P. Halkes
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

#include <stdlib.h>
#include "stream.h"

/** Initialise the shared parts of a @a Stream structure.
    @param stream The @a Stream to initialise.
*/
static void initStreamDefault(Stream *stream) {
	(void) stream;
#ifdef USE_UNICODE
	stream->bufferedChar = -1;
	stream->highSurrogate = 0;
	stream->lastClusterCategory = 0;
	stream->nextChar = -1;
#endif
}

/** @a getChar for @a FILE based streams. */
static int getCharFile(Stream *stream) {
	return fileGetc(stream->data.file);
}

/** @a ungetChar for @a FILE based streams. */
static int ungetCharFile(Stream *stream, int c) {
	return fileUngetc(stream->data.file, c);
}

VTable FileVtable = { getCharFile, ungetCharFile };

/** Create a new @a FILE based stream.
    @param file The @a FILE to wrap.
    @return a new @a Stream wrapping the supplied @a FILE object.
*/
Stream *newFileStream(File *file) {
	Stream *retval;

	if (file == NULL)
		return NULL;

	if ((retval = malloc(sizeof(Stream))) == NULL)
		outOfMemory();

	retval->vtable = &FileVtable;
	retval->data.file = file;

	initStreamDefault(retval);

	return retval;
}

/** @a getChar for string based streams. */
static int getCharString(Stream *stream) {
	return stream->data.string.index >= stream->data.string.length ? EOF : (unsigned char) stream->data.string.string[stream->data.string.index++];
}

/** @a ungetChar for string based streams. */
static int ungetCharString(Stream *stream, int c) {
	if (stream->data.string.index > 0) {
		ASSERT(stream->data.string.string[--stream->data.string.index] == c);
		return c;
	} else {
		return EOF;
	}
}

VTable StringVtable = { getCharString, ungetCharString };

/** Create a new string based stream.
    @param string The string to wrap.
    @return a new @a Stream wrapping the supplied string.
*/
Stream *newStringStream(const char *string, size_t length) {
	Stream *retval;

	if ((retval = malloc(sizeof(Stream))) == NULL)
		outOfMemory();

	retval->vtable = &StringVtable;
	retval->data.string.string = string;
	retval->data.string.index = 0;
	retval->data.string.length = length;

	initStreamDefault(retval);

	return retval;
}

