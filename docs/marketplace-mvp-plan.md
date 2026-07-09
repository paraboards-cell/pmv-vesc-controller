# Adaptive Equipment Marketplace — Executable MVP Plan

**Base repo:** [`itsezlife/flutter-instagram-offline-first-clone`](https://github.com/itsezlife/flutter-instagram-offline-first-clone)
(Flutter + BLoC + PowerSync + Supabase, MIT-style monorepo — verify `LICENSE` before shipping)

> All schema claims in this document (column names, types, constraints on `conversations`,
> `participants`, `messages`, `posts`, `images`) are verified against the upstream
> [`packages/database_client/tables.sql`](https://github.com/itsezlife/flutter-instagram-offline-first-clone/blob/main/packages/database_client/tables.sql)
> as of 2026-07 — not assumed from the clone's README. Re-verify against your forked commit in Phase 0.

**Goal:** strip the clone down to an offline-first equipment exchange marketplace MVP:
**create listing → browse → view detail → message seller**, with a self-profile showing your own listings.

**Non-goals for MVP (deferred backlog):** payments/escrow, offers/negotiation, ratings/reputation,
search filters, push notifications, moderation, watchlist/saves, global chat inbox.

---

## 0. Decisions locked in up front

These were open questions in the draft; the plan below assumes these answers. Change them here, not mid-build.

| Decision | Choice | Rationale |
|---|---|---|
| New repo vs. in-place fork | **Fork into a new repo** (e.g. `paraboards-cell/adaptive-gear-exchange`), keep upstream as a remote for reference only | The delete surface is ~50% of the codebase; you will never merge upstream again |
| Chat model | **Keep the clone's `conversations`/`participants`/`messages` tables, add `listing_id` *and* `buyer_id` to `conversations`** | Reusing the working chat sync path is far cheaper than a bespoke `MessagesRepository(listingId)`. Verified: `conversations` has **no** creator/`created_by` column (only `id, type, name, created_at, updated_at`) — the buyer's identity exists only via the `participants` join table, so "one thread per buyer per listing" needs an explicit `buyer_id` column to hang a uniqueness constraint on |
| Listing media | **Keep the clone's `media` column pattern (renamed onto `listings`)** instead of `media_urls text[]` | Verified: `posts.media` is a **`text` column holding a JSON-encoded array**, not `jsonb`. Keep it exactly as-is — the feed UI blocks, blur-hash previews, and storage upload path all serialize to/from that shape; changing the column type or moving to `text[]` forces a rewrite of working code. MVP restricts listing media to **photos only** (the media JSON can reference video entries; the `videos` table is being dropped) |
| Media picker | Keep **both** `gallery_media_picker` and `image_picker_plus` through Phase 3, consolidate to one in Phase 5 | The create-post flow depends on them; consolidating early risks breaking the one flow we must keep |
| Conflict resolution | Last-write-wins (PowerSync default) | Fine for MVP; listings are single-writer (the seller) |
| Firebase | **Remove entirely** | It only serves push notifications + remote config, both deferred; removing it simplifies bootstrap and CI |
| Auth | Keep Supabase auth from `authentication_client`, email/password (+ Google if it works out of the box) | Required anyway for RLS and messaging |
| Price | `numeric` column, **nullable** — "free / borrow / contact me" is common for adaptive-equipment exchange | Domain reality: much of this gear is given away or lent |

---

## Phase 0 — Bootstrap a green baseline (before touching anything)

Nothing gets deleted until the unmodified clone builds and runs against *your* backend.
Every later phase ends at "app compiles, boots, and the kept flows work" — strangler style, never a big bang.

1. Fork → new repo. Add upstream remote for reference.
2. Follow the repo's `MIGRATION.md` (not the tutorial): FVM-pinned Flutter SDK (3.35.x line), dependency versions.
3. Create the Supabase project; run the clone's `packages/database_client/tables.sql` as-is.
4. Create the PowerSync instance; apply the clone's sync rules as-is.
5. Fill `packages/env` (`.env` per flavor) with your Supabase/PowerSync credentials.
6. Run `main_development.dart`; verify: sign-up, create a post with a photo, see it in feed, chat message round-trips, and **offline**: airplane mode → create post → reconnect → post syncs.
7. **Document the media pipeline split** before touching it: `posts.media` (a `text` column holding a JSON array) and the separate `images`/`videos` tables coexist — write down which one the upload queue writes first, which one the feed reads, and how blur hashes flow between them. Two media-tracking mechanisms living side by side is a classic hiding spot for silent bugs; a half-page note now prevents guessing in Phase 3.

**Exit criteria:** the stock clone works end-to-end, online and offline, on your infrastructure. This is the reference point every later regression is measured against.

---

## Phase 1 — Delete map (file-level)

Grounded in the actual repo layout. Delete in this order; fix compile errors after each block before the next.

### 1a. App features — `lib/`

| Path | Action |
|---|---|
| `lib/stories/` | **Delete** |
| `lib/reels/` | **Delete** |
| `lib/comments/` | **Delete** |
| `lib/timeline/` | **Delete** (explore/ranked grid — replaced by plain latest-first feed) |
| `lib/search/` | **Delete for MVP** (backlog: rebuild as category/keyword search in Phase 6+) |
| `lib/feed/` | **Keep** → becomes `lib/listings/` in Phase 3 |
| `lib/chats/` | **Keep** (scoped to listing threads in Phase 4) |
| `lib/auth/`, `lib/app/`, `lib/home/`, `lib/navigation/`, `lib/l10n/`, `lib/network_error/`, `lib/selector/` | **Keep** |
| `lib/user_profile/` | **Keep, self-view only**: delete follow/unfollow UI, follower counts, other-user statistics tabs |
| `lib/bootstrap.dart`, `lib/main_*.dart` | **Edit**: remove Firebase init, remote-config gates, notification registration |

### 1b. Packages — `packages/`

| Package | Action |
|---|---|
| `stories_editor`, `stories_repository` | **Delete** |
| `notifications_client`, `notifications_repository` | **Delete** (push deferred) |
| `firebase_remote_config_repository` | **Delete** (feature flags deferred; inline any flag reads as constants) |
| `search_repository` | **Delete** (goes with `lib/search/`) |
| `insta_blocks`, `instagram_blocks_ui` | **Trim, then rename** in Phase 3: keep post/feed block types + widgets used by feed card & detail; delete story/reel/comment/like blocks |
| `posts_repository` | **Keep** → becomes `listings_repository` in Phase 3 |
| `chats_repository` | **Keep** |
| `app_ui`, `authentication_client`, `database_client`, `env`, `form_fields`, `shared`, `storage`, `powersync_repository`, `user_repository` | **Keep** |
| `gallery_media_picker`, `image_picker_plus` | **Keep** (consolidate in Phase 5) |
| `user_repository` | **Trim**: delete follow/subscription methods |

### 1c. Mechanical sweep after deletes

- Remove deleted packages from root `pubspec.yaml` and every remaining package's `pubspec.yaml`.
- Remove Firebase deps (`firebase_core`, `firebase_messaging`, remote config) from pubspecs, plus `google-services.json` / `GoogleService-Info.plist` and the Gradle/CocoaPods plugin wiring.
- Delete routes for removed features in `lib/navigation/`; bottom nav collapses to: **Browse · Sell (create) · Messages · Profile**.
- `flutter analyze` clean; app boots; feed/create/chat/profile still work (still Instagram-flavored — that's fine, domain rewrite is next).

**Exit criteria:** compiling app with only auth, feed, post-create, chat, self-profile. No Firebase. Roughly half the packages gone.

---

## Phase 2 — Backend schema, RLS, and sync rules

Do the backend *before* the client domain rewrite so the client has something real to point at.
Write these as Supabase SQL migrations (keep them in `supabase/migrations/` in the new repo — the clone's single `tables.sql` becomes migration 0).

### 2a. Schema (delta from the clone's `tables.sql`)

```sql
-- DROP social tables
drop table if exists likes, comments, subscriptions, stories, videos cascade;

-- posts → listings
alter table posts rename to listings;
alter table listings rename column user_id to seller_id;
alter table listings rename column caption to description;
alter table listings
  add column title text not null default '',
  add column price numeric,                 -- nullable: free / borrow / contact
  add column condition text not null default 'used_good'
    check (condition in ('new','used_like_new','used_good','used_fair','for_parts')),
  add column category text not null default 'other'
    check (category in ('wheelchair','powerchair','mobility_scooter','walker','transfer',
                        'cushion_seating','ramps_access','vehicle','sports_recreation','parts','other')),
  add column location text not null default '',   -- freeform "city, region" for MVP; geo later
  add column status text not null default 'active'
    check (status in ('draft','active','reserved','sold','removed'));
-- keep: id, media (text column holding a JSON array — do NOT change its type), created_at

-- scope chat threads to listings
-- Verified: conversations has NO creator column (id, type, name, created_at, updated_at only),
-- and participants is a plain (user_id, conversation_id) join table — so the buyer must be
-- stored explicitly for the one-thread-per-buyer-per-listing invariant to be enforceable.
alter table conversations
  add column listing_id uuid references listings(id) on delete set null,
  add column buyer_id uuid references profiles(id);
create unique index one_thread_per_buyer_per_listing
  on conversations (listing_id, buyer_id)
  where listing_id is not null;
-- App invariant (enforced by openListingThread in Phase 3): a listing conversation has exactly
-- two participants rows — buyer_id and the listing's seller_id. Optionally add a trigger that
-- inserts both participants on conversation insert so the invariant can't drift.

-- profiles: keep as-is (id, full_name, email, username, avatar_url); drop push_token
alter table profiles drop column if exists push_token;
```

Keep `images` (media upload bookkeeping) and `messages`/`participants`/`attachments` unchanged.
Note: `messages.shared_post_id` FKs into `posts`; the table rename carries the FK along
automatically, and "share a post in chat" becomes "share a listing in chat" for free — leave it.

### 2b. Row-level security (seller/buyer isolation)

```sql
-- listings
alter table listings enable row level security;

-- Read access has THREE legs, not two. Without the third, a seller marking an item
-- sold/reserved instantly revokes the buyer's read access to a listing they have an
-- open thread on — which blanks the thread header and Messages tab that Phase 4
-- builds around the listing. Conversation participants keep read access for the
-- life of the thread, regardless of listing status.
create policy listings_read on listings for select using (
  status = 'active'
  or seller_id = auth.uid()
  or exists (
    select 1
    from conversations c
    join participants p on p.conversation_id = c.id
    where c.listing_id = listings.id
      and p.user_id = auth.uid()
  )
);
create policy listings_insert on listings for insert with check (seller_id = auth.uid());
create policy listings_update on listings for update using (seller_id = auth.uid());
create policy listings_delete on listings for delete using (seller_id = auth.uid());

-- conversations/messages: participants only (mirror the clone's existing chat policies,
-- adding: a conversation with listing_id must include the listing's seller as a participant)

-- profiles: public read, self write (clone default — keep)
```

Storage buckets: keep `avatars` + `posts` (rename bucket to `listings` only if cheap; bucket name is cosmetic).

### 2c. PowerSync sync rules

The buckets must mirror the three-legged read policy above — the sync layer and RLS fail in the
same buyer-loses-the-listing way if either one forgets the conversation leg. Note PowerSync data
queries can't join, so the conversation leg is its own parameterized bucket (one bucket instance
per listing the user has a thread on):

```yaml
bucket_definitions:
  global_listings:            # everyone can browse offline
    data:
      - select * from listings where status = 'active'
      - select id, username, full_name, avatar_url from profiles
  user_own:                   # your drafts/reserved/sold + your threads
    parameters: select request.user_id() as user_id
    data:
      - select * from listings where seller_id = bucket.user_id
      - select * from conversations where id in
          (select conversation_id from participants where user_id = bucket.user_id)
      - select * from participants where user_id = bucket.user_id
      - select * from messages where conversation_id in
          (select conversation_id from participants where user_id = bucket.user_id)
  thread_listings:            # listings you have a thread on, ANY status —
    parameters:               # keeps thread headers alive after the item sells,
      - select c.listing_id as listing_id      # even offline
          from conversations c
          join participants p on p.conversation_id = c.id
          where p.user_id = request.user_id()
            and c.listing_id is not null
    data:
      - select * from listings where id = bucket.listing_id
```

Drop the clone's buckets for likes/comments/subscriptions/stories. If the global listings bucket
grows too large later, bound it (`created_at > now() - interval '90 days'`) — note it as a known cap.

**Exit criteria:** migrations apply cleanly to a fresh Supabase project; RLS verified with two test
users via SQL: (a) user A cannot update user B's listing, (b) non-participant cannot read a thread,
(c) **buyer with an open thread can still `select` the listing after the seller sets
`status = 'sold'`**, (d) a user with no thread cannot read that sold listing. PowerSync dashboard
shows all three buckets syncing, and a device signed in as the buyer still has the sold listing
in its local DB after a fresh sync.

---

## Phase 3 — Domain & repository rewrite (client)

The riskiest phase. Order of operations keeps the app compiling: add new code alongside, port callers, then delete old code.

### 3a. Model

In `packages/insta_blocks` (rename package → `listing_blocks` at the end of this phase):

```dart
class Listing {
  final String id;
  final String sellerId;
  final String title;
  final String description;
  final double? price;            // null = free / contact seller
  final ListingCondition condition;
  final ListingCategory category;
  final String location;
  final ListingStatus status;     // draft | active | reserved | sold | removed
  final List<MediaItem> media;    // reuse the clone's media/blur-hash types
  final DateTime createdAt;
}
```

Delete from the old `PostBlock` world: likes count, comments count, engagement fields, sponsored/ad block types.

### 3b. Repository

`packages/posts_repository` → `packages/listings_repository`:

```dart
abstract class ListingsRepository {
  Stream<List<Listing>> watchListings({int limit, ListingCategory? category});
  Stream<Listing> watchListing(String id);
  Future<String> createListing(CreateListingInput input);   // returns id; works offline
  Future<void> updateStatus(String id, ListingStatus status); // mark sold/reserved
  Stream<List<Listing>> watchUserListings(String userId);
}
```

Implementation notes:
- Back everything with **PowerSync watch queries** (the clone's pattern) — `Stream`, not `Future`, so offline writes appear instantly via the local DB. This is the offline-first payoff; don't regress to request/response.
- `createListing` = local insert + media staged to the storage upload queue (reuse the clone's post-media upload path).
- Update `packages/database_client` queries: table/column renames from Phase 2, delete like/comment/story queries.
- `chats_repository`: add `Future<String> openListingThread(String listingId, String sellerId)` — find-or-create keyed on the Phase 2 unique index: `insert into conversations (listing_id, buyer_id, …) … on conflict (listing_id, buyer_id) do nothing`, then select the row and ensure both `participants` rows (buyer + seller) exist. The DB constraint — not client logic — is what guarantees one thread per buyer per listing when two devices race offline. Everything else in the package stays.
- `user_repository`: delete follow/subscription APIs (done in 1b if not already).

### 3c. BLoC collapse

Keep the clone's bloc-per-feature layout, reduced to:

| Bloc | Events |
|---|---|
| `ListingsFeedBloc` | load / refresh / paginate (category filter optional param, no UI yet) |
| `ListingDetailBloc` | load |
| `CreateListingBloc` | pickMedia / submit |
| `ListingThreadBloc` (from chat bloc) | loadThread / sendMessage |
| `UserListingsBloc` (profile) | load / markSold |

Delete cross-feature orchestration (feed↔stories↔reels event wiring).

**Exit criteria:** `flutter analyze` clean; app compiles with `Listing` end-to-end; creating a listing offline shows it in the local feed immediately and syncs on reconnect.

---

## Phase 4 — UI reduction (4 screens + create flow)

All built from kept `app_ui` / blocks-UI widgets; no new design system work.

1. **Browse** (from feed): `ListView` of `ListingCard` — first photo, title, price (or "Free"), location, condition chip. Latest-first. No engagement overlays, no ranking.
2. **Listing detail**: image carousel (reuse post carousel), title, price, condition, category, location, description, seller row (avatar + name), primary button **Message seller** → `openListingThread`, and — if own listing — **Mark as sold**.
3. **Create listing**: media picker (kept package) → title, price (optional), condition picker, category picker, location text field, description → submit. Validation via `packages/form_fields`. No drafts/autosave.
4. **Thread view**: the clone's chat screen, entered only from a listing; header shows listing thumbnail + title. Messages tab lists your threads grouped by listing (this is the clone's conversation list, relabeled).
5. **Profile (self)**: avatar, name, grid/list of own listings with status badges; edit avatar/name only.

**Exit criteria:** full happy path on device: sign in → create listing with 2 photos → see it in Browse → open detail from a second account → message seller → seller replies → seller marks sold → listing leaves Browse — **and the buyer's thread stays intact**: the Messages tab entry and thread header still show the sold listing's thumbnail and title (this is what the third RLS leg and `thread_listings` bucket exist for; if the header blanks, Phase 2 regressed).

---

## Phase 5 — Offline validation, cleanup, and hardening

1. **Offline test matrix** (manual on device, scripted where possible):
   - create listing offline → sync on reconnect (media too)
   - browse previously-synced listings offline
   - send message offline → delivers on reconnect
   - **seller marks sold while buyer is offline** → buyer reconnects → listing leaves buyer's Browse but their thread header/detail still render from the `thread_listings` bucket
   - two devices race `openListingThread` for the same (listing, buyer) offline → on sync, exactly one conversation survives (unique index wins), no orphaned messages
   - two-device write conflict on same listing (seller edits on A and B) → last-write-wins, no crash, no data corruption
   - kill app mid-upload → resumes/retries
2. Consolidate to **one** media picker package; delete the other.
3. Rename sweep: `insta_blocks` → `listing_blocks`, `instagram_blocks_ui` → `listing_blocks_ui`, app id → `com.paraboards.gearexchange` (or final name), display name, icons, l10n strings that say "post/story/follower".
4. Delete `test/` cases for removed features; keep/port repository tests for listings + chat; add tests for `ListingsRepository` create/watch and the thread find-or-create.
5. CI: single GitHub Actions workflow — analyze + test + debug build for the app package and each kept package.

**Exit criteria = MVP Definition of Done:**
- fresh clone + documented setup (`README` rewrite) gets a new dev to a running app in < 1 hour
- offline matrix above passes
- RLS checks from Phase 2 pass against production project
- no source file references stories/reels/likes/comments/follows

---

## Execution order & rough effort

Assumes one experienced Flutter dev, part-time buffer included. The draft's "1–2 weeks" is optimistic once media sync and chat scoping are included; this is the honest version.

| # | Phase | Effort | Hard dependency |
|---|---|---|---|
| 0 | Green baseline on own infra | 1–2 days | Supabase + PowerSync accounts |
| 1 | Delete map + compile fix | 2–3 days | 0 |
| 2 | Schema, RLS, sync rules | 1–2 days | 0 (parallel with 1) |
| 3 | Domain + repository rewrite | 3–5 days | 1, 2 |
| 4 | UI reduction | 3–4 days | 3 |
| 5 | Offline validation + cleanup | 2–3 days | 4 |

**Plan around the high end: ~3.5 weeks, and hold ~25% contingency (≈4.5 weeks) before promising a date.**
The low end of each range compounds only if nothing surprises you, and the remaining known unknown —
how entangled the blocks packages are — lands squarely in Phase 3, the riskiest phase. (The chat
threading model was the other Phase 3 unknown; it's now resolved against the verified upstream
schema, which is why Phase 2 can be written as migrations rather than intentions.) Where estimates
like this slip is not writing new code but untangling old code that assumed a different domain.

---

## Deferred backlog (explicitly out of MVP)

In rough priority order for post-MVP:

1. Category browse + keyword search (rebuild `search_repository` against `listings`)
2. Watchlist/saves (the old likes plumbing maps here almost 1:1 — this is why we deferred rather than designed it)
3. Push notifications for new messages (restore `notifications_*` packages, re-add Firebase)
4. Offers/negotiation on threads (structured message type on existing `messages.type`)
5. Location: structured geo + distance filter
6. Payments (Stripe Connect) + reserved/escrow flow — only after real user demand; many adaptive-equipment exchanges settle off-platform
7. Ratings/reputation, moderation/reporting

---

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Upstream repo drifts / tutorial outdated | Follow `MIGRATION.md` only; pin SDK with FVM; never plan to merge upstream |
| Blocks packages more entangled than expected (feed widgets importing story/comment types) | Phase 1 deletes leaves blocks *trimmed not renamed*; budget in Phase 3 covers untangling; if a widget resists, copy it into `app_ui` and sever the import. This is the remaining known unknown — it's why the estimate plans around the high end |
| RLS and sync rules drifting apart (each layer looks correct alone, together they strand a user) | The listing-visibility rule is stated once — "active to everyone, everything to the seller, thread listings to participants" — and both 2b (RLS) and 2c (buckets) implement it; any future status/visibility change must update both and re-run Phase 2 exit checks (c)/(d) |
| Two media-tracking mechanisms (`listings.media` JSON text + `images` table) hiding silent bugs | Phase 0 step 7 documents the split before anything touches it; treat the upload queue as read-only reuse |
| PowerSync global bucket unbounded growth | Bound by `status='active'` now; add time window when listing count warrants |
| Media upload offline edge cases | Reuse the clone's existing upload queue untouched; it's the most battle-tested part of the repo |
| RLS mistakes leak drafts or private threads | Phase 2 exit criteria include adversarial two-user SQL tests; repeat in Phase 5 against prod |
| License/attribution | Confirm the repo `LICENSE` terms and retain required notices in the fork before any public release |

---

## First concrete actions (this week)

- [ ] Verify upstream `LICENSE` permits this use; note attribution requirements
- [ ] Create new GitHub repo, fork/import the clone, add upstream remote
- [ ] Create Supabase project + PowerSync instance (dev tier)
- [ ] Run Phase 0 to a green baseline
- [ ] Open tracking issues, one per phase, with each phase's exit criteria pasted in as the acceptance checklist
