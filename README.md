# Mandelbrot Set Explorer

This program provides the capability to view the Mandelbrot Set at any location
and zoom in by a factor of 2^46 (approx 10^14).

Other features include:
* speculative caching of results
* adjustable colors
* saved places
* auto-zoom

This program is built and tested on Fedora 31 Linux. 
The SDL2 library is used.

Refer to mbs_help.txt for detailed instructions.

# Early Versions

* try1: simple program with no pan and zoom, text output
* try2: graphical output, black and white, basic controls
* try3: uses bignum routines with 320 fraction bits, very slow performance
