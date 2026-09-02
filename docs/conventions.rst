.. _conventions:

=========================
Writing these documents
=========================

House style for the reStructuredText in this project. It exists so that
the same kind of statement always looks the same, and so that anyone --
including Claude -- picking the documents up later writes them the same
way rather than inventing a fourth way to draw a box.

Two hard rules for the prose
----------------------------

These are not style preferences. Text that breaks either one is wrong and
gets rewritten.

**Write the current fact, not the story of how it got there.** A document
describes what the binary does and what the reconstruction reproduces. It
is not a changelog and not a lab notebook. The reader was not present for
the work and does not need to be.

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - Wrong
     - Right
   * - That rule is true. It was also, for a while, doing work it had not
       earned.
     - A percentage below 100 measures the save file, not the
       reconstruction.
   * - I had assumed an ordinary unbounded queue. It is not.
     - The queue is bounded.
   * - The right-hand column is now a statement about what a save file can
       prove, which it was not while it absorbed four unfixed bugs.
     - The right-hand column gives the reason for each shortfall.

Words that usually signal a breach: *now*, *previously*, *used to*, *no
longer*, *turned out*, *for a while*, *at first*, *it was also*, and any
first-person account of reading the disassembly. A bug that has been fixed
leaves no trace in the prose. State what is true.

The exception is a trap that a future reader can still fall into. Keep
that, in a ``.. caution::``, written as a standing fact: *"A rect drawn
before its neighbours keeps only two of its four sides"*, not *"this was
drawn in the wrong order"*.

**Write ASD-STE100 Simplified Technical English.** One idea per sentence,
20 words for an instruction and 25 for a description. Active voice with a
named actor. No semicolons: write two sentences. American spelling. One
name for one thing, every time. No marketing adjectives.

Before returning any prose, check it: split every sentence over 20 words,
replace every semicolon with a full stop, expand every contraction, make
every passive with a known actor active, and replace every nominalisation
(*perform an analysis*) with its verb (*analyze*).

Plans
-----

Plans live on the :doc:`future` page and nowhere else. Every item there
carries a status eyebrow: *Decided*, *Open* or *Not started*. A decision
is a direction the project has chosen, written as one; it is never
written as a description of the reconstruction. The facts a decision
rests on are stated with it and link to the page that checks them. The
other pages describe what exists and what the binary does.

Boxes
-----

The single most common mistake is flattening everything into ``note``.
Four levels are in use, and they mean different things:

.. note::

   An aside. Something true and worth knowing that the surrounding
   paragraph did not have room for.

.. warning::

   Something unresolved, unverified or absent. Use it where the
   reconstruction does not yet reproduce the original, or where a value
   is carried through without being understood.

.. caution::

   A trap. Something that looks correct and is not: a calling convention
   that differs from its neighbours, a routine that reads a register the
   caller did not set, an approach that seems obvious and is wrong.

.. tip::

   A shortcut or a better way of doing something. Rare here.

A box that opens with a bold lead-in keeps the lead-in as its title,
which reads far better than a generic ``Note``:

.. code-block:: rst

    .. admonition:: A calling-convention trap
       :class: caution

       Callers push ``clr.l`` for the return slot, so ...

Anything that calls itself a trap gets ``:class: caution`` whatever else
it says.

Subtitles
---------

Three roles sit under a heading, and each has one look. Picking the wrong
one is what makes a line read as a stray fragment rather than a label.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Role
     - What it holds
     - Written as
   * - eyebrow
     - A bare label: what kind of thing this is. ``sc2k-re · renderer ·
       design brief``, ``D1 · container format``.
     - ``.. container:: eyebrow``
   * - lede
     - The opening statement of a page, one or two sentences, set larger
       than body text.
     - ``.. container:: lede``
   * - rubric
     - An informative strapline: the routine names, addresses and phase
       numbers a section covers.
     - ``.. rubric::``

An eyebrow is upper-cased by the stylesheet, so it never carries an
address or anything else that is case-sensitive. A strapline does, which
is why it stays a rubric and stays in sentence case.

A page opens with its title, then the lede, then a ``.. grid::`` of
``.. grid-item-card::`` if it has headline numbers.

A section that covers particular routines carries a
``.. container:: where`` immediately under its heading, listing them.
A listing that needs a label uses the code block's own ``:caption:``,
never a separate paragraph above it.

A design decision is a heading that states the outcome, an eyebrow naming
the topic it settles, then a paragraph opening with ``**Take**`` that
gives the answer. Everything after that paragraph is the reasoning.

Badges
------

Status pills use ``sphinx-design`` roles, not bold text:

:bdg-success:`17 / 17` :bdg-warning:`partial` :bdg-danger:`not started`
:bdg-secondary:`n/a`

Use ``:bdg-success:`` only for something actually verified against the
original, and say against what.

Mechanisms
----------

A mechanism -- a routine, its addresses and what it does -- is a section,
not a box. The heading names it, a ``.. container:: where`` under the
heading carries the addresses, and the body follows. Do not wrap it in a
``.. card::``: the heading already delimits it, and a border around half
a page of prose, figures and tables adds nothing.

Reserve ``.. card::`` and ``.. grid-item-card::`` for things that are
genuinely card-shaped, such as the headline figures under a masthead.

Addresses
---------

Every address is an inline literal: :ref:`$128DE <rt-128DE>`, ``A5-0x4DF6``,
``XTER``. ``default_role = "literal"`` is set in ``conf.py``, so single
backticks give the same result and a bare :ref:`$128DE <rt-128DE>` in running text is
a mistake.

The strapline under a section heading -- the routine names and phase
numbers -- is a ``.. rubric::``. A bare label with no address in it is an
eyebrow instead.

Pictures
--------

A rendered picture on a preview page is a figure declared in
``tools/gen_previews.py``, embedded by key from ``img/preview/`` and
preceded by the command that made it, in a ``.. code-block:: console``::

   arcology --soft assets 'Bayview' out.png --zoom 32 --underground

   .. thumbnail:: img/preview/judge-underground.gif
      :group: renderer-previews

``python3 tools/gen_previews.py --check`` fails if a page embeds a file
that is not a declared figure, prints a command that is not the one that
made the picture, or leaves a declared figure out. It also fails when
``arcology --soft`` or the atlas has changed since the pictures were rendered,
because the manifest records what they were rendered from. A new picture is
therefore a new entry in FIGURES first, and a render second; the page
comes last.

Listings
--------

C goes in ``.. code-block:: c`` so Pygments highlights it. The 68k
disassembly goes in a plain literal block (``::``): there is no lexer
for it, and a wrong one is worse than none.

What not to do
--------------

reStructuredText has no nested inline markup. A literal inside bold, or
a link inside bold, renders as raw backticks and asterisks. Write the
link *or* the emphasis, not both.

An inline literal opens only after whitespace or one of ``- : / ' " < ( [
{``. ``$19004``; ``$188E8`` needs the space after the semicolon, and a
range is written ``$16894`` to ``$1689C``, because a literal cannot open
after a full stop.

Addresses inside a listing stay plain. Backticks are literal text there,
so ``/* $213E8 */`` in a code block is correct and the marked-up form is
not.

Diagrams are SVG files in ``img/``, shown with ``.. figure::`` and a
normal caption. Each file carries its own ``<style>`` block holding the
palette variables *and* the class rules it uses, plus a
``prefers-color-scheme`` block, because a standalone SVG cannot see the
page stylesheet. A figure that references ``class="lbl"`` without
defining it renders every label at the browser default size.

``numfig`` is on, so Sphinx numbers the figures. Do not write "Figure 1"
into a caption.
