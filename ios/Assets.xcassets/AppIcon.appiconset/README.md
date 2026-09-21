# The app icon

One file belongs here: `icon-1024.png`, exactly 1024x1024, opaque (the App
Store rejects an icon with an alpha channel), no rounded corners of its own --
iOS masks it.

`actool` derives every size iOS shows from this single image, so there is no
set of icon files to keep in step and nothing to resize by hand.

It is not in the repository yet. Until it is, `make ios` builds an iconless
app, which runs anywhere but cannot be uploaded: `make ios` with a signing
identity set stops with an error rather than producing a bundle the App Store
would reject.
