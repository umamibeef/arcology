# Plans

The work in progress, one area a page.  A plan says what is to be built, and its status page where the
build stands against it, and nothing else: the rules are `CLAUDE.md`'s, the facts of what is built are `docs/`, and an
open item no plan covers is a line in `TODO.md`.

| page | what it holds |
|---|---|
| `network.md` | the road network's design, layer by layer: what it IS, never where the build stands |
| `network-status.md` | where the build stands against that design, its breaches, and what is left to build |
| `interchanges.md` | how highways meet: the movements an interchange files, the shape it is laid out in, its levels |
| `drawing.md` | how the shaped network is laid as surfaces: what the drawing reads, and the rules it keeps |
| `terrain.md` | the land: a height per tile, the dual ground drawn over it, cliffs, and the region generator |
| `limits.md` | a city's limits, the map and the city, and the region's chunks |

Three rules keep a plan honest:

* **The design is never written from the code.**  Where the build differs from a design page, the difference
  goes on its status page as a breach.
* **Progress lives on the status page alone.**  A design page, like every page but a status page and
  `TODO.md`, states what is and never how far along the work is (`CLAUDE.md`, comment rule 6).
* **A finished plan is deleted.**  Its design goes into `docs/` as fact, its open items into `TODO.md`.
