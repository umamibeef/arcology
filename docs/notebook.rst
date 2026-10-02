.. _notebook:

The reconstruction notebook
===========================

.. note::

   This page is a notebook of findings, kept as they were written. It is
   exempt from the "current fact" rule that holds everywhere else in
   these documents and in the code: a finding here may narrate the work,
   quote a tick count, or name the city it was found on. A finding
   graduates from here into :ref:`sc2k-report` or :ref:`mechanics` when
   it is rewritten as a standing fact.

The clock and its phases
------------------------

Drive the passes in the clock's order, not one at a time
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``oracle_diff.py`` compared the layers after running ``$2317E`` **alone**,
on both sides.  That is a state the game never reaches: the clock runs
``powerGridReset`` (``$21FA6``) and ``waterGrid`` (``$220DA``) in the ticks
before ``cityScanPass`` (``$21FB0``), and they leave state the scan reads.

Running the scan on its own hid a missing line for a long time.
``$217D2`` sets **A5+0x2CA0, the water-treatment flag**: each Water
Treatment plant (``census[0xF4]``, 2x2, so ``>> 2``) covers 2000 units of
what the network actually delivers, and once the plants cover the whole
delivery the flag goes up.  ``$23308`` then adds it to the pollution blur
divisor, so **treatment shows up as less pollution**.

``sim_water_grid`` computed ``water_pct`` and never set the flag.  With the
scan driven alone that costs nothing, because the flag keeps its saved
value.  Run the water phase first, as the clock does, and pollution on
``1898`` falls to **39.6% of live cells matching**.  The flag has to be
set **before** ``water_pct`` is rescaled, because ``$217EC`` compares
against the raw delivered total rather than the percentage.

Both grid phases are otherwise exact: after either one, XBIT is
**byte-identical** on all 16384 tiles.

``oracle_diff.py`` now runs both grids on both sides.  Keep it that way.
When a pass reads state another phase writes, drive them in the clock's
order or the comparison certifies a state the game never has.

Drive the whole clock, not one pass at a time
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tools/clock_check.py`` runs ``sim_tick`` and ``$21EDE`` the same number of
times from the same city and compares **every layer and every scalar**.
It exists because every other checker drives one routine, and a pass
checked alone cannot show anything that travels between phases.

On its first run, at one tick, it found two things:

- ``developed``, ``power_pct``, ``water_pct`` and ``unemployment`` are **saved**
  -- ``MISC[1067]``, ``[1048]``, ``[1049]``, ``[1001]``, indices from
  ``out/miscload.json`` -- and ``city_load`` was not restoring them.  A
  loaded city started with less state than the original has.
- ``$2351A`` **clears** ``A5+0x11D0`` before the loop at ``$23614`` raises it.
  ``build_planes`` did not.  That costs nothing while the count starts at
  zero, and doubles it the moment the saved value is restored, which
  halves the three averages that divide by it.

Those two are a matched pair, and the second only bites once the first
is right.  That is the shape of the whole class: state that is saved,
restored, and recomputed has to be cleared where the original clears it.

At 25 ticks it found two more, both in ``populationPass``:

- ``$34032`` splits the head count three ways --
  ``rci_pop[i] = accum8[2i+1] + accum8[2i+2]`` -- and ``$3402E`` adds the
  new population to ``MISC[16]``.  ``sim_population`` stopped before both.
  Graph series 1 to 3 read zero until this was filled in.

- ``$345DA`` sets the three zone departments' tax base to
  ``rci_pop[i] * 10``, and ``$34602`` adds ``misc[1032]/6`` to department 0
  and ``misc[1032]/12`` to departments 1 and 2.  Missing, so those three
  amounts kept whatever the save loaded, and ``accrued`` drifted with
  them.

The plant rolls are unconditional
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$211B2`` (solar) and ``$211EA`` (wind) draw first and divide afterwards.
The port had ``span ? Random() % span : 0``, which skips the draw when
the span works out at zero.

That cost nothing until the weather was ported, because the solar span
is ``(100 - weather2) / 10`` and reaches zero as soon as cloud cover
passes ninety -- which a static ``weather2`` never did.  Now it does, and
a skipped draw puts the stream out.

The guard belongs on the division alone, where the original would trap,
not on the draw.

**Watch for this shape.**  A guard that skips work when a value is zero
is a guard that also skips a die.  The two are only the same thing while
the value can never be zero.

The plant roll is sixteen bits, not fifteen
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$211B6`` and ``$211EE`` clear a register and move the whole word into it,
so the value the divide sees runs 0..65535.  The port masked with
``0x7FFF``, losing the top bit and giving a different remainder for half
of all draws.  ``(uint16_t)Random()``, not ``Random() & 0x7fff``.

The power ordinance boosts the draw, not the capacity
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$21354`` adds the raw generated total to ``g_powerCapacity`` (``A5+0x11D6``)
and **only then** does ``$2135C`` test the ordinance bit and add a twelfth
to the working total.  The order is the whole point: the extra twelfth
raises the ceiling consumers are clamped against at ``$21374``, it does
**not** raise the capacity the percentage divides by at ``$2107A``.

Boosting first inflates capacity by exactly ``13/12``, so the reported
figure comes out low by the same ratio.  That is what it looked like:
Bayview 24 against 26, HILTON 64 against 69, capacities ``10003`` against
``9235`` and ``12309`` against ``11363`` -- ``1.0833`` every time.

The bits stay right while the totals go wrong, which is why XBIT
compared byte-identical throughout.  ``power_pct`` was reached only
through graph series 8, so nothing looked at it until ``$22330`` was
ported.

``power_capacity`` (``A5+0x11D6``) and ``water_capacity`` (``A5+0x11D2``) are
real globals and are now fields rather than locals, so a checker can
read them.

The season is derived, not saved -- and the oracle needs it too
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``A5+0x1E34`` is the season, ``((month + 1) % 12) / 3``.  Neither it nor
the month is in MISC: ``updateWindowTitle`` computes both from the date
at phase 0.

``runsim`` derived the month and the year count and left the season at
zero, which put the weather walk on the wrong row of its table.  Hawaii
then read ``weather_state`` 1 where the model said 0, and it looked like
the weather port was wrong.  It was not: the oracle was starting cold.

Deriving it in ``_globals`` takes Hawaii to **complete exactness, dice
included**.

This is the third time an unset piece of oracle state has read as a
model bug -- after the NULL pointers and the unseeded LFSR.  When a
comparison fails on something the save does not store, suspect the
harness first.

The weather bytes are read unsigned
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``A5+0x1F00..0x1F03`` are bytes and every read of them zero-extends
(``moveq #0,d0; move.b``).  A checker that sign-extends reports
``temperature`` as -125 against 131 -- the same byte, described two
different ways.  ``clock_check`` honours the signed flag now.

Two blind spots in the checker, and what they were hiding
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``clock_check`` compares every layer and every scalar -- except it did
not.  Two things were missing, and each was hiding a real divergence.

**XTXT was not in the layer list.**  It ran ALTM, XBLD, XZON, XTER,
XUND, XBIT, then straight to XTRF, and neither side dumped it.  That is
exactly the wrong layer to leave out: XTXT carries the sign index and,
on a special building's tile, the XMIC record that owns it (``$33 +
slot``).  ``sim_alloc_micro`` (``$EEAE``) is a stub returning 0 where the
original returns that marker, and a wrong marker changes XTXT **and
nothing else**.

It is in the list now, and the 25-tick corpus still passes -- which says
something narrower than it looks.  The specials the growth scan places
are runways (``$DD``) and cranes (``$E0``), and ``MICRO_CLASS`` gives both a class
of zero, so neither allocates a record.  The stub is invisible in the
corpus because nothing there exercises it, not because it is harmless.

**The age pyramid was not in the scalar list either.**  Twenty brackets
of heads, education and life expectancy -- the whole of the population
model's state -- went uncompared, because nothing reads it back as a
headline number.  A drift of one or two people sat there for sixty ticks
and only surfaced when it changed a branch.

Watch the shape of that: ``A5+0x1EDE``, ``0x1EE2`` and ``0x1EE6`` are **three
separate pointers to three arrays of twenty**.  In MISC the same numbers
are interleaved three longs to a bracket from ``MISC[31]``, which is what
``economy.c``'s HEADS/EDUQ/LIFE index.  Comparing 60 longs from the first
pointer reads one array and then forty longs past its end.

With both in, Bayview is exact at **84 ticks: 1020 of 1020 scalars, the
pyramid included**.

Where the clock stands
~~~~~~~~~~~~~~~~~~~~~~

Six cities, 25 ticks -- one whole turn of the clock:

.. list-table::
   :widths: auto

   * - layers
     - **335928 / 335928** live cells
   * - scalars
     - **5760 / 5760** exact
   * - dice
     - **6 of 6** cities identical
   * - matching completely
     - **6 of 6**

Bayview, Centerville, Hawaii, BIGGESTC, Happyland and 1898 agree with
the original on every layer, every scalar and every die across a whole
month.  There is no remaining difference to describe.

Three things had to be found to get the last three cities there, and
they are worth keeping because each has a different shape.

**Two months are exact now too.**  At 50 ticks Bayview matches on every
layer, every scalar and every die.  Getting there took one more routine,
and finding it is a good advertisement for the ``trail`` ring.

At 50 ticks the port used to end **one draw short**, first differing at
draw 41929 on tick 38, with the stream otherwise aligned.  A watch on
the LFSR's RTS captured ``e.trail`` at that draw and it read
``$10254..$10278 -> $11104 -> $1110C`` -- ``rngMod(0x96, 0x14)`` inside
``$0101AC``, a routine with exactly one caller, ``$26442``, **inside
budgetPass**, that nothing in the port referenced at all.

That routine is the year-end microsimulation pass.  See below.

**Bayview is exact to tick 75** -- three whole months, layers, scalars
and dice.  At 100 it parts company: the scalars are still 960 of 960 but
five XBLD cells and 96 XTRF differ, and the dice diverge at **draw
101548, on tick 89**, with the SAME total count on both sides.  Not a
missing draw this time: at that index the original is rolling wind
plants at ``$211EA`` (phase 1) and the port is somewhere else entirely, so
a branch went the other way and the counts happened to line up again.

Hawaii and Happyland are worse at 100 -- 2838 of 2880 scalars across the
three, Hawaii's differences being the departments and ``crime_tot``.
Hawaii's differences are the departments and ``crime_tot``, which is what
a divergence in the year-end settlement looks like from downstream.
That is the next thread to pull, and the technique is the one that
found the microsim: watch the LFSR's RTS, capture ``e.trail`` at the
first differing draw, and read off the caller.

(``1898`` cannot be run past about tick 80 yet: the clock reaches an
unhandled ``_GetPort`` at ``$F72``.)

The February poll is 100 draws of real state -- ``$3152A``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Centerville was exact through tick 22 and ran **exactly 100 draws
short** at tick 23, all at one site.  Phase 0 calls ``$3152A`` when
``month == 2``, and the note in ``sim_tick`` said it "ranks the city's
problems for the newspaper" and touched nothing.  That was wrong twice
over: it draws a hundred times and it writes three globals.

It is a poll, not a calculation.  Seven complaints get a weight,
contentment gets a weight of its own, and a hundred imaginary citizens
are drawn from the distribution:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - weight
     - from
   * - traffic
     - ``graph[TRAFFIC][0]``, low word
   * - pollution
     - ``graph[POLLUTION][0]``, low word
   * - crime
     - ``graph[CRIME][0]``, low word
   * - taxes
     - ``dept[0].funding * 3``
   * - unemployment
     - ``unemployment``, low word
   * - education
     - ``max(0, 100 - MISC[19])``
   * - health
     - ``max(0, 70 - MISC[18])``
   * - *contentment*
     - ``50 + graph[VALUE][0]``

Contentment is the whole of the poll's optimism, and it is why a rich
city polls well with problems: land value runs in the hundreds while
the complaints run in the tens.  The results sort worst-first into
``A5+0x106E`` and ``A5+0x107C``, and the approval percentage lands in
``A5+0x108A`` -- confirmed by ``$303D8``, which formats it as two decimal
digits for the newspaper.

Two traps in the read.  The three map averages are read as **words from
offset 2 of a long** (``$31548``), so only the low sixteen bits count,
while contentment takes the whole long.  And the tax term loads ``a0``
and ``a1`` from the same pointer and adds ``$64(a0)`` twice (``$31568``), so
it is three times the residential rate, not the sum of three rates.
Reading that as R+C+I would be wrong on any city whose rates differ.

None of the three globals is in MISC, so a freshly loaded city polls as
if nobody had ever been asked.

Why 1898 is the one city that fails
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``sim_place_special`` refused ``$DD`` and ``$E0`` on the strength of a comment
saying they "are never reached from the growth scan".  That is wrong.
The airport ladder at ``$32372`` asks for ``$DD`` and the seaport ladder at
``$321FE`` asks for ``$E0``, and the original lays both.

So on a city with a large airport the original grows runways and the
port does not.  ``1898`` has **1359 zone-8 tiles** against Bayview's 39.
The first tile where it shows is ``(41, 11)`` in phase 10: the original
turns it into a runway, this leaves it as ``$EA``, and from there the two
dice streams are one draw apart -- at ``(41, 15)`` the original sees a
runway and rolls a second time, the port sees ``$EA`` and does not.
Everything after that is downstream of one refused placement.

Tracked down by marking every tile the scan enters and lining the two
draw counts up per row.  The skew appears at one tile and nowhere
before it, which is what a single missing placement looks like.

Both are ported now.  ``$33844`` lays a runway as a five-tile strip whose
direction comes from the parity of ``census[$DD]`` and of the tile's own
coordinates; ``$33A90`` reaches a crane's pier four tiles into whichever
neighbour is water.  With the runway in, 1898 runs **six whole ticks
with every layer, every scalar and every die identical**, where before
it parted company on draw 190 of tick 1.

Two tables the original reads past the end of
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

What broke next was subtler.  At tick 7 one scalar differed --
``accum8[5]``, 198 against 13306 -- with every layer and every die still
identical.  Watching the additions showed 17 tiles contributing 39
between them and **one tile contributing 13108**.

That tile is ``(35, 45)``: a runway (``$E4``) standing on an industrial
zone.  ``$31D2E`` reads the tier as ``words(A5-0x1570)[id - 0x70]``, and
the table has 86 entries while the index here is 116.  It reads past
the end into neighbouring A5 data and gets **514**.  ``$31DF4`` then
credits ``GROWTH_TABLE[514]`` -- past that table's 32 entries as well --
which is **13108**.

Neither read is a bug to be tidied away.  The data is stable, the
original acts on it, and the reconstruction has to see the same thing.
So ``gen_tables.py`` now reads 144 entries of the tier and flag tables
and 515 of GROWTH_TABLE, and ``Building.tier`` is a word rather than a
byte because 514 does not fit in one.

The lesson generalises: when a table is indexed by something wider than
the table, do not clamp and do not assume zero.  Read what the original
reads.

infra[] was never restored, and the runway took the blame
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Turning the runway on made Centerville and Happyland worse, which
looked like the port being wrong.  It was not.

``A5+0x1EFA`` is sixteen word counters, one per id ``$DD..$EC``, saved as
``MISC[1002..1017]``.  The growth scan only ever adjusts them by one as
buildings come and go, so ``city_load`` starting them at zero left the
military and airport ladders reading nothing.  Centerville's stage-3
ladder wants ``infra[3] >> 2 >= (infra[1] + infra[2]) / 5``; the original
has ``8 >> 2 = 2`` against ``(14 + 1) / 5 = 3`` and says no, the port had
``0 >= 0`` and said yes -- so it asked for a runway where the original
asked for ``$F6``.

While the runway placement refused, that wrong answer cost nothing and
Centerville compared clean.  Porting the placement made a latent bug
visible, which is the usual way round: **a regression that appears when
you fix something is often the fix exposing what was already broken.**
Check what the new code depends on before doubting the new code.

With ``infra`` restored, Centerville is exact again with the runway in.

The corner bits go round the block, not across it -- ``$328EC``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The last three cells in the corpus were one church in ``1898``::

        want            got
     16  128          16   32
     32   64          64  128

XZON's high nibble marks the four corners of a footprint, and the codes
go **counter-clockwise from the top-left**: ``0x10`` top-left, ``0x20``
bottom-left, ``0x40`` bottom-right, ``0x80`` top-right.  Every placer in
the game writes them in that order and the port did too -- except the
church, whose four writes are unrolled in the original (``$328EC``,
``$32916``, ``$32938``, ``$3296E``) and which the port had rewritten as a
tidy raster loop over ``dy`` and ``dx``.

Raster order puts ``0x20`` and ``0x40`` on the wrong diagonal, which the
renderer reads as a church facing the other way.  It cost three cells
out of 335928 and it is exactly the kind of thing a loop looks like an
improvement over.

The city scan
-------------

Two measurements, and they answer different questions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reconstructed and verified: the file format and RLE codec, the power and
water grids, pollution, land value, crime, population density, police and
fire coverage, the city centre, the population model, and the monthly
budget pass.

Two measurements, and they answer different questions. Both count **only
cells non-zero in one or both** — all-cell scoring is inflated by empty
countryside and must not be quoted.

**Against the original's own code** (``oracle_diff.py``, 18 shipped cities,
same starting state, so this is purely a test of the transcription):

.. list-table::
   :header-rows: 1
   :widths: auto

   * - layer
     - agreement
   * - XPLT pollution
     - **100.00%**
   * - XVAL land value
     - **100.00%**
   * - XPOP density
     - **100.00%**
   * - XPLC police coverage
     - **100.00%**
   * - XFIR fire coverage
     - **100.00%**
   * - XCRM crime
     - **100.00%**

.. list-table::
   :widths: auto

   * - economy, 37 quantities
     - **18 / 18 exact**
   * - economy, 20 age brackets x 3 series
     - **18 / 18 exact**
   * - growth, whole 16-phase cycle
     - **18 / 18 cities byte-exact**, zero differing bytes

and every one of the sixteen budget department amounts matches the
original's own ``$263C8`` in every city.  ``economyPass $34D04`` is complete
end to end -- indicators, the events block, the era interpolation, the
industry EMA, the labour market, mortality, ageing, births and both
directions of migration -- verified with ``tools/econ_check.py``, which
diffs all sixty-three quantities against the routine running under the
interpreter.

**Against the save files** (``--verify``, 103 cities). Lower, and the gap
between the two tables is the genuine snapshot skew:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - layer
     - exact
     - mean error
   * - power (all tiles)
     - 99.64%
     - —
   * - water (all tiles)
     - 99.57%
     - —
   * - crime
     - 95.27%
     - 0.6 / 255
   * - fire coverage
     - 93.99%
     - 2.2 / 255
   * - police coverage
     - 93.94%
     - 4.0 / 255
   * - density
     - 90.29%
     - 2.5 / 255
   * - land value
     - 64.36%
     - 4.7 / 255

The claim that turned out to be wrong
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Earlier work blamed every shortfall on snapshot skew. That was wrong.**
The first attempt explained every shortfall as *snapshot skew* — a save
records a city mid-cycle, so values written by an earlier phase describe
a map later phases already changed. Running the original's own ``$2317E``
under the interpreter showed that story was largely a rationalisation.
It hid four real bugs in land value alone: the distance term used the
**doubled** city centre; a ``+21`` low-density bonus at ``$23B08`` had never
been read; the pollution blur divisor dropped its ordinance term; and —
the last one — **pollution and land value were given two separate
scratch arrays where the original has one**.

That last one is the interesting one. ``$2317E`` shares a single 128-row
word plane between its stages and never clears it, so stage 4 seeds its
land-value accumulators from stage 1's raw pollution and stage 3's
building marks. Modelling those stages with separate arrays and
*approximating* the residue got land value to 89% against the oracle and
looked like a plausible ceiling. Sharing the plane, as the original
does, took it to 100% — and crime, whose remaining error was entirely
downstream of it, to 100% as well. An approximation that scores well is
still an approximation; the fix was to stop approximating.

**Cross-check both directions.** And when an oracle number is *worse*
than the save-file number, suspect the harness first: coverage scored
34% against the oracle and 94% against the files, because ``runsim.py``
was leaving the budget block zeroed.

The corpus, under the clock's real phase order
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``oracle_diff.py`` over all 85 readable cities, running the two grid
phases before the scan as the clock does:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - layer
     - live cells
     - share
   * - XPLT
     - 178863 / 178863
     - 100.00%
   * - XVAL
     - 182206 / 182206
     - 100.00%
   * - XPOP
     - 46810 / 46810
     - 100.00%
   * - XPLC
     - 44336 / 44336
     - 100.00%
   * - XFIR
     - 33955 / 33955
     - 100.00%
   * - XCRM
     - 80308 / 80308
     - 100.00%

**566478 live cells, every one of them.**  This is the stricter test --
the one that caught the water-treatment flag -- not the old scan-alone
comparison.

XROG is written by the density stage
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$23F30``, inside ``$2317E``, keeps the rate of growth as a weighted
average of the CHANGE in density, seven parts running value to one part
new, offset by 128 so a quarter holding steady reads as the middle of
the range::

    XROG = (XROG * 7 + (new - old) * 8 + 128) / 8

and it is computed **before** the new density overwrites the old.
``sim_density`` never wrote the layer at all.  It was the last one that
still differed.

The city centre is stored at twice what cityCentre returns
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$23506`` doubles both before storing.  ``sim_city_centre`` hands back the
half-resolution centre, which is what the land value stage wants, and
``A5+0x867E``/``0x8680`` hold it in whole tiles, which is what the disasters
want.

They are restored from MISC at ``$29F18`` as ``MISC[1030]`` and ``[1031]``,
which ``_globals`` already handles -- see the note on the signed
displacement under the emulator for why they looked untagged.

The economy and population
--------------------------

populationPass is four passes, not one
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$33FAE`` runs to ``$34D02`` -- 1057 instructions.  Only the first fifth of
it counts the population.  The rest:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - from
     - what
   * - ``$34068``
     - the R/C/I demand model
   * - ``$34792``
     - the forest
   * - ``$348F0``
     - the newspaper's dice
   * - ``$34C58``
     - the weather

**The demand model.**  For each zone it works out how much room the city
has, divides by what is already built, and moves demand by the
shortfall: ``demand += trunc(600 * (room/(pop+1) - 1) + DEMAND_TAX[rate])``,
clamped to +-2000.  Room for residents is the workforce plus a fiftieth
of the residents; commerce and industry both scale the industrial head
count by last month's residents over the workforce, then by their own
multiplier -- city size over 150000 for commerce, ``DEMAND_LEVEL`` for
industry.

Then four ceilings, and they are the interesting part: **amenities cap
residential demand** (stadium, marina, zoo, a third of the parks),
**runways cap commercial**, **cranes cap industrial**.  Ordinance bits
nudge the tax rate the table is read at rather than the demand itself.

**The forest** (``$34792``) picks one tile a month and then its neighbour
in a random direction.  Trees advance a stage, bare ground puts out a
sapling, water grows nothing.  It is why a city left alone turns green.

**The newspaper** (``$348F0``) draws fourteen to eighteen numbers a month
purely to decide headlines.  None of it changes the model except the
deadline sweep at ``$34BF4``, and all of it has to be drawn anyway -- the
train horn's lesson at a larger scale.

**The weather** (``$34C58``) is twelve states walking an eight-way table
that changes with the season, then cloud, wind and temperature each
moving half way toward the new state.  This is not scenery: the wind
plant turns ``weather1`` into output and the solar plant reads
``weather2``, so ``power_capacity`` moves every month.

Two things in that tail are deliberately left out.  ``$2EDE4`` and
``$2EECC`` are the newspaper itself, and ``$366A0`` -- reached only when a
deadline expires -- copies an eighteen-long template from ``A5-0x12E2``
to ``A5+0x5CE``, skipping index 5.  Neither draws a die.

The float sequence in populationPass is a red herring
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$34544`` converts ``rci_pop[i] + 1`` to extended, runs it through five
``_FP68K`` operations and lands a single at ``-$62(a6)``.  It looks like it
is computing the tax base directly above the store that sets it.  It is
not: the float result goes to a **local array at -$18(a6)**, and
``$345E0`` overwrites the same scratch long with the plain integer
``rci_pop[i] * 10`` before ``$345F2`` stores it.

What the floats actually compute is a growth ratio, roughly
``previous / (rci_pop[i] + 1) - 1``, kept for the demand model.  **That
part is not ported.**

The economy's decline event was in the wrong place
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$35464``, immediately after the four-indicator loop in ``economyPass``,
rolls ``Random() & 0x3F``.  One month in sixty-four it comes up zero,
rolls again for one of the four, and knocks that indicator back to
three quarters (``$354AE``) and its partner to a half (``$354FC``).  Both
multipliers are **doubles**, format ``0x0800`` rather than ``0x1000``.

It was ported all along -- and sitting **after the employment block**
rather than where ``$35464`` is.  Two dice in the right order but the
wrong position, so every roll between the loop and the employment block
came out one place shifted.

Moving it to its own address takes **Bayview and Hawaii to complete
exactness, dice included**.

Two attempts were wasted on this before the position was noticed: the
first added a second copy and was reverted for making things worse, the
second added the same copy again.  Both times the routine was read and
the file was not.  **Before porting a routine, grep the C for its
address.**  ``$35464`` was already there.

The economy's decline event
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$35466``, after the four-indicator loop in ``economyPass``, rolls
``Random() & 0x3F``.  One month in sixty-four it comes up zero, rolls
again for one of the four, and knocks that indicator back to three
quarters (``$354AE``) and its partner to a half (``$354FC``).  Both
multipliers are **doubles**, format ``0x0800`` rather than ``0x1000``.

It is ported.  An earlier attempt was reverted because it appeared to
make Hawaii worse; that was the same trap as the runway, and the
reasoning then was wrong.  With the surrounding bugs fixed, the C now
matches the original draw for draw through the whole economy pass --
``$34F44`` once, then ``$3513E`` and ``$35318`` twice per live indicator,
then the event -- and Hawaii's state stays exact at three, five and
seven ticks.

The lesson is the same one twice over: **a correct port that seems to
make a metric worse is usually breaking a pair of cancelling errors.**
Check what it depends on and what it exposes before reverting it.

With the position corrected, Bayview and Hawaii match the original
completely, dice included.

The clock reaches interface the model leaves out, so the checker carries
a stub list -- ``$1DEA`` is the QuickDraw screen-device helper, ``$3E03E``
and ``$3C8E6`` are chart drawing.  Stub whole routines, never traps: the
interpreter refuses an unknown trap on purpose, because guessing a
Pascal stack effect corrupts the frame silently.

A block that draws no dice is invisible to a dice check -- ``$35CAE``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

With the poll in, five cities were exact and ``1898`` still had 891 of
1953 XPLT cells too high, 188 XVAL too low, 55 XCRM too high -- **and
identical dice**.  Dice-identical is not proof of completeness.  It only
proves that everything which *consumes randomness* is right.

The XPLT deltas scaled like ``want/9``, which is a divisor about ten per
cent small.  The pollution blur divides by
``4 - MISC[1037] + MISC[1043]``, and ``MISC[1037]`` was never written --
``$35CAE``, a block inside ``economyPass`` that my port jumped straight
over, from ``$35CA0`` to ``$35D92``.  It computes two indices and draws
nothing::

    base  = rci_pop[2] + 1
    dirty = 100 * (ind[0] + ind[1] + ind[2] + ind[5]) / base
    MISC[1037] = dirty < 20 ? -1 : (dirty - 20) / 30      /* $35CE4 */

    worst = max over the eleven of 100 * ind[k] / base
    MISC[1036] = worst < 20 ? 0 : (worst - 20) / 5        /* $35D42 */

So a mostly-clean city scores ``-1`` and gets **one more unit of
division** in the pollution blur; a city running on the dirty six --
industries 0, 1, 2 and 5 -- scores 0, 1 or 2 and gets less.  ``1898`` is
clean, the port never gave it its ``-1``, and its pollution ran ten per
cent high for the whole run.  ``MISC[1036]`` is only read by the
newspaper.

The same block ends with the case nobody hits: a city whose population
has reached zero wipes the three interleaved age arrays and returns
(``$35D58``).

**The lesson is the checker's, not the code's.**  A pure computation
that is missing moves no die, so every RNG comparison passes.  Only the
layer-and-scalar diff finds it, and it only found it here because a
divisor is a multiplier on every cell -- had the block fed something
sparse, it could have sat there indefinitely.

Where tick 85 goes wrong
~~~~~~~~~~~~~~~~~~~~~~~~

The remaining divergence is one tick and it is now pinned to a few
instructions.

At tick 85 (phase 21) the port makes **142 draws where the original
makes 145**, with the first 142 identical -- so it ends three draws
early rather than diverging.  Those three are at ``$36402``, the
"a bracket that rounds to nobody still loses one person one time in
four" roll in the emigration loop, which fires ONLY when the share
truncates to zero.

The share is not the problem.  Watching ``$363FC`` and recomputing all 54
brackets in single precision gives **zero disagreements** -- ``emig_share``
is right, and so is the loop structure.

The problem is upstream, in the same tick.  At the first bracket of the
emigration sweep the port's pool is 2334 against the original's 2335,
and at the birth block ``fertile`` (heads 4..8) is **12214 against 12216**
-- so ``born`` comes out 40 where the original gets 41.  Two people are in
the wrong brackets before births are even counted, with **no dice
difference at all**, which puts it in the mortality or cohort-shift
arithmetic of ``$35DFA..$35FEE`` and not in anything random.

That is the next thing to read: a deterministic two-person difference in
the ageing block, on one tick, on one city.

Three facts of the population model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- ``MISC_POP_INCREASE`` is ``MISC[16]`` → ``A5+0x1E22``, NOT ``A5+0x1E9A``. The
  arrival and departure counters at ``$1E9A``/``$1E9E`` are transient,
  cleared by ``$33FD8``, and never saved.
- ``accum8`` is ``MISC[380..387]`` and must be loaded. The economy reads
  ``accum8[7]`` in the pollution-per-head term at ``$35FBA`` before any city
  scan has run. Leaving it zero cost exactly Flint and Oakland, off by
  one in every life-expectancy bracket and nowhere else.
- ``$36166`` uses the wrong register. The birth count's fractional
  remainder should be ``births*300`` subtracted from the fertile
  population; the original subtracts ``a2*300``, and ``a2`` is still holding
  the pollution-per-head term from ``$35FE6``. Births therefore almost
  always round up. Transcribed as written — it is 18/18 with the bug and
  would not be without it.

XGRP, the graph history
-----------------------

``$2D52E`` allocates **one 3328-byte block** and ``$2D54A`` slices it into
sixteen series of 0x34 longs, storing the sixteen pointers at
``A5+0x2BDC``.  The save file's ``XGRP`` chunk is that block verbatim, which
is why it is exactly ``16 * 52 * 4`` bytes.  ``A5+0x2BEC``, ``0x2BF0``,
``0x2BF4`` and ``0x2BF8`` are entries 4 to 7 -- the four map-overlay
averages are graph samples, not globals of their own.

Every series carries **three time bases in one array**:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - slots
     - one sample per
     - count
   * - ``[0..11]``
     - month
     - 12
   * - ``[12..31]``
     - six months
     - 20
   * - ``[32..51]``
     - five years
     - 20

``$22330`` shifts the monthly band every pass, the half-yearly band in
January and July, and the five-yearly band every fifth January.  A slow
band's new slot takes a **copy of [0]**, not a fresh reading.

``A5+0x2C1C`` is a seventeenth pointer to sixteen longs, the vertical
scale, and ``$2D594`` brings each up as **1, not 0**.  It is not in the
save file.  Series 0 to 3 share one scale, 4 to 7 share another, and 13
borrows 14's when 14's is larger.  Every comparison is **unsigned**,
which matters: ``water_pct`` can pass 100, so ``100 - water_pct`` goes
negative and a signed compare would order it wrongly.

The sixteen series, and where each sample comes from:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - #
     - series
     - sample
   * - 0
     - city size
     - ``accum8[0]*10 + misc[1032] + arcology bonus``
   * - 1-3
     - residents, commerce, industry
     - ``rci_pop[i]*10`` plus halves and quarters of the same two terms
   * - 4-7
     - traffic, pollution, value, crime
     - ``$224BA``, below
   * - 8-9
     - power, water
     - ``100 - power_pct``, ``100 - water_pct``
   * - 10-11
     - health, education
     - ``misc[MISC_AGE_W65]``, ``misc[MISC_AGE_W90]``
   * - 12
     - unemployment
     - ``accum8[7]*100 / (accum8[0]+accum8[7]+1)``
   * - 13-15
     - national GNP, population, rate
     - ``misc[MISC_NAT_INDEX2]``, ``[MISC_NAT_INDEX]``, ``[MISC_NAT_MOOD]``

The **arcology bonus** at ``$2238E`` adds 20000 residents for every
arcology past the 140th.  The count is ``census[0xFB..0xFE]`` summed **in
sixteen bits** and shifted right four, because an arcology covers a 4x4
footprint and the census counts tiles.

``A5+0x1EBE`` is not one long, it is **three** (``_NewPtr #$c`` at
``$2DCA2``): the R, C and I split of the head count.  They sum to
``accum8[0]``, which is why series 0 equals the sum of series 1 to 3.

``sim_graph_pass`` is ported and exact.  ``$22330`` is **phase 21**, called
at ``$220F0`` straight after ``populationPass`` and ``economyPass``.

The overlay averages inside it
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Traffic divides by ``dept[10..12].amount + 1``; the other three divide by
``developed/4 + 1``, where ``developed`` is ``A5+0x11D0``, incremented at
``$23614`` once per tile that sets the half-resolution developed mark.

Two harness notes, both the familiar trap:

- The sixteen pointers must be built as **slices of one block**, not as
  sixteen separate allocations, or the sizes and adjacency are wrong.
- ``$224BA`` is mid-function.  Entering there skips the prologue, so the
  epilogue pops a frame that was never pushed.  Drive ``$22330``.

``$101AC``, the year-end microsimulation
----------------------------------------

``budgetPass`` calls it once a year from the January settlement, right
after the sixteen departments are reconciled (``$26442``).  It is the only
thing in the game that walks XMIC, and until now it was not ported at
all -- which meant the arcology population had been frozen at whatever
the save loaded since the day the port started.

**XMIC is 150 records of eight bytes** at ``A5+0x2BC6``, one per special
building on the map.  Byte 0 is a building id, byte 1 a per-type byte,
then three big-endian words whose meaning is the type's own business --
a power plant's first word is its output, a marina's is its boats.
There is no common schema and inventing one would be a lie.

Records 1 to 149 are walked in order and ``$1027C`` dispatches on the id:
58 entries covering ``$C6..$FF``, **26 distinct**, of which two do
nothing and one covers all seven burning power plants.

What it actually decides:

- **Power plants age.**  A year on byte 1 each pass, a newspaper story
  at 48, and at 50 the plant is finished.  Which way it goes depends on
  the DISASTER switch, and not the way round you would guess: with
  disasters OFF the city quietly rebuilds it and bills you (the price
  comes from ``BUILD_COST`` through ``MICRO_REBUILD_IDX``, and the nine
  plants price out at exactly what the game charges -- hydro 400, wind
  100, nuclear 15000, fusion 40000).  With disasters ON it just goes.
- **The prisons set the police radius**, not the stations.  Each prison
  adds its inmate rate to ``g_policeTerm``, and the tail divides by the
  prison count and reduces the result to **0 or 1** -- under eighty it
  is 1, at eighty or over it is 0.  ``$23E0C`` reads it as
  ``(term + 5) * funding``, so the whole pass decides one thing: whether
  the beat reaches five tiles or six.
- **The arcologies are the population term** graph series 0 adds to the
  head count, and the three tax departments take a share of.  ``A5+0x2C98``
  is written here and nowhere else.
- **The ending is here.**  Past three hundred Launch Arcos with six
  million people in arcologies, every ``$FE`` tile is demolished and the
  treasury is paid a hundred thousand a piece.

Three things cost real time:

``$11246`` **is a WORD cap, and the comparison is signed.**  It answers
``min(want, population / per)``, and both arguments, the quotient and the
comparison are 16-bit.  A big city divides out past 32767 and the
comparison then reads it as negative, so the cap does not clamp -- it
wraps, and a museum in a million-strong city reports a negative
attendance.  Computing it in ints gives a tidier answer and the wrong
one; it was the last two records in the corpus.

**A prison reads LAST year's police workload.**  ``$10646`` loads the
global at ``A5+0x2C92``, and the pass does not write that global until its
tail -- so the running total the loop is accumulating is not what the
prisons see.  Using it is wrong by exactly one year and the arithmetic
never gives it away.

``moveq`` **sign-extends, twice, in ways that change the model.**
``moveq #$ce`` in the library arm is -50, so a library funded under fifty
per cent LOSES knowledge rather than gaining 206 of it.  ``moveq #$a6``
in the prison arm is -90.  Both read as plausible constants the wrong
way round.

``tools/micro_check.py`` drives it: XMIC byte for byte, the six scalars,
and the draws.  **894 of 894 records and 36 of 36 scalars across six
cities.**  Because the arms draw in record order, one wrong arm puts
every later record out of phase -- so the checker reports the FIRST
differing record and that is the only one worth looking at.

One piece is transcription only, unreachable in the corpus and so
unchecked: the plant-replacement path needs a fifty-year-old plant,
which no shipped city has.

It is NOT missing a routine, though I recorded that it was.  ``$392E``
was named ``powerLineRemove`` in ``tools/symbols.json`` and it is nothing
of the kind: it calls ``$8F10``, which is the isometric projection
(``y - x``, ``y + x``, scaled by the zoom at ``$2C26``, less altitude times
three), and moves the scroll position at ``$2C3C``/``$2C3E``.  It **scrolls
the view to a tile**, and the game does it on a plant failure so you
see the explosion.  Pure interface, nothing to port.

Two things made that hide.  An earlier session had worked it out and
added ``"0x00392e": "scrollToTile"`` -- but under a ``functions`` key when
the annotator reads ``funcs``, so the stale name won and the listing kept
printing ``powerLineRemove`` at all 28 call sites.  **symbols.json had the
same address twice, in two different sections, and nothing complained.**
Both are fixed; if a name ever looks wrong, check for a second entry
before believing it.

``$30E30``, the story picker and the disaster trigger
-----------------------------------------------------

``$30E30``, and how a disaster starts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Phase 24 runs ``$30E30``.  It is 586 instructions in three parts: a ladder
over the city's statistics picking a newspaper story, the story being
printed, and then -- the part that matters -- **the trigger that decides
whether anything goes wrong**.  The nineteen disasters themselves were
already ported; nothing connected them to the clock.

Three gates, and the difficulty setting is the same number in all of
them.  ``DISASTER_ODDS`` is ``A5-0x15EA`` = ``{-, 100, 60, 30}``::

    if (disasters_off) return;                       /* A5+0x13AA */
    if (date / 25 < ODDS[difficulty]) return;        /* too young a city */
    d3 = Random() % ODDS[difficulty];

So a hard game rolls one in thirty and the easiest one in a hundred, and
a city is spared entirely until it is that many months old.

Two kinds come on the weather alone and set the kind directly.
Otherwise ``d3`` must be zero, and then ``Random() % 19`` picks one of
nineteen -- five of which do nothing at all.  Each of the rest asks the
city whether it is the sort of place this can happen to:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - case
     - what it wants
   * - 1
     - heat: a roll in 127..254 no higher than the temperature
   * - 2, 14
     - a weather flag, and 14 also wants a stormier sky
   * - 3, 13
     - unemployment past 10 AND temperature past 170; 13 also 30000 people
   * - 4
     - the dirtiest half-res cell above 150, one in ten per candidate
   * - 6
     - nothing -- it can always happen
   * - 7
     - weather state 8 or worse
   * - 8
     - 45000 people
   * - 9, 10
     - a particular building to go wrong, or nothing happens
   * - 15
     - a department over 10000
   * - 16
     - a stormy sky and a flag
   * - 18
     - a runway, for something to fall out of the sky onto

Case 4 is the nicest: it walks the whole 64x64 pollution layer keeping
the worst cell it has seen, rolling one in ten at each candidate, and
puts the smog over that with a few tiles of jitter.  The ones that start
downtown read ``A5+0x867E``/``0x8680`` and scatter within about sixteen
tiles of it.

``$3151C`` writes the chosen index to ``A5+0x13A0`` and the place to
``$13A2``/``$13A4``.  Choosing is all it does; the dispatch fires it.

The anatomy of the routine
~~~~~~~~~~~~~~~~~~~~~~~~~~

This is where Happyland used to part company, on its twelfth tick --
phase 24, one extra draw at ``$310E2``.  It is ported now and Happyland
runs a whole month exactly, but the anatomy is worth keeping because
the routine is the join between the clock and the disasters.

It is 586 instructions and it is **not only interface**.  It has three
parts:

1. ``$30E30..$3106E`` -- a ladder over the city's statistics picking a
   newspaper story id, with one gated draw at ``$3105E``.
2. ``$31070..$310AC`` -- emitting the story, then ``tst.b $13AA`` and
   return if set.
3. ``$310B0`` to the end -- **the random disaster trigger**, which is
   real simulation and is not ported at all.

Part 3 is the missing link between the clock and the nineteen disaster
routines, which ARE ported::

    if (cityDate / 25 < DISASTER_ODDS[difficulty]) return;
    d3 = Random() % DISASTER_ODDS[difficulty];
    if (weather_state == 10 && $2BCE && d3 < 15) { MISC24 = 0x10; return; }
    if (weather_state == 11 && d3 < 15) {
        MISC24 = 7;                      /* two draws for a map point */
        SetPt(Random() % 126 + 1, Random() % 126 + 1);
        return;
    }
    if (d3 != 0) return;
    switch (Random() % 19) { ... }       /* jump table at $3118E */

``DISASTER_ODDS`` is ``A5-0x15EA`` = ``{-, 100, 60, 30}`` by difficulty, so a
hard game rolls one in thirty and an easy one in a hundred, and nothing
happens at all before that many months have passed.

Twenty-two draw sites in total; the nineteen arms hold the rest.

**All fourteen arms are verified.**  The jump table at ``$3118E`` has
nineteen entries but **five of them -- 0, 5, 11, 12 and 17 -- point at
$3151A, which falls straight into the epilogue and does nothing.**  So
there are fourteen real disasters, not nineteen, and
``tools/disaster_check.py`` has an entry for every one.

It runs a disaster on both sides from the same city, the same point and
the same two seeds, then diffs the tiles each marked.  All fourteen are
**exact, 10 runs out of 10, on Bayview and Hawaii**::

    riot storm flood chem poll crash fire tornado monster
    micro volcano quake melt hurricane

``fire`` and ``quake`` had never run at all: they stop to draw and hit
``_PenMode`` then ``_PaintRect``, neither of which ``m68kemu.py`` knew.  Both
are QuickDraw procedures that return nothing, so they are ``(2, 0, 0)``
and ``(4, 0, 0)`` no-ops, and with them the two arms come out exact like
the rest.  **Two missing trap entries were the whole of the "not
verified" story** -- worth remembering before concluding that a routine
is untestable.

What the clock corpus still cannot show is the trigger reaching an arm
during a real month, because all six of its cities have disasters
switched off.

The budget passes an ordinance behind your back
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$2670A``, at the end of the budget pass, is gated by the same
``A5+0x13AA`` switch as the disasters and is easy to miss because it does
not look like one::

    if (disasters_off) skip;
    if ((Random() & 7) != 0) skip;              /* one month in eight */
    if ((uint16)Random() + 50000 >= funds) skip;
    ordinances |= 1 << ((uint16)Random() % 20); /* $26748 */
    news(0x29, that ordinance);

A city with money in the bank finds an ordinance has been passed
without it, and the paper announces it.  The treasury has to beat a
random figure plus fifty thousand, so it only happens to a rich city.

Porting it took **Happyland to complete exactness** -- every layer,
every scalar, every die -- and it was the last thing that city needed.

Two lessons.  The ``$13AA`` switch guards more than the disaster trigger,
so grep for it rather than assuming.  And a routine that is already
ported can still be missing its tail: ``sim_budget`` looked finished
because the part it was written for was finished.

The exact cities all have disasters switched off
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``MISC[1024]`` is the player's "no disasters" switch, and ``$310A6``
returns on it before the trigger rolls anything.  It lines up exactly
with which cities match:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - city
     - disasters
     - matches
   * - Bayview, Centerville, Hawaii, BIGGESTC
     - **off**
     - yes
   * - 1898, Happyland
     - on
     - no

So the flag gates a **draw**, not just an event: a city with disasters
on rolls once more per phase 24 than one with them off, and the two
populations have different random streams.

Be careful what that licenses.  "Three of six match completely" is
true, and all three are cities where this whole subsystem is switched
off -- they never reach the trigger at all.  Use ``1898`` or ``Happyland``
when testing anything downstream of it.

Difficulty is ``MISC[7]`` and every shipped city tested is on 1, the
easiest, so the odds are one in a hundred and phase 24 comes round once
in twenty-five ticks.  A hundred ticks rolls the odds four times and
reaches the nineteen-way table not at all; that needs about 2500.  The
trigger's whole effect on the stream is the one odds draw, and that is
what is worth testing.

``DISASTER_ODDS[0]`` is therefore never indexed in practice, which is as
well -- the original would divide by zero there.  The guard in the port
is defensive, not faithful.

The disasters
-------------

``$370DC`` dispatches on the disaster type in ``$13A0`` through a 19-entry
jump table at ``$3710C``.  The names come from a pointer table at
**A5-0x2947**, seventeen Pascal strings; the Disasters menu maps its
items to types through a second jump table at ``$3BF56``.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - type
     - name
     - routine
     - state
   * - 0
     - None
     - --
     - --
   * - 1
     - Fire
     - ``$38290``
     - **exact**
   * - 2
     - Flood
     - ``$379FC``
     - **exact**
   * - 3
     - Riots
     - ``$38002`` x3
     - **exact**
   * - 4
     - Pollution
     - ``$37FB6``
     - **exact**
   * - 5
     - Crash
     - nothing here
     - --
   * - 6
     - Earthquake
     - ``$383D4``
     - **exact**
   * - 7
     - Tornado
     - ``$38766``
     - **exact**
   * - 8
     - Monster
     - ``$38574``
     - **exact**
   * - 9
     - Meltdown
     - ``$38916``
     - **exact**
   * - 10
     - Microwave
     - ``$38B6C``
     - **exact**
   * - 11
     - Volcano
     - ``$37DD6``
     - needs ``$12C04`` -> ``$128DE``
   * - 12
     - Fire Storm
     - ``$37C66``
     - **exact**
   * - 13
     - Mass Riots
     - ``$37D34``
     - **exact**
   * - 14
     - Major Flood
     - ``$37940``
     - **exact**
   * - 15
     - Chemical Spill
     - ``$37888``
     - **exact**
   * - 16
     - Hurricane
     - ``$3755A``
     - **exact**, all four wind directions
   * - 17
     - --
     - nothing here
     - --
   * - 18
     - Air Crash
     - ``$38186``
     - **exact**

The menu's third item is type 18, which is how Air Crash was named: it
spawns an ordinary kind-1 aircraft record but sets field 5 to ``$10`` and
field 2 to 7, which no ordinary flight does.  Type 5 "Crash" is only a
name, used by the news line.

XTXT carries the marks: ``$FB`` chemical, ``$FC`` water, ``$FD`` and ``$FE``
fire, ``$FF`` cleared, and ``$C9`` and up an index into XTHG.

``tools/disaster_check.py <kind> <city>...`` runs each disaster from five
points on both sides and compares **every** XTXT tile and all forty
XTHG records.  180 runs across four cities are exact.

Three findings worth keeping:

- The flood's spread (``$379FC``) tests one neighbour and floods the one on
  the **opposite** side in two of its four cases, and guards those two
  with the ring offset rather than the resulting coordinate.  Both
  transcribed; the comparison is exact with them and would not be
  without.
- ``A5+0x12EA`` is the **monster** count, not a general "disasters active"
  flag.  Aircraft refuse to spawn while it is set, which is why it read
  that way.
- In a scenario the monster's field 11 is fixed at 0; outside one it is
  random.  ``$386C6`` tests the same byte the scenario checker reads.

Drawing code still rolls dice
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$5FAA``'s collapse animation changes nothing on the map and draws ``2n^3``
numbers from the same generator every later decision depends on.  So
does the volcano's choice of redraw.  "It only draws" is not a reason to
leave a call out; only "it draws no numbers" is.

Moving objects
--------------

**$09E0A is the thing-stepper, and it is NOT part of the 25-phase
clock.**  It runs from the main loop, once a frame.  That is why a saved
city reproduces exactly without it, and why nothing in the reconstruction
moved until this work.

**It is complete for the shipped corpus: 17 of 17 cities, 11,831 of
11,831 values byte-exact over fifty passes.**

Seven of the seventeen types never move.  Types 0, 4, 11, 13 and 14 are
disabled in the table at ``A5-0x635A``; types 7 and 8 are enabled but reach
``$D7CE`` and ``$D7D6``, which are ``link; unlk; rts``.  Type 11, the carriage,
is the commonest thing in the corpus and is one of the disabled ones --
it moves only by being dragged through the ``next`` field at +2.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - stepper
     - types
     - state
   * - ``$D7DE`` + ``$DEA6``, ``$DF9E``, ``$E07E``, ``$BF96``
     - 10, 12, and every carriage
     - **exact**
   * - ``$E262`` + ``$E4DA``, ``$E388``
     - 9 sailboat
     - **exact**
   * - ``$E5C8`` + ``$E978``, ``$EA0E``, ``$EAC2``
     - 3 ship
     - **exact**
   * - ``$C7A4`` + ``$CBFE``, ``$CB86``
     - 2 helicopter
     - **exact**
   * - ``$C1E6`` + ``$B394``, ``$B418``
     - 1 aeroplane
     - **exact**
   * - ``$B568``, ``$B46A``, ``$B3D8``, ``$B4CC``, ``$A6E4``
     - shared movement
     - **exact**
   * - ``$CE3E``, ``$CD00``, ``$D2CE``, ``$D406``
     - 5, 6, 15, 16
     - not read -- **none occurs in any shipped city**

The three advance routines
~~~~~~~~~~~~~~~~~~~~~~~~~~

Every mover carries a pixel offset at +6/+7 that carries into the tile
coordinate at +3/+4, but there are three separate versions of it and they
do not agree:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - routine
     - ``$E388`` boat
     - ``$EAC2`` ship
     - ``$B568`` aircraft
   * - step
     - ±16, four ways
     - ±3/±4, eight ways
     - unit x speed, eight ways
   * - bounds
     - ``$7F``
     - ``$80``
     - ``$80``
   * - tile underneath
     - not kept
     - saved in +$0A
     - saved in +$0A
   * - tile already taken
     - not checked
     - not checked
     - **steps again** until free

All three test ``> 16`` rather than ``>= 16``, so a mover that lands exactly
on a boundary spends one frame sitting on it.

The state bytes
~~~~~~~~~~~~~~~

The byte at +2 is a state in every one of them, but the aeroplane packs
two things into it: the **low** nibble is the state and the **high**
nibble is the runway heading, which states 3 and 4 read back out to line
the approach up.  ``$C208`` masks it on the way in.

The aeroplane reads the ART
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$C2DA`` looks up the building under the aeroplane in the shape
descriptor at ``A5+0x1226``, divides its height by three, and crashes the
aeroplane into anything taller than it is flying.  **That table is built
from the TSET resource**, so the simulation depends on the artwork -- the
one place it does.  ``tools/tset.py`` reads it, ``runsim.py`` builds the
descriptor, and ``BUILDING[].sprite_h`` carries the same numbers into the
reconstruction.

Before that, ``A5+0x1226`` was NULL under the oracle and ``$C2DA`` was reading
address 0.  See the null-pointer note under the harness.

The emulator and the oracle harness
-----------------------------------

Facts that cost real effort
~~~~~~~~~~~~~~~~~~~~~~~~~~~

- **1.** Absolute ``jsr`` targets are ambiguous between CODE 1 and CODE 2.
  If CODE 2 has ``link.w a6`` (``4E56``) there it is CODE 2; otherwise it is
  a CODE 1 runtime helper — ``$4B8`` mul, ``$4D8`` udiv, ``$524`` sdiv,
  ``$546`` umod, ``$58E`` smod, ``$5A6``/``$5CA`` ``%_SWITCH``.
- **2.** CODE 2 code spans offset 12 … ``$43268``; the rest is relocations.
- **3.** The A5 data image aligns at shift +95: ``block0[0x8008 + 95 + off]``
  is ``A5[off]``. Without this every static table reads as garbage.
- **4.** The BFS queue is 512 entries and discards the oldest on overflow
  (``$13B2`` = ``NewPtr(0x800)``, indices masked ``0x1FF``, ``$21E26``).
- **5.** The scratch plane at ``A5+0x13BA`` is never cleared between stages.
  One 128-row word plane serves stage 1 at half res, stage 3 at full,
  stage 4 at quarter. Stage 4 reads what is already there.
- **15.** There are THREE generators.  Besides the Toolbox ``_Random`` and
  the LFSR at ``$20F30``, ``$20EE6`` is THINK C's library ``rand()``: a
  32-bit LCG at ``A5-0x3EDC``, ``seed = seed*0x41C64E6D + 0x3039``,
  returning ``((seed >> 16) & 0x7FFF) % n``.  ``pickDirection`` uses it.
  ``tools/rng_diff.py`` compares both streams draw by draw and prints the
  first divergence -- that is what found it.
- **6.** The RNG seed is not saved. ``$11DC`` never reaches MISC, so the
  stochastic parts cannot be replayed from a file even by the game.
- **14.** ``adda.w``/``suba.w`` read their source as a long in m68kemu,
  then kept the low word -- which on a big-endian memory operand is the
  NEXT word.  ``adda.w $a(a6), a2`` added the building id instead of x,
  and the resulting misdirected write read as two separate C bugs.  Read
  the source at the instruction's own size.
- **13.** The whole bit family was missing from m68kemu.
  ``btst``/``bset``/``bclr``/``bchg`` aborted the run.  One site is the
  software ``rngMod`` at ``$20F22``; the other is the water test inside
  ``stampFootprint`` at ``$36CC``, which means the oracle had never
  executed it.  Size comes from the destination: a data register is a
  long, bit number mod 32; memory is a byte, mod 8.
- **12.** m68kemu answered signed branches from a cached compare verdict.
  ``cond()`` read a ``cmp_lt``/``cmp_gt`` pair that only ``cmp`` and ``tst`` ever
  wrote, so ``blt``/``bge``/``ble``/``bgt`` following ARITHMETIC answered the
  previous compare.  ``placeSpecial``'s dispatch is a chain of
  ``subq``/``subi`` + ``blt`` with no ``cmp`` in it, so the oracle took the
  wrong arm and the C looked wrong.  Fixed with a real V flag: ``lt`` is
  N != V.  Six layers and the economy re-verified unchanged after.

Two bugs lived in the interpreter itself, and both were found the same
way — by the C and the oracle disagreeing:

- shifts ignored operand size, so ``asr.w`` dragged stale high-word bits
  into the result and corrupted the compiler's signed-division idiom;
- ``cmp`` computed carry across all 32 bits regardless of operand size. A
  data register read returns the full register and ``move.w`` leaves the
  top half stale, so a ``cmp.w``/``bhi`` guarding a switch took its default
  arm and returned garbage — which silently zeroed six of the twenty
  ordinance costs.

The first attempt claimed an emulator was needed for ground truth while
an interpreter sat unused in its own toolbox. Do not repeat that.

The listing decodes one jump table as code, and the emulator needs telling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``$34912`` is the newspaper switch's six-word jump table.  The
disassembler decoded it as instructions, and the six-byte ``ori.l`` it
invented at ``$3491C`` swallowed the two real ones after it.  Case 0 of
that switch jumps to ``$3491E``, so the interpreter stops with "no code
at $3491E" -- but only on a run long enough to roll a one in six, which
is why twenty-five ticks never showed it and two hundred did.

``m68kemu`` now carries the two entries read straight off the bytes,
``554f a861``.  If another table turns up the same way, the fix is the
same: read the bytes, add the entry, say where it came from.

**A "no code at" stop is a hole in the disassembly, not a wrong branch.**

A (d16,An) displacement is SIGNED
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The listing prints ``move.w d0, $867e(a5)`` and it is tempting to read
that as ``A5 + 0x867E``.  It is not.  The 68k displacement is a signed
sixteen-bit word, so ``$867e`` is **-0x7982**, and the city centre lives
at ``A5-0x7982`` and ``A5-0x7980``.

This cost an hour.  Reading the positive offset showed the oracle
storing 62 and the next instruction reading back 0, which looks like an
emulator fault and is not one -- the store went where it should and the
read went somewhere else.  It also made ``out/miscload.json`` look as
though it had never tagged the centre, when it had: ``MISC[1030]`` and
``[1031]``, filed under offset ``-31106``.

Two rules follow.  **Any offset the listing shows as $8000 or above
is negative.**  And when a value seems to be missing from the tag map,
search for the signed form before concluding it is not saved.

Seed BOTH sides, or the first draw lies
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``clock_check`` seeded the C with ``rng_seed(1, 1)`` and let the
interpreter keep whatever the save file left in the LFSR at
``A5+0x11DC``.  The two streams then parted company on **draw 0**, the
same generator answering 2 against 0, on four cities out of five.

That reads exactly like a transcription error and is not one.  The
oracle needs ``A5+0x11DC = 1`` and ``e.tb_seed = 1`` set before the run,
which is what ``rng_diff.py`` had been doing all along.  With it, Bayview
matches for **12319 of 29320 draws** instead of none, and its layers go
from 58411 to 58787 of 58840.

Any new checker that compares dice has to seed both sides.  A
divergence at draw 0 is the harness, not the model.

Stubs have to answer, not just return
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A stubbed call is skipped, so D0 keeps whatever the code before it left
there.  Where the caller **tests** the result, that is not an answer --
it is an accident, and it makes the oracle authoritative about nothing.

``$30FE`` is the case that cost the most.  It hit-tests a point against
the visible window; the volcano at ``$37F6A`` tests it to decide whether
to roll for which of two sounds to play.  That roll comes from the same
generator every tile choice comes from, so getting it wrong shifts the
whole eruption.  Stubbed, it answered from the water bit ``$37F30``
happened to leave in D0 -- which is why the volcano sat at 3 of 5 for so
long and looked like a porting bug.

The game scrolls the view to a disaster before running it, so the
erupting tile is on screen and the answer is **yes**.  Told that,
the oracle draws 647 numbers over 95 iterations, which is exactly what
the reconstruction does, and **the volcano is 5 of 5**.

``Emu.run`` now takes ``stubs`` as a dict as well as a set: map an address to
the value the real routine would leave in D0, or to ``None`` to keep the
old skip-and-hope behaviour.  Any stub whose result is tested should be
given a value.

While reading it: ``$30FE`` really is a second entry into ``$30E4``, past the
``link`` and past the load of ``$2C42`` into the frame, so the y it hands
``$8E60`` comes off the caller's return address.  Five call sites do it.
That is a quirk of the shipped binary, not a bad disassembly -- an
earlier note in this file said the opposite and was wrong.

The stub list changes the answer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Happyland was differing on XBLD, XZON and XTRF.  Adding two stubs for
``1898``'s sake -- ``$3C480``, a menu fetch, and ``$023EE``, a draw -- made
Happyland exact, without touching the model.

Those routines were drawing dice in the interpreter.  Stubbing them
took the draws away and the streams lined up.  So a stub is not free
and "it is only drawing" is not a reason on its own: the real game does
run that code, and every die it rolls there is a die the model has to
account for somehow.

Choose the stub list on principle and write down why each entry is
there.  Never add one because it makes a run finish.

The state is exact.  What is left is the stream: it parts company where
the C draws from the LFSR and the original draws a Toolbox random.

An oracle run that stopped early is not the original's answer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``Emu.run`` returns a string on any failure -- out of steps, or a trap
the interpreter does not implement -- and every check tool ignored it.
A truncated earthquake reported the untouched city as ground truth and
sent me hunting a phantom bug in the C.  ``Sim.run`` now raises, and so
does every ``*_check.py``.  It caught a real case the same hour.

A byte argument sits in the high half of its word
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pushing a flag as ``0x00FF`` makes ``tst.b $c(a6)`` read zero.  A
measurement built that way said "the animation flag changes no draws",
which was wrong by ``2n^3`` per demolition and cost an afternoon.  Push
``0xFF00``.

The dice comparison is a result
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Run the dice comparison even when you expect it to be clean.
``tools/rng_diff.py`` matching on every draw is a RESULT. It rules out
every RNG-shaped bug at once and points at the logic. Atlanta matched
on all 71,383 draws while four XBLD bytes differed, and that is what
found the edge bug in ``placeBuilding``.

Extract data.  Do not generate behavior
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

About twenty tables here carry an A5 address in their comment. Those
are real tables in the data segment. The game indexes them at run
time. Read them out. There is no logic to transcribe.

Four other tables flatten CONTROL FLOW into data, and that was a
mistake. A table records the answer for the inputs you thought to
vary. It records a plausible answer for every input you did not, and
it never tells you which is which.

``WALK_STEP_BLD/UND`` hid three separate conditions this way. Modes 2
and 6 test a tunnel field in ALTM. Mode 13 surfaces at a subway
station on XBLD. All three read a second map layer, so a
``[mode][one layer]`` table cannot hold them.

Run ``tools/walk_deps.py`` after any change to ``gen_walk.py``. It sweeps
each layer for each mode. It names every mode whose answer depends on
more than one layer.

Sweep a word layer as a word. ALTM is two bytes per cell. The first
version of ``walk_deps.py`` wrote bytes at a byte stride. It reported
modes 2 and 6 as safe. They were not.

Never check a renderer against another renderer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tools/render.py`` was an 83-line sketch that got the terrain and two
thirds of the buildings wrong, and was briefly treated as ground truth --
so a C port matching it exactly was still wrong. Its replacement,
``tools/render_atlas.py``, was a faithful Python twin of the C, and it had
the same flaw: it could only ever prove the two agreed, which they did
while both were missing shadows, mis-anchoring the underground and
drawing roads on slopes 12 px low. Both are gone, along with
``tests/compare_soft.py``, ``tools/png.py`` and ``tools/miff.py`` (dead once
the twin went, and superseded by ``artpack``).

What replaced them runs the original:

* ``render_diff.py`` -- stub ``$18E96`` and record what the game blits, then
  compare shape for shape. Blind to anything that is not a blit.
* ``pixel_diff.py`` / ``pixel_scan.py`` -- let ``$18E96`` run for real into a
  framebuffer and compare PIXELS. This is the one that finds things; it
  caught the aircraft shadow, which is a destination-reading darken pass
  and so is invisible to a blit list by construction.
* ``blit_check.py`` -- our ``blit()`` against ``$18E96`` for every sprite in
  both mirror states.

Two traps worth remembering, both of which cost a day: a check that
shares an assumption with the thing it checks will agree with it forever
(an early "verify the anchor" test compared ``ay`` against the formula
that produced it, and reported 0 of 499 disagreements); and a check with
a bug of its own invents defects that are not there (a decoder
cross-check filled its framebuffer with 255, called the leftovers
transparent -- 255 is black -- and reported 295 of 498 sprites broken
when the decoder was exact).

``--check`` is the renderer's test: identical CRCs at 32 and 8 px on
three cities, ten pixels of traffic-car stencil at 16 px.

The file format and .arco
-------------------------

.arco, the native format
~~~~~~~~~~~~~~~~~~~~~~~~

``src/sim/arco.c``.  A ZIP: ``world.json`` manifest, ``cities/<id>.json``,
``cities/<id>/misc.bin`` and ``graphs.bin``, and ``chunks/<cx>_<cy>/<layer>.bin``
little-endian.  The 1995 IFF stays the import path and the baseline; this
is what the chunked, unbounded, mask-per-city world needs.
``arcology --convert a b`` goes either way and ``city_load`` sniffs ``PK\3\4``.

**The test is tools/arco_check.py, and it is not the obvious one.**
Converting a save and back does NOT reproduce the original on 62 of 101
cities -- the ``.sc2`` writer's RLE differs from the 1995 encoder, which
predates this format.  What .arco must prove is that it changes nothing:
``sc2 -> sc2`` and ``sc2 -> arco -> sc2`` are byte-identical on **101 of
101**.

Three ZIP traps, each of which cost a debugging pass:

- The central directory needs FIVE u16 after the sizes -- name, extra,
  comment, disk, **internal attributes** -- then u32 external, u32
  offset.
- Measure the directory's size BEFORE writing the end record, or its own
  first twelve bytes land inside the count.  Python reports this as "Bad
  magic number for central directory", which points nowhere near it.
- Deflate can make a dense layer bigger; fall back to method 0.

And two that are ours, not ZIP's: ``city_load`` answers 1 for success while
``arco_load`` answers 0, so every call site has to normalise; and XGRP is
``graph[16][52]`` big-endian rather than an opaque blob, so .arco stores the
numbers and ``city_save`` rebuilds the chunk.  Passing the blob through
leaves ``graph[][]`` zero and writes a 52-byte chunk where 3537 belongs.

The flag bits and the key slots
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- ``XBIT``: 0 salt, 2 water covered, 3 flood scratch, 4 watered,
  5 water-conducts, 6 powered, 7 power-conducts. Bit 1 unidentified.
- Key MISC slots: 25/26 weather, 912 pump term, 1000 ordinances,
  1030/1031 city centre, 1035 population.

Scenarios
---------

All 18 shipped "cities" are **scenarios** (Finder type ``SCEN``), and their
rules are in the **resource fork**, which the reconstruction does not
read at all.  ``SCEN 128`` is 48 bytes described by the file's own
``TMPL 128``: a disaster type and location, a time limit, six goals, three
limits, and two build requirements.  ``tools/scen.py`` decodes it.

Goal checking and win/lose are done (``$0221A8``), and eleven of the
sixteen real disasters are exact.  See the disasters above.

When two sources of truth disagree, look for a THIRD.  The scenario
goals differ between the SCEN resource and the saved MISC.  Neither
could settle it.  ``TEXT 129``, the briefing shown to the player, states
every goal in words and decided it.

A self-describing artifact is still a CLAIM.  The scenario record
ships with a ResEdit ``TMPL`` that names every field, and I quoted it as
authoritative.  The goal checker at ``$0221A8`` reads a different
layout: two extra word goals for life expectancy and education where
the template has one long.  Every shipped scenario has zeros there, so
the data cannot tell them apart.  The code is what runs.

What is not done
----------------

The growth scan reaches **zero stubs** on all 18 shipped cities. One
path is still unported and simply never fires on them:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - routine
     - state
   * - ``$BA7A`` / ``$B76E`` aircraft
     - **ported** (``spawn_plane``, ``spawn_helicopter``), but never reached
       by a shipped city, so unverified. What is still missing is the
       routine that MOVES a thing once spawned; ``$A3E4`` is not it -- that
       only projects to screen coordinates.

The growth ratio the float sequence in ``populationPass`` computes is not
ported.  The plant-replacement path of the microsimulation is
transcription only, unreachable in the corpus.  ``sim_alloc_micro``
(``$EEAE``) is a stub returning 0 where the original returns the XMIC
marker, invisible in the corpus because nothing there allocates a record.
Tick 85 of Bayview carries a deterministic two-person difference in the
ageing block, and ``1898`` cannot be run past about tick 80 because the
clock reaches an unhandled ``_GetPort`` at ``$F72``.
