This is an old experiment I had lying around, I never did wind up shipping it,
but hopefully someone finds it useful!

It achieves very fast downsampling by staying close to the DXT encoding
throughout, avoiding excess conversions between RGBA float or 32bpp, and using
only integer instructions. As a side effect, the results are very good because
those conversions can introduce extra lossiness.

Caveats:
- I never finished the DXT5 big endian support
- DXT 1 alpha handling is poor; it becomes viral, so an image with alpha will
  become increasingly transparent as it is scaled down.
