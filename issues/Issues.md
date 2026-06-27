# GlowKitchen

GlowKitchen is an ESP32-C3 (Arduino/PlatformIO) firmware that drives FastLED installations at multiple locations, controlled over MQTT (via a Home Assistant broker) and an optional IR remote, with over-the-air firmware updates pulled from GitHub Releases. Issues here track firmware behavior, the OTA pipeline, and the companion shell scripts in `scripts/`.

This file is the local guide for managing issues in this project. The companion Mac app (Issues.app) watches the `issues/` folder and renders the current state. Markdown files (and `project.json`) are the source of truth — there is no generated artifact or index to keep in sync.

The `# GlowKitchen` heading above matches the `name` field in `issues/project.json`, which is the canonical source for the project's identity (name + repo URL).

## Status values

| File value | Display name | Meaning |
|---|---|---|
| `open` | Open | Filed but not yet started |
| `in-progress` | In Progress | Actively being worked on |
| `resolved` | Resolved | Work is done; awaiting user confirmation |
| `closed` | Closed | User has confirmed the fix |
| `wontfix` | Won't Fix | Acknowledged but won't be addressed |

Use the **file value** (lowercase, hyphenated) in the issue's metadata table. The Mac app converts to the display name when rendering.

## Critical rule: never close without explicit confirmation

An issue must **never** be marked `resolved`, `closed`, or `wontfix` based on inference — only when the user says so in plain language. Do not infer resolution from a code change, a commit message, the filing of a related issue, or the user saying "thanks, that looks better". Leave status at `open` (or `in-progress` if work has started) until the user confirms with words like "close this", "mark resolved", or "won't fix". The deliberate exception: the orchestrator may set `resolved` after the review gate passes (see below). Only the user sets `closed`.

## Git tracking

`issues/` **is tracked** in git for this project, so each lifecycle event produces a commit. Issue work happens on per-issue branches (`issue/NNNN`) that land on `main` as a single squash commit.

- File a new issue → commit the `NNNN.md` on `main` with message `#NNNN <issue title>`.
- Work an issue → branch `issue/NNNN`, commit freely (prefix `#NNNN`), squash-merge to `main` after review approval. Keep the branch; never delete it.
- Ad-hoc edits (notes, close, won't-fix) → commit on `main` with a fitting `#NNNN …` message.

## Issue file format

Each issue is `NNNN.md` (4-digit zero-padded):

```markdown
# NNNN — Title

| | |
|---|---|
| **Status** | open |
| **Module** | <module name(s)> |
| **Platform** | ESP32-C3 |
| **First seen** | YYYY-MM-DD |

## Description

What is wrong / what's needed. Lead with the punchline — the first paragraph shows in the Mac app summary. Keep it terse (1–3 sentences); deeper detail goes in `## Long Description`.
```

Format details: title separator is an em-dash (`—`), not a hyphen; metadata field names stay `**bold**`; dates are `YYYY-MM-DD`; `Module` may list several separated by ` / `; `## Description` must be the first `##` section. When status moves to `resolved`/`closed`, add a `**Closed**` row (and a `**Branch**` row when resolved via an issue branch).

## Filing a new issue

1. Ensure `issues/project.json` and this file exist.
2. Find the highest existing `NNNN.md` and increment (start at `0001`; skip reserved `8888`/`9999`).
3. Create `issues/NNNN.md` from the template; status `open`; First seen = today.
4. Title is a single declarative sentence.
5. Commit the new file on `main` with `#NNNN <issue title>`.

## Resolving an issue (review-gated branch workflow)

Work one issue at a time, in ascending order, each on its own `issue/NNNN` branch cut from `main`:

- **Implementer subagent (Sonnet)** — sets status `in-progress`, makes the change, builds **and** verifies, commits checkpoints on the branch. Never touches `main`.
- **Reviewer subagent (Opus)** — reviews `git diff main...HEAD` against the issue; approves or requests changes. Never edits/commits/changes status.
- **Orchestrator (main session)** — creates the branch, dispatches both, routes feedback, records `## Work log` rows, and after approval marks the issue `resolved` and squash-merges to `main` (`git merge --squash issue/NNNN` → `git commit -m "#NNNN <verb> <title>"`). Keep the branch.

On resolve, add (after `## Description`): `## Root cause`, `## Fix`, `## Review`, `## Verification` (mandatory — name the exact command run and what was observed), `## Files changed`, optional `## Gotchas`. Status flow is `open` → `in-progress` → `resolved`; never set `closed`.

### Build / verify command for this project

- **Build:** `pio run -e esp32c3` — must reach `[SUCCESS]`.
- **On-device verification:** OTA/MQTT behavior cannot be proven by a build alone. Flash the dev board (`pio run -e esp32c3 -t upload`) and exercise the change over MQTT (e.g. `scripts/ota_update.sh`, `scripts/list_devices.sh`) while watching the serial log. If on-device verification can't be run in the agent's environment, that's a bail (note what couldn't be verified) — not a pass.

## Module conventions for this project

- `Firmware` — `src/main.cpp` core behavior (LED/theme/IR/MQTT loops, preferences).
- `OTA` — over-the-air update pipeline (`checkForOtaUpdate`, `loopOta`, releases, rollback). See `OTA.md`.
- `MQTT` — command/state topics and payloads.
- `Scripts` — the `scripts/*.sh` control/utility tooling.

## Token usage and cost tracking

When a subagent works an issue, the orchestrator records a `## Work log` row (model, exact deduped token counts, cost from `issues/model-pricing.json`) and keeps a running total. Refresh the pricing cache once per day. Full recipe in the issues skill reference.
