# When something goes wrong

## Zooming out

Zooming out draws the units as dots when the picture of them would take more
memory than the game can spare. The game keeps running. After you zoom back
in, that extra memory is given back.

If a box says Open Annihilation ran out of memory, close other programs and
start the game again. The box says how full the heap is. ErrorLog.txt, beside
the program, keeps the line "Out of memory! Your hard disk may be full".

A run can limit the memory the units' pictures take by setting
`OA_RENDERED_UNITS_BUDGET` to a number of bytes.

The `+stats` console command shows that memory on its "models" row: the
megabytes the units' picture holds, "MB of" the most it may take, and "dots"
when the last frame drew the units as dots.

## Downloads

A download that fails says why on its line in the Library, and **RETRY**
tries it again. Each failure also writes one line to the game's log, which
starts `open-annihilation: content: download of` and says what the
registry's server answered: the status, the content type and the length, or
the network failure. "Core Prime asked for a check the game cannot do. Try
again later." means the server answered with a check meant for a web
browser, which the game never answers: try again later
([when a download fails](downloads.md#when-a-download-fails)).

## The view stops scrolling at one edge

If moving the pointer to one edge of the screen stops scrolling the view,
while the other edges still do, start the game with `--trace-input`, play
until it happens, push the pointer against that edge, and send the game's
log. Its lines say where the game's window lies on its display, whether the
window holds the pointer, and whether the pointer left the window there
([what the lines mean](command-line.md#--trace-input)). Say also where
your taskbar is, whether a tool bar or overlay shows at that edge, and how
your displays are arranged.
