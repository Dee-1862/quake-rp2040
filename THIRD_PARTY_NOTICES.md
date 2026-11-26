# Third party notices

This repository's own code is under the GNU GPL, version 2 or later (see LICENSE), except for the parts listed here.

## id Software, Quake

`engine/` is id Software's GPL release of Quake with changes. Copyright (C) 1996-1997 Id Software, Inc. GPL version 2 or later. Changed files say so at the top. See `engine/PROVENANCE.md`.

## wiliOGbsp, the FreeWili OG Board Support Package

The BSP is not included in this repository. You clone it from https://github.com/freewili/wiliOGbsp and it keeps its own licence.

One short part is copied from it, the 8 instruction PIO program for I2S sound in `port/rp2040/snd_rp2040.c`. That part stays under the FreeWili Hardware License below, not under the GPL.

```
wiliOGbsp - FreeWili OG Board Support Package
Copyright (c) 2026 Dave Robins

FreeWili Hardware License (Section A of the BSP's LICENSE)

"FreeWili Hardware" means hardware designed, manufactured, or authorized by
Intrepid Control Systems, Inc. under the FreeWili brand, including but not
limited to FreeWili OG (also known as FreeWili 1 or FreeWili Classic) and
FreeWili 2.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

1. FIELD OF USE. The rights granted above extend only to developing, building,
   testing, distributing, and running firmware or software that targets
   FreeWili Hardware. Use of the Software to develop, build, or run firmware or
   software targeting other hardware is not licensed under this Section A; see
   Section B.

   This condition does not limit: (a) building and running the host-side test
   suite, tooling, and documentation in this repository; (b) reading, studying,
   or discussing the Software; or (c) any right you hold under applicable law
   that cannot be limited by contract.

2. NOTICE. The above copyright notice, this permission notice, and the
   field-of-use condition shall be included in all copies or substantial
   portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

===========================================================================
```

## Raspberry Pi Pico SDK

`port/rp2040/ld_hottext/default_text_excludes.incl` replaces a linker script file of the Pico SDK and keeps its last line. Copyright (c) Raspberry Pi (Trading) Ltd. SPDX-License-Identifier: BSD-3-Clause.

## Not included

Quake's game data (pak files and anything made from them) is not part of this repository and is not licensed by it.
