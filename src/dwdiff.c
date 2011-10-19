/* Copyright (C) 2006-2011 G.P. Halkes
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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "definitions.h"
#include "option.h"
#include "util.h"
#include "stream.h"
#include "unicode.h"
#include "dispatch.h"

typedef enum {
	NONE,
	WHITESPACE,
	WORD
} MatchState;

int differences = 0;
Statistics statistics;
bool UTF8Mode;

Context whitespaceBuffer;
bool tokenWritten;

/** Contains the last read character. This is a global variable, because many
    routines use the same data and would require constant passing of either
    @a charData or a pointer to @a charData. Using a global-variable makes more
    sense in this case. */
CharData charData;

/** Write an escaped character.
	@param file The file to write to.
	@param ch The character to write.
	@return A boolean that indicates whether the character should still be written.
*/
static bool writeEscape(File *file, int ch) {
	/* ch is either a single byte, or a UTF-16 char. */
	if (option.transliterate) {
		if (ch == '\n') {
			fileWrite(file, "\\n", 2);
			return false;
		} else if (ch == '\\') {
			fileWrite(file, "\\\\", 2);
			return false;
		}
	}
	return true;
}

static void writeEndOfToken(InputFile *file) {
	sputc(file->tokens->stream, '\n');
	sputc(file->diffTokens->stream, '\n');
	tokenWritten = true;
}

/*===============================================================*/
/* Single character (SC) versions of the classification and storage routines.
   Descriptions can be found in the definition of the DispatchTable struct. */

bool getNextCharSC(Stream *file) {
	return (charData.singleChar = sgetc(file)) != EOF;
}

bool isWhitespaceSC(void) {
	return TEST_BIT(option.whitespace, charData.singleChar);
}

bool isDelimiterSC(void) {
	return TEST_BIT(option.delimiters, charData.singleChar);
}

void writeTokenCharSC(InputFile *file) {
	int diffChar = option.ignoreCase ? tolower(charData.singleChar) : charData.singleChar;

	if (writeEscape(file->diffTokens->stream->data.file, diffChar))
		filePutc(file->diffTokens->stream->data.file, diffChar);
	if (writeEscape(file->tokens->stream->data.file, charData.singleChar))
		filePutc(file->tokens->stream->data.file, charData.singleChar);
}

void writeWhitespaceCharSC(InputFile *file) {
	/* Don't want to change interface (yet), so prevent warning. */
	(void) file;

	ensureBufferSpace(&whitespaceBuffer, 2);
	if (charData.singleChar == 0 || charData.singleChar == '\\')
		whitespaceBuffer.line[whitespaceBuffer.filled++] = '\\';

	whitespaceBuffer.line[whitespaceBuffer.filled++] = charData.singleChar;
}

void writeWhitespaceDelimiterSC(InputFile *file) {
	sputc(file->whitespace->stream, 0);
}

#ifdef USE_UNICODE
/*===============================================================*/
/* UTF-8 versions of the classification and storage routines.
   Descriptions can be found in the definition of the DispatchTable struct. */

bool getNextCharUTF8(Stream *file) {
	bool retval = getCluster(file, &charData.UTF8Char.original);
	if (retval)
		decomposeChar(&charData);
	return retval;
}

bool isWhitespaceUTF8(void) {
	if (option.whitespaceSet)
		return bsearch(&charData.UTF8Char.converted, option.whitespaceList.list, option.whitespaceList.used,
			sizeof(UTF16Buffer), (int (*)(const void *, const void *)) compareUTF16Buffer) != NULL;
	else
		return isUTF16Whitespace(&charData.UTF8Char.converted);
	return false;
}

bool isDelimiterUTF8(void) {
	return bsearch(&charData.UTF8Char.converted, option.delimiterList.list, option.delimiterList.used,
		sizeof(UTF16Buffer), (int (*)(const void *, const void *)) compareUTF16Buffer) != NULL || (option.punctuationMask && isUTF16Punct(&charData.UTF8Char.converted));
}

void writeTokenCharUTF8(InputFile *file) {
	UTF16Buffer *writeBuffer;
	int32_t i;

	if (option.ignoreCase) {
		casefoldChar(&charData);
		writeBuffer = &charData.UTF8Char.casefolded;
	} else {
		writeBuffer = &charData.UTF8Char.converted;
	}

	/* Write the "original" characters. Note that high and low surrogates
	   and other invalid characters have been converted to REPLACEMENT
	   CHARACTER. */
	for (i = 0; i < charData.UTF8Char.original.length; i++)
		if (writeEscape(file->tokens->stream->data.file, charData.UTF8Char.original.data[i]))
			putuc(file->tokens->stream, charData.UTF8Char.original.data[i]);

	for (i = 0; i < writeBuffer->length; i++)
		if (writeEscape(file->diffTokens->stream->data.file, writeBuffer->data[i]))
			putuc(file->diffTokens->stream, writeBuffer->data[i]);
}

void writeWhitespaceCharUTF8(InputFile *file) {
	UChar32 highSurrogate = 0;
	int32_t i;

	/* Don't want to change interface (yet), so prevent warning. */
	(void) file;

	for (i = 0; i < charData.UTF8Char.original.length; i++) {
		char utf8char[4];
		size_t bytes;

		if ((bytes = filteredConvertToUTF8(charData.UTF8Char.original.data[i], utf8char, &highSurrogate)) == 0)
			continue;

		/* Make sure we have at least room for the char and backshlash. Not optimal, but easy. */
		ensureBufferSpace(&whitespaceBuffer, bytes + 1);

		if (charData.UTF8Char.original.data[i] == 0 || charData.UTF8Char.original.data[i] == '\\')
			whitespaceBuffer.line[whitespaceBuffer.filled++] = '\\';

		memcpy(whitespaceBuffer.line + whitespaceBuffer.filled, utf8char, bytes);
		whitespaceBuffer.filled += bytes;
	}
}

void writeWhitespaceDelimiterUTF8(InputFile *file) {
	putuc(file->whitespace->stream, 0);
}

/*===============================================================*/
#endif

/** Handle the end of a whitespace sequence.
	@param file The @a InputFile from which the whitespace came.

	If the paragraph delimiter mode is selected, this will check whether such
	a delimiter should be written and break up the whitespace if necessary.
*/
void handleWhitespaceEnd(InputFile *file) {
	if (option.paraDelim) {
		size_t i, firstNewline = 0;
		bool firstNewlineFound = false;

		for (i = 0; i < whitespaceBuffer.filled; i++) {
			if (whitespaceBuffer.line[i] != '\n')
				continue;
			if (!firstNewlineFound) {
				firstNewlineFound = true;
				firstNewline = i;
				continue;
			}
			break;
		}

		/* The whitespace preceeding any text must be treated differently, as a
		   newline there results in an empty line. This is different from other
		   whitespace where two newlines are required for an empty line. */
		if (firstNewlineFound && !tokenWritten) {
			/* Write everything upto but excluding the first newline */
			swrite(file->whitespace->stream, whitespaceBuffer.line, firstNewline);
			writeWhitespaceDelimiter(file);

			swrite(file->whitespace->stream, whitespaceBuffer.line + firstNewline, whitespaceBuffer.filled - firstNewline);
			writeWhitespaceDelimiter(file);
			writeEndOfToken(file);
			whitespaceBuffer.filled = 0;
			return;
		}

		if (i != whitespaceBuffer.filled) {
			/* Write everything upto and including the first newline */
			swrite(file->whitespace->stream, whitespaceBuffer.line, firstNewline + 1);
			writeWhitespaceDelimiter(file);

			swrite(file->whitespace->stream, whitespaceBuffer.line + firstNewline + 1, whitespaceBuffer.filled - (firstNewline + 1));
			writeWhitespaceDelimiter(file);
			writeEndOfToken(file);
			whitespaceBuffer.filled = 0;
			return;
		}
		/* Fall through to default case */
	}

	swrite(file->whitespace->stream, whitespaceBuffer.line, whitespaceBuffer.filled);
	writeWhitespaceDelimiter(file);
	whitespaceBuffer.filled = 0;
}

enum {
	CAT_OTHER,
	CAT_DELIMITER,
	CAT_WHITESPACE
};

/** Read a file and separate whitespace from the rest.
    @param file The @a InputFile to read.
    @return The number of "words" in @a file.

    The separated parts of @a file are put into temporary files. The temporary
    files' information is stored in the @a InputFile structure.

    For runs in which the newline character is not included in the whitespace list,
    the newline character is transliterated into the first character of the
    whitespace list. Just before writing the output the characters are again
    transliterated to restore the original text.
*/
static int readFile(InputFile *file) {
	MatchState state = NONE;
	int wordCount = 0;
	int category;

	if (file->name != NULL && (file->input = newFileStream(fileOpen(file->name, FILE_READ))) == NULL)
		fatal(_("Can't open file %s: %s\n"), file->name, strerror(errno));

	if ((file->tokens = tempFile(-1)) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));

	if ((file->diffTokens = tempFile(-1)) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));
	if (option.matchContext)
		fileSetContext(file->diffTokens->stream->data.file);

#ifdef NO_MINUS_A
	file->diffTokens->stream->data.file->escapeNonPrint = 1;
#endif

	if ((file->whitespace = tempFile(-1)) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));

	tokenWritten = false;

	while (getNextChar(file->input)) {
		/* Need to make sure we test delimiters first, because in UTF8Mode
		   we can't simply remove any overlapping delimiters from the
		   whitespace list. */
		category = isDelimiter() ? CAT_DELIMITER : (isWhitespace() ? CAT_WHITESPACE : CAT_OTHER);
		switch (state) {
			case NONE:
				if (category == CAT_WHITESPACE) {
					writeWhitespaceChar(file);
					state = WHITESPACE;
					break;
				}
				handleWhitespaceEnd(file);
				writeTokenChar(file);
				if (category == CAT_DELIMITER) {
					writeEndOfToken(file);
					state = WHITESPACE;
				} else {
					state = WORD;
				}
				break;
			case WORD:
				if (category == CAT_WHITESPACE) {
					/* Found the end of a "word". Go to whitespace mode. */
					wordCount++;
					writeEndOfToken(file);
					writeWhitespaceChar(file);
					state = WHITESPACE;
				} else if (category == CAT_DELIMITER) {
					/* Found a delimiter. Finish the current word, add a zero length whitespace
					   to the whitespace file, add the delimiter as a word, and go into
					   whitespace mode. */
					wordCount += 2;
					writeEndOfToken(file);
					writeTokenChar(file);
					writeEndOfToken(file);
					handleWhitespaceEnd(file);
					state = WHITESPACE;
				} else {
					writeTokenChar(file);
				}
				break;
			case WHITESPACE:
				if (category == CAT_WHITESPACE) {
					writeWhitespaceChar(file);
				} else if (category == CAT_DELIMITER) {
					/* Found a delimiter. Finish the current whitespace, and add the delimiter
					   as a word. Then start new whitespace. */
					wordCount++;
					writeTokenChar(file);
					writeEndOfToken(file);
					handleWhitespaceEnd(file);
				} else {
					/* Found the start of a word. Finish the whitespace, and go into
					   word mode. */
					handleWhitespaceEnd(file);
					writeTokenChar(file);
					state = WORD;
				}
				break;
			default:
				PANIC();
		}
	}

	if (sferror(file->input))
		fatal(_("Error reading file %s: %s\n"), file->name, strerror(sgeterrno(file->input)));


	/* Make sure there is whitespace to end the output with. This may
	   be zero-length. */
	handleWhitespaceEnd(file);

	/* Make sure the word is terminated, or otherwise diff will add
	   extra output. */
	if (state == WORD) {
		wordCount++;
		writeEndOfToken(file);
	}
	/* Close the input, and make sure the output is in the filesystem.
	   Then rewind so we can start reading from the start. */
	sfclose(file->input);

	sfflush(file->whitespace->stream);
	if (sferror(file->whitespace->stream))
		fatal(_("Error writing to temporary file %s: %s\n"), file->name, strerror(sgeterrno(file->whitespace->stream)));
	srewind(file->whitespace->stream);

	sfflush(file->tokens->stream);
	if (sferror(file->tokens->stream))
		fatal(_("Error writing to temporary file %s: %s\n"), file->name, strerror(sgeterrno(file->tokens->stream)));
	srewind(file->tokens->stream);

	sfflush(file->diffTokens->stream);
	if (sferror(file->diffTokens->stream))
		fatal(_("Error writing to temporary file %s: %s\n"), file->name, strerror(sgeterrno(file->diffTokens->stream)));
	sfclose(file->diffTokens->stream);

	return wordCount;
}

DEF_TABLE(SC)
ONLY_UNICODE(DEF_TABLE(UTF8))
DispatchTable *dispatch = &SCDispatch;

typedef enum {
	FIRST_HEADER,
	FIRST,
	OLD,
	NEW,
	COMMON,
	HEADER,
	LINE_COUNTS
} DiffInputMode;


void splitDiffInput(void) {
	Stream *input;
	TempFile *oldFile, *newFile;
	DiffInputMode mode = FIRST_HEADER;

	if (option.oldFile.name == NULL) {
		input = option.oldFile.input;
	} else {
		if ((input = newFileStream(fileOpen(option.oldFile.name, FILE_READ))) == NULL)
			fatal(_("Can't open file %s: %s\n"), option.oldFile.name, strerror(errno));
	}

	oldFile = tempFile(-1);
	newFile = tempFile(-1);

	while (getNextCharSC(input)) {
		switch (mode) {
			case FIRST_HEADER:
				sputc(oldFile->stream, charData.singleChar);
				sputc(newFile->stream, charData.singleChar);
				mode = charData.singleChar != '@' ? HEADER : LINE_COUNTS;
				break;
			case FIRST:
				if (charData.singleChar == '+') {
					sputc(newFile->stream, ' ');
					mode = NEW;
				} else if (charData.singleChar == '-') {
					sputc(oldFile->stream, ' ');
					mode = OLD;
				} else if (charData.singleChar == ' ') {
					sputc(oldFile->stream, ' ');
					sputc(newFile->stream, ' ');
					mode = COMMON;
				} else if (charData.singleChar == '@') {
					sputc(oldFile->stream, charData.singleChar);
					sputc(newFile->stream, charData.singleChar);
					mode = LINE_COUNTS;
				} else {
					sputc(oldFile->stream, charData.singleChar);
					sputc(newFile->stream, charData.singleChar);
					mode = HEADER;
				}
				break;
			case OLD:
				sputc(oldFile->stream, charData.singleChar);
				if (charData.singleChar == '\n')
					mode = FIRST;
				break;
			case NEW:
				sputc(newFile->stream, charData.singleChar);
				if (charData.singleChar == '\n')
					mode = FIRST;
				break;
			case COMMON:
				sputc(oldFile->stream, charData.singleChar);
				sputc(newFile->stream, charData.singleChar);
				if (charData.singleChar == '\n')
					mode = FIRST;
				break;

			case HEADER:
				sputc(oldFile->stream, charData.singleChar);
				sputc(newFile->stream, charData.singleChar);
				if (charData.singleChar == '\n')
					mode = FIRST_HEADER;
				break;

			case LINE_COUNTS:
				sputc(oldFile->stream, charData.singleChar);
				sputc(newFile->stream, charData.singleChar);
				if (charData.singleChar == '\n')
					mode = FIRST;
				break;
			default:
				PANIC();
		}
	}
	sfclose(oldFile->stream);
	sfclose(newFile->stream);
	option.oldFile.name = oldFile->name;
	option.newFile.name = newFile->name;
}

/** Main. */
int main(int argc, char *argv[]) {
#if defined(USE_GETTEXT) || defined(USE_UNICODE)
	setlocale(LC_ALL, "");
#endif

#ifdef USE_GETTEXT
	bindtextdomain("dwdiff", LOCALEDIR);
	textdomain("dwdiff");
#endif

#ifdef USE_UNICODE
	/* Check whether the input is UTF-8 encoded. */
#ifdef USE_NL_LANGINFO
	if (strcmp(nl_langinfo(CODESET), "UTF-8") == 0)
		UTF8Mode = true;
#else
	{
		char *lc_ctype, *location;
		int i;

		if ((lc_ctype = setlocale(LC_CTYPE, NULL)) == NULL)
			goto end_utf8_check;
		if ((lc_ctype = strdupA(lc_ctype)) == NULL)
			outOfMemory();

		/* Use ASCII specific tolower function here, because it is not certain
		   that using tolower with the user locale will give correct results. */
		for (i = strlen(lc_ctype) - 1; i >= 0; i--)
			lc_ctype[i] = ASCIItolower(lc_ctype[i]);

		if ((location = strstr(lc_ctype, ".utf8")) != NULL) {
			if (location[5] == 0 || location[5] == '@')
				UTF8Mode = true;
		} else if ((location = strstr(lc_ctype, ".utf-8")) != NULL) {
			if (location[6] == 0 || location[6] == '@')
				UTF8Mode = true;
		}
	}
  end_utf8_check:
#endif // USE_NL_LANGINFO
#endif // USE_UNICODE

#ifdef USE_UNICODE
	if (UTF8Mode) {
		UTF16BufferInit(&charData.UTF8Char.original);
		UTF16BufferInit(&charData.UTF8Char.converted);
		UTF16BufferInit(&charData.UTF8Char.casefolded);
		dispatch = &UTF8Dispatch;
	}
#endif

	parseCmdLine(argc, argv);

	if (option.diffInput)
		splitDiffInput();

	initBuffer(&whitespaceBuffer, INITIAL_WORD_BUFFER_SIZE);

	statistics.oldTotal = readFile(&option.oldFile);
	statistics.newTotal = readFile(&option.newFile);

	/* Whitespace buffer won't be used after this. */
	free(whitespaceBuffer.line);

	doDiff();

	if (option.statistics) {
		int common = statistics.oldTotal - statistics.deleted - statistics.oldChanged;
		if (statistics.oldTotal == 0) {
			fprintf(stderr, _("old: 0 words\n"));
		} else {
			fprintf(stderr, _("old: %d words  %d %d%% common  %d %d%% deleted  %d %d%% changed\n"), statistics.oldTotal,
				common, (common * 100)/statistics.oldTotal,
				statistics.deleted, (statistics.deleted * 100) / statistics.oldTotal,
				statistics.oldChanged, (statistics.oldChanged * 100) / statistics.oldTotal);
		}
		common = statistics.newTotal - statistics.added - statistics.newChanged;
		if (statistics.newTotal == 0) {
			fprintf(stderr, _("new: 0 words\n"));
		} else {
			fprintf(stderr, _("new: %d words  %d %d%% common  %d %d%% inserted  %d %d%% changed\n"), statistics.newTotal,
				common, (common * 100)/statistics.newTotal,
				statistics.added, (statistics.added * 100) / statistics.newTotal,
				statistics.newChanged, (statistics.newChanged * 100) / statistics.newTotal);
		}
	}

	fflush(option.output);

	return differences;
}