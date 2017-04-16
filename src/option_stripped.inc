	OPTIONS
		OPTION('d', "delimiters", REQUIRED_ARG)
		END_OPTION
		OPTION('P', "punctuation", NO_ARG)
		END_OPTION
		OPTION('W', "white-space", REQUIRED_ARG)
		END_OPTION
		OPTION('h', "help", NO_ARG)
			int i = 0;
			printf(_("Usage: dwdiff [OPTIONS] <OLD FILE> <NEW FILE>\n"));
			while (descriptions[i] != NULL)
				fputs(_(descriptions[i++]), stdout);
			exit(EXIT_SUCCESS);
		END_OPTION
		OPTION('v', "version", NO_ARG)
		END_OPTION
		OPTION('1', "no-deleted", NO_ARG)
		END_OPTION
		OPTION('2', "no-inserted", NO_ARG)
		END_OPTION
		OPTION('3', "no-common", NO_ARG)
		END_OPTION
		OPTION('i', "ignore-case", NO_ARG)
		END_OPTION
#ifdef USE_UNICODE
		OPTION('I', "ignore-formatting", NO_ARG)
#else
		OPTION('I', "ignore-formatting", NO_ARG)
#endif
		END_OPTION
		OPTION('s', "statistics", NO_ARG)
		END_OPTION
		OPTION('a', "autopager", NO_ARG)
		END_OPTION
		OPTION('p', "printer", NO_ARG)
		END_OPTION
		OPTION('l', "less-mode", NO_ARG)
		END_OPTION
		LONG_OPTION("less", NO_ARG)
		END_OPTION
		OPTION('t', "terminal", NO_ARG)
		END_OPTION
		OPTION('w', "start-delete", REQUIRED_ARG)
		END_OPTION
		OPTION('x', "stop-delete", REQUIRED_ARG)
		END_OPTION
		OPTION('y', "start-insert", REQUIRED_ARG)
		END_OPTION
		OPTION('z', "stop-insert", REQUIRED_ARG)
		END_OPTION
		OPTION('n', "avoid-wraps", NO_ARG)
		END_OPTION
		SINGLE_DASH
		END_OPTION
		DOUBLE_DASH
			NO_MORE_OPTIONS;
		END_OPTION
		OPTION('c', "color", OPTIONAL_ARG)
		END_OPTION
		OPTION('L', "line-numbers", OPTIONAL_ARG)
		END_OPTION
		OPTION('C', "context", REQUIRED_ARG)
		END_OPTION
		OPTION('m', "match-context", REQUIRED_ARG)
		END_OPTION
		BOOLEAN_LONG_OPTION("aggregate-changes", option.aggregateChanges)
		OPTION('S', "paragraph-separator", OPTIONAL_ARG)
		END_OPTION
		BOOLEAN_LONG_OPTION("wdiff-output", option.wdiffOutput)
		LONG_OPTION("dwfilter", REQUIRED_ARG)
			option.dwfilterMode = true;
		END_OPTION
		OPTION('r', "reverse", NO_ARG)
		END_OPTION
		OPTION('R', "repeat-markers", NO_ARG)
		END_OPTION
		OPTION('u', "diff-input", NO_ARG)
		END_OPTION
		OPTION('A', "algorithm", REQUIRED_ARG)
		END_OPTION
		LONG_OPTION("profile", REQUIRED_ARG)
			option.profile = optArg;
		END_OPTION
		LONG_OPTION("no-profile", NO_ARG)
			option.profile = NULL;
		END_OPTION
	NO_OPTION
	END_OPTIONS
