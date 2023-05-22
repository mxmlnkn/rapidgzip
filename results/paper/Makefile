# Requires the Ubuntu/Debian package 'latexmk'

main.pdf: main.tex sections/* plots/* figures/* Makefile
	latexmk
	# This effective compressed the PDF by ~25%. -dFastWebView is not used because it leads to this pdfinfo output:
	# Syntax Warning: Invalid least number of objects reading page offset hints table
	/usr/bin/gs -sDEVICE=pdfwrite -dCompatibilityLevel=1.5 -dPDFSETTINGS=/default -dNOPAUSE -dBATCH -dQUIET -sOutputFile="Knespel, Brunst - 2023 - Rapidgzip - Parallel Decompression and Seeking in Gzip Files Using Cache Prefetching.pdf" main.pdf

all: main.pdf
