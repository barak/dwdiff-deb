/* Copyright (C) 2006-2008 G.P. Halkes
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
}


/** Contains the last read character. This is a global variable, because many
    routines use the same data and would require constant passing of either
    @a c or a pointer to @a c. Using a file-scope global-variable makes more
    sense in this case. */
static CharData c;

/*===============================================================*/
/* Single character (SC) versions of the classification and storage routines.
   Descriptions can be found in the definition of the DispatchTable struct. */

bool getNextCharSC(Stream *file) {
	return (c.singleChar = sgetc(file)) != EOF;
}

bool isWhitespaceSC(void) {
	return TEST_BIT(option.whitespace, c.singleChar);
}

bool isDelimiterSC(void) {
	return TEST_BIT(option.delimiters, c.singleChar);
}

void writeTokenCharSC(InputFile *file) {
	int diffChar = option.ignoreCase ? tolower(c.singleChar) : c.singleChar;

	if (writeEscape(file->diffTokens->stream->data.file, diffChar))
		filePutc(file->diffTokens->stream->data.file, diffChar);
	if (writeEscape(file->tokens->stream->data.file, c.singleChar))
		filePutc(file->tokens->stream->data.file, c.singleChar);
}

void writeWhitespaceCharSC(InputFile *file) {
	if (c.singleChar == 0 || c.singleChar == '\\')
		sputc(file->whitespace->stream, '\\');

	sputc(file->whitespace->stream, c.singleChar);
}

void writeWhitespaceDelimiterSC(InputFile *file) {
	sputc(file->whitespace->stream, 0);
}

#ifdef USE_UNICODE
/*===============================================================*/
/* UTF-8 versions of the classification and storage routines.
   Descriptions can be found in the definition of the DispatchTable struct. */

bool getNextCharUTF8(Stream *file) {
	bool retval = getCluster(file, &c.UTF8Char.original);
	if (retval)
		decomposeChar(&c);
	return retval;
}

bool isWhitespaceUTF8(void) {
	if (option.whitespaceSet)
		return bsearch(&c.UTF8Char.converted, option.whitespaceList.list, option.whitespaceList.used,
			sizeof(UTF16Buffer), (int (*)(const void *, const void *)) compareUTF16Buffer) != NULL;
	else
		return isUTF16Whitespace(&c.UTF8Char.converted);
	return false;
}

bool isDelimiterUTF8(void) {
	return bsearch(&c.UTF8Char.converted, option.delimiterList.list, option.delimiterList.used,
		sizeof(UTF16Buffer), (int (*)(const void *, const void *)) compareUTF16Buffer) != NULL || (option.punctuationMask && isUTF16Punct(&c.UTF8Char.converted));
}

void writeTokenCharUTF8(InputFile *file) {
	UTF16Buffer *writeBuffer;
	int32_t i;

	if (option.ignoreCase) {
		casefoldChar(&c);
		writeBuffer = &c.UTF8Char.casefolded;
	} else {
		writeBuffer = &c.UTF8Char.converted;
	}

	/* Write the "original" characters. Note that high and low surrogates
	   and other invalid characters have been converted to REPLACEMENT
	   CHARACTER. */
	for (i = 0; i < c.UTF8Char.original.length; i++)
		if (writeEscape(file->tokens->stream->data.file, c.UTF8Char.original.data[i]))
			putuc(file->tokens->stream, c.UTF8Char.original.data[i]);

	for (i = 0; i < writeBuffer->length; i++)
		if (writeEscape(file->diffTokens->stream->data.file, writeBuffer->data[i]))
			putuc(file->diffTokens->stream, writeBuffer->data[i]);
}

void writeWhitespaceCharUTF8(InputFile *file) {
	int32_t i;
	for (i = 0; i < c.UTF8Char.original.length; i++) {
		if (c.UTF8Char.original.data[i] == 0 || c.UTF8Char.original.data[i] == '\\')
			sputc(file->whitespace->stream, '\\');
		putuc(file->whitespace->stream, c.UTF8Char.original.data[i]);
	}
}

void writeWhitespaceDelimiterUTF8(InputFile *file) {
	putuc(file->whitespace->stream, 0);
}

/*===============================================================*/
#endif

#define CAT_OTHER 0
#define CAT_DELIMITER 1
#define CAT_WHITESPACE 2

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

	if ((file->tokens = tempFile()) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));

	if ((file->diffTokens = tempFile()) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));
#ifdef NO_MINUS_A
	file->diffTokens->stream->data.file->escapeNonPrint = 1;
#endif

	if ((file->whitespace = tempFile()) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));

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
				writeWhitespaceDelimiter(file);
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
					writeWhitespaceDelimiter(file);
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
					writeWhitespaceDelimiter(file);
				} else {
					/* Found the start of a word. Finish the whitespace, and go into
					   word mode. */
					writeTokenChar(file);
					writeWhitespaceDelimiter(file);
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
	writeWhitespaceDelimiter(file);

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
		UTF16BufferInit(&c.UTF8Char.original);
		UTF16BufferInit(&c.UTF8Char.converted);
		UTF16BufferInit(&c.UTF8Char.casefolded);
		dispatch = &UTF8Dispatch;
	}
#endif

	parseCmdLine(argc, argv);

	statistics.oldTotal = readFile(&option.oldFile);
	statistics.newTotal = readFile(&option.newFile);

	doDiff();

	if (option.statistics) {
		int common = statistics.oldTotal - statistics.deleted - statistics.oldChanged;
		printf(_("old: %d words  %d %d%% common  %d %d%% deleted  %d %d%% changed\n"), statistics.oldTotal,
			common, (common * 100)/statistics.oldTotal,
			statistics.deleted, (statistics.deleted * 100) / statistics.oldTotal,
			statistics.oldChanged, (statistics.oldChanged * 100) / statistics.oldTotal);
		common = statistics.newTotal - statistics.added - statistics.newChanged;
		printf(_("new: %d words  %d %d%% common  %d %d%% inserted  %d %d%% changed\n"), statistics.newTotal,
			common, (common * 100)/statistics.newTotal,
			statistics.added, (statistics.added * 100) / statistics.newTotal,
			statistics.newChanged, (statistics.newChanged * 100) / statistics.newTotal);
	}
	return differences;
}
