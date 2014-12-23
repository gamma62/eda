eda
===

text editor and analyzer for programmers

 The eda program is a text editor, built to work on source code files.
 It is useful for editing all kinds of plain text but it was designed
 for program development and code analysis.
 Eda is a mixture of simple and intuitive user interface, base editor
 features and heavy-weight tools in the background to help everyday work
 in program development.

Mostly for C/C++, Perl, Tcl but also bash scripts, Python.

 * line based selection handling, operations on block of lines
 * filtering lines (multi level) by rules, regex patterns and dynamic
 * folding whole functions or blocks, jump to matching brace
 * regex based search, highlight (patterns or lines), replace
 * multifile search, external and internal
 * external tools (make, find/egrep, version control) binding
 * parsers for external and internal tools (make, lsdir, etc)
 * output filter APIs for printing (a2ps) or others (indent, macros, etc)
 * bookmarks, motion history, projects
 * version control tools supported: cvs, subversion, mercurial
 * macro interface (with record facility)
 * ctags support, show value, jump to definition

See man pages eda(1), edamcro(5) and README for details.
