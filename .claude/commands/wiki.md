---
description: Update the FOME wiki after a user-facing change (behaviour, settings, TunerStudio labels or dialogs, new feature, removal)
---

# Update the Wiki

After a user-facing change to the firmware, update the pages on the FOME wiki that describe it, so they
don't drift. The wiki source is a separate repo (github.com/FOME-Tech/wiki), usually checked out next to
this one at `../wiki`. Pages are under `docs/`. If it isn't there, ask where it is.

## What needs a docs update

The same changes that need a changelog entry (see `/changelog`), plus:

- Renamed, added or removed TunerStudio fields, dialogs or menu entries. Wiki pages quote TS labels exactly.
- Changed defaults or limits only if a page states them. Pages give starting values, not firmware defaults.
- Changed fallback behaviour, i.e. what happens when a sensor fails or a feature is inhibited.

Internal refactors, test-only changes and tooltip-only rewording need nothing.

## Find the affected pages

There's no mapping file, so work it out from the change:

1. Start from the diff: `git diff` for uncommitted work, or `git show <commit>` / `git diff <base>..HEAD`.
2. For each changed config field, find its TS dialog in `firmware/tunerstudio/tunerstudio.template.ini`.
   If the dialog has a `topicHelp`, follow it to the `help = ...` block and its
   `webHelp = "https://wiki.fome.tech/r/<slug>"`. The slug's target page is in `../wiki/docusaurus.config.ts`.
3. Also grep `../wiki/docs` for the field's TS label, the feature name, and any output channel names that
   changed. Labels on pages are in bold, e.g. `**Jam detection error max**`.
4. Check pages that describe this feature's failure behaviour indirectly. For example, a sensor's page lists
   every feature that uses it, under "If it fails".

## How to update

- **Small changes** (a renamed label, one changed behaviour, a new setting): edit the page directly.
- **Larger changes** (a new feature, a reworked feature): use the `wiki-chunk` agent in `revise` mode (or
  `new` for a new page), giving it the wiki checkout path, the page path(s) and the firmware files involved.
  Then wire up any short links or TS help links it reports, and build once at the end.
  Otherwise, follow the page conventions below by hand.

Page conventions (the full version is under "Feature page conventions" in the wiki's `docs/13-How-to-edit-wiki.md`):

- Describe behaviour for a tuner. No file, function or variable names, except TS output channel and gauge names.
- Quote TS labels exactly, in bold. Write menu paths as `**Menu > Item**`.
- Check every behavioural claim against the code as it is after your change.
- Use "If it fails" sections only for real fallback or inhibit behaviour.
- Internal links are absolute and use no number prefixes or `.md`, e.g. `/Base-Engine/Trigger/Trigger-Setup`.
- TunerStudio links to the wiki only through `/r/<slug>` short links (in `docusaurus.config.ts`, linked from
  the ini with `topicHelp` / `webHelp`). If you add a dialog for a documented feature, link it this way.
  If you move or rename a page, repoint its short link and fix inbound wiki links. Old URLs don't get redirects.
- Never put `;` inside `fome_config.txt` help text: it breaks the generator.

## Check

From the wiki repo, run on each edited page: `npx markdownlint <file>` and `node scripts/linkValidator.js '<file>'`.
For moved pages or new short links, also run `npm run build`, which checks broken links and redirects.

The wiki is a separate repo, so its changes are a separate commit (or PR) there.
