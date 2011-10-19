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
static unsigned int oldLineNumber = 1, newLineNumber = 1;
static bool lastWasLinefeed = true;

/** Start the diff program. */
static FILE *startDiff(void) {
	FILE *diff;
	char *command;
	size_t length;

	/* Allocate memory to hold the diff command with arguments. */
	length = strlen(DIFF_COMMAND) + 1 + strlen(option.oldFile.diffTokens->name) + 1 + strlen(option.newFile.diffTokens->name) + 1;
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
	strcat(command, option.oldFile.diffTokens->name);
	strcat(command, " ");
	strcat(command, option.newFile.diffTokens->name);

	/* Start diff */
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
		if (option.colorMode && option.needStartStop && mode != COMMON) {
			if (mode == ADD)
				writeString(option.addColor, option.addColorLen);
			else
				writeString(option.delColor, option.delColorLen);
		}
	}
}

/** Skip or print the next bit of whitespace from @a file.
	@param file The file with whitespace.
	@param print Skip or print.
	@param mode What type of output to generate.
*/
static void handleNextWhitespace(TempFile *file, bool print, Mode mode) {
	int c;
	while ((c = sgetc(file->stream)) != EOF) {
		if (c == 0)
			return;

		if (c == '\\') {
			c = sgetc(file->stream);
			if (c == EOF)
				fatal(_("Error reading back input\n"));
		}

		if (print) {
			doPostLinefeed(mode);

			/* Less mode also over-strikes whitespace */
			if (option.less && c != '\n' && mode == DEL) {
				addchar('_', mode & COMMON);
				addchar('\010', mode & COMMON);
			}
			addchar(c, mode & COMMON);

			if (c == '\n')
				lastWasLinefeed = true;
		}

		if (c == '\n') {
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
}

/** Skip or print the next bit of whitespace from the new file, keeping the
	old file synchronized as far as line numbers are concerned.

	This is only for printing common text, and will skip over the old
	whitespace.
*/
static void handleSynchronizedNextWhitespace(void) {
	bool oldValid = true;
	int c;

	while ((c = sgetc(option.newFile.whitespace->stream)) != EOF) {
		if (c == 0)
			break;

		if (c == '\\') {
			c = sgetc(option.newFile.whitespace->stream);
			if (c == EOF)
				fatal(_("Error reading back input\n"));
		}

		if (option.printCommon) {
			doPostLinefeed(COMMON);

			addchar(c, true);

			if (c == '\n')
				lastWasLinefeed = true;
		}

		/* If a newline was found, see if the old file also has a newline. */
		if (c == '\n') {
			newLineNumber++;
			if (oldValid) {
				oldValid = false;
				while ((c = sgetc(option.oldFile.whitespace->stream)) != EOF) {
					if (c == 0) {
						oldValid = false;
						break;
					}

					if (c == '\\') {
						c = sgetc(option.oldFile.whitespace->stream);
						if (c == EOF)
							fatal(_("Error reading back input\n"));
					}

					oldValid = true;
					if (c == '\n') {
						oldLineNumber++;
						break;
					}
				}
			}
		}
	}

	/* Process any remaining whitespace from the old file. */
	if (oldValid) {
		while ((c = sgetc(option.oldFile.whitespace->stream)) != EOF) {
			if (c == 0)
				break;

			if (c == '\\') {
				c = sgetc(option.oldFile.whitespace->stream);
				if (c == EOF)
					fatal(_("Error reading back input\n"));
			}

			if (c == '\n')
				oldLineNumber++;
		}
	}
}

/** Skip or print the next token from @a file.
	@param file The file with tokens.
	@param print Skip or print.
	@param mode What type of output to generate.
*/
static void handleNextToken(TempFile *file, bool print, Mode mode) {
	int c;
	while ((c = sgetc(file->stream)) != EOF) {
		if (c == '\n')
			return;

		/* Re-transliterate the characters, if necessary. */
		if (option.transliterate && c == '\\') {
			c = sgetc(file->stream);
			switch (c) {
				case 'n':
					c = '\n';
					break;
				case '\\':
					break;
				/* Transliteration of \X is not necessary as we are reading
				   back the tokens file, and not the diffTokens file. */
				default:
					fatal(_("Error reading back input\n"));
			}
		}

		if (print) {
			doPostLinefeed(mode);

			/* Printer mode and less mode do special stuff, per character. */
			if ((option.printer || option.less) && c != '\n') {
				if (mode == DEL) {
					addchar('_', mode & COMMON);
					addchar('\010', mode & COMMON);
				} else if (mode == ADD) {
					addchar(c, mode & COMMON);
					addchar('\010', mode & COMMON);
				}
			}
			addchar(c, mode & COMMON);

			if (c == '\n')
				lastWasLinefeed = true;
		}

		if (c == '\n') {
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
		handleNextWhitespace(file->whitespace, print, mode);
		handleNextToken(file->tokens, print, mode);
		file->lastPrinted++;
	}
}

/** Print (or skip if the user doesn't want to see) the common words.
	@param idx The last word to print (or skip).
*/
void printToCommonWord(int idx) {
	while (option.newFile.lastPrinted < idx) {
		handleSynchronizedNextWhitespace();
		handleNextToken(option.newFile.tokens, option.printCommon, COMMON);
		handleNextToken(option.oldFile.tokens, false, OLD_COMMON);
		option.newFile.lastPrinted++;
		option.oldFile.lastPrinted++;
	}
}

/** Print (or skip if the user doesn't want to see) words from @a file.
	@param range The range of words to print (or skip).
	@param file The @a InputFile to print from.
	@param mode Either ADD or DEL, uded for printing of start/stop markers.
*/
static void printWords(int *range, InputFile *file, bool print, Mode mode) {
	ASSERT(file->lastPrinted == range[0] - 1);

	/* Print the first word. As we need to add the markers AFTER the first bit of
	   white space, we can't just use handleWord */

	/* Print preceding whitespace. Should not be overstriken, so print as common */
	handleNextWhitespace(file->whitespace, print, mode + COMMON);
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
		handleSynchronizedNextWhitespace();
		handleNextToken(option.newFile.tokens, true, COMMON);
		handleNextToken(option.oldFile.tokens, false, OLD_COMMON);
	}
}

/** Read the output of the diff command, and call the appropriate print routines */
void doDiff(void) {
	int c, command, currentIndex, range[4];
	FILE *diff;

	diff = startDiff();

	if (option.needMarkers)
		puts("======================================================================");

	while ((c = getc(diff)) != EOF) {
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
			/* Print common words. For delete commands we need to take into
			   account that the words were BEFORE the named line. */
			printToCommonWord(command != 'd' ? range[2] - 1 : range[2]);
			if (command != 'a')
				printDeletedWords(range);
			if (command != 'd')
				printAddedWords(range + 2);
			if (option.needMarkers) {
				puts("\n======================================================================");
				lastWasLinefeed = true;
			}

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
	pclose(diff);
	printEnd();
}
