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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "definitions.h"
#include "file.h"
#include "util.h"
#include "option.h"

#ifdef NO_MINUS_A
#include <ctype.h>
#endif

static int filePutcReal(File *file, int c);
static int fileWriteReal(File *file, const char *buffer, int bytes);
static int fileFlushReal(File *file);

static FileVtable vtableReal = { fileWriteReal, filePutcReal, fileFlushReal };

/** Wrap a file descriptor in a @a File struct. */
File *fileWrapFD(int fd, FileMode mode) {
	File *retval = malloc(sizeof(File));

	if (retval == NULL)
		return NULL;

	retval->fd = fd;
	retval->errNo = 0;
	retval->bufferFill = 0;
	retval->bufferIndex = 0;
	retval->eof = EOF_NO;
	retval->mode = mode;
	retval->vtable = &vtableReal;
	retval->context = NULL;
#ifdef NO_MINUS_A
	retval->escapeNonPrint = 0;
#endif
	return retval;
}

/** Open a file.
	@param name The name of the file to open.
	@param mode The mode of the file to open.

	@a mode must be one of @a FILE_READ or @a FILE_WRITE. @a FILE_WRITE can
	only be used if compiled with LEAVE_FILES.
*/
File *fileOpen(const char *name, FileMode mode) {
	int fd, openMode;

	switch (mode) {
		case FILE_READ:
			openMode = O_RDONLY;
			break;
#ifdef LEAVE_FILES
		case FILE_WRITE:
			openMode = O_RDWR | O_CREAT | O_TRUNC;
			break;
#endif
		default:
			PANIC();
	}

	if ((fd = open(name, openMode, 0600)) < 0)
		return NULL;

	return fileWrapFD(fd, mode);
}

/** Get the next character from a @a File. */
int fileGetc(File *file) {
	ASSERT(file->mode == FILE_READ);
	if (file->errNo != 0)
		return EOF;

	if (file->bufferIndex >= file->bufferFill) {
		ssize_t bytesRead = 0;

		if (file->eof != EOF_NO) {
			file->eof = EOF_HIT;
			return EOF;
		}

		/* Use while loop to allow interrupted reads */
		while (1) {
			ssize_t retval = read(file->fd, file->buffer + bytesRead, FILE_BUFFER_SIZE - bytesRead);
			if (retval == 0) {
				file->eof = EOF_COMING;
				break;
			} else if (retval < 0) {
				if (errno == EINTR)
					continue;
				file->errNo = errno;
				break;
			} else {
				bytesRead += retval;
				if (bytesRead == FILE_BUFFER_SIZE)
					break;
			}
		}
		if (file->errNo != 0)
			return EOF;
		if (bytesRead == 0) {
			file->eof = EOF_HIT;
			return EOF;
		}
		file->bufferFill = bytesRead;
		file->bufferIndex = 0;
	}

	return (unsigned char) file->buffer[file->bufferIndex++];
}

/** Push a character back into the buffer for a @a File. */
int fileUngetc(File *file, int c) {
	ASSERT(file->mode == FILE_READ);
	if (file->errNo != 0)
		return EOF;

	ASSERT(file->bufferIndex > 0);
	return file->buffer[--file->bufferIndex] = (unsigned char) c;
}

/** Flush the buffer associated with a @a File to disk. */
static int flushBuffer(File *file) {
	ssize_t bytesWritten = 0;

	if (file->mode == FILE_READ)
		return 0;

	if (file->errNo != 0)
		return EOF;

	if (file->bufferFill == 0)
		return 0;

	/* Use while loop to allow interrupted reads */
	while (1) {
		ssize_t retval = write(file->fd, file->buffer + bytesWritten, file->bufferFill - bytesWritten);
		if (retval == 0) {
			PANIC();
		} else if (retval < 0) {
			if (errno == EINTR)
				continue;
			file->errNo = errno;
			return EOF;
		} else {
			bytesWritten += retval;
			if (bytesWritten == file->bufferFill)
				break;
		}
	}

	file->bufferFill = 0;
	return bytesWritten;
}

/** Close a @a File. */
int fileClose(File *file) {
	int i;
	int retval = flushBuffer(file);

	if (close(file->fd) < 0 && retval == 0) {
		retval = errno;
	} else {
		retval = 0;
	}

	if (file->context != NULL) {
		for (i = 0; i < option.matchContext + 1; i++)
			free(file->context[i].line);
		free(file->context);
	}


	free(file);
	return retval;
}

static int fileFlushReal(File *file) {
	return flushBuffer(file) == EOF ? -1 : 0;

	/* The code below also fsync's the data to disk. However, this should not
	   be necessary to allow another program to read the entire file. It does
	   however slow the program down, so we skip it. */

/*	if (flushBuffer(file) == EOF)
		return -1;
	fsync(file->fd);
	return 0; */
}

/** Write a character to a @a File. */
static int filePutcReal(File *file, int c) {
	ASSERT(file->mode == FILE_WRITE);
	if (file->errNo != 0)
		return EOF;

	if (file->bufferFill >= FILE_BUFFER_SIZE
#ifdef NO_MINUS_A
		|| (file->escapeNonPrint && (!isprint(c) || c == '%') && c != '\n' && file->bufferFill >= FILE_BUFFER_SIZE - 2)
#endif
	) {
		if (flushBuffer(file) == EOF)
			return EOF;
	}

#ifdef NO_MINUS_A
	/* Escape non-printable characters for diff programs that do not have a -a
	   switch.*/
	if (file->escapeNonPrint) {
		if (c == '%') {
			file->buffer[file->bufferFill++] = '%';
		} else if (!isprint(c) && c != '\n') {
			file->buffer[file->bufferFill++] = '%';
			file->buffer[file->bufferFill++] = "0123456789ABCDEFGHIJKLMNOPQRSTUV"[(c >> 5) & 0x1F];
			file->buffer[file->bufferFill++] = "0123456789ABCDEFGHIJKLMNOPQRSTUV"[c & 0x1F];
			return 0;
		}
	}
#endif

	file->buffer[file->bufferFill++] = (unsigned char) c;
	return 0;
}

/** Write a buffer to a @a File. */
static int fileWriteReal(File *file, const char *buffer, int bytes) {
	ASSERT(file->mode == FILE_WRITE);
	if (file->errNo != 0)
		return EOF;

#ifdef NO_MINUS_A
	/* Make sure the output is correctly escaped. It is simplest to just do it
	   character by character. */
	if (file->escapeNonPrint) {
		int i;

		if (bytes == 0)
			return 0;

		for (i = 0; i < bytes - 1; i++)
			filePutcReal(file, *buffer++);
		return filePutcReal(file, *buffer);
	}
#endif

	while (1) {
		size_t minLength = FILE_BUFFER_SIZE - file->bufferFill < bytes ? FILE_BUFFER_SIZE - file->bufferFill : bytes;

		memcpy(file->buffer + file->bufferFill, buffer, minLength);
		file->bufferFill += minLength;
		bytes -= minLength;
		if (bytes == 0)
			return 0;

		if (flushBuffer(file) == EOF)
			return EOF;
		buffer += minLength;
	}
}

/** Write a string to a @a File. */
int filePuts(File *file, const char *string) {
	size_t length;

	ASSERT(file->mode == FILE_WRITE);
	if (file->errNo != 0)
		return EOF;

	length = strlen(string);

	return fileWrite(file, string, length);
}

/** Rewind a @a File, changing its mode. */
int fileRewind(File *file, FileMode mode) {
	ASSERT(mode == FILE_READ || (file->mode == FILE_WRITE && mode == FILE_WRITE));

	if (flushBuffer(file) != 0)
		return -1;

	if (lseek(file->fd, 0, SEEK_SET) < 0) {
		file->errNo = errno;
		return -1;
	}
	file->eof = EOF_NO;
	file->mode = mode;
	return 0;
}

/** Return if a @a File is in error state. */
int fileError(File *file) {
	return file->errNo != 0;
}

/** Return if a @a File is at the end of file. */
int fileEof(File *file) {
	return file->eof == EOF_HIT;
}

/** Get the errno for the failing action on a @a File. */
int fileGetErrno(File *file) {
	return file->errNo;
}

static int fileWriteContext(File *file, const char *buffer, int bytes) {
	Context *currentBuffer = file->context + file->currentWordIdx;
	ensureBufferSpace(currentBuffer, bytes);
	memcpy(currentBuffer->line + currentBuffer->filled, buffer, bytes);
	currentBuffer->filled += bytes;
	return 0;
}

#define FLAG_CONTEXT_PADDING (1<<0)
#define NUM_BUFFER_SIZE (sizeof(int) * 2)
static int filePutcContext(File *file, int c) {
	int i;
	/* Add character to buffer if it is not the word separator. */
	if (c != '\n') {
		char _c = c;
		return fileWriteContext(file, &_c, 1);
	}

	for (i = 0; i < option.matchContext + 1; i ++) {
		char numBuffer[NUM_BUFFER_SIZE];
		int numBufferIdx = 0;
		bool printed = false;
		int value, j;

		Context *currentBuffer = file->context + ((file->currentWordIdx + i + 1) % (option.matchContext + 1));

		if (currentBuffer->flags & FLAG_CONTEXT_PADDING) {
			/* Use '0' + 16 as special marker, such that the range of characters
			   is continuous */
			numBuffer[numBufferIdx++] = '0' + 16;
		} else {
			/* We don't use snprintf or similar as the number formatting is locale
			   dependant. Also, we can use a simple hex encoding, which is more
			   efficient. */
			for (j = sizeof(int) * 2 - 1; j >= 0; j--) {
				if ((value = currentBuffer->filled >> (j * 4) & 0xF) || printed) {
					printed = true;
					numBuffer[numBufferIdx++] = value + '0';
				}
			}
			if (!printed)
				numBuffer[numBufferIdx++] = '0';
		}

		fileWriteReal(file,  numBuffer, numBufferIdx);
		filePutcReal(file, ',');
	}

	for (i = 0; i < option.matchContext + 1; i ++) {
		Context *currentBuffer = file->context + ((file->currentWordIdx + i + 1) % (option.matchContext + 1));
		fileWriteReal(file, currentBuffer->line, currentBuffer->filled);
	}

	file->currentWordIdx++;
	file->currentWordIdx %= option.matchContext + 1;
	file->context[file->currentWordIdx].filled = 0;
	file->context[file->currentWordIdx].flags = 0;

	return filePutcReal(file, '\n');
}

static int fileFlushContext(File *file) {
	int i;
	for (i = 0; i < option.matchContext ; i++) {
		file->context[file->currentWordIdx].flags = FLAG_CONTEXT_PADDING;
		filePutcContext(file, '\n');
	}

	return fileFlushReal(file);
}

static FileVtable vtableContext = { fileWriteContext, filePutcContext, fileFlushContext };

/** Initialise this @a File to keep match context. */
void fileSetContext(File *file) {
	int i;

	errno = 0;
	if ((file->context = (Context *) malloc((option.matchContext + 1) * sizeof(Context))) == NULL)
		outOfMemory();

	for (i = 0; i < option.matchContext + 1; i++) {
		initBuffer(file->context + i, INITIAL_BUFFER_SIZE);
		file->context[i].flags = FLAG_CONTEXT_PADDING;
	}
	file->context[0].flags = 0;
	file->currentWordIdx = 0;

	file->vtable = &vtableContext;
}

void fileClearEof(File *file) {
	file->eof = EOF_COMING;
}
