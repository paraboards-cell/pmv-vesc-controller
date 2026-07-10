# Adaptive Equipment Marketplace — Executable MVP Plan (v3)

**Base repo:** [`itsezlife/flutter-instagram-offline-first-clone`](https://github.com/itsezlife/flutter-instagram-offline-first-clone)
(Flutter + BLoC + PowerSync + Supabase, MIT-style monorepo — verify `LICENSE` before shipping)

> All schema claims in this document (column names, types, constraints on `conversations`,
> `participants`, `messages`, `posts`, `images`) are verified against the upstream
> [`packages/database_client/tables.sql`](https://github.com/itsezlife/flutter-instagram-offline-first-clone/blob/main/packages/database_client/tables.sql)
> as of 2026-07 — not assumed from the clone's README. Re-verify against your forked commit in Phase 0.

> **Team execution:** the workstream split, ownership map, and merge plan for building this
> concurrently with a team live in [`marketplace-team-split.md`](./marketplace-team-split.md).

> **v3** reconciles two independent reviews of v2. Every accepted, corrected, and rejected
> suggestion is logged with reasoning in the **Appendix — v3 reconciliation log** at the bottom.
> Two review claims failed schema verification and are corrected inline (`images.post_id`, price
> semantics); one review's RLS sketch would have reintroduced the buyer-loses-listing bug fixed in v2.

**Goal:** strip the clone down to an offline-first equipment exchange marketplace MVP:
**create listing → browse → view detail → message seller**, with a self-profile showing your own listings.

**Non-goals for MVP (deferred backlog):** payments/escrow, offers/negotiation, ratings/reputation,
keyword search + price-range filters (category chips *are* in MVP — see decisions), push
notifications, moderation, watchlist/saves, global chat inbox.

---

## 0. Decisions locked in up front

These were open questions in the draft; the plan below assumes these answers. Change them here, not mid-build.

| Decision | Choice | Rationale |
|---|---|---|
| New repo vs. in-place fork | **Fork into a new repo** (e.g. `paraboards-cell/adaptive-gear-exchange`), keep upstream as a remote for reference only | The delete surface is ~50% of the codebase; you will never merge upstream again |
| Chat model | **Keep the clone's `conversations`/`participants`/`messages` tables, add `listing_id` *and* `buyer_id` to `conversations`** | Reusing the working chat sync path is far cheaper than a bespoke `MessagesRepository(listingId)`. Verified: `conversations` has **no** creator/`created_by` column (only `id, type, name, created_at, updated_at`) — the buyer's identity exists only via the `participants` join table, so "one thread per buyer per listing" needs an explicit `buyer_id` column to hang a uniqueness constraint on |
| Listing media | **Keep the clone's `media` column pattern (renamed onto `listings`)** instead of `media_urls text[]` | Verified: `posts.media` is a **`text` column holding a JSON-encoded array**, not `jsonb`. Keep it exactly as-is — the feed UI blocks, blur-hash previews, and storage upload path all serialize to/from that shape; changing the column type or moving to `text[]` forces a rewrite of working code. MVP restricts listing media to **photos only** (the media JSON can reference video entries; the `videos` table is being dropped) |
| Media picker | Keep **both** `gallery_media_picker` and `image_picker_plus` through Phase 3, consolidate to one in Phase 5 | The create-post flow depends on them; consolidating early risks breaking the one flow we must keep |
| Conflict resolution | Last-write-wins (PowerSync default) | Safe here *because of* the RLS design, not despite it: listings are single-writer — only the seller can update, so buyers physically cannot generate competing writes — and messages are append-only inserts, which cannot conflict. A reviewer proposed per-field merge rules ("seller fields → seller wins, status → server authoritative"); that machinery would resolve conflicts the permission model already prevents, at the cost of custom PowerSync upload logic. Revisit only when multi-party writes appear (e.g. offers) |
| Firebase | **Remove entirely** | It only serves push notifications + remote config, both deferred; removing it simplifies bootstrap and CI |
| Auth | Keep Supabase auth from `authentication_client`, email/password (+ Google if it works out of the box); enable **magic-link OTP** if the clone's auth screens make it cheap | Required anyway for RLS and messaging. Passwordless sign-in is also an accessibility win for users with motor or cognitive impairments — worth having for this audience if it's a config flag, not worth new screens in MVP |
| Price | `numeric` column, **nullable** — "free / borrow / contact me" is common for adaptive-equipment exchange | Domain reality: much of this gear is given away or lent |
| **Schema migration strategy** | **Idempotent SQL migrations in `supabase/migrations/`** | Each phase's schema changes are a numbered migration file; never edit a deployed migration. The clone's `tables.sql` becomes `00000000000000_initial_clone_schema.sql` |
| **Media storage bucket** | **Keep `posts` bucket name, add `listings` alias/view only if needed** | Renaming buckets in Supabase Storage is not trivial (requires data migration or new bucket + copy). The bucket name is purely cosmetic; changing it risks breaking the upload queue. Document the mismatch and move on |
| **Multi-flavor builds** | **Collapse to `development` and `production` only** | The clone may have `staging` or `profile` flavors. For MVP, only two `.env` files and two `main_*.dart` entry points are needed. Delete extra flavors in Phase 1 to reduce env surface area |
| **Analytics** | **Remove entirely** | No analytics in MVP. If the clone has Firebase Analytics or similar, strip it with the rest of Firebase. Add a simple event log table later if needed |
| **Accessibility** | **WCAG 2.1 AA compliance required from day one** | This is adaptive equipment for people with disabilities. Screen reader support, sufficient color contrast, and dynamic type scaling are not nice-to-haves — they are the reason the app exists. Test with VoiceOver/TalkBack in Phase 4 |
| **Soft delete vs hard delete** | **Soft delete via `status = 'removed'`** | Never `DELETE` a listing row. Hard deletes break thread history (the `listing_id` FK on conversations would cascade or nullify). Sellers "remove" listings; buyers in threads still see the header. This also preserves evidence for any future moderation needs |
| **Conversation title for listing threads** | **Auto-generated: listing title truncated to 40 chars** | The `conversations.name` column stays; populate it on thread creation with the listing title. Falls back to "Conversation" if listing title is empty. This lets the Messages tab show something meaningful without a join at render time |
| **Buyer profile visibility in thread** | **Show avatar + first name only** | The seller sees the buyer's profile avatar and `full_name` from the `profiles` table. No link to a full profile view (that feature is deleted). This is enough context to know who you're talking to without rebuilding the social graph |
| **Existing data migration** | **Wipe all clone test data before Phase 2 migrations** | The `likes`, `comments`, `subscriptions`, `stories`, `videos` tables are dropped with `CASCADE`. Any existing `posts` rows become `listings` with `status = 'active'` and empty `title` (backfilled from first 50 chars of `caption` if possible). **Document this decision**: if you have production clone data you care about, extract it before running Phase 2 migrations |
| **ID generation for offline listing creation** | **Client-generated UUID v4** | PowerSync supports offline inserts. The client generates the `listings.id` UUID before the insert hits the local SQLite DB. This lets media uploads queue against a known ID immediately. The `created_at` timestamp is also client-generated (`DateTime.now().toUtc()`) |
| **Price display** | **`0` → "Free", `null` → "Contact seller", `>0` → "$X"** | *Corrected in v3*: the reviewed draft inverted this (null = free, 0 = contact seller), assigning each magic value its less intuitive meaning. Zero dollars reading as "free" is what every user and every future dev will guess; an *absent* price reading as "ask me" likewise. Implement the mapping in exactly one place — a `PriceLabel` helper used by the card, the detail screen, and the create form preview — never inline |
| **Location field** | **Freeform text, max 100 chars, no validation** | MVP does not geocode or validate locations. The field is a text label like "Portland, OR" or "Ships nationwide". Structured geo with lat/lng is deferred. Add a placeholder hint in the UI: "City, state or shipping info" |
| **Images table RLS** | **Owner-based, not listing-join** | *Corrected in v3*: the reviewed draft claimed `images` has a `post_id` column and built its RLS join and a rename migration on it. Verified upstream, `images` is `(id, owner_id, url, blur_hash)` — there is **no** `post_id`, and `alter table images rename column post_id to listing_id` fails on first run. The underlying concern (images needs RLS) stands; the correct policy is owner-based: open read (rows are blur-hash/upload bookkeeping; the files themselves are governed by Storage policies), write only where `owner_id = auth.uid()`. If per-listing image visibility ever matters, *add* a nullable `listing_id` column in a new migration — don't rename a phantom one |
| **Supabase Storage RLS** | **Public read for listings bucket, authenticated write only** | Listing photos are public (anyone browsing can view them). Upload requires auth. The storage path convention: `listings/{listing_id}/{filename}`. This matches the clone's existing `posts/{post_id}/{filename}` pattern — just update the path builder in the upload queue |
| **Conversation auto-participant trigger** | **Required, not optional** | The plan mentions this trigger as "optional" — it is not. Without it, a race condition between two offline devices creating the same conversation can leave one participant row missing. The trigger fires on `conversations` insert and ensures both `participants` rows exist atomically. Implement this in Phase 2, not Phase 3 |
| **Test data / seeding** | **Seed script in `supabase/seed.sql`** | After Phase 2 migrations, run a seed script that creates 3 test users, 5 test listings across categories, and 2 test conversations. This gives you realistic data for UI development in Phase 4 without manually creating listings every time you reset the dev database |
| **App store compliance** | **Expect extra scrutiny in the adaptive/medical space; document accessibility in the store listing** | *Softened in v3*: there is no formal "accessibility review" gate at Apple or Google, so don't plan around one — but reviewers do scrutinize health/adaptive-adjacent apps more closely, and an accessibility statement in the listing helps both review and users. Include alt text for all listing photos (fallback: listing title + condition) |
| **Listing editing** | **In MVP**: `updateListing` in the repository + an edit screen that reuses the create form | *Added in v3 (review-sourced)*: a seller who typos a price must be able to fix it without delete-and-relist — relisting would orphan every open thread on the listing. `CreateListingBloc` gains an edit mode; RLS already restricts updates to the seller, so this is UI + one repository method |
| **Category filter on Browse** | **In MVP**: a single row of single-select category chips | *Added in v3 (review-sourced)*: the `category` column and the repository's `category` param already exist in this plan, so the marginal cost is one chip-row widget — and for a marketplace, category browse is closer to core navigation than to "search". Keyword search and price-range filters stay deferred |
| **Watchlist** | **Stays deferred** — rejecting a review's promotion to MVP as "high ROI, low cost" | It is not low cost: a new synced table + RLS policies + a PowerSync bucket + save-state UI on every card and detail screen, all on the critical path of a four-screen MVP. Deferral is cheap precisely because the deleted likes plumbing maps ~1:1 onto it when the time comes — that asymmetry is the argument for waiting |
| **Thread behavior after sale** | **Threads stay open; header shows a "Sold" badge** — rejecting a review's read-only lock | Buyer and seller coordinate pickup, payment, and handoff *after* the item is marked sold; locking the thread cuts the conversation at exactly the moment they need it most. The lock would also add state machinery (and an unlock story) for negative value |

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
- Sweep the kept widgets for **engagement counters** (like/comment counts baked into feed cards and detail views) and remove them — they're easy to miss because they live inside widgets you're keeping.
- Remove **deep links / app links** that route to deleted features (stories, reels, social post shares); keep the listing-detail route working.
- Collapse any **multi-feed abstraction** to the single listings feed — one feed, one source.
- Delete the **staging flavor**: `main_staging.dart`, its `.env`, and the flavor wiring (per the two-flavor decision above).
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

-- Backfill titles from existing posts (run before enforcing NOT NULL if needed)
-- update listings set title = left(description, 50) where title = '';

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

-- CRITICAL: Auto-participant trigger. Not optional.
-- Ensures both buyer and seller are participants when a listing conversation is created.
-- Prevents race conditions where two offline devices create the same conversation
-- and one participant row is lost.
create or replace function auto_insert_listing_participants()
returns trigger as $$
begin
  if new.listing_id is not null and new.buyer_id is not null then
    -- insert buyer participant
    insert into participants (user_id, conversation_id)
    values (new.buyer_id, new.id)
    on conflict do nothing;
    -- insert seller participant
    insert into participants (user_id, conversation_id)
    select seller_id, new.id from listings where id = new.listing_id
    on conflict do nothing;
  end if;
  return new;
end;
$$ language plpgsql;

create trigger trg_auto_participants
  after insert on conversations
  for each row
  execute function auto_insert_listing_participants();

-- profiles: keep as-is (id, full_name, email, username, avatar_url); drop push_token
alter table profiles drop column if exists push_token;

-- Add indexes for browse performance
-- The global_listings bucket queries by status; this index makes it fast.
create index idx_listings_status_created on listings(status, created_at desc);
create index idx_listings_seller on listings(seller_id, created_at desc);
create index idx_listings_category on listings(category, status, created_at desc);

-- images: NOTHING to rename here. Verified shape is (id, owner_id, url, blur_hash) —
-- there is no post_id column. (A review proposed `alter table images rename column
-- post_id to listing_id`; it would fail on first run. Kept as a warning, not a step.)
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

-- images: owner-based RLS. The listing-join policy a review proposed is impossible as
-- written — images has no listing/post FK (verified: id, owner_id, url, blur_hash).
-- These rows are blur-hash/upload bookkeeping; the photo files themselves are governed
-- by Storage policies, and listing visibility is enforced on the listings row whose
-- media JSON carries the URLs. Open read matches the public-photos Storage decision.
alter table images enable row level security;
create policy images_read   on images for select using (true);
create policy images_insert on images for insert with check (owner_id = auth.uid());
create policy images_update on images for update using (owner_id = auth.uid());
create policy images_delete on images for delete using (owner_id = auth.uid());

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
  final DateTime? updatedAt;      // column exists upstream (nullable); set on every edit —
}                                 // needed for "edited" display and LWW debugging
```

Delete from the old `PostBlock` world: likes count, comments count, engagement fields, sponsored/ad block types.

### 3b. Repository

`packages/posts_repository` → `packages/listings_repository`:

```dart
abstract class ListingsRepository {
  Stream<List<Listing>> watchListings({int limit, ListingCategory? category});
  Stream<Listing> watchListing(String id);
  Future<String> createListing(CreateListingInput input);   // returns id; works offline
  Future<void> updateListing(String id, UpdateListingInput input); // seller edits; RLS enforces ownership
  Future<void> updateStatus(String id, ListingStatus status); // mark sold/reserved/removed
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
| `ListingsFeedBloc` | load / refresh / paginate / setCategory (backs the Browse chip row) |
| `ListingDetailBloc` | load |
| `CreateListingBloc` | pickMedia / submit / edit mode (loadExisting + save) |
| `ListingThreadBloc` (from chat bloc) | loadThread / sendMessage |
| `UserListingsBloc` (profile) | load / markSold |

Delete cross-feature orchestration (feed↔stories↔reels event wiring).

**Exit criteria:** `flutter analyze` clean; app compiles with `Listing` end-to-end; creating a listing offline shows it in the local feed immediately and syncs on reconnect.

---

## Phase 4 — UI reduction (4 screens + create flow)

All built from kept `app_ui` / blocks-UI widgets; no new design system work.

1. **Browse** (from feed): `ListView` of `ListingCard` — first photo, title, price label (via the single `PriceLabel` helper), location, condition chip. Latest-first, paginated (limit 20) with lazy/cached images (the clone's feed already does both — verify, don't rebuild). A single-select **category chip row** above the list drives `ListingsFeedBloc.setCategory`. **Pull-to-refresh** and an **empty state** ("No listings yet — be the first"). No engagement overlays, no ranking.
2. **Listing detail**: image carousel (reuse post carousel), title, price, condition, category, location, description, seller row (avatar + name), primary button **Message seller** → `openListingThread`; if own listing: **Edit** (create form in edit mode) and **Mark as sold**.
3. **Create listing**: media picker (kept package) → title, price (optional — free/borrow gear is core to this domain, so no `price > 0` rule), condition picker, category picker, location text field, description → submit. Validation via `packages/form_fields`: **≥ 1 photo, title min length, condition and category required**. No drafts/autosave.
4. **Thread view**: the clone's chat screen, entered only from a listing; header shows listing thumbnail + title + **status badge ("Sold"/"Reserved") when applicable — the thread never locks**. Messages tab lists your threads grouped by listing (this is the clone's conversation list, relabeled).
5. **Profile (self)**: avatar, name, grid/list of own listings with status badges; edit avatar/name only.
6. **Accessibility pass (WCAG 2.1 AA)** — a Phase 4 exit gate, not polish: every screen navigable with VoiceOver and TalkBack, semantic labels on all interactive elements, alt text on listing photos (fallback: title + condition), contrast ≥ 4.5:1, tap targets ≥ 48dp, layouts survive large dynamic type. This audience is the reason the app exists.

**Exit criteria:** full happy path on device: sign in → create listing with 2 photos → see it in Browse → open detail from a second account → message seller → seller replies → seller marks sold → listing leaves Browse — **and the buyer's thread stays intact**: the Messages tab entry and thread header still show the sold listing's thumbnail and title (this is what the third RLS leg and `thread_listings` bucket exist for; if the header blanks, Phase 2 regressed). The full happy path must also be completable with TalkBack or VoiceOver enabled.

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
- accessibility: happy path passes under screen reader; contrast and tap-target audit clean
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
| 4 | UI reduction (incl. category chips, edit mode, accessibility pass) | 4–6 days | 3 |
| 5 | Offline validation + cleanup | 2–3 days | 4 |

**Plan around the high end: ~4 weeks, and hold ~25% contingency (≈5 weeks) before promising a date.**
(v3 note: the review-sourced additions — listing editing, category chips, and above all the
WCAG pass — bought Phase 4 one to two extra days. That's the honest cost of accepting them;
plans that absorb scope without moving the estimate are the ones that slip silently.)
The low end of each range compounds only if nothing surprises you, and the remaining known unknown —
how entangled the blocks packages are — lands squarely in Phase 3, the riskiest phase. (The chat
threading model was the other Phase 3 unknown; it's now resolved against the verified upstream
schema, which is why Phase 2 can be written as migrations rather than intentions.) Where estimates
like this slip is not writing new code but untangling old code that assumed a different domain.

---

## Deferred backlog (explicitly out of MVP)

In rough priority order for post-MVP:

1. Keyword search + price-range filters (category chips ship in MVP; rebuild `search_repository` against `listings` for the rest)
2. Watchlist/saves (the old likes plumbing maps here almost 1:1 — this is why we deferred rather than designed it; a review's push to include it in MVP is rejected in the appendix)
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
| **Accessibility audit failure** | **Build WCAG 2.1 AA compliance into Phase 4 from day one. Test with screen readers. The target users depend on this.** |
| **Conversation participant race condition** | **The auto-participant trigger in Phase 2 eliminates this. Do not skip it.** |
| **Images table RLS gap** | Owner-based RLS on `images` (write: owner only; open read matching public Storage). Note: a review proposed listing-join visibility via `images.listing_id` — that column doesn't exist (verified: `id, owner_id, url, blur_hash`), so the join policy and its rename migration were corrected, not adopted |
| **Storage path mismatch after bucket rename** | **Keep the `posts` bucket name. Update only the path builder string in the upload queue from `posts/` to `listings/` if you want cosmetic consistency. The bucket name itself is irrelevant.** |
| **Soft delete confusion** | **Document clearly: sellers "remove" listings (status = 'removed'), they do not delete them. The UI button says "Remove listing", not "Delete".** |
| **Price = 0 vs price = null ambiguity** | Convention (corrected in v3): **0 = "Free", null = "Contact seller", >0 = "$X"** — the intuitive reading of each value. One `PriceLabel` helper owns the mapping; nothing else interprets `price` |

---

## First concrete actions (this week)

- [ ] Verify upstream `LICENSE` permits this use; note attribution requirements
- [ ] Create new GitHub repo, fork/import the clone, add upstream remote
- [ ] Create Supabase project + PowerSync instance (dev tier)
- [ ] Run Phase 0 to a green baseline
- [ ] Open tracking issues, one per phase, with each phase's exit criteria pasted in as the acceptance checklist

---

## Appendix — v3 reconciliation log

Two independent reviews of v2 were reconciled into this version: **Review A** (an annotated
draft that expanded the decisions table and hardened Phase 2) and **Review B** (a restructured
plan built around "intent primitives"). Every suggestion got one of three verdicts —
**integrated**, **corrected** (right concern, wrong facts or wrong shape), or **rejected**.
The tiebreaker throughout: does the suggestion survive contact with the *verified* upstream
schema, and does it buy MVP value worth its scope?

### Review A — verdicts

| Suggestion | Verdict | Reasoning |
|---|---|---|
| Migration strategy, keep `posts` bucket, two flavors only, strip analytics, seed script, client-generated UUIDs + timestamps, location cap, auto conversation title, buyer visibility scope, dev-data wipe/backfill | **Integrated as-is** | Low-risk operational hygiene that makes the plan more executable without changing its shape. The conversation-title item is quietly important: `conversations.name` is `NOT NULL` upstream, so it *must* be populated on thread creation anyway |
| WCAG 2.1 AA from day one | **Integrated and promoted to a Phase 4 exit gate + DoD line** | The single best addition from either review. The users of an adaptive-equipment exchange are disproportionately screen-reader, switch-access, and large-type users — shipping inaccessible would fail the product's entire premise. Costed honestly: Phase 4 grew 1–2 days |
| Soft delete via `status = 'removed'` | **Integrated** | Consistent with the existing status enum (which already had `removed`) and protects thread history behind the `listing_id` FK; belt-and-suspenders with the FK's `on delete set null` |
| Auto-participant trigger "required, not optional" | **Integrated** — upgraded from v2's "optionally add a trigger" | The race argument is correct: two offline devices resolving the same find-or-create can leave a participant row missing if only client logic inserts them. Server-side trigger makes the invariant atomic; the client still inserts locally for offline UX |
| Performance indexes on `listings` | **Integrated** | Matches exactly how the buckets and Browse query the table |
| Images RLS via join on `images.post_id` → rename to `listing_id` | **Corrected** | Built on a column that does not exist. Verified upstream: `images` is `(id, owner_id, url, blur_hash)` — no `post_id`. The rename migration would fail on first run, and the join policy references a phantom column. The valid underlying concern (images lacks RLS) is kept, implemented owner-based. This is precisely why v2 added the provenance rule: every schema claim gets checked against `tables.sql`, including reviewers' |
| Price semantics: null = "Free", 0 = "Contact seller" | **Corrected (inverted)** | Assigns each magic value its less intuitive meaning — $0 reads as "free" to every user and future maintainer, and an absent price reads as "ask me". v3 locks the intuitive mapping and centralizes it in one `PriceLabel` helper |
| "Apple/Google flag adaptive apps for accessibility review" | **Corrected (softened)** | No such formal review gate exists; planning around a fictional gate misallocates effort. Kept as: expect extra reviewer scrutiny, document accessibility in the store listing |

### Review B — verdicts

| Suggestion | Verdict | Reasoning |
|---|---|---|
| Bespoke `message_threads` table + new `MessagesRepository` | **Rejected** | Re-solves a problem v2 already solved *against the verified schema* — its "threads tied only to listingId break multi-buyer" critique targets the original draft, not v2, which already enforces uniqueness on `(listing_id, buyer_id)`. A bespoke table discards the clone's working chat sync path (typed messages, replies, read state, attachments) and rebuilds it worse |
| `media_urls text[]` on listings | **Rejected** | Contradicts the verified `media` `text`/JSON column that the entire UI-block and upload pipeline serializes against; rejected in v2 for the same reason |
| Its RLS sketch (read: public if active; write: seller) | **Rejected** | Strictly weaker than v2's — it's missing the third read leg, so it would *reintroduce* the buyer-loses-sold-listing bug that the previous critique round existed to fix. A useful reminder that "add security section" isn't the same as "add the right policies" |
| Apply RLS at step 7, after wiring the UI | **Rejected** | Backend-before-client ordering stays (Phase 2 before 3). Policies applied after the client is built are policies the client was never tested against — that's how leaks ship |
| `price > 0` required validation | **Rejected** | Conflicts with the locked domain decision: free/borrow/lend is core to adaptive-equipment exchange, not an edge case |
| Lock thread read-only when listing sells | **Rejected, replaced with a "Sold" header badge** | Buyers and sellers coordinate handoff *after* the sale; locking severs the conversation at its most necessary moment |
| Watchlist in MVP ("high ROI, low cost") | **Rejected — stays backlog #2** | The cost claim doesn't hold: new synced table + RLS + PowerSync bucket + per-card UI state, on the critical path of a four-screen MVP. Deferral stays cheap because the deleted likes plumbing maps ~1:1 later |
| Lifecycle `active→pending→sold→archived` | **Already covered** | v2's enum (`draft/active/reserved/sold/removed`) is a superset; `reserved` ≈ `pending`, `removed` ≈ `archived` with soft-delete semantics |
| "Ownership model" section | **Already covered** | v2 expresses ownership where it's actually enforced — RLS policies — rather than as prose; restating it adds no mechanism |
| Per-field conflict rules (seller wins / server-authoritative status) | **Reconciled: justification adopted, machinery rejected** | The concern is legitimate, but the RLS design already makes listings single-writer and messages append-only, so LWW is provably sufficient. v3 upgrades the conflict-resolution decision's rationale from "fine for MVP" to the actual argument, and names the trigger for revisiting (multi-party writes, e.g. offers) |
| `updatedAt` in the domain model | **Integrated** | The column exists upstream (nullable) and v2's model omitted it; needed once editing exists |
| `updateListing` (edit flow) | **Integrated** | Genuine gap in v2: a seller who typos a price could only delete-and-relist, orphaning threads. Repository method + create-form edit mode |
| Category filter in MVP | **Integrated as a chip row** | Right call: the column and repository param already existed in the plan, so marginal cost is one widget, and category browse is core marketplace navigation. Price-range and keyword search stay deferred |
| Pull-to-refresh, empty states, pagination limit + lazy images | **Integrated** | Cheap, real UX table stakes; pagination/caching mostly verify-don't-rebuild since the clone's feed already does it |
| Extra cuts: deep links to social content, engagement counters inside kept widgets, multi-feed abstractions | **Integrated into Phase 1c** | Good catches — all three are the kind of residue that survives directory-level deletes because it lives inside files you keep |
| Magic-link auth | **Integrated as a config-level option** | Passwordless is an accessibility win for this audience; taken only if the clone's auth screens make it a flag, not new screens |
| "Intent primitives" framing (buy/save/availability) | **Noted, no structural change** | As vocabulary it's fine — message-seller, watchlist, and status already *are* those primitives. It prescribes no mechanism the plan lacks, so it changes nothing |
