# Arcology — house rules

## Output: never printf

`printf` is not used anywhere in `src/render` or `src/app`.

* A **message** goes through the log — `R_ERR`, `R_WARN`, `R_NOTE`,
  `R_DBG` from `src/util/log.h`. It carries a source tag and a level, and
  it can be filtered.
* A **developer dump or a report line** goes through `dumpf` from
  `src/util/dump.h`. It writes to the file `--dump-to` names and to
  stdout when none is named, so the same line can be read by a person or
  captured by a script.

A bare `printf` obeys neither switch: it cannot be redirected, cannot be
levelled, and is invisible to the tooling that reads everything else.
`ctest -R no_printf` enforces this (`tools/no_printf.sh`); it needs no
build and runs in a moment.

A log inside a per-frame path says its line **once, when what it is
saying changes** — never once a frame.

## Comments: what the code does, once

A comment describes the code **as it stands**.  Three rules, checked by
`ctest -R comment_style` (`tools/comment_style.py`, no build needed):

* **No attribution.**  Not "the user asked for", not a quoted request,
  not a date from this project's own era.  Who wanted the code and when
  is the repository's history; the source is not the place for it.  A
  date from the game's own era is provenance of the material and stays.
* **No history.**  In `src/render`, `src/app` and `src/ui`, no account
  of what the code used to do -- no "used to", "had been", "it once",
  "before this", "no longer".  A reader needs the rule that holds now.
  `src/sim` is exempt because there "the old value" and "used to"
  describe the ORIGINAL 68k game, which is the subject matter.
* **Never stack.**  Two multi-line block comments must never sit back to
  back with no code between them.  That shape means a change wrote a new
  comment and left the old one standing: the two then disagree and the
  reader cannot tell which one the code obeys.

The **docs** are held to the attribution rule too: a page explains the
renderer, it does not minute who asked for what and when.  A date in a
page's own eyebrow or rubric is the page's own, and stays.

So when you change code, **rewrite the comment beside it**.  Do not add
a second one; do not explain what you changed.  If the old comment is
still true, leave it alone; if it is not, replace it.
