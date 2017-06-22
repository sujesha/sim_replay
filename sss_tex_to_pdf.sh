#!/bin/sh
if [ $# -ne 1 ]
then
        echo "Usage: ./$0 <tex_file without extension .tex>"
        exit
fi

latex $1.tex
bibtex $1
makeindex $1
latex $1.tex
latex $1.tex
dvips -Ppdf -o $1.ps $1.dvi
gs -dSAFER -dNOPAUSE -dBATCH -sDEVICE=pdfwrite -sPAPERSIZE=a4 -dPDFSETTINGS=/printer -dCompatibilityLevel=1.3 -dMaxSubsetPct=100 -dSubsetFonts=true -dEmbedAllFoonts=true -sOutputFile=$1.pdf $1.ps
#ps2pdf $1.ps $1.pdf
