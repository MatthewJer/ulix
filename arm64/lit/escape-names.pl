#!/usr/bin/perl -p
# noweave writes chunk names as \LA{}name\RA{}; an underscore in a name must be escaped for LaTeX
s/(\\LA\{\})(.*?)(\\RA\{\})/my($a,$n,$b)=($1,$2,$3); $n =~ s{(?<!\\)_}{\\_}g; "$a$n$b"/ge;
s/(\\moddef\{)([^}]*)(\})/my($a,$n,$b)=($1,$2,$3); $n =~ s{(?<!\\)_}{\\_}g; "$a$n$b"/ge;
