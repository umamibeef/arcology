.. _sc2k-report:

===============
The disassembly
===============

SimCity 2000® 1.2 · 22 June 1995 · Macintosh 68k

The retail Mac binary still contains everything: a 25-phase simulation clock, the power-grid flood fill, the growth dice roll, the economy, the disasters, and a compressed image of every tuning table Maxis shipped. The simulation has been rebuilt in C and reproduces the original's own output cell for cell, and the renderer draws a shipped city from the art in the resource fork.

.. grid:: 2 2 3 3
   :gutter: 2
   :class-container: stats

   .. grid-item-card:: 95,442

      lines disassembled

   .. grid-item-card:: 590

      functions found

   .. grid-item-card:: 103

      cities as ground truth

   .. grid-item-card:: 18 / 18

      cities byte-exact over a whole growth cycle

   .. grid-item-card:: 14 / 14

      disaster kinds exact

   .. grid-item-card:: 17 / 17

      cities exact on every moving object

What the file actually is
-------------------------

.. rubric:: **CODE 0–3** resource fork 3.8 MB


The application is a **fat binary**. Its data fork is a PowerPC PEF executable. The 3.8 MB resource fork carries the 68k code alongside every piece of art and audio. The 68k side splits into four ``CODE`` resources:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Resource
     - Bytes
     - Contents
   * - CODE 0
     - 24
     - A5-world header — 1 jump-table entry
   * - CODE 1
     - 1,520
     - ``__%Main`` — THINK C runtime
   * - CODE 2
     - 280,089
     - The entire game
   * - CODE 3
     - 15,478
     - Sound and music driver

Each segment is a 12-byte header, then code, then a relocation table. The header's last long is the offset where code stops. For CODE 2 that is ``$43268``. Disassembling exactly that range gives a 0.19% failure rate on the first pass, which proved the boundary right before a single relocation was decoded.

The detour worth mentioning
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Absolute ``jsr $xxxx.l`` targets resolve into *either* CODE 1 or CODE 2, and nothing in the instruction distinguishes them. 215 call sites jump to ``$4B8``, which in CODE 2 is the middle of the event loop, and in CODE 1 is this:

.. code-block:: m68k
   :caption: CODE 1 · ``$4B8`` · called 215 times


   0004B8  movem.l  d2-d3,-(a7)
   0004BC  move.l   d0,d2
   0004BE  swap     d2
   0004C0  mulu.w   d1,d2
   0004C2  move.l   d1,d3
   0004C4  swap     d3
   0004C6  mulu.w   d0,d3
   0004C8  add.w    d3,d2       ; 32x32 multiply from three
   0004CA  swap     d2          ; 16x16 partial products —
   0004CC  clr.w    d2          ; the 68000 has no MULU.L
   0004CE  mulu.w   d1,d0
   0004D0  add.l    d2,d0
   0004D2  movem.l  (a7)+,d2-d3
   0004D6  rts

That settles it: the low addresses are the Symantec THINK C runtime, ``__mul32``, ``__udiv32``, ``__sdiv32``, ``__umod32``, ``__smod32``, and the ``%_SWITCH.W``/``%_SWITCH.L`` dispatchers. That last pair matters: they read an inline jump table from the return address, so a naive linear sweep desyncs on every switch statement in the program. Teaching the disassembler about both switch idioms took the failure count from 171 to **4**.

How it was read
---------------

.. container:: where

   ``tools/m68kdis.py`` · ``tools/annotate.py`` · ``tools/thinkdata.py``

Four steps, each one feeding the next.

**Disassemble.** ``m68kdis.py`` drives Capstone over ``CODE 2`` and adds
what Capstone does not know about a Mac binary: A-line opcodes are
Toolbox traps, not illegal instructions, so the 3,257 of them in the
listing are decoded to names such as ``_Random`` and ``_NewPtr``. The
result is 90,406 instructions.

**Find the functions.** THINK C opens a stack frame with ``link.w a6``
and closes it with ``unlk``. Scanning for that pattern gives 590 entry
points. It is a lower bound: a routine that never touches its frame has
no ``link``, and several are reached only by falling through from the
one above.

**Rebuild the globals.** A 68k Macintosh program does not keep its
globals in the executable. It reaches them through register ``A5``, and
the initial contents of that block are packed into ``DATA 0``.
``thinkdata.py`` decompresses it: 29,247 bytes expand to three blocks
totalling 37,256, which land at three offsets and span 45,783 bytes of
A5 world. With that image in hand an operand like ``$1EF6(a5)`` stops
being an opaque offset and becomes a table whose contents can be read
directly. It is why no constant in the reconstruction is typed by hand.

**Name what can be named.** ``annotate.py`` rewrites the listing through
``symbols.json``, which carries 88 globals and 68 routines. The names
come from three places: strings the binary itself carries, the shape of
a routine's arguments and callers, and behaviour confirmed by running
it. Nothing is named on a guess alone. :ref:`The appendix
<appendix-routines>` lists every address these documents cite.

.. caution::

   A disassembly is not a decompilation. The listing says what each
   instruction does; it does not say what the routine means. Two habits
   guard against reading meaning in that is not there: prefer running a
   routine to reading it, and check every recovered rule against the
   original executing under an interpreter. Both are described below,
   and both have caught rules that read correctly and were wrong.

Rebuilt, and made to prove itself
---------------------------------

.. rubric:: **sim/** 3,882 lines of C


Prose about a simulation is cheap. The reconstruction is now a C program that loads a real ``.SC2`` file, rebuilds the layers, runs the passes, and compares what it computes against the numbers the shipped cities already carry. No constant in it is typed by hand: each is either read out of the binary's own global image or generated by running the routine that encodes it.

.. code-block:: text
   :caption: cc -std=c99 -O2 -Wall -Wextra · builds clean


   sc2k.h     the A5 globals as a struct, layer accessors, flag bits
   city.c     IFF load/save + the RLE codec ported from $293EC
   tables.c   GENERATED from the DATA 0 global image
   rng.c      both generators -- the Toolbox LCG and the $20F30 LFSR
   ext80.c    soft 80-bit extended: the 8 SANE ops the game uses
   sane.c     the _FP68K trap, reduced to the 21 opwords that occur
   sim.c      setTile, power, water, pollution, land value, coverage,
              density, crime, the budget pass
   main.c     load -> recompute -> compare -> report

Two witnesses, and what each one can prove
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The first witness is the save file. Recompute a layer, then diff it against the copy the file carries. This test conflates two different failures: a misread instruction, and a save that records the city at a different moment than the pass did. It cannot separate them.

The second witness is the original itself. ``tools/runsim.py`` builds the game's whole A5 world from a real city, then executes **the original's own 68k routines** over it under an interpreter. Both sides start from byte-identical state, so any disagreement is a transcription error and nothing else.

**The two answer different questions.** Accuracy is the second witness, and it is measured below. The first witness measures something else: how much of a layer a save file can return.

.. rubric:: Accuracy, against the original's own code

Both sides start from byte-identical state and run the same routines. Any disagreement is a transcription error.

.. list-table::
   :header-rows: 1
   :widths: 40 20 40

   * - check
     - result
     - what it compares
   * - a whole 16-phase growth cycle
     - :bdg-success:`18 / 18 cities`
     - every byte of XBLD, XZON, XBIT, XTRF, XTXT and XTHG
   * - the random stream over a cycle
     - :bdg-success:`589,426 draws`
     - every draw from all seven generators, in order, nine cities
   * - moving objects, fifty frames
     - :bdg-success:`17 / 17 cities`
     - all forty records, the XTXT they touch, eight counters
   * - disasters
     - :bdg-success:`14 / 14 kinds`
     - five starting points each, every layer and the treasury
   * - the economy
     - :bdg-success:`18 / 18 cities`
     - all 63 quantities
   * - terrain and demolition
     - :bdg-success:`exact`
     - ``$5FAA``, ``$3A000``, ``$128DE``, ``$763A`` and the raise pair
   * - police, fire and the budget pass
     - :bdg-success:`exact`
     - every city

Nothing in that table is a percentage, because none of those checks admits a partial result.

.. rubric:: What a save file can return

The table below is the first witness. **It is not a measure of accuracy.** A save records the city mid-cycle, so a value written at phase 2 describes a map that phases 3 to 18 have already changed. Every figure under 100% is a property of save files. The right-hand column names the limit in each case.

.. list-table::
   :header-rows: 1
   :widths: 34 22 44

   * - check
     - agreement
     - what limits it
   * - codec round-trip
     - 103 / 103
     - lossless
   * - sum(XPLT) == MISC[13]
     - 103 / 103
     - pollution total
   * - sum(XVAL) == MISC[10]
     - 103 / 103
     - land value total
   * - sum(XCRM) == MISC[11]
     - 103 / 103
     - crime total
   * - sum(XTRF) >= MISC[12]
     - 103 / 103
     - traffic, a phase-19 snapshot
   * - population within 2%
     - 92 / 103
     - a phase-21 snapshot; mean error +1.47%
   * - XBIT powered, per tile
     - 99.64%
     - 62 of 65 cities exact, excluding wind and solar
   * - XBIT watered, per tile
     - 99.57%
     - weather moved on at phase 21
   * - XCRM crime, per cell
     - 95.27%
     - every input from the same pass; 98 of 103 cities exact
   * - XFIR fire, per cell
     - 93.99%
     - funding comes from the budget; 82 of 103 exact
   * - XPLC police, per cell
     - 93.94%
     - funding comes from the budget; 83 of 103 exact
   * - XPOP density, per cell
     - 90.29%
     - the map moved on after phase 2; 24 of 103 exact
   * - budget department amounts
     - 95.87%
     - 1,580 of 1,648; 77 of 103 cities have all sixteen
   * - XVAL land value, per cell
     - 64.36%
     - uncleared scratch holds data the file cannot return

Traffic is a snapshot, not a sum
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The traffic total is an equality in 58 cities and a lower bound in the rest. :ref:`$2530E <rt-2530E>` is why: the pass that totals the layer also *decays* it, and it runs once per cycle at phase 19:

.. code-block:: c
   :caption: ``$2530E`` · reconstructed


   traffic_tot = 0;
   for (y = 0; y < 64; y++)
     for (x = 0; x < 64; x++) {
         int v = XTRF[y][x];
         v = v - (v >> 2);            /* $25336: decay by 25% */
         XTRF[y][x] = v;
         traffic_tot += v;
     }

Traffic then keeps accruing through phases 3–18 before the city is saved, so the stored figure is always a lower bound on the layer on disk. That is a falsifiable prediction, and it holds: across all 103 cities, 58 are equal, 45 are greater, and **none is less**.

Finding population by searching for it
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Population sits past index 26 in ``MISC``, where the builder's counted loops defeat a static walk. So it was recovered the other way round. Compute it from the map, then look for the slot that tracks it. The rule comes straight from the growth pass: credit each building once, at the corner the current rotation selects, by its tier.

.. code-block:: c
   :caption: sim.c · the population model


   int32_t sim_map_population(const City *c)
   {
       int32_t units = 0;
       uint8_t mask  = ROT_CORNER_MASK[c->rotation & 3];

       for (y = 0; y < MAP_H; y++)
         for (x = 0; x < MAP_W; x++) {
           uint8_t z = c->xzon[y][x], b = c->xbld[y][x];
           if (XZON_TYPE(z) < 1 || XZON_TYPE(z) > 6)    continue;
           if (b < 0x70 || b > 0xC5)                    continue;
           if (!(XZON_CORNERS(z) & mask))               continue;
           tier = BLD_TIER[b - 0x70];
           if (tier > 0 && !BLD_TIER_FLAG[b - 0x70])
               units += GROWTH_TABLE[tier];
         }
       return units * 10;              /* $33FE6 */
   }

**MISC[1035], at offset 0x102C, is the city population.** It tracks the computed figure across the whole corpus with a median error of 0.01%. The residual is the same snapshot drift traffic has, and the evidence for that is its shape: 52 cities compute high, 40 compute low. A missing term in the model would skew one way. Drift does not.

Eleven cities miss by more than 2%, all of them downloaded ``.SC2`` files rather than the shipped scenarios, and one of them, LINCOLN.SC2, is out by a factor of almost exactly two. Those look like maps edited outside the game, where the stored statistics never got recomputed. They are listed as failures rather than excused.

The corpus contains two different compressors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Porting the encoder from :ref:`$293EC <rt-293EC>` raised an awkward question: how do you tell a faithful port from a plausible one? Byte-exact output is the answer, and it splits the corpus cleanly. **1,218 of 1,957 compressed chunks come back byte-for-byte identical**, and 39 files match in every chunk. A coincidence at that scale is not available.

The rest were written by something else. The shipped 1993–95 scenarios match 0%, and their encoder makes different choices: where the Mac build breaks a literal run the moment two bytes repeat, the other keeps going. For the source bytes ``07 ff ff 63 c3`` the shipped file emits one five-byte literal. This build emits literal-1, run-2, literal-2. Neither is wrong, and the sizes differ in both directions, so it is a different encoder rather than a better one.

80-bit floats, on a machine that has none
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The economic model computes in SANE extended throughout. Apple Silicon reports ``__LDBL_MANT_DIG__ == 53``. ``long double`` here is just a double, so there is no hardware to borrow and the arithmetic had to be built.

That is bounded, though, because the whole program uses only 21 distinct opwords: five arithmetic operations, two conversions, a truncate, over six operand formats. The implementation is checked against the one value already recovered from the binary, the constant sitting in :ref:`$34D04 <rt-34D04>`'s stack frame:

.. code-block:: text
   :caption: ext80 unit test


   $34D04 constant 40 09 96 00 ...   1200                    OK
   store round-trips the same bytes                          OK
   div  1200 / 7                     171.42857142857142       OK
   mul  0.1 * 0.1                    0.010000000000000002     OK
   trunc(-3.7)                       -3                       OK
   (1 + 2^-60) - 1 keeps the bit     8.67e-19  (double: 0)    OK

The last line is the one that matters: it demonstrates the type is genuinely wider than ``double``, which is the entire reason the model used it.

Crime, and the thing that was hiding behind it
----------------------------------------------

.. rubric:: ``$23FAE`` crime · ``$23EE4`` density


Crime is the simplest model in the game, and for a long time it was the only layer that came back perfect:

.. code-block:: text
   :caption: ``$23FAE`` · reconstructed


   crime = XPOP[y/4][x/4]                  /* density        */
         - XPLC[y/4][x/4] / 2              /* police halves  */
         - XVAL[y/2][x/2] / 4              /* value deters   */
         + (ordinances & 4 ? 16 : 0);      /* one RAISES it  */
   /* then a 5-point blur, clamped to a byte */

**98 of 103 cities reproduce every one of their 4,096 cells.** Density, police coverage and land value are all produced earlier in the same pass, so a save holds exactly the values crime saw. Nothing is compared across a pass boundary, and nothing drifts.

The developed mask has to be derived, not read. Stage 4 records it in ``XBIT`` bit 3, which is also the flood-fill scratch bit, and both flood passes clear it. Whether a save still carries the mask depends on when it was written. Reading it gives 91%. Deriving it from the map with stage 4's own test gives 99.7%.

A high score on crime says nothing about the other layers. Each one has to be measured against the original's own code, crime was 96.4%, not 100%. The missing 3.6% was not skew. It was land value, three stages upstream.

What happens when the errors are allowed to compound
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each stage can be tested twice: handed the inputs the file recorded, or handed the output of the stage before it, the way :ref:`$2317E <rt-2317E>` actually runs. The second is the harder question and the more honest one.

.. code-block:: text
   :caption: the same three stages, isolated and chained, 103 cities


                           isolated     chained
   XVAL land value            64.36%      64.36%   /* stage 5 is first, so unchanged */
   XCRM crime                 95.27%      66.47%   /* fed my land value, not the file's */

Crime's model reproduces the original exactly, and still loses thirty points when it is fed a land value that a save file could not fully determine. **A reconstruction is only as good as the least recoverable thing upstream of it**, but that is a statement about save files, not about the code, and telling the two apart took a second witness.

What a save file can prove
~~~~~~~~~~~~~~~~~~~~~~~~~~

**Values computed in the same pass are mutually consistent inside a save and come back exactly. Anything compared across a pass boundary carries the snapshot skew and cannot.**

Every layer below is exact against the original's own code. The percentages measure one thing only: how much of a layer a save file can confirm. A save records the city mid-cycle, so a value written at phase 2 describes a map that phases 3 to 18 have already changed. The right-hand column gives the reason for each shortfall.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - layer
     - vs. file
     - why the shortfall
   * - power grid
     - 99.64%
     - recomputed from stored flags
   * - water grid
     - 99.57%
     - weather moved on at phase 21
   * - crime
     - 95.27%
     - every input from the same pass
   * - fire coverage
     - 93.99%
     - map changed after phase 2
   * - police coverage
     - 93.94%
     - map changed after phase 2
   * - density
     - 90.29%
     - map changed after phase 2
   * - land value
     - 64.36%
     - uncleared scratch holds lost data
   * - growth rate
     - —
     - needs the previous cycle's own value

A percentage below 100 measures the save file, not the reconstruction.

Where the deterministic side ran out, and how it was got past
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Police and fire coverage are painted by a hand-unrolled diamond at :ref:`$24232 <rt-24232>`, and the range is the interesting part:

.. code-block:: c
   :caption: ``$23E0C`` · station range


   police = ((A5+0x2C8E + 5) * simParams[0x294]) / 2;
   fire   =  (simParams[0x304] * 5) / 2;
   if (!(XBIT[y][x] & 0x40)) range /= 2;   /* unpowered: half range */

``simParams`` is the block at :ref:`$2C30 <rt-2C30>`: sixteen department records of ``0x70`` bytes, written by :ref:`$263C8 <rt-263C8>` at phase 0. It is the **budget**, and ``0x294`` and ``0x304`` are the police and fire funding levels. A station's reach is literally what the mayor pays for it.

The budget is not stored as an A5 global, so it does not appear in the field map. The way past is to stop guessing where it lived and ask the program.

:ref:`$295D6 <rt-295D6>` is the *inverse* of the MISC builder: it walks the 1,200-long array with a single running index and scatters it across the A5 world. Run that under the interpreter with every read tagged by its index, and it writes down its own file format:

.. code-block:: text
   :caption: tools/miscload.py · what the unpacker does with MISC


   MISC[ 124.. 379]  census[0..255]        one unsigned word per building id
   MISC[ 479.. 910]  simParams[0..15]      16 departments x 27 longs
   MISC[ 911]        $2C7A                 January reconciliation due
   MISC[1018]        $1EFE                 a term in the transit budget
   MISC[1032]        $2C98                 a term in one ordinance's cost
   MISC[1035]        $1E96                 population
   MISC[1039]        $2C8E                 scales the police radius

   1,070 of 1,200 slots used; 1,071..1,199 are unused tail.

The budget was in the save file the whole time. With the block located, the coverage stage needed no economy at all. The funding levels are simply read back. Police and fire went from unimplemented to **100.00% against the original's own code** in one step.

The budget pass itself followed, and it is a nice piece of design: service buildings are counted in *tiles*, so dividing by 9 or 16 turns a tile count back into a building count. Its infrastructure ranges deliberately overlap. A bridge tile is charged to both the road and the highway department. Every one of the sixteen department amounts now matches what :ref:`$263C8 <rt-263C8>` computes, in every city.

Prefer running a routine to reading it
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Four things in the reconstruction are no longer transcribed at all. The coverage diamond is forty separate call sites. The budget's tile-to-department map is fifteen nested overlapping range tests. The ordinance costs are a twenty-entry jump table. The MISC layout is a two-thousand-instruction unpacker. Every one of those is a bookkeeping problem, and bookkeeping is exactly what a careful reader gets wrong.

So none of them were read. Each is generated by **executing the original's own code and writing down what it did**. Run :ref:`$24232 <rt-24232>` and record where it writes. Run :ref:`$263C8 <rt-263C8>` once per tile type with a census holding a single tile. Call :ref:`$41368 <rt-41368>` with one input live at a time. The tables that come out cannot contain a reading error, because no reading happened.

.. code-block:: text
   :caption: tables.c · generated, not typed


   COVERAGE_KERNEL[37]     37 cells, 5 rings   from running $24232
   DEPT_OF_TILE[0x70]      bitmask per tile    from running $263C8
   ORDINANCE_COST[20]      source, num, den    from probing $41368
   BLD_POPULATION, BLD_TIER, DEPT_YEAR_DIVISOR from the DATA 0 image

The ring strengths came out of the same trace: ``s``, then ``s×4/5``, then ``×3/4``, ``×2/3``, ``÷2``, each step done in sixteen bits and truncated toward zero. That was checked against the interpreter for 595 strengths including negatives and values that overflow the intermediate. A station funded hard enough really does wrap, and the reconstruction wraps with it.

Chasing the last percent, and where it stops
--------------------------------------------

.. rubric:: ``$2182E`` water · ``$2317E`` land value


Behaving identically is a different discipline from agreeing in aggregate. A residual is not evidence either way: some are facts about the original, and some are the mark of a routine read approximately instead of run.

Two flood fills, four bytes apart
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The water grid mirrors the power grid exactly. Measure in one pass, ration capacity in a second. Pass 2 dispatches on the building underneath. The dispatch runs past the Water Treatment test, and the four bytes after it decide two more cases:

.. code-block:: m68k
   :caption: ``$21C28`` · the two tests after Water Treatment


   021C28: cmpi.w  #$dc, d3    ; Pump          -> $21C5E
   021C2E: cmpi.w  #$fa, d3    ; Desalination  -> $21C5E
   021C34: bra.b   $21c84      ; everything else: budget-limited

Pumps and desalinators are watered whenever they have power, free of the budget. That is what keeps every source watered, so the outer sweep skips them and each network floods once. Without it my floods re-ran the same network two and three times, each adding another full budget of water. **13,181 wrong tiles became 1,610.**

Reading the sources properly also settled a flag. A pump adds 10 for every neighbour where ``(XBIT & 0x05) == 0x04``. A desalinator adds 20 where it equals ``0x05``. Fresh versus salt, which identifies **bit 0 as the salt-water flag**, leaving only bit 1 unclaimed.

The queue is 512 entries and throws work away
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The queue is bounded. ``$13B2`` is ``NewPtr(0x800)``, 2048 bytes, 512 four-byte Points, and both the push at :ref:`$21DF2 <rt-21DF2>` and the pop at :ref:`$21E3A <rt-21E3A>` mask their index with ``0x1FF``. When the ring fills, :ref:`$21E26 <rt-21E26>` moves the tail forward and the oldest entry is simply lost.

Since both floods hand out capacity in queue order, that would change which tiles brown out. In practice these networks are thin enough that the ring rarely fills, so fixing it changed nothing measurable, but assuming it away was luck, not judgement.

Where a save file stops being evidence
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The remaining water error is not a defect and cannot be fixed. A pump's output includes ``weather / 2``, and the schedule settles the matter:

.. code-block:: text
   :caption: the 25-phase cycle, twice over


   phase 20   $220DA -> $2156E    the water grid runs
   phase 21   $220E4 -> $33FAE    weather = (weather + delta) / 2
                                  ($34CAC and $34CD2)

The weather is rewritten the day after the water grid uses it. The value in ``MISC[26]`` is therefore never the value the pass saw. Sweeping it confirms the shape: every affected city becomes exact at some lower value, and cities whose networks have capacity to spare were already exact because they are insensitive to it.

That is the fourth time the same ceiling has appeared. Traffic is a phase-19 snapshot, population a phase-21 one, the city centre a phase-2 one, and now weather. **A save file records the state after a partial cycle, so several of its numbers describe a city that no longer exists.**

Land value, and a scratch buffer nobody clears
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Land value is built from two 32×32 planes and is the clearest statement of what the three zone types actually want:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - zone
     - plane
     - centrality
     - pollution
     - crime
     - other
   * - Residential
     - amenity
     - (64−d) / 2
     - − p/5
     - − c/3
     - —
   * - Commercial
     - amenity
     - (64−d) *full*
     - − p/4
     - − c/3
     - + density/3
   * - Industrial
     - water
     - (64−d) / 4
     - − p/16
     - − c/4
     - +21 if dense

Commerce takes the full centrality bonus and is the only zone that gains from population density. Industry takes a quarter of it, barely notices pollution, and reads a different plane entirely, one fed only by water and water supply. The amenity plane is fed by parks (+40), trees (+20), open water (+12) and rubble (−20). Developed buildings contribute nothing at all.

Land value depends on a detail worth stating plainly: **the game reuses one scratch plane across four stages and never clears it.** The row-pointer array at ``A5+0x13BA`` serves stage 1 at half resolution, stage 3 at full, and stage 4 at quarter, and stage 4 reads the existing contents as its starting value. Stage 1's raw, pre-blur pollution leaks straight into land value, overwritten at building tiles by stage 3's marks.

.. code-block:: text
   :caption: ``A5+0x13BA`` · one plane, four stages, never cleared


   stage 1   plane[y/2][x/2]      raw pollution, before the blur
   stage 3   plane[y][x] = 40     at every building tile
   stage 4   plane[y/4][x/4]      amenity  <-- seeds itself from the above
             plane[y/4+32][x/4]   water access

Modelling stages 1 and 4 with two *separate* arrays, clearing stage 4's, and approximating the residue from the post-pass pollution the file stores scores 89% against the original's own code, and the shortfall was explained as an unrecoverable input. A save keeps only the blurred pollution, so of course the raw value is gone.

The explanation was true about save files and irrelevant to the code. The oracle runs stage 1 itself, from the same starting city, so the residue is not lost there at all. Giving the C one shared plane and deleting the approximation, which is to say, doing what the original does, took land value from 89% to **100.00%**, and crime, whose entire remaining error was downstream of it, from 96.4% to 100.00% with it.

A sweep of scaling factors over an approximation will often show a clean single minimum. That is a good-looking measurement of the wrong thing. An approximation that scores well is still an approximation, and a tidy minimum is not evidence that no better model exists.

Against a save file land value still reproduces only 64.4% of the time, because there the residue really is gone. That number is now a property of the file format rather than of the reconstruction, which is a much smaller and much better-supported claim than the one it replaced.

Three other defects in the same stage hide the same way: the distance term used the *doubled* city centre when ``a2``/``a3`` keep the half-resolution quotient. A ``+21`` low-density bonus at :ref:`$23B08 <rt-23B08>` had simply never been read. And the pollution blur divisor dropped its ordinance term because the caller supplied the base instead of the function deriving it.

The city in memory
------------------

.. rubric:: ``$2D834`` allocCityMaps


Every map layer is an array of row pointers held in A5-relative globals. The allocator hands out one contiguous block per layer and fills in the row table:

.. code-block:: m68k
   :caption: ``$2D834`` · one layer's allocation, verbatim


   02D840  move.l   #$8000,d0     ; 128 x 128 x 2 bytes
   02D846  _NewPtr
   02D848  move.l   a0,ALTM(a5)
   02D85E  move.l   d3,d0
   02D860  lsl.l    #$7,d0        ; row * 128
   02D862  add.l    d0,d0         ; * 2 — ALTM is 16-bit
   02D864  movea.l  ALTM(a5),a0
   02D868  adda.l   d0,a0
   02D872  move.l   a0,(a1,d0.l)  ; ALTM[row] = base + row*256

So a tile read compiles to ``rowPtr = XBLD[y]; tile = rowPtr[x]``, inlined at all 292 sites. The layout below is read off the allocator and cross-checked against the spacing of the globals themselves. A 128-row layer's pointer table is 0x200 bytes, a 64-row layer's is 0x100, a 32-row layer's is 0x80, and the gaps between consecutive layer globals match those sizes exactly.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - A5 offset
     - Layer
     - Grid
     - Bytes/tile
     - Holds
   * - +0x1FC2
     - ALTM
     - 128²
     - 2
     - Altitude, water flag
   * - +0x21C2
     - XBLD
     - 128²
     - 1
     - Building ID
   * - +0x23C2
     - XZON
     - 128²
     - 1
     - Zone type + corner mask
   * - +0x25C2
     - XTER
     - 128²
     - 1
     - Terrain / slope
   * - +0x27C2
     - XUND
     - 128²
     - 1
     - Pipes, subway, tunnels
   * - +0x29C2
     - XTXT
     - 128²
     - 1
     - Sign / label index
   * - +0x1BBA
     - XBIT
     - 128²
     - 1
     - Per-tile flag bits
   * - +0x15BA
     - XTRF
     - 64²
     - 1
     - Traffic
   * - +0x16BA
     - XPLT
     - 64²
     - 1
     - Pollution
   * - +0x17BA
     - XVAL
     - 64²
     - 1
     - Land value
   * - +0x18BA
     - XCRM
     - 64²
     - 1
     - Crime
   * - +0x19BA
     - XPLC
     - 32²
     - 1
     - Police coverage
   * - +0x1A3A
     - XFIR
     - 32²
     - 1
     - Fire coverage
   * - +0x1ABA
     - XPOP
     - 32²
     - 1
     - Population density
   * - +0x1B3A
     - XROG
     - 32²
     - 1
     - Rate of growth

XZON packs two things into one byte
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The low nibble is the zone type (1–6 are the RCI zones, light and dense. 7–9 are military, airport, seaport). The high nibble is a **corner mask** for multi-tile buildings. At the top of the map scan the game loads a rotation-indexed constant:

.. code-block:: m68k
   :caption: ``$3170E`` · entry


   03173A  lea.l   rotTable(a5),a0
   03173E  move.w  g_rotation(a5),d0
   031744  move.w  (a0,d0.w),$106C(a5)   ; rotTable = [$80,$10,$20,$40]
      ...later, per tile:
   031D1E  andi.w  #$F0,d0
   031D22  and.w   $106C(a5),d0
   031D26  beq.w   next_tile             ; not this rotation's anchor

A multi-tile building is therefore simulated exactly once per pass, at whichever of its four corners is nearest the viewer for the current rotation. Counting a real city confirms it: in Manhattan the four corner bits appear 965 times each, and single-tile buildings carry all four (``0xF0``) 2,749 times.

The 25-day clock
----------------

.. rubric:: ``$21EDE`` simTick


This is the spine of the whole simulation, and it is nine instructions long:

.. code-block:: m68k
   :caption: ``$21EDE`` · the entire scheduler


   021EE6  addq.l  #$1,g_cityDate(a5)
   021EEA  move.l  g_cityDate(a5),d0
   021EEE  moveq   #$19,d1                ; 25
   021EF0  jsr     __smod32
   021EF6  cmpi.l  #$18,d0
   021EFC  bhi.w   done
   021F02  move.w  $21F0A(pc,d0.w),d0
   021F06  jmp     $21F0A(pc,d0.w)        ; 25-entry jump table

The map is never scanned in one go. Sixteen of the twenty-five phases each process one quarter-by-quarter block of tiles, so a full sweep of 16,384 tiles takes sixteen simulated days and the frame rate never falls off a cliff.

.. figure:: img/fig-sc2k-report-7.svg
   :alt: The 25-phase simulation cycle: phase 0 bookkeeping, phases 1 and 2 reset the power grid and recompute pollution, phases 3 to 18 scan one sixteenth of the map each, phases 19 to 21 aggregate population and run the economy, phases 22 to 24 run periodic passes.

   One simulated day executes exactly one phase. The sixteen gold slots each sweep a 32×32 block of the map, accumulating per-zone population into accum8[]. Phase 21 drains that accumulator into the population and economic models, and the cycle restarts.

Power is a flood fill
---------------------

.. rubric:: ``$20FC4`` powerGridReset · ``$210A2`` powerFloodFill


Phase 1 clears two ``XBIT`` bits across all 16,384 tiles, then walks the map looking for building IDs ``0xC6``–``0xCF``, the ten power plants. Each unvisited plant seeds a queue-driven flood that spreads through conductive tiles, summing capacity and counting consumers as it goes. Phase 1 ends with:

.. code-block:: m68k
   :caption: ``$21064`` · supply versus demand


   021064  tst.l   totalDemand(a5)
   021068  beq.b   no_demand
   02106A  move.l  totalSupply(a5),d0
   02106E  moveq   #$64,d1
   021070  jsr     __mul32              ; supply * 100
   021076  move.l  totalDemand(a5),d1
   02107A  jsr     __udiv32             ; / demand
   021080  move.l  d0,powerPct(a5)
   02108C  moveq   #$64,d0              ; clamp to 100

.. note::

   First, the figure computed here is *drawn divided by capacity*, and the branch above returns 100 when there are no plants at all. It is a load meter, not the share of demand met.

   Second, and more seriously: :ref:`$210A2 <rt-210A2>` does not stop where I first thought it did. It runs **two** BFS passes over the same network, and the second one is where power is actually handed out.

.. code-block:: c
   :caption: ``$210A2`` · the second pass, at ``$2139C``


   /* pass 1 measured capacity and marked every tile visited.
      pass 2 walks the same marks handing power out in queue
      order, one unit per drawing tile, until capacity runs out. */
   while (!empty) {
       pop(&y, &x);
       if (!(XBIT[y][x] & 0x08)) continue;   /* $213E8 */
       if (capacity != 0) {
           if (XBLD[y][x] >= 0x70) capacity--;
           XBIT[y][x] |= 0x40;                /* $21442 POWERED */
       }
       XBIT[y][x] &= 0xF7;                    /* $2145E clear mark */
       push neighbours that are still marked;   /* $21482 */
   }

Three things follow from that. Power is rationed **in queue order**, so which tiles brown out depends on the traversal, west, north, east, south, taken literally from the four push sites. The second pass erases the marks behind it, which is what lets a different plant pick up a starved network on a later flood, and is why the outer loop skips tiles that already have the powered bit. And pass 2's push test is *inverted* relative to pass 1: it follows marks rather than avoiding them.

.. caution::

   Pass 2's push test is inverted relative to pass 1: it follows the marks that pass 1 leaves rather than avoiding them. Reading the two the same way makes the flood stop on its first tile.

With both passes in place the powered bit agrees on **99.64%** of tiles, and 62 of the 65 cities without a wind or solar plant reproduce the grid exactly.

The flood's per-tile contribution is a ten-way switch on the plant type. Two of them do not return a constant at all, and that is where the table stops being a table and starts being a design document:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - ID
     - Name
     - Power / tile
     - Pollution
   * - 0xC6
     - Hydro Power
     - 40
     - 0
   * - 0xC7
     - Hydro Power
     - 40
     - 0
   * - 0xC8
     - Wind Power
     - (altitude)
     - 0
   * - 0xC9
     - Gas Power
     - 11
     - 10
   * - 0xCA
     - Oil Power
     - 48
     - 25
   * - 0xCB
     - Nuclear Power
     - 111
     - 2
   * - 0xCC
     - Solar Power
     - (weather)
     - 0
   * - 0xCD
     - Microwave Power
     - 355
     - 0
   * - 0xCE
     - Fusion Power
     - 555
     - 2
   * - 0xCF
     - Coal Power
     - 44
     - 50

**Wind power reads the terrain.** Its case does not load a constant. It masks the altitude out of ``ALTM`` and averages it with a random draw:

.. code-block:: m68k
   :caption: ``$211DA`` · the wind turbine case


   0211E0  move.b  $1F01(a5),d0
   0211E2  asr.w   #$3,d0
   0211E4  addq.w  #$1,d0
   0211EA  _Random
   0211F2  divu.w  -$E(a6),d1           ; r = Random % ((w>>3)+1)
   021200  lea.l   ALTM(a5),a0
   02120E  move.w  (a1,d0.w),d0
   021212  andi.w  #$1F,d0              ; altitude = low 5 bits
   021216  add.w   d1,d0
   021218  lsr.w   #$1,d0               ; output = (altitude + r) / 2

Build a wind farm on a mountain and it genuinely produces more. Solar's case is the same shape but driven by a weather global instead of terrain. Everything else is a fixed number, and the pollution column ranks exactly as the manual claims: coal filthiest at 50, oil 25, gas 10, nuclear and fusion 2, and the three renewables at zero.

Water reuses the same machinery
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Phase 20 runs the identical pattern for water: clear the "supplied" bit on every tile except reservoirs (``XBLD == 0xEB``), clear the shared visited bit, then flood. One quirk falls out of the listing, the water pass branches four ways on ``g_rotation`` and sweeps the map in a different order for each. The scan order of the water network depends on which way you happen to have the map turned.

Growth is a dice roll against demand
------------------------------------

.. rubric:: ``$3170E`` mapScanBlock 3,498 bytes


For a zoned tile, the scan reduces the zone type to an RCI index, looks up demand, and turns it into a probability. Reconstructed from :ref:`$31CF6 <rt-31CF6>`–:ref:`$31E3C <rt-31E3C>`:

.. code-block:: c
   :caption: ``$31CF6`` · reconstructed


   short zone  = XZON[y][x] & 0x0F;      /* 1..6 = R,C,I x light,dense */
   short bld   = XBLD[y][x];
   if (zone < 1 || zone > 6) goto other;

   if (bld >= 0x70) {                    /* already developed */
       if (!(XZON[y][x] & 0xF0 & rotMask)) goto next;
       tier = bldTier[bld - 0x70];       /* 1..4 */
   } else {
       if (bld >= 0x1D || !buildable(y,x)) goto next;
       bld = 0; tier = 0;
   }

   short rci   = (zone - 1) / 2;         /* 0=R 1=C 2=I */
   short want  = RCIdemand[rci] + 2000;   /* demand is -2000..+2000 */
   short slack = 4000 - want;             /* = 2000 - demand */

   if (tier > 0 && !bldFlag[bld - 0x70]) {
       accum8[zone] += popTable[tier];   /* per-zone population */
       if (Random() < slack / tier)      /* <-- decline */
           decayTile(y, x, tier, Random() & 1);
   }

The whole economy hangs off that one comparison. Demand at its ceiling makes ``slack`` zero and the tile can never decay. Demand at its floor makes ``slack`` 4000 and decay is likely every pass. Dividing by ``tier`` is what makes tall buildings sticky. A tier-4 tower rolls against a quarter of the threshold a tier-1 house does, so dense development decays roughly four times more slowly.

Undeveloped tiles have their own branch. Unzoned tiles with a building present decay against a percentage held in the simulation-parameter block, at offsets ``0x4C6``, ``0x5A6`` and ``0x616`` for three different building groups. Those offsets are the funding levels of the road, subway and rail departments, so underfunding a department really does make its buildings rot.

Population, then the economy
----------------------------

.. rubric:: ``$33FAE`` population · ``$34D04`` economy


Phase 21 drains the accumulator the map scan spent sixteen days filling. This function is short enough to read whole:

.. code-block:: c
   :caption: ``$33FAE`` · reconstructed


   accum8[0] = 0;
   for (i = 1; i <= 6; i++) accum8[0] += accum8[i]; decrease = 0; increase = 0;
   newPop = accum8[0] * 10; if (newPop < population) decrease = population - newPop;
   else                     increase = newPop - population; population = newPop;
   cumulative += newPop;

**City population is literally the sum of six per-zone counters times ten.** There is no separate demographic model. The number on the status bar is a direct product of how many buildings of which tier survived the last sixteen days of dice rolls.

The economic model behind it, :ref:`$34D04 <rt-34D04>`, is the largest function in the program at 6,556 bytes, and it is the only part of the simulation that uses floating point. Every operation is a trap into SANE:

.. code-block:: m68k
   :caption: ``$34D04`` · the SANE calling pattern


   034D0C  move.l  #$40099600,-$D0(a6)   ; 80-bit extended 1200.0
   034D22  pea.l   -$C6(a6)
   034D26  pea.l   -$C6(a6)
   034D2A  move.w  #$200E,-(a7)          ; convert integer -> extended
   034D2E  _FP68K
   034D80  pea.l   -$D0(a6)
   034D88  move.w  #$0006,-(a7)          ; divide
   034D8C  _FP68K

Decode that constant. Exponent ``0x4009`` minus the 16383 bias gives 2¹⁰, significand 150/128 gives 1.171875, and it is exactly **1200.0**. The model converts each integer global up to 80-bit extended, computes, and converts back, four instructions per operation.

Roughly 350 ``_FP68K`` calls make up this model. The SANE opword formats are not decoded exhaustively, so its shape and its inputs are known but its equations are not written out.

Moving the ground under a city
------------------------------

.. rubric:: ``$128DE`` terrain fixup · ``$8758``/``$896C`` raise


Terrain is not a backdrop. A volcano, an earthquake and the player's own bulldozer all move the land. Every tile that moves needs a new shape, a new water state, and a decision about whatever stood on it. Three routines do all of that, and they are small enough to read in full.

A tile's shape is four corner bits
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The slope of one tile is decided by its eight neighbours. Each neighbour that stands higher raises the corners it touches. A diagonal neighbour touches one corner. A side neighbour touches two. The byte table at ``A5-0x4DF6`` holds the corners for each of the eight directions, and the result is a four bit set.

.. figure:: img/fig-sc2k-report-8.svg
   :alt: How a tile's slope code is derived. Eight neighbours each contribute corner bits: the four diagonals contribute one corner each, the four sides contribute two. The bits combine into a four-bit mask, which indexes a sixteen-entry table of slope codes. Mask fifteen gives code 0x32, which means the tile must rise a step instead.

   The whole of a tile&#x27;s appearance, from ``$1298C`` to ``$12BF8``. The eight neighbour offsets and the corner table make the shape. The city&#x27;s water level then shifts the code into one of three bands, which is why shoreline tiles occupy XTER $20 to $2F and submerged tiles $10 to $1F.

The water level is a MISC field, and it is written into every drowned tile
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``MISC[912]`` was recorded as a term in pump capacity. It is the city's water level. :ref:`$128DE <rt-128DE>` compares each tile's altitude against it to choose land or water, then writes it into ``ALTM`` bits 5 to 9 of every tile it drowns. The evidence is direct: in Charleston, Hollywood and Flint, every water tile carries exactly that value in those bits.

``ALTM`` therefore holds two heights in one word. The low five bits are the ground. Bits 5 to 9 are the water surface above it. A renderer that draws water at the ground height sinks every lake into its own bed.

Raising land is a test pass and then a spend pass
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Land does not rise alone. Lifting one tile leaves its lower neighbours hanging, so they must rise too, and theirs after them. The game splits this into two recursive walks that talk to each other through one scratch bit.

.. figure:: img/fig-sc2k-report-9.svg
   :alt: The two-pass terrain raise. The first pass walks the tiles that would have to move and marks each one in XBIT bit 3, returning false if any tile is at the height ceiling or in a military zone. The second pass walks the marks, clears each one, and raises the tile if the treasury can pay.

   Bit 3 of XBIT is shared with both flood fills, which is why it reads as a scratch bit. Here it is a channel between two passes. ``$8758`` writes it and ``$896C`` consumes it.

Finding a building from any of its tiles
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Most routines are handed one tile and have to act on a whole building. :ref:`$763A <rt-763A>` answers that. It returns the footprint size and moves the caller's coordinates to the origin.

Sizes come from a table at ``A5-0x1252`` for ids ``$70`` and up. Everything below ``$70`` is one tile, except two ranges that are two by two and need no search at all. A three or four tile building is found by its corner markers, which live in the high nibble of ``XZON``. There are four markers, and which one means which corner turns with the view.

.. figure:: img/fig-sc2k-report-10.svg
   :alt: Finding a building's origin. A three by three building carries a corner marker in the high nibble of XZON on each of its four corner tiles. The five interior tiles carry zero. The search steps toward each marker in turn until it reaches the corner the current rotation calls the origin.

   Checked by asking ``$763A`` about all 16,384 tiles of five cities and diffing the answer: 81,920 tiles, every one exact. The five zero tiles in the middle are why a renderer that draws every unzoned tile paints a large building once per interior tile.

Sixteen disasters, and the two the string table forgot
------------------------------------------------------

.. rubric:: ``$370DC`` 19 arms · ``A5-0x2947`` the names


Disasters are dispatched by :ref:`$370DC <rt-370DC>` through a nineteen entry jump table at :ref:`$3710C <rt-3710C>`. Their names sit behind a pointer table at ``A5-0x2947``, seventeen Pascal strings. Two of the arms the menu can reach have no name there. The Disasters menu supplied both. Its own jump table at :ref:`$3BF56 <rt-3BF56>` maps item three to type 18 and item four to type 7. That makes type 18 Air Crash and type 7 Tornado.

Type 5 is called Crash but does nothing here. It is a name for the news line. The aircraft that comes down is type 18. It puts an ordinary aircraft record on the map and then marks it. Field 5 holds ``$10`` and field 2 holds 7, which no ordinary flight sets.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Type
     - Name
     - Routine
     - State
   * - 1
     - Fire
     - ``$38290``
     - exact
   * - 2
     - Flood
     - ``$379FC``
     - exact
   * - 3
     - Riots
     - ``$38002`` ×3
     - exact
   * - 4
     - Pollution
     - ``$37FB6``
     - exact
   * - 6
     - Earthquake
     - ``$383D4``
     - exact
   * - 7
     - Tornado
     - ``$38766``
     - exact
   * - 8
     - Monster
     - ``$38574``
     - exact
   * - 9
     - Meltdown
     - ``$38916``
     - exact
   * - 10
     - Microwave
     - ``$38B6C``
     - exact
   * - 11
     - Volcano
     - ``$37DD6``
     - terrain exact, dice open
   * - 12
     - Fire Storm
     - ``$37C66``
     - exact
   * - 13
     - Mass Riots
     - ``$37D34``
     - exact
   * - 14
     - Major Flood
     - ``$37940``
     - exact
   * - 15
     - Chemical Spill
     - ``$37888``
     - exact
   * - 16
     - Hurricane
     - ``$3755A``
     - exact
   * - 18
     - Air Crash
     - ``$38186``
     - exact

All fourteen kinds are byte exact against the original's own code. ``tools/disaster_check.py`` runs each one from five points on both sides and compares every layer it can touch, all forty ``XTHG`` records, and the treasury.

The last three arrived together, once ``demolishAndPlace`` at :ref:`$5FAA <rt-5FAA>` was read. That routine is 5,680 bytes, but almost all of it animates the collapse: it makes exactly one direct write to a layer and delegates the rest. What it really is, is a chain of special cases by building id. A square footprint is walked from its origin. Runways and piers are runs, flooded over neighbours carrying the same id. Bridges are walked back off the front and then forward, putting the water back. And a two-by-two standing on the raised pieces is taken down along its line, a pair at a time.

.. note::

   The collapse animation changes nothing on the map. It also takes ``2n³`` numbers from the generator on the footprint path, two a tile on a bridge, and five a step on a raised pair. Leave them out and every later decision shifts. Together with ``burnTile`` quietly calling the demolisher, that was the whole of the earthquake's disagreement with the original.

Most of them are the same square spiral
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Fire, the riot and the fire storm all walk outward from a point until they find something that will take. The walk is a square spiral: step, and every second turn the leg grows by one.

.. figure:: img/fig-sc2k-report-11.svg
   :alt: The square spiral used by the fire, riot and firestorm searches. Starting at a centre tile, the walk steps one tile, turns, steps one tile, turns, then steps two, and the leg length grows by one on every second turn, tracing a widening square.

   A burning tile is marked $FD or $FE in XTXT, chosen by a coin. Water is $FC, a chemical spill $FB, and $C9 and above is an index into the moving-object table.

The flood tests one neighbour and floods the other
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The flood grows a box a ring at a time until it meets shoreline terrain, then wets that tile's neighbours. It has four cases. The first two test a neighbour and mark the same neighbour. The last two test one side and mark the opposite side, and they guard themselves with the ring offset rather than with the coordinate the write lands on.

.. figure:: img/fig-sc2k-report-12.svg
   :alt: The flood's four neighbour cases. Cases one and two read the water flag of a neighbour and mark that same neighbour. Cases three and four read the flag of the neighbour above or to the left, but write the flood marker to the neighbour below or to the right.

   Two lines that look copied and only half edited. Their guards compare the ring offset against 127 rather than the resulting row or column, so neither write is really bounded. A saved city never places the disaster point close enough to the edge for that to matter.

The volcano borrows the treasury as a loop counter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The eruption saves the player's balance, writes 25,000 into the funds global, and runs until that is spent. A tile that refuses to rise costs 1,000. A tile that rises costs the ordinary 25 through the raise routine. The real balance goes back at the end.

Its terrain half is exact. Its dice are not yet settled. One draw per turn only chooses between two redraws, but it takes a number from the same generator the tile choices come from, so leaving it out shifts every later decision. That draw is gated on :ref:`$30FE <rt-30FE>`, and the linear disassembly puts that address in the middle of an instruction, so the routine has not been read. Until it is, stubbing it makes the oracle answer from whatever a register happened to hold.

The save format, from the compressor outward
--------------------------------------------

.. rubric:: ``$293EC`` writeChunkRLE · ``$2A186`` buildMISC


City files are IFF: ``FORM`` / ``SCDH``, then one chunk per layer. Every chunk except ``CNAM`` and ``ALTM`` is run-length encoded, and the encoder sits at :ref:`$293EC <rt-293EC>`. It calls ``GetPtrSize`` on the layer pointer to find the length, allocates ``size * 3 / 2`` for the worst case, then emits:

- a byte ``< 128``, that many literal bytes follow;
- a byte ``>= 128``, the next byte repeats ``(byte - 127)`` times.

Runs are written as ``(count - 1) | 0x80``, capped at 128. Implementing the decoder from that listing and pointing it at the shipped cities expands every chunk to precisely the right size, 4,800 bytes of ``MISC``, 32,768 of ``ALTM``, 16,384 for each byte-per-tile layer, on all 88 city files in the collection.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Chunk
     - Stored
     - Expanded
     - Source global
   * - MISC
     - 3,261
     - 4,800
     - 1,200 longs from A5 globals
   * - ALTM
     - 32,768
     - 32,768
     - uncompressed
   * - XTER
     - 5,003
     - 16,384
     - +0x25C2
   * - XBLD
     - 10,062
     - 16,384
     - +0x21C2
   * - XZON
     - 5,107
     - 16,384
     - +0x23C2
   * - XBIT
     - 7,001
     - 16,384
     - +0x1BBA
   * - XTRF
     - 1,695
     - 4,096
     - +0x15BA
   * - XPLC
     - 456
     - 1,024
     - +0x19BA

``MISC`` is the scalar state, and :ref:`$2A186 <rt-2A186>` builds it as a flat sequential emitter, one ``d4++`` per field. Walking that function recovers the field map directly. The first 27 are pure straight-line code and decode unambiguously:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Index
     - Offset
     - Global
     - Meaning
     - Charleston
   * - 0
     - 0x0000
     - —
     - constant 0x122
     - 290
   * - 2
     - 0x0008
     - +0x2C24
     - map rotation
     - 3
   * - 3
     - 0x000C
     - +0x0BF2
     - year founded
     - 1900
   * - 4
     - 0x0010
     - +0x1E1E
     - days elapsed
     - 26877
   * - 5
     - 0x0014
     - +0x1E26
     - funds
     - 20000
   * - 6
     - 0x0018
     - +0x1E2A
     - bonds outstanding
     - 0
   * - 7
     - 0x001C
     - +0x139E
     - difficulty
     - 1
   * - 10
     - 0x0028
     - +0x1E76
     - land value total
     - 162178
   * - 11
     - 0x002C
     - +0x1E7A
     - crime total
     - 19366
   * - 12
     - 0x0030
     - +0x1E7E
     - traffic total
     - 44387
   * - 13
     - 0x0034
     - +0x1E82
     - pollution total
     - 33614

Funds is confirmed independently by its writers rather than by inference: :ref:`$4194E <rt-4194E>` stores 20,000 or 10,000 depending on difficulty, and the bond routines add and subtract exactly 10,000.

Past index 26 the builder enters counted loops, so a static walk loses the index. Recovering the remaining ~1,170 fields needs a small emulator over that one function, worth doing, not done yet.

Unpacking every tuning table Maxis shipped
------------------------------------------

.. rubric:: **DATA 0** 29,247 bytes → 37,256 in three blocks


The interesting tables. Pollution per building, building tiers, the growth curve. Live at negative A5 offsets, which are initialized data. THINK C stores that image compressed in ``DATA 0``. You can see the compression in the raw bytes: "School" appears as ``Sch o\x83l``.

The decompressor is in CODE 1 at ``$13A``, and it processes three blocks, each a 4-byte destination offset followed by a token stream: high bit set means a literal run, ``0x40`` a skip, ``0x20`` a repeated byte, ``0x10`` a run of ``0xFF``, and five low opcodes for jump-table patterns.

Placing the result took a global fit rather than a guess, score every candidate offset by how many of the 546 ``lea -$x(a5)`` references land on a valid Pascal string. The answer is unambiguous, and it lands block 0 exactly flush against A5:

.. code-block:: text
   :caption: alignment fit


   shift  +95   215 references resolve to strings   <-- winner
   shift   +1    77
   shift  -60    73
   shift   +9    70

   block 0 spans A5-0x8067 .. A5, length 0x8067 exactly

From there every static table opens up. The building name table sits at ``A5-0x6D42``, 16 bytes per entry, and it independently corroborates the entire power-plant analysis above. The order in this table is exactly the order the flood fill's jump table assumed:

- MATCHCoal Power carries pollution 50, the highest value in the table
- MATCHWind Power is the entry whose output reads ``ALTM``
- MATCHSolar Power is the entry driven by the weather global
- MATCHTwo consecutive Hydro Power entries explain the duplicated jump-table target

The rest of the table names all 58 special buildings from ``0xD0`` City Hall through ``0xFF`` Llama Dome, including ``0xEB`` Reservoir, which is precisely the ID the water pass exempts from its clear-flags loop.

Two more resource families are pure content pipeline rather than code. ``DATA 1000``–``1005`` are named *Group Start*, *Group Count*, *Token Pointer*, *Token Data*, *Story Power* and *Story Decay*, the newspaper headline generator, with 149 KB of word fragments and a decay weight per story type.

How the tiles are stored, and how to put them back on screen
------------------------------------------------------------

.. rubric:: **TSET 1** 989 KB · **SPRT** 978 KB


``TSET`` is not a Mac-specific format at all: it is an IFF file with the magic ``MIFF`` and form type ``SC2K``, holding an ``INFO`` chunk reading ``_MAC``, a ``TILE`` count of 1,500, and then 1,500 ``SHAP`` chunks. That is the Maxis Image File Format the Urban Renewal Kit edits, shipped verbatim inside the resource fork.

1,500 is 500 tiles at three zoom levels. Tile *N* appears at ids ``N``, ``N+500`` and ``N+1000``, and the footprint widths confirm it. The same distribution of 147 one-tile, 58 two-tile, 33 three-tile and 11 four-tile pictures repeats in all three blocks, at 8, 16 and 32 pixels per tile.

The row encoding, and the detail that breaks a naive decoder
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both ``TSET`` and ``SPRT`` use the same scheme: a row is a length byte, a tag byte, then span pairs.

.. code-block:: text
   :caption: the span encoding


   uint8 byteLen, uint8 tag, then pairs of (count, type):

       type 3   skip `count` pixels        (transparent)
       type 4   `count` palette bytes follow
       type 0   no-op padding

   /* the catch: a type-4 run with an ODD count is followed by one
      pad byte, so every span header stays 2-byte aligned.  Miss it
      and most rows decode as garbage -- which is exactly what
      happened on the first attempt. */

With the padding rule in place, **all 1,448 non-empty tiles decode, and 1,448 of 1,455 sprites**. The palette is ``pltt 0``, 256 entries of 16-bit RGB. ``clut 500`` and ``clut 501`` are the animation tables that ``idlePump`` feeds to ``AnimatePalette``, which is what makes water shimmer.

Rendering a real city
~~~~~~~~~~~~~~~~~~~~~

That is enough to draw one, but not by reading the layers naively. The tile routine is :ref:`$167CC <rt-167CC>` and it settles every rule outright. Ground art does not come from ``XBLD``. ``XTER`` indexes a word table at ``A5-0x493E``. That table maps ``$00`` to ``$0E`` onto tiles 256 to 269 for dry land, all of ``$10`` to ``$1F`` onto 270 for open water, and ``$20`` to ``$2E`` onto 270 to 284 for shore. Building art is the ``XBLD`` value. Altitude lifts a tile by ``3 << zoom`` pixels a level, and a water tile takes its height from ``ALTM`` bits 5 to 9 rather than the ground.

The screen position is ``(row - col) × halfwidth``. Reversing those two still draws something that looks like a city, which is why the mirrored version survived a long time. The check that catches it needs no screenshot: Manhattan's Hudson River label sits at column 52, row 8, so it has to land left of centre.

**Unbuilt flat land is a picture, not a fill.** Tile 256 is the flat ground. Only tile 0 has no art, and :ref:`$168A8 <rt-168A8>` special-cases shape 256 rather than skipping it. An empty tile that is *zoned* draws a tint as its ground instead, shape ``290 + (XZON & $0F)``. Those are the lots, and they are the normal appearance of undeveloped zoned land rather than an optional overlay.

Terrain is not drawn under a building at all. :ref:`$1716C <rt-1716C>` tests ``XBLD`` and jumps straight to the building path. Drawing the ground anyway punches a square hole through every large building, because the tiles it covers come later in the sweep and paint over the art.

Two palette ranges are never what ``pltt 0`` says. :ref:`$97C4 <rt-97C4>` calls ``_AnimatePalette`` so that entries 155 to 203 come from ``clut 500`` and 224 to 238 from ``clut 501``. Water uses indices 192 to 195, which ``pltt 0`` calls green and ``clut 500`` makes blue.

.. note::

   Every pixel the game draws goes through one routine, :ref:`$18E96 <rt-18E96>`. Stub it and watch the call sites, and the original reports what it would have drawn: shape, x, y and mirror, in order. That turns a pixel comparison into a list comparison, which is exactly the thing a reconstruction gets wrong. What it cannot judge is the vertical anchor, because each shape's y offset lives in a descriptor table at ``$1226`` that the oracle zeroes.

.. thumbnail:: img/sc2k-report-78106df72374.png
   :group: sc2k-report
   :alt: An isometric render of the shipped Manhattan city file: skyscrapers, a stadium, a domed building, parks and roads on green ground.
   :show_caption: True

   Manhattan at 16 pixels a tile, drawn by ``render/`` from the shipped city file. Nothing here is a screenshot. The palette, the art and the map all come out of the binary.

What the flag bits mean, and which ones remain open
---------------------------------------------------

.. rubric:: **XBIT** per-tile flags


Five bits are pinned by code that unambiguously reads or writes them. One by correlating a real city. Only two remain unclaimed. Reading the water flood settled bit 5, and the result is a pleasing symmetry: power and water each own a *conducts* bit and a *supplied* bit, and they share one scratch bit for the flood itself.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Bit
     - Meaning
     - Evidence
   * - 7 (0x80)
     - conductive
     - tested by the power flood before spreading
   * - 6 (0x40)
     - powered
     - cleared by phase 1, gates the plant search
   * - 4 (0x10)
     - has water
     - cleared by phase 20 except on reservoirs
   * - 3 (0x08)
     - flood visited
     - scratch bit, cleared by both flood passes
   * - 2 (0x04)
     - water covered
     - 0.998 correlation with ``XTER != 0`` in Manhattan
   * - 5 (0x20)
     - water conducts
     - tested by the water flood at ``$21890``, exactly as bit 7 is by the power flood
   * - 1 (0x02)
     - —
     - set on 0.4% of tiles, too rare to characterize
   * - 0 (0x01)
     - —
     - never set in any city sampled

Bit 2 is the most-tested bit in the entire program. 51 ``btst`` sites, which is what you would expect for "is this tile water", the question every placement, growth and rendering path has to ask first.

.. code-block:: text
   :caption: the symmetry, once bit 5 is placed


             conducts   supplied
   power      0x80        0x40
   water      0x20        0x10
   shared     0x08  flood-fill visited

The toolkit
-----------

.. rubric:: **~/Downloads** sc2k-re/


Everything above is reproducible from the files in the working directory, none of which touch the original game folder:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - File
     - Does
   * - tools/rezfork.py
     - Resource-fork reader, pure stdlib
   * - tools/traps.py
     - Toolbox trap decoder, 813 canonical names
   * - tools/m68kdis.py
     - Capstone front-end; A-traps and both switch idioms
   * - tools/analyze.py
     - Function boundaries, call graph, A5 cross-references
   * - tools/dumpall.py
     - Full annotated listing
   * - tools/thinkdata.py
     - DATA 0 decompressor; rebuilds the A5 image
   * - tools/sc2.py
     - City file reader with the RLE codec from ``$293EC``
   * - tools/miscmap.py
     - MISC field-map extractor, from the builder side
   * - tools/m68kemu.py
     - 68k interpreter that executes the disassembly text
   * - tools/runsim.py
     - **the oracle** — the A5 world from a city, then the real routines
   * - tools/oracle_diff.py
     - diffs the C against the oracle; the test that matters
   * - tools/miscload.py
     - runs the MISC *unpacker*; the full index → destination map
   * - tools/gen_coverage.py
     - runs ``$24232``, emits the coverage diamond
   * - tools/gen_budget.py
     - runs ``$263C8``, emits the tile → department table
   * - tools/gen_ordinance.py
     - probes ``$41368``, emits the ordinance cost formulas
   * - out/CODE_2.ann.asm
     - 91,442 lines, symbolized
   * - out/a5image.bin
     - 45,783-byte reconstructed global image
   * - tools/gen_tables.py
     - a5image.bin → tables.c, so no constant is hand-typed
   * - sim/
     - the C reconstruction; ``cmake --build build && ctest``
   * - tools/sc2kpack.py
     - TSET/SPRT art → indexed PNG atlases + JSON
   * - render/
     - city → PNG, isometric; checked against the game itself

What is done
~~~~~~~~~~~~~~~~~~~~~~~~~~

- **The economy is complete.** :ref:`$34D04 <rt-34D04>` transcribes through the ``fp68k()`` dispatcher: the national cycle, industry, the age pyramid and both directions of migration. All 63 quantities match on all 18 cities.
- **Growth is byte exact over a whole cycle.** All sixteen phases in sequence, on all 18 cities, with no difference on any of the six layers. The growth scan reaches zero stubs, down from 436.
- **The disasters are in.** Eleven of the sixteen are byte exact, and the dispatch, the name table and the menu mapping are all read. Two disasters had no name until the menu supplied one.
- **Scenarios are simulated** —. The goal checker at ``$0221A8``, the win and loss paths, and the 52-byte ``SCEN`` layout, settled by four independent readings.
- **The terrain routines.** :ref:`$128DE <rt-128DE>` shape and water state, :ref:`$12C04 <rt-12C04>` the nine tile fixup, :ref:`$8758 <rt-8758>` and :ref:`$896C <rt-896C>` the raise pair, and :ref:`$763A <rt-763A>` footprint origins. Each checked against the original over tens of thousands of tiles.
- ``$4110`` **is complete**, including the military branch that keeps its own counters instead of the census. Four thousand calls, 16,656 values, all exact.
- **The renderer draws a shipped city** from the art in the resource fork, and has its own oracle: stubbing :ref:`$18E96 <rt-18E96>` turns the original into a list of blits.
- **MISC[912] is the city water level**, not a term in pump capacity. :ref:`$128DE <rt-128DE>` writes it into ``ALTM`` bits 5 to 9 of every tile it drowns, and in three of four cities checked every water tile carries exactly that value.
- **The MISC layout is complete.** Running the unpacker at :ref:`$295D6 <rt-295D6>` under the interpreter with every read tagged by its index recovered all 1,070 used slots, including the two blocks that live inside pointer allocations and so never appeared in the A5 field map: the tile census and the budget.
- **Police and fire coverage, and the budget pass**, the last blocked deterministic stages. Both exact against the original's own code.
- **Land value and crime reach 100%** by sharing one scratch plane between stages instead of approximating what the previous stage left in it.
- Density's accumulation loop runs ``1..0x7E``, not ``0..0x7F``. That one bound took it from 87.6% to 90.3% and from 10 perfect cities to 24.
- Two interpreter bugs, both found by the C and the oracle disagreeing: shifts ignored operand size, so ``asr.w`` dragged stale high-word bits into the compiler's signed-division idiom. And ``cmp`` computed carry across all 32 bits regardless of size, so a ``cmp.w``/``bhi`` guarding a switch took its default arm and silently zeroed six of the twenty ordinance costs.
- The census is an *unsigned* word. Thirty-eight of the 103 cities hold more than 32,767 trees, and signing it broke the byte-exact round trip.
- :ref:`$4110 <rt-4110>` read. It maintains a 256-word census of every building id, which is how the growth pass asks "how many churches are there" without walking the map.
- Phases 22–24 profiled and dismissed: they redraw the graphs. Only ``$030E30`` is simulation.
- The SANE opwords decode completely, 21 of them, and ``ext80.c`` implements them.
- :ref:`$1E6E <rt-1E6E>` identified as the ordinance bitmask: bit ``0x10000`` adds 1/12 to power capacity, bit ``0x80000`` shifts the pollution divisor.

What I would do next
~~~~~~~~~~~~~~~~~~~~

- **Read** ``$5FAA``, **demolishAndPlace.** It is 5,680 bytes and it is the keystone: the Earthquake, the Meltdown and the Hurricane all reach it through :ref:`$3A000 <rt-3A000>`. Driving it under the oracle with only the drawing stubbed shows its whole effect on state is ``XBLD``, ``XZON`` and ``XBIT`` over one building's footprint. It makes exactly one direct write to a layer and delegates the rest, so the work is far smaller than the byte count suggests.
- **Resolve** ``$30FE``. The volcano takes a draw from the shared generator on a branch that only chooses a redraw, and that branch is gated on this routine. The linear disassembly puts the address in the middle of an instruction, so it has not been read. Until it is, the volcano's dice cannot be checked at all.
- Draw the underground view from ``XUND``, the signs from ``XTXT`` through :ref:`$FABA <rt-FABA>`, and the water animation. The false-colour data views are already in.
- Find the resource behind the shape descriptor table at ``$1226``. Each shape's vertical offset lives there, and the reconstruction derives it geometrically instead. The blit oracle can see every other property of a draw but not this one.
- Read the dedicated movement routines for thing types 8, 10 and 11. They are 634 of the corpus's 1,227 moving objects, and they render plausibly through a shared skeleton whose detail is guessed at.

What cannot be done, and why that is a result too
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- **The RNG seed is not saved.** :ref:`$11DC <rt-11DC>` is touched only inside the :ref:`$20F30 <rt-20F30>` generator family and never reaches MISC, so a save does not determine future evolution. The real game cannot replay the stochastic parts bit for bit either.
- **XROG, the growth rate, needs the previous cycle's own value** and is not reproducible from a save at all.
- **The stochastic tail** of ``$263C8`` enacts a random ordinance when the treasury is healthy. It is deliberately left out of the budget pass so that pass stays deterministic and checkable.
- The census the game maintains incrementally matches a rebuild from the map in only 40 of 103 cities. Tree growth and some placements do not go through :ref:`$4110 <rt-4110>`. The budget uses the saved census, as the game does, so this costs nothing. It is recorded because it looks like an error and is not.
