eda
===

text editor and source code analyzer

 The eda program is a text editor, built to work on source code files.
 It was designed for program development but can edit all kinds of text files.
 Eda has a simple and intuitive user interface, provides the base editor features
 and many tools in the background to help development efforts.

Mostly for C, shell scripts, Perl but also for Python, C++, XML/HTML, Java, Go, etc.

 * line based selection handling, cp, mv, rm and over
 * text line filter (7 levels) by regex patterns and manually
 * folding whole functions or blocks, an additional filter
 * regex based search, replace and highlight (patterns and lines)
 * multifile search, external and internal
 * external tools (make, find/egrep, version control tools) integrated
 * parsers for external and internal tools (make, lsdir, unified diff)
 * output filter API (for example: a2ps, indent, xmllint, macros)
 * bookmarks, projects, ctags support
 * version control support: cvs, svn, hg, git
 * macro interface (with record)
 * shell fork

See man pages eda(1), edamcro(5) and cmds.txt for details.

