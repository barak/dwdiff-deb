/* Copyright (C) 2007 G.P. Halkes
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
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "definitions.h"
#include "util.h"
#include "option.h"

typedef struct {
	char *line;
	size_t allocated;
	size_t filled;
} Context;

typedef enum {
	PRINTING_CHANGED,
	PRINTING_AFTER_CONTEXT,
	PRE_BUFFERING,
	BUFFERING,
	/* The initial states are only used at the start of printing, to prevent
	   printing of the separator string (--) before the first block of output. */
	PRE_BUFFERING_INITIAL,
	BUFFERING_INITIAL
} PrintState;

#define INITIAL_BUFFER_SIZE 80

static Context *contextBuffers;
static PrintState state = PRE_BUFFERING_INITIAL;
static int bufferIndex;
static int afterContextLines;

/** Ensure that a buffer has sufficient unused storage remaining.
	@param buffer The context buffer to check.
	@param required The number of bytes required in the buffer.
*/
static void ensureBufferSpace(Context *buffer, size_t required) {
	if (buffer->allocated - buffer->filled >= required)
		return;

	while (buffer->allocated - buffer->filled < required) {
		buffer->allocated *= 2;
		/* Sanity check for overflow of allocated variable. Prevents infinite loops. */
		ASSERT(buffer->allocated > buffer->filled);
	}

	errno = 0;
	if ((buffer->line = realloc(buffer->line, buffer->allocated)) == NULL)
		outOfMemory();
}

/** Initialize the context buffers. */
void initContextBuffers(void) {
	int i;

	errno = 0;
	if ((contextBuffers = (Context *) malloc((option.contextLines + 1) * sizeof(Context))) == NULL)
		outOfMemory();

	contextBuffers[0].filled = 0;
	for (i = 0; i <= option.contextLines; i++) {
		contextBuffers[i].allocated = INITIAL_BUFFER_SIZE;
		errno = 0;
		if ((contextBuffers[i].line = malloc(INITIAL_BUFFER_SIZE)) == NULL)
			outOfMemory();
	}
}

/** Write a character, buffering if necessary.
	@param c The character to write.
	@param common Boolean to indicate whether the character belongs to both
		the old and the new file.
*/
void addchar(char c, bool common) {
	int i;

	if (!option.context) {
		putchar(c);
		return;
	}

	/* Check whether we have buffered output that we need to print because of
	   encountering a non-common character. */
	if (!common) {
		switch (state) {
			case PRINTING_CHANGED:
			case PRINTING_AFTER_CONTEXT:
				break;
			/* If we have entered a regular buffering state, all buffers are in use.
			   However, the set of buffers is used as a ring buffer so first print all
			   buffers after the current one, then print all buffers before the current. */
			case BUFFERING:
				fwrite("--\n", 1, 3, stdout);
			case BUFFERING_INITIAL:
				for (i = bufferIndex + 1; i <= option.contextLines; i++)
					fwrite(contextBuffers[i].line, 1, contextBuffers[i].filled, stdout);
				/* FALLTHROUGH */
			/* In pre-buffering state, only the first buffers are used. Once we start
			   wrapping around, we change state to a regular buffering state. */
			case PRE_BUFFERING:
			case PRE_BUFFERING_INITIAL:
				for (i = 0; i <= bufferIndex; i++)
					fwrite(contextBuffers[i].line, 1, contextBuffers[i].filled, stdout);
				break;
			default:
				PANIC();
		}
		state = PRINTING_CHANGED;
	}

	/* Decided whether to buffer or print the character. */
	switch (state) {
		case BUFFERING:
		case PRE_BUFFERING:
		case BUFFERING_INITIAL:
		case PRE_BUFFERING_INITIAL:
			ensureBufferSpace(&contextBuffers[bufferIndex], 1);
			contextBuffers[bufferIndex].line[contextBuffers[bufferIndex].filled++] = c;
			break;
		case PRINTING_AFTER_CONTEXT:
		case PRINTING_CHANGED:
			putchar(c);
			break;
		default:
			PANIC();
	}

	if (c == '\n') {
		switch (state) {
			case PRINTING_CHANGED:
				if (option.contextLines == 0) {
					state = PRE_BUFFERING;
					break;
				}
				state = PRINTING_AFTER_CONTEXT;
				afterContextLines = 0;
				break;
			case PRINTING_AFTER_CONTEXT:
				afterContextLines++;
				if (afterContextLines == option.contextLines) {
					state = PRE_BUFFERING;
					bufferIndex = 0;
				}
				break;
			case PRE_BUFFERING_INITIAL:
			case PRE_BUFFERING:
				bufferIndex++;
				/* Once we have filled all buffers, we change to a regular buffering state. */
				if (bufferIndex > option.contextLines) {
					state = state == PRE_BUFFERING ? BUFFERING : BUFFERING_INITIAL;
					bufferIndex = 0;
				}
				break;
			case BUFFERING_INITIAL:
			case BUFFERING:
				bufferIndex++;
				if (bufferIndex > option.contextLines)
					bufferIndex = 0;
				break;
			default:
				PANIC();
		}
		/* We just saw a newline, so we have either entered a buffering state,
		   or already were in one, and finished filling the old buffer. The new
		   buffer must be filled from the start. */
		contextBuffers[bufferIndex].filled = 0;
	}
}

/* Macro to ensure we use the same format and arguments for printing the line
   number information in all print statements. */
#define LINENUMBERS_FMT_ARGS "%*d:%-*d ", option.lineNumbers, oldLineNumber, option.lineNumbers, newLineNumber

/** Print line number information, buffering if necessary.
	@param oldLineNumber The line number in the old file.
	@param newLineNumber The line number in the new file.
*/
void printLineNumbers(int oldLineNumber, int newLineNumber) {
	int printed, allowed;

	if (!option.context) {
		printf(LINENUMBERS_FMT_ARGS);
		return;
	}

	switch (state) {
		case BUFFERING:
		case PRE_BUFFERING:
		case BUFFERING_INITIAL:
		case PRE_BUFFERING_INITIAL:
			allowed = contextBuffers[bufferIndex].allocated - contextBuffers[bufferIndex].filled;
			/* SUSv2 specification of snprintf does not handle zero sized buffers nicely :-(
			   Therefore, we have to ensure that there is at least room for one byte, but as
			   we will be printing 2 numbers and a colon and a space, we might as well ask for
			   4 bytes. */
			ensureBufferSpace(&contextBuffers[bufferIndex], 4);
			printed = snprintf(contextBuffers[bufferIndex].line + contextBuffers[bufferIndex].filled, allowed, LINENUMBERS_FMT_ARGS);
			/* If there was not enough room to hold all the bytes for the line numbers,
			   resize the buffer and try again. */
			if (printed > allowed) {
				ensureBufferSpace(&contextBuffers[bufferIndex], printed);
				allowed = contextBuffers[bufferIndex].allocated - contextBuffers[bufferIndex].filled;
				printed = snprintf(contextBuffers[bufferIndex].line + contextBuffers[bufferIndex].filled, allowed, LINENUMBERS_FMT_ARGS);
			}
			/* Sanity check: the number of characters printed should be at least 2
			   [probably 4, but I don't know all possible number systems]. */
			ASSERT(printed >= 2);
			contextBuffers[bufferIndex].filled += printed;
			break;
		case PRINTING_AFTER_CONTEXT:
		case PRINTING_CHANGED:
			printf(LINENUMBERS_FMT_ARGS);
			break;
		default:
			PANIC();
	}
}

/** Print a string, buffering if necessary.
	@param string The string to print.
	@param bytes The length of the string.

	The string should not contain newline characters.
*/
void writeString(const char *string, size_t bytes) {
	if (!option.context) {
		fwrite(string, 1, bytes, stdout);
		return;
	}

	switch (state) {
		case BUFFERING:
		case PRE_BUFFERING:
		case BUFFERING_INITIAL:
		case PRE_BUFFERING_INITIAL:
			ensureBufferSpace(&contextBuffers[bufferIndex], bytes);
			memcpy(contextBuffers[bufferIndex].line + contextBuffers[bufferIndex].filled, string, bytes);
			contextBuffers[bufferIndex].filled += bytes;
			break;
		case PRINTING_AFTER_CONTEXT:
		case PRINTING_CHANGED:
			fwrite(string, 1, bytes, stdout);
			break;
		default:
			PANIC();
	}
}
