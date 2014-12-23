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
 .
 o line based selection handling, operations on block of lines
 o filtering lines (multi level) by rules, regex patterns and dynamic
 o folding whole functions or blocks, jump to matching brace
 o regex based search, highlight (patterns or lines), replace
 o multifile search, external and internal
 o external tools (make, find/egrep, version control) binding
 o parsers for external and internal tools (make, lsdir, etc)
 o output filter APIs for printing (a2ps) or others (indent, macros, etc)
 o bookmarks, motion history, projects
 o version control tools supported: cvs, subversion, mercurial
 o macro interface (with record facility)
 o ctags support, show value, jump to definition
 .
 See man pages eda(1), edamcro(5) and README for details.
