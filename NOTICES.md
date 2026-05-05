# Third-party notices

OpenBounty bundles or links the following third-party components. Each is
distributed under its own license; the full license text for every
shipped component is reproduced inline below so this file is
self-contained when packaged with a release binary.

The `greatest` testing framework is used only by the unit-test binary
and is not linked into the shipped game; it is listed for completeness.

---

## raylib

A simple and easy-to-use library to enjoy videogames programming.

- License: zlib/libpng
- Project: https://www.raylib.com

```
Copyright (c) 2013-2026 Ramon Santamaria (@raysan5)

This software is provided "as-is", without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
  claim that you wrote the original software. If you use this software in
  a product, an acknowledgment in the product documentation would be
  appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not
  be misrepresented as being the original software.

  3. This notice may not be removed or altered from any source distribution.
```

---

## cJSON

Ultralightweight JSON parser in ANSI C.

- License: MIT
- Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
- Project: https://github.com/DaveGamble/cJSON

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## miniz

Single-file zlib-replacement library with deflate/inflate, gzip, ZIP read/write.

- License: MIT
- Copyright 2013-2014 RAD Game Tools and Valve Software
- Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
- Project: https://github.com/richgel999/miniz

```
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## minih264

Minimalistic H.264 encoder, single header.

- License: CC0 1.0 Universal (public domain dedication)
- Project: https://github.com/lieff/minih264

The author dedicates this work to the public domain under
Creative Commons CC0 1.0. Full text:
https://creativecommons.org/publicdomain/zero/1.0/legalcode

---

## minimp4

Minimalistic MP4 mux/demux, single header.

- License: CC0 1.0 Universal (public domain dedication)
- Project: https://github.com/lieff/minimp4

The author dedicates this work to the public domain under
Creative Commons CC0 1.0. Full text:
https://creativecommons.org/publicdomain/zero/1.0/legalcode

---

## greatest *(test-only, not shipped in the game binary)*

Single-header testing library used by the unit tests.

- License: ISC
- Copyright (c) 2011-2018 Scott Vokes
- Project: https://github.com/silentbicycle/greatest

```
Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

---

## Original game attribution

OpenBounty is a clean-room reimplementation of *King's Bounty* (1990).
The original game was designed by Jon Van Caneghem, programmed by Mark
and Andy Caldwell, with graphics by Kenneth L. Mayfield and Vincent
DeQuattro, Jr. Copyright 1990 New World Computing, Inc. All rights
reserved.

OpenBounty ships none of the original game's binaries, art, or audio.
Original assets must be supplied by the player from a legally-owned
copy of the DOS distribution; the `--extract` mode produces a runnable
asset pack from such a distribution.
