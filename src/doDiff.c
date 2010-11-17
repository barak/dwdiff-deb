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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "definitions.h"
#include "option.h"
#include "util.h"
#include "stream.h"
#include "buffer.h"
#include "unicode.h"

static const char resetColor[] = "\033[0m";
static const char eraseLine[] = "\033[K";
static unsigned int oldLineNumber = 1, newLineNumber = 1;
static bool lastWasLinefeed = true, lastWasDelete = false, lastWasCarriageReturn = false;
static int recursionIndex = -1;

/** Check whether the last-read character equals a certain value. */
static bool charDataEquals(int c) {
#ifdef USE_UNICODE
	if (UTF8Mode)
		return charData.UTF8Char.original.data[0] == c;
#endif
	return charData.singleChar == c;
}

static bool readNextChar(Stream *stream) {
#ifdef USE_UNICODE
	if (UTF8Mode)
		return getBackspaceCluster(stream, &charData.UTF8Char.original);
#endif
	charData.singleChar = stream->vtable->getChar(stream);
	return charData.singleChar != EOF;
}

static void addCharData(bool common) {
#ifdef USE_UNICODE
	if (UTF8Mode) {
		int32_t i;
		char encoded[4];
		int bytes, j;
		UChar32 highSurrogate = 0;

		for (i = 0; i < charData.UTF8Char.original.length; i++) {
			bytes = filteredConvertToUTF8(charData.UTF8Char.original.data[i], encoded, &highSurrogate);

			for (j = 0; j < bytes; j++)
				addchar(encoded[j], common);
		}
		return;
	}
#endif
	addchar(charData.singleChar, common);
}

/** Start the diff program. */
static FILE *startDiff(TempFile *oldDiffTokens, TempFile *newDiffTokens) {
	FILE *diff;
	char *command;
	size_t length;

	/* Allocate memory to hold the diff command with arguments. */
	length = strlen(DIFF_COMMAND) + 1 + strlen(oldDiffTokens->name) + 1 + strlen(newDiffTokens->name) + 1;
	if (option.diffOption != NULL)
		length += strlen(option.diffOption) + 1;

	errno = 0;
	if ((command = malloc(length)) == NULL)
		outOfMemory();

	/* Compose the diff command. */
	strcpy(command, DIFF_COMMAND);
	strcat(command, " ");
	if (option.diffOption != NULL) {
		strcat(command, option.diffOption);
		strcat(command, " ");
	}
	strcat(command, oldDiffTokens->name);
	strcat(command, " ");
	strcat(command, newDiffTokens->name);

	/* Start diff */
#ifdef LEAVE_FILES
	fprintf(stderr, "%s\n", command);
#endif
	if ((diff = popen(command, "r")) == NULL)
		fatal(_("Failed to execute diff: %s\n"), strerror(errno));
	free(command);
	return diff;
}

/* Note: ADD should be 0, OLD_COMMON should be DEL + COMMON. */
typedef enum {ADD, DEL, COMMON, OLD_COMMON} Mode;

/** If the last character printed was a newline, do some special handling.
	@param mode What kind of output is generated next.
*/
static void doPostLinefeed(Mode mode) {
	if (lastWasLinefeed) {
		if (mode & COMMON)
			mode = COMMON;

		lastWasLinefeed = false;
		if (option.lineNumbers) {
			if (option.colorMode && mode != COMMON)
				writeString(resetColor, sizeof(resetColor) - 1);
			printLineNumbers(oldLineNumber, newLineNumber);
		}
		if (option.needStartStop && mode != COMMON) {
			if (option.colorMode) {
				if (mode == ADD)
					writeString(option.addColor, option.addColorLen);
				else
					writeString(option.delColor, option.delColorLen);
			}
			if (option.repeatMarkers) {
				if (mode == ADD)
					writeString(option.addStart, option.addStartLen);
				else
					writeString(option.delStart, option.delStartLen);
			}
		}
	}
}


/** Handle a single whitespace character.
	@param print Skip or print.
	@param mode What type of output to generate.
*/
static void handleWhitespaceChar(bool print, Mode mode) {
	if (print) {
		doPostLinefeed(mode);

		/* Less mode also over-strikes whitespace */
		if (option.less && !charDataEquals('\n') && !charDataEquals('\r') && mode == DEL) {
			addchar('_', mode & COMMON);
			addchar('\010', mode & COMMON);
		} else if (option.needStartStop && (mode & COMMON) != COMMON && (charDataEquals('\r')  || (!lastWasCarriageReturn && charDataEquals('\n')))) {
			if (option.repeatMarkers) {
				if (mode == ADD)
					writeString(option.addStop, option.addStopLen);
				else
					writeString(option.delStop, option.delStopLen);
			}

			if (option.colorMode)
				/* Erase rest of line so it will use the correct background color */
				writeString(eraseLine, sizeof(eraseLine) - 1);
		}
		addCharData(mode & COMMON);

		if (charDataEquals('\n'))
			lastWasLinefeed = true;

		lastWasCarriageReturn = charDataEquals('\r');
	}

	if (charDataEquals('\n')) {
		switch (mode) {
			case COMMON:
			case ADD:
				newLineNumber++;
				break;
			case OLD_COMMON:
			case DEL:
				oldLineNumber++;
				break;
			default:
				PANIC();
		}
	}
}

/** Skip or print the next bit of whitespace from @a file.
	@param file The file with whitespace.
	@param print Skip or print.
	@param mode What type of output to generate.
*/
static void handleNextWhitespace(InputFile *file, bool print, Mode mode) {
	if (file->whitespaceBufferUsed) {
		Stream *stream = newStringStream(file->whitespaceBuffer.line, file->whitespaceBuffer.filled);
		while (readNextChar(stream))
			handleWhitespaceChar(print, mode);
		free(stream);
		file->whitespaceBuffer.filled = 0;
		file->whitespaceBufferUsed = false;
	} else {
		while (readNextChar(file->whitespace->stream)) {
			if (charDataEquals(0))
				return;

			if (charDataEquals('\\')) {
				if (!readNextChar(file->whitespace->stream))
					fatal(_("Error reading back input\n"));
			}
			handleWhitespaceChar(print, mode);
		}
	}
}

/** Skip or print the next bit of whitespace from the new or old file, keeping
	the other file synchronized as far as line numbers are concerned.
	@param printOld Use the old file for printing instead of the new file.

	This is only for printing common text, and will skip over the old
	whitespace.
*/
static void handleSynchronizedNextWhitespace(bool printNew) {
	bool BValid = true;
	unsigned int *lineNumberA, *lineNumberB;
	Stream *whitespaceA, *whitespaceB;

	if (printNew) {
		whitespaceA = option.newFile.whitespace->stream;
		whitespaceB = option.oldFile.whitespace->stream;
		lineNumberA = &newLineNumber;
		lineNumberB = &oldLineNumber;
	} else {
		whitespaceA = option.oldFile.whitespace->stream;
		whitespaceB = option.newFile.whitespace->stream;
		lineNumberA = &oldLineNumber;
		lineNumberB = &newLineNumber;
	}

	while (readNextChar(whitespaceA)) {
		if (charDataEquals(0))
			break;

		if (charDataEquals('\\')) {
			if (!readNextChar(whitespaceA))
				fatal(_("Error reading back input\n"));
		}

		if (option.printCommon) {
			doPostLinefeed(COMMON);

			/* Note that we don't have to check less mode here as we only print
			   common whitespace. */
			addCharData(true);

			if (charDataEquals('\n'))
				lastWasLinefeed = true;

			lastWasCarriageReturn = charDataEquals('\r');
		}

		/* If a newline was found, see if the B file also has a newline. */
		if (charDataEquals('\n')) {
			(*lineNumberA)++;
			/* Only process the B file if it has not reached then end of the token yet. */
			if (BValid) {
				bool result;
				while ((result = readNextChar(whitespaceB))) {
					if (charDataEquals(0))
						break;

					if (charDataEquals('\\')) {
						if (!readNextChar(whitespaceB))
							fatal(_("Error reading back input\n"));
					}

					if (charDataEquals('\n')) {
						(*lineNumberB)++;
						break;
					}
				}
				if (charDataEquals(0) || !result)
					BValid = false;
			}
		}
	}

	/* Process any remaining whitespace from the BS file. */
	if (BValid) {
		while (readNextChar(whitespaceB)) {
			if (charDataEquals(0))
				break;

			if (charDataEquals('\\')) {
				if (!readNextChar(whitespaceB))
					fatal(_("Error reading back input\n"));
			}

			if (charDataEquals('\n'))
				(*lineNumberB)++;
		}
	}
}

/** Wrapper for addchar which takes printer and less mode into account
	@param mode What type of output to generate.
*/
void addTokenChar(Mode mode) {
	/* Printer mode and less mode do special stuff, per character. */
	if ((option.printer || option.less) && !charDataEquals('\n') && !charDataEquals('\r')) {
		if (mode == DEL) {
			addchar('_', mode & COMMON);
			addchar('\010', mode & COMMON);
		} else if (mode == ADD) {
			addCharData(mode & COMMON);
			addchar('\010', mode & COMMON);
		}
	}
	addCharData(mode & COMMON);
}

/** Skip or print the next token from @a file.
	@param file The file with tokens.
	@param print Skip or print.
	@param mode What type of output to generate.
*/
static void handleNextToken(TempFile *file, bool print, Mode mode) {
	bool empty = true;
	while (readNextChar(file->stream)) {
		if (charDataEquals('\n')) {
			/* Check for option.paraDelim _should_ be superfluous, unless there is a bug elsewhere. */
			if (option.paraDelim && print && empty && mode != COMMON) {
				Stream *stream = newStringStream(option.paraDelimMarker, option.paraDelimMarkerLength);
				while (readNextChar(stream)) {
					/* doPostLinefeed only does something if the last character was a line feed. However,
					   the paragraph delimiter may contain line feeds as well, so call doPostLinefeed
					   every time a character was printed. */
					doPostLinefeed(mode);
					addCharData(mode);
				}
				free(stream);
			}
			return;
		}
		empty = false;

		/* Re-transliterate the characters, if necessary. */
		if (option.transliterate && charDataEquals('\\')) {
			if (!readNextChar(file->stream))
				fatal(_("Error reading back input\n"));
			if (charDataEquals('n')) {
#ifdef USE_UNICODE
				if (UTF8Mode)
					charData.UTF8Char.original.data[0] = '\n';
				else
#endif
					charData.singleChar = '\n';
			}
			else if (!charDataEquals('\\'))
				fatal(_("Error reading back input\n"));
			/* Transliteration of \X is not necessary as we are reading
			   back the tokens file, and not the diffTokens file. */
		}

		if (print) {
			doPostLinefeed(mode);

			if (option.needStartStop && (mode & COMMON) != COMMON && (charDataEquals('\r')  || (!lastWasCarriageReturn && charDataEquals('\n')))) {
				if (option.repeatMarkers) {
					if (mode == ADD)
						writeString(option.addStop, option.addStopLen);
					else
						writeString(option.delStop, option.delStopLen);
				}

				if (option.colorMode)
					/* Erase rest of line so it will use the correct background color */
					writeString(eraseLine, sizeof(eraseLine) - 1);
			}

			addTokenChar(mode);

			if (charDataEquals('\n'))
				lastWasLinefeed = true;

			lastWasCarriageReturn = charDataEquals('\r');
		}

		if (charDataEquals('\n')) {
			switch (mode) {
				/* When the newline is a word character rather than a whitespace
				   character, we can safely count old and new lines together
				   for common words. This will keep the line numbers in synch
				   for these cases. Note that this also means that for
				   OLD_COMMON the line counter is not incremented. */
				case COMMON:
					oldLineNumber++;
				case ADD:
					newLineNumber++;
					break;
				case DEL:
					oldLineNumber++;
					break;
				case OLD_COMMON:
					break;
				default:
					PANIC();
			}
		}
	}
}

/** Skip or print the next whitespace and tokens from @a file.
	@param file The @a InputFile to use.
	@param idx The last word to print or skip.
	@param print Skip or print.
	@param mode What type of output to generate.
*/
static void handleWord(InputFile *file, int idx, bool print, Mode mode) {
	while (file->lastPrinted < idx) {
		handleNextWhitespace(file, print, mode);
		handleNextToken(file->tokens, print, mode);
		file->lastPrinted++;
	}
}

/** Print (or skip if the user doesn't want to see) the common words.
	@param idx The last word to print (or skip).
*/
void printToCommonWord(int idx) {
	while (option.newFile.lastPrinted < idx) {
		handleSynchronizedNextWhitespace(!lastWasDelete);
		lastWasDelete = false;
		handleNextToken(option.newFile.tokens, option.printCommon, COMMON);
		handleNextToken(option.oldFile.tokens, false, OLD_COMMON);
		option.newFile.lastPrinted++;
		option.oldFile.lastPrinted++;
	}
}

/** Print (or skip if the user doesn't want to see) words from @a file.
	@param range The range of words to print (or skip).
	@param file The @a InputFile to print from.
	@param mode Either ADD or DEL, used for printing of start/stop markers.
*/
static void printWords(int *range, InputFile *file, bool print, Mode mode) {
	ASSERT(file->lastPrinted == range[0] - 1);

	/* Print the first word. As we need to add the markers AFTER the first bit of
	   white space, we can't just use handleWord */

	/* Print preceding whitespace. Should not be overstriken, so print as common */
	handleNextWhitespace(file, print, mode + COMMON);
	/* Ensure that the the line numbers etc. get printed before the markers */
	if (print)
		doPostLinefeed(COMMON);

	/* Print start marker */
	if (print && option.needStartStop) {
		if (mode == ADD) {
			if (option.colorMode)
				writeString(option.addColor, option.addColorLen);
			writeString(option.addStart, option.addStartLen);
		} else {
			if (option.colorMode)
				writeString(option.delColor, option.delColorLen);
			writeString(option.delStart, option.delStartLen);
		}
	}

	/* Print first word */
	handleNextToken(file->tokens, print, mode);
	file->lastPrinted++;
	/* Print following words */
	handleWord(file, range[range[1] >= 0 ? 1: 0], print, mode);

	if (print)
		doPostLinefeed(mode);

	/* Print stop marker */
	if (print && option.needStartStop) {
		if (mode == ADD)
			writeString(option.addStop, option.addStopLen);
		else
			writeString(option.delStop, option.delStopLen);

		if (option.colorMode)
			writeString(resetColor, sizeof(resetColor) - 1);
	}
}

/** Print (or skip if the user doesn't want to see) deleted words.
	@param range The range of words to print (or skip).
*/
void printDeletedWords(int *range) {
	printWords(range, &option.oldFile, option.printDeleted, DEL);
}

/** Print (or skip if the user doesn't want to see) inserted words.
	@param range The range of words to print (or skip).
*/
void printAddedWords(int *range) {
	printWords(range, &option.newFile, option.printAdded, ADD);
}

/** Print (or skip if the user doesn't want to see) the last (common) words of both files. */
void printEnd(void) {
	if (!option.printCommon)
		return;
	while(!sfeof(option.newFile.tokens->stream)) {
		handleSynchronizedNextWhitespace(!lastWasDelete);
		lastWasDelete = false;
		handleNextToken(option.newFile.tokens, true, COMMON);
		handleNextToken(option.oldFile.tokens, false, OLD_COMMON);
	}
}

/** Load a piece of whitespace from file.
	@param file The @a InputFile to read from and store the result.
	@return a boolean indicating whether a newline was found within the whitespace.
*/
static bool loadNextWhitespace(InputFile *file) {
	bool newlineFound = false;

	file->whitespaceBufferUsed = true;
	file->whitespaceBuffer.filled = 0;
	while (readNextChar(file->whitespace->stream)) {
		if (charDataEquals(0))
			return newlineFound;

		if (charDataEquals('\\')) {
			if (!readNextChar(file->whitespace->stream))
				fatal(_("Error reading back input\n"));
		}

		if (charDataEquals('\n'))
			newlineFound = true;

#ifdef USE_UNICODE
		if (UTF8Mode) {
			int32_t i;
			char encoded[4];
			int bytes, j;
			UChar32 highSurrogate = 0;

			for (i = 0; i < charData.UTF8Char.original.length; i++) {
				bytes = filteredConvertToUTF8(charData.UTF8Char.original.data[i], encoded, &highSurrogate);

				ensureBufferSpace(&file->whitespaceBuffer, bytes);
				for (j = 0; j < bytes; j++)
					file->whitespaceBuffer.line[file->whitespaceBuffer.filled++] = encoded[j];
			}
		} else
#endif
		{
			ensureBufferSpace(&file->whitespaceBuffer, 1);
			file->whitespaceBuffer.line[file->whitespaceBuffer.filled++] = charData.singleChar;
		}
	}
	return newlineFound;
}

#define NEEDS_OUTPUT (processedLines >= startLine && processedLines < endLine && i >= startToken && i < endToken)
/** Split the tokens from the diff output.
	@param diff The pipe to the diff program.
	@param oldPart A pointer to the return value for the new 'old' set of diff-tokens.
	@param oldPart A pointer to the return value for the new 'new' set of diff-tokens.
	@param context The size of the context used.
	@param newContext The desired size of the context for the output.
*/
static int splitDifference(FILE *diff, TempFile **oldPart, TempFile **newPart, int context, int newContext, int *range) {
	int tokenLengths[option.matchContext + 1];
	int c, i, j;
	int processedLines = 0;
	Stream *output;

	int startLine = (context - newContext) / 2;
	int endLineOld = range[1] - range[0] - startLine + 1;
	int endLineNew = range[3] - range[2] - startLine + 1;
	int startToken = (context - newContext) / 2;
	int endToken = startToken + newContext + 1;

	if ((*oldPart = tempFile(recursionIndex * 2)) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));
	if ((*newPart = tempFile(recursionIndex * 2 + 1)) == NULL)
		fatal(_("Could not create temporary file: %s\n"), strerror(errno));

	output = (*oldPart)->stream;
	while ((c = getc(diff)) != EOF) {
		bool printed = false;
		memset(tokenLengths, 0, sizeof(tokenLengths));

		if (c == '<' || c == '>') {
			int endLine = c == '<' ? endLineOld : endLineNew;

			/* Skip any spaces */
			while ((c = getc(diff)) != EOF && c == ' ') {}
			/* '0' + 16 is a special marker that indicates an empty token used for
			   padding the start and end. This is to ensure that they are different
			   from empty tokens, which are inserted for paragraph separators (empty
			   lines). */
			if (c == EOF || c < '0' || c > ('0' + 16))
				fatal(_("Error parsing diff output\n"));

			tokenLengths[0] = c == ('0' + 16) ? 0 : c - '0';

			/* Read token lengths */
			for (i = 0; i < context + 1; i++) {
				while ((c = getc(diff)) != EOF && c != ',') {
					if (!(c < '0' || c > ('0' + 15)))
						tokenLengths[i] = tokenLengths[i] * 16 + (c - '0');
					else if (c == ('0' + 16))
						tokenLengths[i] = 0;
					else
						fatal(_("Error parsing diff output\n"));
					if (NEEDS_OUTPUT && newContext != 0)
						sputc(output, c);
				}
				if (NEEDS_OUTPUT && newContext != 0)
					sputc(output, ',');

				if (c == EOF)
					fatal(_("Error parsing diff output\n"));
			}

			for (i = 0; i < context + 1; i++) {
				if (NEEDS_OUTPUT)
					printed = true;
				for (j = 0; j < tokenLengths[i]; j++) {
					if ((c = getc(diff)) == EOF)
						fatal(_("Error parsing diff output\n"));

					if (NEEDS_OUTPUT)
						sputc(output, c);
#ifdef NO_MINUS_A
					/* If the output stream was escaped, the tokenLengths don't take
					   that into account so we have handle a couple of extra bytes
					   here. */
					if (c == '%') {
						if ((c = getc(diff)) == EOF)
							fatal(_("Error parsing diff output\n"));
						if (NEEDS_OUTPUT)
							sputc(output, c);
						if (c == '%')
							continue;
						if ((c = getc(diff)) == EOF)
							fatal(_("Error parsing diff output\n"));
						if (NEEDS_OUTPUT)
							sputc(output, c);
					}
#endif
				}
			}
			if (printed)
				sputc(output, '\n');
			processedLines++;
			if (getc(diff) != '\n')
				fatal(_("Error parsing diff output\n"));
		} else if (c == '-') {
			/* Skip lines between diff lines */
			while ((c = getc(diff)) != EOF && c != '\n') {}
			if (c == EOF)
				fatal(_("Error parsing diff output\n"));
			output = (*newPart)->stream;
			processedLines = 0;
		} else {
			return c;
		}
	}
	return c;
}


/** Read the output of the diff command, and call the appropriate print routines.
	@param baseRange The range associated with the diff-token files, or NULL if the whole file.
	@param context The size of the context used.
	@param oldDiffTokens The file with diff-tokens from the 'old' file.
	@param newDiffTokens The file with diff-tokens from the 'new' file.
*/
static void doDiffInternal(int *baseRange, int context, TempFile *oldDiffTokens, TempFile *newDiffTokens) {
	int c, command, currentIndex, range[4];
	bool reverseDeleteAdd = false;
	FILE *diff;
	recursionIndex++;

	diff = startDiff(oldDiffTokens, newDiffTokens);

	if (option.needMarkers && baseRange == NULL)
		puts("======================================================================");

	while ((c = getc(diff)) != EOF) {
	  nextChar:
		if (c == '<' || c == '>' || c == '-') {
			/* Skip all lines showing differences */
			while ((c = getc(diff)) != EOF && c != '\n') {}
			if (c == EOF)
				break;
		} else if (isdigit(c)) {
			/* Initialise values for next diff */
			currentIndex = 0;
			memset(range, 0xff, sizeof(range));
			range[0] = c - '0';
			command = 0;
			/* Read until the end of the line, or the end of the input */
			while ((c = getc(diff)) != EOF && c != '\n') {
				if (isdigit(c)) {
					/* Line number */
					range[currentIndex] = range[currentIndex] * 10 + (c - '0');
				} else if (c == ',') {
					/* Line number separator */
					if (currentIndex == 3 || currentIndex == 1)
						fatal(_("Error parsing diff output\n"));
					range[++currentIndex] = 0;
				} else if (strchr("acd", c) != NULL) {
					/* Command code */
					if (currentIndex == 3 || command != 0)
						fatal(_("Error parsing diff output\n"));
					command = c;
					currentIndex = 2;
					range[currentIndex] = 0;
				} else {
					fatal(_("Error parsing diff output\n"));
				}
			}
			/* Check that a command is set */
			if (command == 0)
				fatal(_("Error parsing diff output\n"));
			differences = 1;
			if (currentIndex != 2 && currentIndex != 3)
				fatal(_("Error parsing diff output\n"));

			if (option.matchContext && context != 0) {
				/* If the match-context option was specified, the diff output will
				   generally be too long. Here we trim the ranges to the minimum
				   range required to show the diff. However, the ranges are only
				   too long when the difference is a change, not when it is an add
				   or delete. */
				if (command == 'c') {
					if (range[1] == -1) {
						range[0]--;
						range[3]--;
						command = 'a';
					} else if (range[3] == -1) {
						range[2]--;
						range[1]--;
						command = 'd';
					} else if (range[1] - context < range[0]) {
						range[0]--;
						range[3] -= range[1] - range[0];
						range[1] = -1;
						command = 'a';
					} else if (range[3] - context < range[2]) {
						range[2]--;
						range[1] -= range[3] - range[2];
						range[3] = -1;
						command = 'd';
					} else if (!option.aggregateChanges &&
							/* Only split when the change is not trivial. */
							range[1] - range[0] > context &&
							range[3] - range[2] > context) {
						TempFile *oldPart, *newPart;
						int newContext = (context / 4) * 2;

						c = splitDifference(diff, &oldPart, &newPart, context, newContext, range);
						sfclose(oldPart->stream);
						sfclose(newPart->stream);
						if (baseRange != NULL) {
							int i;
							for (i = 0; i < 4; i++) {
								if (range[i] >= 0)
									range[i] += baseRange[i & 0xFE] - 1;
							}
						}
						doDiffInternal(range, newContext, oldPart, newPart);
						if (c == EOF)
							break;
						else
							goto nextChar;
					} else {
						range[1] -= context;
						range[3] -= context;
					}
				}
			}

			if (baseRange != NULL) {
				int i;
				for (i = 0; i < 4; i++) {
					if (range[i] >= 0)
						range[i] += baseRange[i & 0xFE] - 1;
				}
			}

			/* Print common words. For delete commands we need to take into
			   account that the words were BEFORE the named line. */
			printToCommonWord(command != 'd' ? range[2] - 1 : range[2]);

			/* Load whitespace for both files and analyse. If old ws does not
			   contain a newline, don't bother with loading the new because we need
			   regular printing. Otherwise, determine if we need reverse printing or
			   need to change the new ws into a single space */
			if (command == 'c' && !option.wdiffOutput) {
				if (loadNextWhitespace(&option.oldFile)) {
					if (loadNextWhitespace(&option.newFile)) {
						option.newFile.whitespaceBuffer.line[0] = ' ';
						option.newFile.whitespaceBuffer.filled = 1;
					} else {
						reverseDeleteAdd = true;
					}
				}
			}

			if (reverseDeleteAdd) {
				/* This only happens for change commands, so we don't have to check
				   the command letter anymore. */
				printAddedWords(range + 2);
				printDeletedWords(range);
				reverseDeleteAdd = false;
			} else {
				if (command != 'a')
					printDeletedWords(range);
				if (command != 'd')
					printAddedWords(range + 2);
			}
			if (option.needMarkers) {
				puts("\n======================================================================");
				lastWasLinefeed = true;
			}

			if (command == 'd' && option.printDeleted && !option.wdiffOutput)
				lastWasDelete = true;

			/* Update statistics */
			switch (command) {
				case 'a':
					statistics.added += range[3] >= 0 ? range[3] - range[2] + 1 : 1;
					break;
				case 'd':
					statistics.deleted += range[1] >= 0 ? range[1] - range[0] + 1 : 1;
					break;
				case 'c':
					statistics.newChanged += range[3] >= 0 ? range[3] - range[2] + 1 : 1;
					statistics.oldChanged += range[1] >= 0 ? range[1] - range[0] + 1 : 1;
					break;
				default:
					PANIC();
			}
			if (c == EOF)
				break;
		} else {
			fatal(_("Error parsing diff output\n"));
		}
	}
	/* FIXME: Check return value of pclose! */
	pclose(diff);
	recursionIndex--;
}

/** Do the difference action. */
void doDiff(void) {
	initBuffer(&option.oldFile.whitespaceBuffer, INITIAL_WORD_BUFFER_SIZE);
	initBuffer(&option.newFile.whitespaceBuffer, INITIAL_WORD_BUFFER_SIZE);
	doDiffInternal(NULL, option.matchContext, option.oldFile.diffTokens, option.newFile.diffTokens);
	printEnd();
}
