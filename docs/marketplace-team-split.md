# Adaptive Equipment Marketplace — Team Split & Merge Plan

Companion to [`marketplace-mvp-plan.md`](./marketplace-mvp-plan.md) (v3). That doc says *what and how*;
this one says *who, in what order, and how it merges back together*. Phase numbers below refer to the v3 plan.

**Design principle — the barbell:** the project contains two *global* operations that touch nearly
every file: the Phase 1 strip (delete ~half the codebase) and the Phase 5 rename sweep
(`insta_blocks` → `listing_blocks`, app id, l10n strings). Global operations cannot run concurrently
with anything — a branch cut before the strip merges is a branch that will conflict with the strip.
So the shape is: **serial cut → parallel middle → serial close**. Everything in this doc exists to
make the middle as wide as possible and the two serial ends as short as possible.

```
  serial            parallel                                serial
┌─────────┐   ┌──────────────────────────────────────┐   ┌──────────┐
│ Phase 0 │   │ WS-A backend    ──────────────┐      │   │ swap DI  │
│ mob     ├──▶│ WS-B domain     ──────────┐   ├──────┼──▶│ e2e + a11y│
│ Phase 1 │   │ WS-D commerce UI (fakes) ─┤   │      │   │ rename   │
│ strip   │   │ WS-E chat UI     (fakes) ─┘   │      │   │ sweep    │
│ (solo)  │   │ WS-F CI/QA rails ─────────────┘      │   │ (solo)   │
└─────────┘   └──────────────────────────────────────┘   └──────────┘
   THE CUT ▲                                    ▲ MERGE GATE M3
```

---

## 1. The cut: what must be true before anyone branches

The cut happens at a tagged commit (`cut-v1`) on `main`. **No workstream branch is created before
this tag exists.** Prerequisites, in order:

1. **Phase 0 green baseline** — done as a *mob/pair session with the whole team* (1–2 days). This is
   deliberate: it's the cheapest way for every member to learn the build system, the sync pipeline,
   and the media path before working in isolation. Everyone leaves able to run the app and the
   offline test.
2. **Phase 1 strip merged** — executed by ONE person (WS-C owner) directly on `main` while others
   prepare their streams (§4 shows nobody idles). Deleting half the codebase concurrently with open
   feature branches is guaranteed merge hell; done solo-first it's a fast, boring diff.
3. **Contracts frozen and merged** (§2) — the interfaces, schema, and conventions every stream
   builds against.
4. **Phase 2 migrations applied** to the shared dev Supabase + PowerSync instance.
5. **CI gate live**: `flutter analyze` + tests + debug build required on every PR to `main`.

Tag `cut-v1`. Branches open. The cut is clean because after this commit, no planned work deletes or
renames anything another stream owns.

---

## 2. Contracts — frozen at the cut, changed only by protocol

These are the interfaces between workstreams. All of them already exist in the v3 plan; the cut
turns them from "plan text" into "merged code/SQL that others compile against."

| # | Contract | Defined in | Frozen artifact at cut |
|---|---|---|---|
| C1 | DB schema: `listings`, `conversations(+listing_id, buyer_id)`, trigger, indexes | Plan §2a | Migration files merged; applied to dev instance |
| C2 | RLS + sync rules (three-legged visibility) | Plan §2b/2c | Policies + `sync-rules.yaml` merged |
| C3 | `Listing` model + enums (`condition`, `category`, `status`) | Plan §3a | Dart file in the blocks package |
| C4 | `ListingsRepository` interface + `openListingThread` signature | Plan §3b | Abstract classes merged **with in-memory fake implementations** |
| C5 | Route names + bloc public APIs (events/states per §3c) | Plan §3c | Stub blocs emitting fake-backed states |
| C6 | Conventions: `PriceLabel` mapping, storage path builder, conversation auto-title | Plan decisions | One `conventions.dart` + doc comments |

**C4 is the load-bearing one.** UI streams (D, E) build against the fakes from day one of the
parallel phase; WS-B builds the real PowerSync-backed implementations behind the same interface.
The final integration is then a *dependency-injection flip*, not a merge of two divergent
understandings — this is what makes the merge seamless rather than hopeful.

**Contract change protocol** (for when — not if — a contract turns out wrong mid-build):
change requests go as a PR touching *only* the contract artifact, owner of the consuming stream
reviews within one working day, and the PR description states which streams must react. Contract
PRs jump the merge queue. Nobody "temporarily" works around a contract in their own stream — that
recreates the divergence the contracts exist to prevent.

---

## 3. Workstreams — non-overlapping by owned paths

Ownership is by directory, enforced mechanically with `CODEOWNERS` (§5). No one edits outside
their paths without a PR approved by the path's owner.

### WS-A — Backend & sync platform
- **Scope:** Phase 2 end-to-end, then platform support: migrations, RLS, auto-participant trigger,
  indexes, PowerSync sync rules, storage policies, seed script, env/config docs.
- **Owns:** `supabase/` (migrations, seed), `sync-rules.yaml`, `packages/env`,
  `packages/powersync_repository`, `packages/database_client` (SQL/query layer).
- **Deliverables & exit:** Phase 2 exit criteria (a)–(d) pass, incl. adversarial two-user RLS tests;
  seed data live; all three buckets syncing on a device.
- **Profile:** the SQL/security-minded person; enjoys policies, data modeling, and being the one
  who says "no, the database enforces that."
- **After main deliverable:** pairs with WS-B on query wiring; owns backend side of offline matrix.

### WS-B — Domain & repositories
- **Scope:** Phase 3: `posts_repository` → `listings_repository` (real PowerSync-backed impl behind
  C4), `chats_repository.openListingThread` find-or-create, `user_repository` trim, blocks-package
  model untangling (the plan's remaining known unknown lives here).
- **Owns:** `packages/listings_repository`, `packages/chats_repository`, `packages/user_repository`,
  `packages/insta_blocks` (model layer).
- **Deliverables & exit:** Phase 3 exit criteria; unit tests for create/watch and thread
  find-or-create racing; real impls pass the same test suite the fakes pass (write that suite
  against the *interface* so it runs on both).
- **Profile:** the architecture-minded Dart person; likes interfaces, streams, and deleting code.
- **Note:** this is the plan's riskiest stream. It gets the most senior person and *no* side quests.

### WS-D — Commerce UI
- **Scope:** Phase 4 items 1–3 + 5: Browse (cards, category chips, pull-to-refresh, empty state,
  pagination), Listing detail (carousel, PriceLabel, Message seller / Edit / Mark sold), Create+Edit
  form with validation, self Profile. Built entirely against C4 fakes until M3.
- **Owns:** `lib/listings/`, `lib/user_profile/`, `packages/instagram_blocks_ui` (widget layer),
  `packages/form_fields`.
- **Deliverables & exit:** all screens function on fakes with seed-shaped data; a11y semantics
  built in as screens are written (labels, tap targets), not retrofitted.
- **Profile:** the widget/product person; cares about polish, empty states, and how the create
  form *feels* one-handed.

### WS-E — Messaging UI
- **Scope:** Phase 4 item 4: thread view scoped to listings, header with thumbnail/title/status
  badge, Messages tab (conversation list relabeled), entry from listing detail via C4's
  `openListingThread`.
- **Owns:** `lib/chats/`.
- **Deliverables & exit:** thread flow works on fakes, incl. the sold-listing header case.
- **Profile:** comfortable reading existing code more than writing new — this stream is 80%
  constraining the clone's chat, 20% new UI.
- **After main deliverable (it's the smallest stream):** joins WS-D for the create/edit form, or
  starts the Phase 5 offline test scripts with WS-F.

### WS-C → WS-F — Shell, then integration & quality
- **Scope pre-cut (as WS-C):** the Phase 1 strip, executed solo on `main`: feature deletes,
  Firebase removal, flavor collapse, nav reduction, engagement-counter/deep-link sweep.
- **Scope post-cut (as WS-F):** CI workflow, README rewrite, test scaffolding, WCAG audit tooling
  and checklist, hot-file stewardship (§5), merge-queue coordination, then drives Phase 5:
  offline matrix, media-picker consolidation, and the **final rename sweep (solo, last)**.
- **Owns:** `lib/app/`, `lib/navigation/`, `lib/bootstrap.dart`, `lib/main_*.dart`, `lib/l10n/`,
  root `pubspec.yaml`, `.github/workflows/`, `packages/app_ui` (steward — see §5), `README`.
- **Profile:** the integrator/toolsmith; likes green pipelines, owns quality gates without owning
  features, and is temperamentally suited to saying "your PR is red."

**Auth note:** `lib/auth/` + `packages/authentication_client` stay untouched at MVP (the clone's
flow survives the strip). Nominal owner: WS-F. Magic-link config toggle: WS-A (it's a Supabase
setting) + WS-F (screen flag) if taken.

---

## 4. Timeline & load distribution

Four people; workdays. The serial prefix is fully loaded — the strip being solo doesn't idle the
other three, because backend and contracts don't depend on client code:

| Day | WS-A (backend) | WS-B (domain) | WS-D (commerce UI) | WS-E→C→F (msg / shell / quality) |
|---|---|---|---|---|
| 1–2 | **Phase 0 mob — whole team** | ← | ← | ← |
| 3–5 | Phase 2: migrations, RLS, trigger, sync rules, seed | C3/C4/C5: model, interfaces, **fakes**, stub blocs | Screen inventory, a11y checklist, form spec; review contracts | **C: Phase 1 strip solo on `main`**; then CI gate |
| — | **CUT: tag `cut-v1`, open branches** (end of week 1) | | | |
| 6–10 | Storage policies, RLS adversarial tests, bucket verification on device; support B | Real repositories behind C4; blocks untangling; interface test suite | Browse + detail + create/edit on fakes | E: thread view + Messages tab on fakes; F: test scaffolds, README |
| 11–12 | **M3 integration (all hands): DI flip fakes→real, smoke the happy path** | ← | ← | ← |
| 13–15 | Offline matrix (backend half) | Offline matrix (client half); fix fallout | A11y pass on own screens; picker consolidation | A11y audit lead; e2e happy path; merge-queue |
| 16 | — | — | — | **Rename sweep, solo on `main`** (mirror of the strip) |
| 17–18 | **Buffer / hardening — whole team** (DoD checklist from plan Phase 5) | ← | ← | ← |

**Wall-clock: ~3.5 weeks with 4 people**, vs ~5 weeks (with contingency) solo. Be honest about why
it isn't 4× faster: the serial ends (mob onboarding, strip, integration, rename, hardening) are
~40% of the calendar and don't parallelize — that's Amdahl's law, not poor planning. The buffer
days are real; if they go unused, ship early.

---

## 5. Merge plan — decided now, before the cut

**Branch model:** trunk-based. `main` is protected: CI green + one review required; the reviewer is
the *adjacent* stream's owner (A↔B, D↔E, F reviews shell/config touches) so knowledge spreads and
the DI flip has no cold readers. One branch per workstream (`ws/a-backend`, `ws/b-domain`, …),
**rebased on `main` and merged at least every 2 working days** — small merges, always green, no
long-lived divergence. A branch that can't merge green in 2 days is a scope problem; split the PR.

**Merge order at the gates:**
1. Continuous (days 6–10): A merges freely (SQL/config — no client code conflicts). B, D, E merge
   their package-/dir-scoped work as it greens. F merges CI/test rails.
2. **M3 gate (day 11):** order matters — B's real repositories merge first, then the one-PR DI flip
   (small, written by B, reviewed by D+E), then D/E fixups. Nothing else merges during the flip.
3. Phase 5: fixes merge continuously; the **rename sweep is the final solo commit** — announced,
   everyone else's branches merged or parked first, exactly like the strip. Global renames at the
   edges, never in the middle.

**Hot files — the known conflict magnets, each with a single steward:**

| File/area | Why hot | Rule |
|---|---|---|
| Root + package `pubspec.yaml`s | every stream touches deps | F stewards; dep changes are their own tiny PR, never buried in a feature PR |
| `lib/navigation/` routes | D and E both add routes | Route *names* frozen in C5; route registration edits go through F |
| `packages/app_ui` (theme, shared widgets) | D and E both consume | F stewards; additions by PR, no in-place edits to existing widgets |
| `lib/l10n/` ARB strings | D and E both add copy | Append-only during parallel phase; F dedupes at Phase 5 |

**CODEOWNERS (mechanical enforcement of §3):**

```
supabase/                         @ws-a
packages/powersync_repository/    @ws-a
packages/database_client/         @ws-a
packages/env/                     @ws-a
packages/listings_repository/     @ws-b
packages/chats_repository/        @ws-b
packages/user_repository/         @ws-b
packages/insta_blocks/            @ws-b
lib/listings/                     @ws-d
lib/user_profile/                 @ws-d
packages/instagram_blocks_ui/     @ws-d
packages/form_fields/             @ws-d
lib/chats/                        @ws-e
lib/app/                          @ws-f
lib/navigation/                   @ws-f
lib/l10n/                         @ws-f
packages/app_ui/                  @ws-f
pubspec.yaml                      @ws-f
.github/                          @ws-f
```

**Definition of a clean merge (every merge, not just gates):** `flutter analyze` clean, tests green,
app boots to Browse, and — post-M3 — the happy path smoke passes. Any merge that breaks this is
reverted first and debugged second; `main` stays shippable.

---

## 6. Milestones (integration gates, not status meetings)

| Gate | When | Proves | Blocking criteria |
|---|---|---|---|
| M1 `cut-v1` | end W1 | clean cut is possible | strip merged; contracts C1–C6 merged; migrations on dev; CI live |
| M2 | day 8 | streams are really parallel | D & E screens render on fakes; B's interface test suite passes on fakes |
| M3 | day 11–12 | the seam holds | DI flip merged; happy path on real backend, on device |
| M4 | day 15 | feature complete | plan Phase 4 exit criteria incl. sold-thread + screen-reader path |
| M5 | day 18 | done | plan Phase 5 DoD, offline matrix, rename sweep landed |

---

## 7. Team-size variants

- **3 people:** merge WS-E into WS-D (one UI stream, chat after commerce); WS-F duties split — the
  strip/rename + hot-file stewardship go to whoever runs WS-B's review pair, CI to WS-A. Wall-clock
  ~+3–4 days.
- **5 people:** do *not* create a fifth code stream — the seams are the constraint, not hands.
  Fifth person pairs into WS-B (the risk concentration) and owns the Phase 5 test matrix full-time
  from day 6. Quality rises more than the schedule shrinks.

---

## 8. Parallelization-specific risks

| Risk | Mitigation |
|---|---|
| A stream branches before the cut "to get ahead" | Don't. Nothing exists to get ahead *of* until the strip merges; pre-cut prep work (§4 days 3–5) is already assigned |
| Fakes drift from real behavior (UI works on fakes, breaks on real) | The C4 interface test suite runs against **both** fake and real impls in CI; seed data (Phase 2) mirrors the fake data shapes |
| Contract "quick workaround" inside one stream | Change protocol in §2 — contract PRs are fast-tracked precisely so working around them is never the quicker path |
| Two streams block on one review | Adjacent-reviewer pairs + 1-working-day review SLA; F monitors the queue as steward |
| B (riskiest stream) slips and starves M3 | B has no side duties, gets the senior; if blocks-untangling explodes, the plan's fallback applies (copy widget into `app_ui`, sever the import) and M3 slides into the buffer, not into D/E's scope |
| Rename sweep conflicts with late fixes | Sweep is the announced final solo commit; everything else merges or parks first — same discipline as the strip, symmetric by design |
