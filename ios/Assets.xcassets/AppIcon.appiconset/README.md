# The app icon

One file has belonged here: `icon-1024.png`, exactly 1024x1024, opaque (the
App Store has rejected an icon with an alpha channel), with no rounded corners
of its own -- iOS has masked it.

`actool` has derived every size iOS shows from this single image, so there has
been no set of icon files to keep in step and nothing to resize by hand.

Without it `make ios` has built an iconless app, which has run anywhere but
has not been uploadable; `make ios` with a signing identity set has stopped
with an error rather than producing a bundle the App Store would reject.
