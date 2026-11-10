---
name: specification-stewardship
description: Keep co-located specifications and essential reasoning concise and accurate as AI-assisted development evolves. Use when requirements, plans, implementation, or architectural decisions are created, revised, reversed, or reviewed.
---

# Specification Stewardship

Maintain **current accepted intent** and the **shortest defensible explanation** of it, not a history of exploration. Code and tests are evidence of implementation, not substitutes for intent.

## Every change

1. **Locate:** Read the existing spec, affected code/tests, and repository conventions. Edit the existing document; do not create a parallel plan by default.
2. **Classify:** Mark the request internally as **add**, **revise**, **retract**, or **unresolved**. For reversals, remove the old requirement and its dependent rationale throughout the document. Preserve a concise prohibition only when needed to prevent regression.
3. **Resolve:** Distinguish accepted intent, observed behavior, and assumptions. Ask about consequential ambiguity; otherwise record an open question instead of inventing an answer.
4. **Reconcile:** Update observable behavior, interfaces, failure modes, constraints, and acceptance criteria. Check affected neighboring requirements and explicit negative constraints. Keep unverified implementation claims labeled as such.
5. **Compress:** Replace chronology with a minimal causal path from problem to current design. Retain a decision only when removing it would obscure a consequential constraint or tradeoff. Combine partial insights; remove dead ends, repetition, and obsolete text.
6. **Check:** Confirm each accepted requirement is represented, each retracted requirement is absent, each essential choice has a reason, and no section contradicts another. Link to evidence rather than reproducing it. Report gaps and changes briefly.

## Document shape

Follow repository conventions; omit empty sections.
- **Purpose:** problem, outcome, scope.
- **Current specification:** testable requirements and acceptance criteria.
- **Essential rationale:** only the non-obvious causal steps needed to understand or safely change the design.
- **Open questions:** consequential unresolved decisions, with no guessed answers.

## Splitting into files

A spec that one reader can't take in at once is split into a folder of files, one per feature area.
- **When:** a file passes about 300 lines or 40 KB, or holds areas that change independently. Split again by sub-area when a file grows past that.
- **By feature area:** cut where a reader looks things up (an engine stage, a file format, a UI area, testing, packaging), not by document position. Aim for files of roughly 50-300 lines. Don't create a file for a few lines unless the area is distinct and about to grow; otherwise put it in the closest file.
- **Index:** the folder's `README.md` states the purpose and scope, how sections are cited, one line per file saying what it covers, and every numbered section with a link to its file. It summarizes and links; it never restates a requirement.
- **Each file** opens with one sentence saying what it covers and a link to the index.
- **Stable identifiers:** section, decision and question numbers are global, not per file. A moved section keeps its number, and the index is updated. Cite by number, not by file, so citations survive moves. Never renumber to make a file look tidy.
- **One list per sequence:** append-only numbered lists (decisions, open questions) each live in a single file, so new numbers never collide.
- **Adding:** a new section goes in the file that owns its area, with the next free number under its parent, and gets an index row. A new area gets a new file.
- **Move, don't rewrite:** a split moves text verbatim (check that no line was lost or duplicated). Rewording is a separate change.
- **Check:** each number is in exactly one file, the index lists every section and file, links resolve, and no requirement is stated in two files.

## Human-comprehension test

For each paragraph: **Would removing it change what must work, why a consequential choice exists, or what remains undecided?** If not, delete it. If already expressed elsewhere, consolidate or link. Do not shorten away security, data integrity, compatibility, or acceptance criteria. Do not append a change log or decision archive unless explicitly required; version control retains exploration. Do not claim verification without actual evidence.
