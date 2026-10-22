# Driver patches

Patches to PS5_Mesa's RADV, applied on top of the Mesa revision PS5_Vulkan pins (`1ba9874`). Three
from [PS5CEMU-HAR](https://github.com/premohq/PS5CEMU-HAR) (`patches/mesa`, GPL-3.0):

- `0007`: a destroyed buffer's memory waits until the GPU is done with it, so freed memory
  still in use cannot fault the GPU and stop the console. Always on.
- `0008`: submissions handed to a thread of the driver's own (`RADV_PS5_SUBMIT_THREAD=1`).
- `0009`: how long a wait for the GPU spins and sleeps (`RADV_PS5_WAIT_SPIN_US`,
  `RADV_PS5_WAIT_SLEEP_US`).

And one of PS5SD's own:

- `0010`: a buffer that no free block of the pool holds is made of several, mapped one after the
  other at one address, and the heap budget is the sum of the free blocks with the driver's bytes
  counted once (docs/CONSOLE.md, "Buffers of several blocks"). Always on.

0008 and 0009 change nothing unless asked. On the console neither made generation faster
(2026-10-16, Qwen3.5 0.8B and 9B), so the app does not ask. Put the switches in
`/data/PS5LM/env.txt` to try them. To build RADV with the patches:

```sh
cd .deps/src/PS5_Mesa && git am ../../../patches/mesa/*.patch
(cd ../PS5_Vulkan && PS5_MESA_REVISION=$(git -C ../PS5_Mesa rev-parse HEAD) tools/build-radv.sh release)
```
