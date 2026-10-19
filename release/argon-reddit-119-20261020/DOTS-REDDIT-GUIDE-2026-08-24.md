# Colosseum Reddit Marketing Research — Comprehensive Master Guide

**Research date:** 2026-08-24  
**Purpose:** Consolidated policy-audit and subreddit-selection guide for Colosseum's Reddit marketing campaign.  
**Scope:** Research and selection only. No marketing copy, moderator contact, publishing, cross-posting, or account manipulation is authorized by this document.

This master guide combines three completed research passes:

1. the original first-wave subreddit policy audit;
2. the deeper second-wave subreddit search after the first four communities had already been targeted; and
3. the ecosystem-adjacent research covering Stremio, MPV, Calibre, Jellyfin, Plex, Audiobookshelf, Kavita, Trakt, Kodi, Emby, Komga, and related communities.

The opening sections below are the **deduplicated operational view**. The complete underlying research reports are preserved afterward as appendices.

---

## 1. Governing campaign rules

The approved Colosseum marketing specification and implementation plan remain authoritative.

### Product framing

Colosseum is a native Windows desktop media environment with:

- **Tankoban** for manga and western comics;
- **Biblio** for books and audiobooks;
- **Theatre** for movies, shows, and anime;
- **Vault** for local media the user already owns;
- shared Home, Continue, Collection, Downloads, readers, sessions, and player surfaces.

### Public claims and disclosure

Every eventual post must:

- identify Hemanth openly as the developer;
- describe Colosseum as a client/local-media environment, not a media host;
- use only product claims that pass the separate runtime claims gate;
- avoid implying guaranteed third-party availability;
- avoid leading with torrents, external sources, extensions, free copyrighted content, "watch anything", "read anything", or unauthorized access;
- avoid astroturfing, fake user-testimonial language, artificial karma-building, or disguised promotion.

### Policy gate

Use these statuses:

- `audited` — current evidence clearly provides a relevant developer/project-post path.
- `blocked-for-human-review` — policy is ambiguous, moderator approval is required, account eligibility matters, authorship restrictions apply, or material safety/moderation risk remains.
- `not-recommended` — promotion is prohibited, topical fit is poor, or piracy/moderation risk is unacceptable for the current product/campaign.

A precedent post that merely survived moderation is useful evidence, but **does not override an unclear current rule**.

### Publication boundary

This guide never authorizes publication. Hemanth decides whether and when any audited bundle is posted.

---

# 2. Master campaign map

## A. Already targeted: original clean first wave

These were the strongest policy-clean communities from the first audit and have already been targeted.

| Priority | Community | Route | Status | Core reason |
|---|---|---|---|---|
| Completed | `r/windowsapps` | General / Windows | `audited` | Explicit developer-promotion pathway, `Developer` flair, weekly cadence, strong native media support |
| Completed | `r/SideProject` | General / maker | `audited` | Community explicitly exists for sharing side projects |
| Completed | `r/foss` | General / FOSS | `audited` | Promotion allowed when the project is FOSS; Colosseum is MIT licensed |
| Completed | `r/coolgithubprojects` | General / GitHub | `audited` | Own GitHub projects explicitly welcome, clear repost rules |

These should not be treated as fresh second-wave recommendations.

---

## B. Next clean targets: second-wave audited communities

These are the strongest **new** communities that passed the strict policy gate.

| Order | Community | Route | Best format | Key constraint | Status |
|---|---|---|---|---|---|
| 1 | `r/software` | General / software | Image/text/link-led software showcase | Follow current open-source/self-promo path; account karma threshold may apply | `audited` |
| 2 | `r/IMadeThis` | General / maker | Native video/GIF showcase | Keep it as a transparent "I made this" project post | `audited` |
| 3 | `r/ShowYourApp` | General / apps | Video/GIF + contextual app/demo link | No bare-link promotion | `audited` |
| 4 | `r/buildinpublic` | General / story | Build story, lessons, decisions, progress | Must not exist solely as an advertisement | `audited` |
| 5 | `r/github` | General / GitHub | Pinned project-promotion megathread entry | Standalone promotion is prohibited | `audited` |

### Recommended order

`r/software` → `r/IMadeThis` → `r/ShowYourApp` → `r/buildinpublic` → `r/github` megathread.

Do not reuse near-identical copy across these five. Their native expectations differ enough that each should receive its own later bundle after the claims gate passes.

---

## C. Highest-value ecosystem targets: resolve before generic expansion

These communities are **more relevant to Colosseum than many generic app subs**, but remain behind the human-review gate.

| Priority | Community | Why it matters | Current blocker | Status |
|---|---|---|---|---|
| 1 | `r/StremioMods` | Recent accepted independent Windows Stremio clients, including MPV-backed clients; close precedent for Theatre | Current explicit self-promotion rule could not be verified cleanly | `blocked-for-human-review` |
| 2 | `r/StremioAddons` | Colosseum genuinely supports Stremio-compatible extensions; accepted community-project announcements exist | Rule ambiguity + mandatory rules acknowledgement + very high piracy adjacency | `blocked-for-human-review` |
| 3 | `r/mpv` | Theatre actually uses MpvQt/libmpv | Current explicit developer-promotion cadence/flair still needs confirmation | `blocked-for-human-review` |
| 4 | `r/Calibre` | Excellent Biblio reader/library audience; current third-party reader developer posts exist | No current Calibre/OPDS integration; broader promo policy unclear | `blocked-for-human-review` |

### Ecosystem conclusion

The Stremio route is the most important unresolved Reddit opportunity because Colosseum has **real protocol-level adjacency**, rather than mere audience similarity.

If only one policy ambiguity gets resolved next, resolve `r/StremioMods`.

---

## D. Conditional niche communities worth keeping in reserve

These are not copy-ready, but could become valuable after a specific human or policy gate is resolved.

### Manga / comics

| Community | Gate | Status |
|---|---|---|
| `r/comicbooks` | Genuine prior community participation/self-promotion ratio required | `blocked-for-human-review` |
| `r/MangaCollectors` | Personal-project/self-promotion prohibition | `not-recommended` |
| `r/manga` | Drive-by self-promotion heavily moderated | `not-recommended` |
| `r/graphicnovels` | Self-promotion explicitly disallowed | `not-recommended` |
| `r/comics` | Wrong topical center: comics/OC rather than reader/library software | `not-recommended` |

### Books / audiobooks

| Community | Gate | Status |
|---|---|---|
| `r/ebooks` | Self-promotion rules clearly cover authors/publishers, not app developers | `blocked-for-human-review` |
| `r/audiobooks` | Promotion rules focus authors/narrators/publishers | `blocked-for-human-review` |
| `r/ereader` | Moderator approval for commercial/service promotion + AI-content restriction | `blocked-for-human-review` |
| `r/Calibre` | Strong audience, but no current Calibre/OPDS integration | `blocked-for-human-review` |
| `r/audiobookshelf` | Excellent future client ecosystem, but no current Audiobookshelf integration | `not-recommended` now |
| `r/KavitaManga` | Excellent books/comics/manga ecosystem, but no current Kavita integration | `not-recommended` now |
| `r/komga` | Strong comics/manga ecosystem, but no current Komga integration/policy route | `not-recommended` now |

### Theatre / video / anime

| Community | Gate | Status |
|---|---|---|
| `r/StremioMods` | Current self-promo rule verification | `blocked-for-human-review` |
| `r/StremioAddons` | Current self-promo rule verification + rules acknowledgement + piracy risk | `blocked-for-human-review` |
| `r/mpv` | Current promotion-policy confirmation | `blocked-for-human-review` |
| `r/Stremio` | Main subreddit requires posts to remain directly about Stremio | `not-recommended` for standalone Colosseum |
| `r/animeapp` | Prior moderator approval + very high piracy adjacency | `not-recommended` |
| `r/htpc` | Recent moderation rejects vested-interest/new-software promotion | `not-recommended` |
| `r/Jellyfin` | No current Jellyfin integration + strict AI/LLM disclosure/authorship policy | `not-recommended` now |
| `r/PleX` | No Plex integration + contentious self-promotion environment | `not-recommended` now |
| `r/trakt` | No current Trakt integration | `not-recommended` now |
| `r/kodi` | No integration and no strong current promotion pathway established | `not-recommended` |
| `r/emby` | No integration and no strong current promotion pathway established | `not-recommended` |
| `r/firetvstick` | Windows-only Colosseum creates a direct platform mismatch | `not-recommended` |

### Vault / local media / privacy

| Community | Gate | Status |
|---|---|---|
| `r/homelab` | Account/community eligibility + true homelab relevance | `blocked-for-human-review` |
| `r/degoogle` | Must truthfully replace/reduce a Google dependency | `blocked-for-human-review` |
| `r/PrivacyGuides` | Current developer-policy confirmation + genuine privacy/threat-model fit | `blocked-for-human-review` |
| `r/privacy` | Current advertising/app-promotion approval path closed | `not-recommended` |
| `r/DataHoarder` | Software-advertising/project moderation risk | `not-recommended` |
| `r/selfhosted` | Colosseum is not a self-hosted service | `not-recommended` |
| `r/HomeServer` | Native desktop client does not fit server-centric rules | `not-recommended` |

### General / developer / testing

| Community | Gate | Status |
|---|---|---|
| `r/opensource` | AI-generated public content prohibited; Hemanth must personally author/materially rewrite | `blocked-for-human-review` |
| `r/developersIndia` | Engineering-story framing required; traffic-driving launch tone risky | `blocked-for-human-review` |
| `r/TestMyApp` | Use only if the objective is genuinely testing/feedback, not marketing | `blocked-for-human-review` |
| `r/apps` | Developer self-promotion route remains ambiguous | `blocked-for-human-review` |
| `r/OpenSourceProjects` | Restricted posting access | `blocked-for-human-review` |
| `r/indiehackers` | Self-promo recognized but current cadence/account policy is evolving | `blocked-for-human-review` |
| `r/vibecoding` | Must be a real process/lessons story, not disguised advertising | `blocked-for-human-review` |
| `r/windows` | App-promotion approval route not currently accepting applicants | `not-recommended` |
| `r/Windows10` | App-promotion approval route not currently accepting applicants | `not-recommended` |
| `r/Windows11` | App-promotion approval route not currently accepting applicants | `not-recommended` |
| `r/FreeSoftware` | Current policy rejects AI-generated-code software submissions | `not-recommended` |
| `r/AppBusiness` | Explicitly prohibits own-app promotion outside feedback structure | `not-recommended` |
| `r/alphaandbetausers` | Real alpha/beta recruitment only; current campaign is a stable release | `not-recommended` |
| `r/BetaTesting` | No useful current path for external Windows app campaign | `not-recommended` |
| `r/softwaretesting` | Testing-profession community, not user recruitment/product promotion | `not-recommended` |

---

# 3. Master recommendation by campaign objective

## If the objective is "find more users now"

Use the clean second-wave sequence:

1. `r/software`
2. `r/IMadeThis`
3. `r/ShowYourApp`
4. `r/buildinpublic`
5. `r/github` megathread

## If the objective is "find the most relevant users"

Resolve:

1. `r/StremioMods`
2. `r/StremioAddons`
3. `r/comicbooks`
4. Biblio moderator gates (`r/ebooks`, `r/audiobooks`, `r/ereader`)
5. `r/homelab`

This is a slower policy path, but the audience quality is materially higher.

## If the objective is "find testers"

Only use `r/TestMyApp` if the post is honestly a testing request with concrete feedback needs.

Do not relabel marketing as beta/testing to fit a subreddit.

## If the objective is "tell the development story"

Best candidates:

- `r/buildinpublic`
- `r/developersIndia`
- `r/vibecoding`
- `r/indiehackers`
- `r/mpv` for a Theatre/libmpv technical story

## If the objective is "promote Biblio"

No Biblio-native subreddit currently clears the automatic gate.

Resolve moderator/policy questions for:

- `r/ebooks`
- `r/audiobooks`
- `r/ereader`

Longer-term integrations that would unlock dramatically better native ecosystems:

- Audiobookshelf → `r/audiobookshelf`
- Calibre-Web / OPDS → `r/Calibre`
- Kavita → `r/KavitaManga`

## If the objective is "promote Tankoban"

The best real reader-audience opportunity discovered is `r/comicbooks`, but only if Hemanth already satisfies its genuine community-participation requirements.

Do not manufacture activity to qualify.

The largest manga communities remain poor launch targets under current rules.

## If the objective is "promote Theatre"

The strongest opportunity is the **Stremio ecosystem** because Colosseum has actual extension compatibility.

Resolve `r/StremioMods` first, then `r/StremioAddons`.

`r/mpv` is the best technical-player route.

---

# 4. Media-format map

## Strong video/GIF-first destinations

- `r/windowsapps` — already targeted
- `r/SideProject` — already targeted
- `r/foss` — already targeted
- `r/IMadeThis`
- `r/ShowYourApp`
- `r/buildinpublic`

Conditional:
- `r/TestMyApp`
- `r/vibecoding`
- `r/StremioMods` after policy confirmation

## Better image/text/link destinations

- `r/software`
- `r/coolgithubprojects`
- `r/github`
- `r/comicbooks`
- `r/ebooks`
- `r/audiobooks`
- `r/Calibre`

## Better story/lessons destinations

- `r/buildinpublic`
- `r/developersIndia`
- `r/vibecoding`
- `r/indiehackers`
- `r/mpv`

---

# 5. Human decisions still required

Before copywriting begins for the affected communities, Hemanth must resolve these questions:

1. **Stremio ecosystem:** Should the first ecosystem post frame Colosseum as an independent Windows media environment with Stremio-compatible extensions, and should compatibility be freshly runtime-verified before the claim is used?
2. **r/StremioMods:** Do current rules explicitly allow an independent Stremio-compatible client showcase?
3. **r/StremioAddons:** After Hemanth personally reads/acknowledges the current rules, is an independent client considered a permitted community project?
4. **r/comicbooks:** Does Hemanth's existing account already satisfy genuine community-participation expectations?
5. **Biblio route:** Is moderator contact worth doing for `r/ebooks`, `r/audiobooks`, and `r/ereader`?
6. **r/TestMyApp:** Is the objective truly bug/feedback testing rather than marketing?
7. **r/opensource:** Will Hemanth personally author/materially rewrite the final public post to satisfy the AI-content rule?
8. **r/developersIndia / r/vibecoding:** Does Hemanth want a real engineering/process story rather than product-launch copy?
9. **r/homelab:** Does the account satisfy eligibility, and is Vault genuinely relevant enough to the homelab problem space?
10. **r/degoogle:** Does Colosseum genuinely replace or reduce a Google dependency that Hemanth is willing to claim publicly?

---

# 6. Master actionable registry

## Audited communities

This registry includes both the already-used first-wave communities and the clean second-wave communities.

```json
{
  "communities": [
    {
      "route": "general",
      "community": "r/windowsapps",
      "url": "https://www.reddit.com/r/windowsapps/",
      "audit_file": "marketing/audits/windowsapps.md",
      "post_file": "marketing/posts/windowsapps.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "already-targeted",
      "reason": "Explicit developer-promotion pathway with Developer flair, weekly cadence, legal-link requirements, and native media support."
    },
    {
      "route": "general",
      "community": "r/SideProject",
      "url": "https://www.reddit.com/r/SideProject/",
      "audit_file": "marketing/audits/sideproject.md",
      "post_file": "marketing/posts/sideproject.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "already-targeted",
      "reason": "Community purpose explicitly invites users to share side projects and project links."
    },
    {
      "route": "general",
      "community": "r/foss",
      "url": "https://www.reddit.com/r/foss/",
      "audit_file": "marketing/audits/foss.md",
      "post_file": "marketing/posts/foss.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "already-targeted",
      "reason": "Current rule permits promotion when the promoted project is FOSS; Colosseum is MIT licensed."
    },
    {
      "route": "general",
      "community": "r/coolgithubprojects",
      "url": "https://www.reddit.com/r/coolgithubprojects/",
      "audit_file": "marketing/audits/coolgithubprojects.md",
      "post_file": "marketing/posts/coolgithubprojects.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "already-targeted",
      "reason": "Community explicitly welcomes users sharing their own GitHub projects."
    },
    {
      "route": "general",
      "community": "r/software",
      "url": "https://www.reddit.com/r/software/",
      "audit_file": "marketing/audits/software.md",
      "post_file": "marketing/posts/software.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "next-wave",
      "reason": "Current rules provide a defined open-source/self-promotion path and explicitly prohibit piracy."
    },
    {
      "route": "general",
      "community": "r/IMadeThis",
      "url": "https://www.reddit.com/r/IMadeThis/",
      "audit_file": "marketing/audits/imadethis.md",
      "post_file": "marketing/posts/imadethis.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "next-wave",
      "reason": "Community explicitly exists for people to show what they built and supports native video/GIF showcases."
    },
    {
      "route": "general",
      "community": "r/ShowYourApp",
      "url": "https://www.reddit.com/r/ShowYourApp/",
      "audit_file": "marketing/audits/showyourapp.md",
      "post_file": "marketing/posts/showyourapp.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "next-wave",
      "reason": "Current rules explicitly invite developers to share applications and contextual demo links."
    },
    {
      "route": "general",
      "community": "r/buildinpublic",
      "url": "https://www.reddit.com/r/buildinpublic/",
      "audit_file": "marketing/audits/buildinpublic.md",
      "post_file": "marketing/posts/buildinpublic.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "next-wave",
      "reason": "Current rules permit sharing work when the post contributes progress, lessons, decisions, or feedback rather than existing solely as an advertisement."
    },
    {
      "route": "general",
      "community": "r/github",
      "url": "https://www.reddit.com/r/github/",
      "audit_file": "marketing/audits/github.md",
      "post_file": "marketing/posts/github.md",
      "status": "audited",
      "confidence": "high",
      "campaign_state": "next-wave-megathread-only",
      "reason": "Current rules provide a clear project-promotion path in the pinned self-promotion megathread while prohibiting standalone promotional posts."
    }
  ]
}
```

---

# 7. Priority human-review registry

These are not approved for copy production yet.

```json
{
  "communities": [
    {
      "route": "theatre-anime",
      "community": "r/StremioMods",
      "url": "https://www.reddit.com/r/StremioMods/",
      "audit_file": "marketing/audits/stremiomods.md",
      "post_file": "marketing/posts/stremiomods.md",
      "status": "blocked-for-human-review",
      "confidence": "high",
      "reason": "Recent accepted posts include independent Windows Stremio clients and an MPV-backed customized client, but current explicit self-promotion rules require confirmation."
    },
    {
      "route": "theatre-anime",
      "community": "r/StremioAddons",
      "url": "https://www.reddit.com/r/StremioAddons/",
      "audit_file": "marketing/audits/stremioaddons.md",
      "post_file": "marketing/posts/stremioaddons.md",
      "status": "blocked-for-human-review",
      "confidence": "high",
      "reason": "Colosseum has real Stremio-extension compatibility and current community-project announcements are accepted, but rule ambiguity, mandatory acknowledgement, and piracy adjacency require manual review."
    },
    {
      "route": "theatre-anime",
      "community": "r/mpv",
      "url": "https://www.reddit.com/r/mpv/",
      "audit_file": "marketing/audits/mpv.md",
      "post_file": "marketing/posts/mpv.md",
      "status": "blocked-for-human-review",
      "confidence": "medium-high",
      "reason": "Theatre genuinely uses MpvQt/libmpv and MPV frontend developer posts are topical, but current promotional rules need confirmation."
    },
    {
      "route": "books-audiobooks",
      "community": "r/Calibre",
      "url": "https://www.reddit.com/r/Calibre/",
      "audit_file": "marketing/audits/calibre.md",
      "post_file": "marketing/posts/calibre.md",
      "status": "blocked-for-human-review",
      "confidence": "medium-high",
      "reason": "Third-party reader developer posts are accepted and the audience fits Biblio, but Colosseum has no current Calibre/OPDS integration and broader promotion policy is unclear."
    },
    {
      "route": "manga-comics",
      "community": "r/comicbooks",
      "url": "https://www.reddit.com/r/comicbooks/",
      "audit_file": "marketing/audits/comicbooks.md",
      "post_file": "marketing/posts/comicbooks.md",
      "status": "blocked-for-human-review",
      "confidence": "high",
      "reason": "Strong reader audience and self-promotion can be permitted, but only for genuinely active community participants."
    },
    {
      "route": "books-audiobooks",
      "community": "r/ebooks",
      "url": "https://www.reddit.com/r/ebooks/",
      "audit_file": "marketing/audits/ebooks.md",
      "post_file": "marketing/posts/ebooks.md",
      "status": "blocked-for-human-review",
      "confidence": "medium-high",
      "reason": "The audience fits Biblio, but current self-promotion language clearly covers authors/publishers rather than software developers."
    },
    {
      "route": "books-audiobooks",
      "community": "r/audiobooks",
      "url": "https://www.reddit.com/r/audiobooks/",
      "audit_file": "marketing/audits/audiobooks.md",
      "post_file": "marketing/posts/audiobooks.md",
      "status": "blocked-for-human-review",
      "confidence": "high",
      "reason": "Current promotional rules focus authors, narrators, and publishers; ordinary software promotion requires clarification."
    },
    {
      "route": "vault-local",
      "community": "r/homelab",
      "url": "https://www.reddit.com/r/homelab/",
      "audit_file": "marketing/audits/homelab.md",
      "post_file": "marketing/posts/homelab.md",
      "status": "blocked-for-human-review",
      "confidence": "high",
      "reason": "Software showcases exist, but subreddit participation eligibility, disclosure, and genuine homelab relevance must be satisfied."
    }
  ]
}
```

---

# 8. Future integration → community unlock map

One of the strongest findings across all three passes is that **real ecosystem integrations unlock better marketing communities than generic promotion hunting**.

| Future Colosseum capability | Native community unlocked | Why |
|---|---|---|
| Freshly verified Stremio-compatible extension support | `r/StremioMods`, `r/StremioAddons` | Already real protocol adjacency; strongest immediate ecosystem route |
| Audiobookshelf client/library/progress integration | `r/audiobookshelf` | Official ecosystem actively encourages third-party clients |
| Calibre-Web / OPDS support | `r/Calibre` | Existing third-party reader/client culture |
| Kavita integration | `r/KavitaManga` | Manga/comics/books audience already uses companion readers |
| Jellyfin client/library integration | `r/Jellyfin` | Strong local-media audience with third-party tool culture |
| Plex client/library integration | `r/PleX` | Large personal-media audience, but promotion rules need care |
| Trakt progress/history sync | `r/trakt` | Existing developer-addon posts around Continue/resume workflows |
| Komga integration | `r/komga` | Strong manga/comics library fit |
| Kodi/Emby interoperability | corresponding communities | Converts conceptual similarity into genuine ecosystem relevance |

This should inform product strategy as well as marketing strategy.

---

# 9. Campaign sequencing recommendation

## Immediate

Use the remaining clean second-wave communities one at a time:

1. `r/software`
2. `r/IMadeThis`
3. `r/ShowYourApp`
4. `r/buildinpublic`
5. `r/github` megathread

## In parallel, research-only

Resolve:

1. `r/StremioMods`
2. `r/StremioAddons`
3. `r/mpv`
4. `r/comicbooks`
5. Biblio moderator gates
6. `r/homelab`

## Do not spend time on

The current avoid list unless the product changes or the subreddit rules materially change.

---

# 10. No-publish boundary

Nothing in this master guide authorizes:

- publishing to Reddit;
- commenting from a project account;
- contacting moderators;
- requesting exceptions;
- creating artificial subreddit history;
- karma farming;
- cross-posting near-identical copy;
- pretending to be an ordinary user;
- hiding AI involvement where a community requires disclosure;
- leading with piracy-adjacent acquisition behavior.

Every outward action remains a separate Hemanth decision.

---

# 11. Source report integrity

The appendices below preserve the three underlying research reports exactly as generated, apart from the appendix heading added before each file.

Source files:

- `colosseum-subreddit-policy-audit-2026-08-24.md` — SHA-256 `62fa65e90c7443a10d470db1e932bb2910b4fe11074d71581f2524c0aee08db7`
- `colosseum-subreddit-deep-research-second-wave-2026-08-24.md` — SHA-256 `4d975136b3525af15f9e76f2320942d5d297d4583a0cce375a6f92a12f5a1b87`
- `colosseum-ecosystem-adjacent-subreddit-deep-research-2026-08-24.md` — SHA-256 `6d3a2079bb1c08c4c72f4ab71d5a9f742cc2168c4c06d3bd99e1b8573dbd71ba`

---

# Appendix A — Original First-Wave Policy Audit

# Colosseum First Marketing Campaign — Subreddit Selection and Policy Audit

**Research date:** 2026-08-24  
**Scope:** Research and selection only. No publishing, moderator contact, or marketing copy.  
**Source-of-truth constraints:** Approved Colosseum marketing system spec, implementation plan, and current README were inspected read-only before research.

## Executive recommendation

The first wave should contain **four communities, not eight**:

1. **r/windowsapps** — strongest first target. Current rules explicitly permit promotional content once per week when tagged `Developer`, require safe/legal links, prohibit piracy links, and support native video/images.
2. **r/SideProject** — clean broad showcase community. Its community description explicitly exists for sharing side projects and project links, with native video supported.
3. **r/foss** — strongest open-source route. Its current rule allows promotion when the promoted project is FOSS; Colosseum is MIT licensed.
4. **r/coolgithubprojects** — explicitly welcomes users sharing their own GitHub projects, with clear repost rules, but is link-first rather than video-first.

Do **not** force manga/comics, books/audiobooks, Theatre/anime, or Vault/local-media communities into wave one simply to fill route slots. The strongest direct candidates in those lanes currently have moderator gates, ambiguous software-promotion rules, account eligibility gates, or unacceptable piracy-adjacency.

Best backups:
- **r/github** — project-sharing megathread only.
- **r/opensource** — only if Hemanth personally authors/materially rewrites the eventual public copy because the subreddit prohibits AI-generated content.
- **r/developersIndia** — suitable for an engineering-story treatment rather than a launch post.
- **r/ebooks** and **r/audiobooks** — only after moderator clarification.
- **r/homelab** — only if subreddit-karma and project-relevance eligibility requirements are satisfied.

---

## Candidate audit table

| Route | Subreddit | URL | Audience fit | Self-promo policy | Developer post allowed? | Flair/mod approval | Media/link support | Piracy risk | Cadence risk | Status | Confidence |
|---|---|---|---|---|---|---|---|---|---|---|---|
| General / Windows | r/windowsapps | https://www.reddit.com/r/windowsapps/ | Excellent | Promotion permitted, max 1/week | Yes | `Developer` flair | Video, images, galleries, links | Low | Medium | `audited` | High |
| General | r/SideProject | https://www.reddit.com/r/SideProject/ | Strong | Community explicitly invites project sharing | Yes | No special promo flair found | Video + project links | Low | Low | `audited` | High |
| General / FOSS | r/foss | https://www.reddit.com/r/foss/ | Strong | Promoted project must be FOSS | Yes | No special promo flair found | Video, images, links | Low | Low | `audited` | High |
| General / GitHub | r/coolgithubprojects | https://www.reddit.com/r/coolgithubprojects/ | Good | Own GitHub projects explicitly welcome | Yes | Language flair automatic | Link-first, native video disabled | Moderate | Medium | `audited` | High |
| General / GitHub | r/github | https://www.reddit.com/r/github/ | Good | Self-promo only in pinned megathread | Yes, thread only | Standalone promo prohibited | Repo-link/comment format | Low | Low | `audited` backup | High |
| General / OSS | r/opensource | https://www.reddit.com/r/opensource/ | Strong | Limited self-promo allowed | Conditional | `Promotional`; AI-generated content prohibited | Links; native video disabled | Low | Medium-high | `blocked-for-human-review` | High |
| General / developer | r/developersIndia | https://www.reddit.com/r/developersIndia/ | Good | Project posts allowed when engineering-first, not traffic-driving | Conditional | `I Made This` | Video + links | Low | Medium | `blocked-for-human-review` | High |
| Books | r/ebooks | https://www.reddit.com/r/ebooks/ | Excellent | Self-promo rules are written mainly for authors/publishers | Unclear for software | `Self Promotion` exists | Video + direct links | Low | Medium | `blocked-for-human-review` | Medium-high |
| Audiobooks | r/audiobooks | https://www.reddit.com/r/audiobooks/ | Strong | Promotion addressed mainly for authors/narrators/publishers | Unclear for software | Moderator clarification advisable | Text-first, links inside body | Low | Medium | `blocked-for-human-review` | High |
| Vault / local | r/homelab | https://www.reddit.com/r/homelab/ | Moderate | Software showcase allowed under eligibility conditions | Conditional | Software-project flair, AI disclosure, karma gate | Screenshots + public repo | Low-moderate | Medium | `blocked-for-human-review` | High |
| Theatre / anime | r/animeapp | https://www.reddit.com/r/animeapp/ | Excellent audience fit | App promotion requires prior mod approval | Not without approval | Prior approval mandatory | Media common | High | High | `not-recommended` | High |
| Vault / local | r/DataHoarder | https://www.reddit.com/r/DataHoarder/ | Good audience fit | Software advertising restricted; current rules also create AI/vibe-code risk | Poor fit | Approval needed | Technically supports media/links | Moderate | High | `not-recommended` | High |
| Self-hosted | r/selfhosted | https://www.reddit.com/r/selfhosted/ | Weak-moderate | Must actually concern self-hosting | No for current positioning | Project constraints | Links supported | Low | High | `not-recommended` | High |
| Manga | r/MangaCollectors | https://www.reddit.com/r/MangaCollectors/ | Strong audience fit | Explicit no-self-promotion / no personal projects | No | N/A | N/A | Low | Low | `not-recommended` | High |
| Manga | r/manga | https://www.reddit.com/r/manga/ | Excellent audience fit | Drive-by self-promotion is heavily moderated | Poor fit | High participation expectations | Product showcase unsuitable | Moderate | High | `not-recommended` | High |
| Theatre / HTPC | r/htpc | https://www.reddit.com/r/htpc/ | Strong audience fit | Vested-interest promotion / new software project promotion repeatedly removed | No | Mods decline exceptions | N/A | Low | Medium | `not-recommended` | High |

---

## Per-community audit

### r/windowsapps

- **Route:** General / Windows desktop
- **Audience:** Windows users specifically interested in applications.
- **Self-promotion:** Promotional content is allowed, with a maximum of one promotional post per week.
- **Developer/project posts:** Allowed.
- **Flair:** `Developer`.
- **Moderator pre-approval:** No preapproval requirement was found in the current rules reviewed.
- **Media:** Native video, images, galleries, and standard posts are supported.
- **External links:** Safe/legal official links are acceptable; piracy links are prohibited.
- **Disclosure:** Hemanth should identify himself as the developer immediately and use the required developer flair.
- **Cadence:** One promotional post per week.
- **Piracy adjacency:** Low. The subreddit itself prohibits pirated-content links.
- **Recommended eventual format:** Native short video showcase + developer disclosure + official release link.
- **Avoid:** torrents, third-party source acquisition, “watch/read anything,” content-hosting implications.
- **Status:** `audited`
- **Research date:** 2026-08-24

### r/SideProject

- **Route:** General
- **Audience:** Builders, early adopters, and users browsing side projects.
- **Self-promotion:** Sharing projects is part of the community’s stated purpose.
- **Developer/project posts:** Allowed.
- **Flair:** No special promotional flair requirement found.
- **Moderator pre-approval:** None found.
- **Media:** Native video supported.
- **External links:** Project/startup links are explicitly contemplated by community guidance.
- **Disclosure:** Explicit developer disclosure should still be used to satisfy Colosseum’s campaign rules.
- **Cadence:** No explicit subreddit-specific promotional cadence was found.
- **Piracy adjacency:** Low.
- **Recommended eventual format:** Trailer-led project showcase with one concrete feedback question.
- **Avoid:** source/acquisition mechanics and broad unsupported availability claims.
- **Status:** `audited`
- **Research date:** 2026-08-24

### r/foss

- **Route:** General / FOSS
- **Audience:** Free/open-source software users and contributors.
- **Self-promotion:** Allowed when the promoted project is FOSS.
- **Developer/project posts:** Allowed.
- **Flair:** No specific promotional flair requirement found.
- **Moderator pre-approval:** None found.
- **Media:** Video, images, galleries, links.
- **External links:** Supported.
- **Disclosure:** Openly identify Hemanth as the developer.
- **Cadence:** No explicit promotional cadence found in the current rule set.
- **Piracy adjacency:** Low.
- **Recommended eventual format:** Video-first FOSS project showcase with repo/release reference.
- **Avoid:** acquisition and third-party-source positioning.
- **Status:** `audited`
- **Research date:** 2026-08-24

### r/coolgithubprojects

- **Route:** General / GitHub
- **Audience:** Users browsing interesting GitHub projects.
- **Self-promotion:** Users may share their own GitHub projects.
- **Developer/project posts:** Explicitly allowed.
- **Flair:** Language flair assigned automatically.
- **Moderator pre-approval:** None found.
- **Media:** Link-first. Native video submissions are disabled.
- **External links:** GitHub hosting is required.
- **Disclosure:** Developer identity should be explicit.
- **Cadence:** Reposts require meaningful new features and at least six months since the original submission.
- **Piracy adjacency:** Moderate because users landing on the repository can see README references to third-party integrations; the Reddit lead must remain strictly client/local-library framed.
- **Recommended eventual format:** Direct GitHub project link with a precise project-oriented title.
- **Avoid:** making source integrations the reason to click.
- **Status:** `audited`
- **Research date:** 2026-08-24

### r/github

- **Route:** General / GitHub backup
- **Audience:** GitHub users and developers.
- **Self-promotion:** Only in the dedicated pinned project-sharing megathread.
- **Developer/project posts:** Allowed only inside that thread.
- **Flair/mod approval:** Standalone project promotion is prohibited.
- **Media:** Treat as comment/repository-link format, not a trailer destination.
- **External links:** Repository links are appropriate in the megathread.
- **Disclosure:** Explicit developer disclosure.
- **Cadence:** Use the current active project-sharing megathread only.
- **Piracy adjacency:** Low-moderate.
- **Recommended eventual format:** Concise megathread entry.
- **Status:** `audited` backup
- **Research date:** 2026-08-24

### r/opensource

- **Route:** General / open source backup
- **Audience:** Open-source users and developers.
- **Self-promotion:** Limited self-promotion is permitted, with an approximate sub-10% participation guideline and anti-drive-by expectations.
- **Developer/project posts:** Conditionally allowed.
- **Flair:** `Promotional`.
- **Moderator pre-approval:** Not inherently required.
- **Media:** Native video submissions disabled.
- **External links:** Repository links supported.
- **Disclosure:** Required by Colosseum’s campaign policy.
- **Critical blocker:** Current rules prohibit AI-generated content.
- **Cadence:** Participation ratio matters.
- **Piracy adjacency:** Low.
- **Recommended eventual format:** Human-written open-source project discussion.
- **Human decision:** Hemanth must decide whether he will personally author or materially rewrite the public submission.
- **Status:** `blocked-for-human-review`
- **Research date:** 2026-08-24

### r/developersIndia

- **Route:** General / developer backup
- **Audience:** Indian software developers.
- **Self-promotion:** Project posts can be allowed, but current moderator guidance rejects posts primarily intended to drive installs, waitlists, or repository traffic.
- **Developer/project posts:** Best suited to engineering-story posts.
- **Flair:** `I Made This`.
- **Moderator pre-approval:** No blanket preapproval requirement found.
- **Media:** Video demos are encouraged.
- **External links:** Supported when accompanying substantive engineering context.
- **Disclosure:** Explicit developer disclosure.
- **Cadence:** Showcase Sunday / project-showcase conventions should be respected.
- **Piracy adjacency:** Low.
- **Recommended eventual format:** Engineering retrospective: problem, stack, tradeoffs, lessons, limitations.
- **Avoid:** product-launch tone or traffic-driving CTA.
- **Human decision:** Hemanth must decide whether he wants a personally edited engineering-story treatment.
- **Status:** `blocked-for-human-review`
- **Research date:** 2026-08-24

### r/ebooks

- **Route:** Books / Biblio backup
- **Audience:** Ebook readers and ebook tooling users.
- **Self-promotion:** Explicitly addressed for authors and publishers, not clearly for software developers.
- **Developer/project posts:** Ambiguous.
- **Flair:** `Self Promotion` exists.
- **Moderator pre-approval:** Moderator clarification recommended before drafting.
- **Media:** Native video and direct links supported.
- **External links:** Direct non-referral links are permitted.
- **Disclosure:** Developer identity must be explicit.
- **Cadence:** Similar promotions should be consolidated.
- **Piracy adjacency:** Low because piracy/torrent content is explicitly restricted.
- **Recommended eventual format if approved:** Biblio/read-along demonstration.
- **Avoid:** LibGen, acquisition paths, free-content framing.
- **Status:** `blocked-for-human-review`
- **Research date:** 2026-08-24

### r/audiobooks

- **Route:** Books / audiobooks backup
- **Audience:** Audiobook listeners.
- **Self-promotion:** Rules explicitly discuss authors, narrators, and publishers, but not ordinary software products.
- **Developer/project posts:** Ambiguous.
- **Flair/mod approval:** Moderator clarification recommended.
- **Media:** Text-first; native video is not the right format.
- **External links:** Links should be embedded in text posts with context/summary.
- **Disclosure:** Required.
- **Cadence:** Promoters in the specified categories are limited to one promotional thread per week.
- **Piracy adjacency:** Low; piracy/source discussion is strongly restricted.
- **Recommended eventual format if approved:** Text explanation of ebook/audiobook read-along and shared progress.
- **Avoid:** acquisition/source language.
- **Status:** `blocked-for-human-review`
- **Research date:** 2026-08-24

### r/homelab

- **Route:** Vault / local-media backup
- **Audience:** Homelab builders and local-service users.
- **Self-promotion:** Software showcases are allowed under conditions.
- **Developer/project posts:** Conditional.
- **Flair:** Software-project flair.
- **Moderator pre-approval:** Not necessarily, but eligibility must be satisfied.
- **Media:** Screenshots and public repository are expected.
- **External links:** Public repository required.
- **Disclosure:** AI involvement disclosure is required by current project-showcase policy.
- **Eligibility:** Requires subreddit participation/karma and clear homelab relevance.
- **Piracy adjacency:** Low-moderate.
- **Recommended eventual format:** Technical local-library software showcase.
- **Human decision:** Confirm Hemanth’s account eligibility and whether Colosseum is sufficiently homelab-relevant before drafting.
- **Status:** `blocked-for-human-review`
- **Research date:** 2026-08-24

### r/animeapp

- **Route:** Theatre / anime
- **Audience:** Extremely relevant on paper.
- **Self-promotion:** App promotion requires prior moderator approval.
- **Developer/project posts:** Not without approval.
- **Flair/mod approval:** Prior moderator approval mandatory.
- **Media:** Media posts are common.
- **External links:** Technically possible, but not relevant to the recommendation.
- **Cadence:** Current rules include a high-frequency posting limit.
- **Piracy adjacency:** High. Current community discussion is heavily associated with sources, scraping, downloads, TorBox and related acquisition topics.
- **Recommended format:** None for first campaign.
- **Status:** `not-recommended`
- **Research date:** 2026-08-24

### r/DataHoarder

- **Route:** Vault / local media
- **Audience:** Relevant to ownership/local storage.
- **Self-promotion:** Software promotions/advertising are restricted; current project rules also create material AI/vibe-coded-project risk.
- **Developer/project posts:** Poor fit.
- **Moderator pre-approval:** Approval required for advertising/promotion.
- **Media:** Technically supported.
- **Piracy adjacency:** Moderate.
- **Status:** `not-recommended`
- **Research date:** 2026-08-24

### r/selfhosted

- **Route:** Self-hosted / Vault-adjacent
- **Audience:** Weak fit because Colosseum is a native desktop client, not a self-hosted service.
- **Self-promotion:** Must genuinely concern self-hosting.
- **Developer/project posts:** Current Colosseum positioning does not meet the core topical gate.
- **Status:** `not-recommended`
- **Research date:** 2026-08-24

### r/MangaCollectors

- **Route:** Manga/comics
- **Audience:** Strong audience match.
- **Self-promotion:** Explicitly prohibited; personal projects are not allowed.
- **Developer/project posts:** No.
- **Status:** `not-recommended`
- **Research date:** 2026-08-24

### r/manga

- **Route:** Manga
- **Audience:** Excellent theoretical audience fit.
- **Self-promotion:** Current moderator enforcement strongly disfavors drive-by self-promotion and accounts dominated by their own material.
- **Developer/project posts:** Poor fit for a launch/showcase.
- **Moderation risk:** High.
- **Status:** `not-recommended`
- **Research date:** 2026-08-24

### r/htpc

- **Route:** Theatre / HTPC
- **Audience:** Good theoretical fit.
- **Self-promotion:** Moderators repeatedly remove vested-interest/new-software-project promotion.
- **Developer/project posts:** Not recommended.
- **Status:** `not-recommended`
- **Research date:** 2026-08-24

---

## First-wave registry

```json
{
  "communities": [
    {
      "route": "general",
      "community": "r/windowsapps",
      "url": "https://www.reddit.com/r/windowsapps/",
      "audit_file": "marketing/audits/windowsapps.md",
      "post_file": "marketing/posts/windowsapps.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Explicit developer promotion rule, one-post-per-week cadence, legal-link requirement, and native video support."
    },
    {
      "route": "general",
      "community": "r/SideProject",
      "url": "https://www.reddit.com/r/SideProject/",
      "audit_file": "marketing/audits/sideproject.md",
      "post_file": "marketing/posts/sideproject.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Community purpose and submission guidance explicitly invite sharing side projects and project links, with native video enabled."
    },
    {
      "route": "general",
      "community": "r/foss",
      "url": "https://www.reddit.com/r/foss/",
      "audit_file": "marketing/audits/foss.md",
      "post_file": "marketing/posts/foss.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Current rule explicitly permits promotion when the promoted project is FOSS; Colosseum is MIT licensed."
    },
    {
      "route": "general",
      "community": "r/coolgithubprojects",
      "url": "https://www.reddit.com/r/coolgithubprojects/",
      "audit_file": "marketing/audits/coolgithubprojects.md",
      "post_file": "marketing/posts/coolgithubprojects.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Sidebar explicitly welcomes users sharing their own GitHub projects and provides clear repost and flair guidance."
    }
  ]
}
```

---

## Final recommendation

1. **Research/use first:**  
   `r/windowsapps` → `r/SideProject` → `r/foss` → `r/coolgithubprojects`

2. **Avoid for this campaign:**  
   `r/DataHoarder`, `r/selfhosted`, `r/MangaCollectors`, `r/manga`, `r/htpc`, `r/animeapp`

3. **Require moderator contact before drafting:**  
   `r/ebooks`, `r/audiobooks`  
   `r/animeapp` technically requires approval, but is not recommended anyway.

4. **Best video/GIF-first showcase targets:**  
   - `r/windowsapps`
   - `r/foss`
   - `r/SideProject`

5. **Better for story/lessons than product demo:**  
   - `r/developersIndia`
   - `r/opensource` if Hemanth writes/materially rewrites the public copy himself

6. **Human decisions required before copywriting:**  
   - Whether Hemanth will personally author/materially rewrite an `r/opensource` submission.
   - Whether to pursue an engineering-story version for `r/developersIndia`.
   - Whether moderator contact is worthwhile for `r/ebooks` and `r/audiobooks`.
   - Whether Hemanth’s Reddit account and project framing satisfy `r/homelab` eligibility.

## Implementation boundary

This document is a **research artifact only**.

It does **not** authorize:
- drafting subreddit-specific marketing copy;
- publishing;
- cross-posting;
- moderator contact;
- replying from the project account; or
- treating any `blocked-for-human-review` community as copy-ready.

Only `audited` communities should proceed into Task 3/Task 4 of the approved marketing implementation sequence after the claims gate is also satisfied.

---

# Appendix B — Deeper Second-Wave Research

# Colosseum Second-Wave Subreddit Selection & Policy Audit

**Research date:** 2026-08-24  
**Campaign stage:** Second-wave target discovery  
**Excluded because already targeted:** `r/windowsapps`, `r/SideProject`, `r/foss`, `r/coolgithubprojects`  
**Scope:** Research and selection only. No post copy, trailer copy, comments, publishing instructions, moderator contact, cross-posting, or account manipulation.

## Governing Colosseum constraints

This audit inherits the approved Colosseum marketing-system rules inspected before the first research pass:

- Colosseum is presented as a native Windows desktop media environment and local-media client. It does not host media.
- Every eventual Reddit post must disclose that Hemanth is the developer.
- Manga/comics and Theatre/anime positioning must not lead with torrents, third-party sources, extensions, free content, “watch anything,” “read anything,” or unauthorized access.
- A subreddit is `audited` only when a relevant developer/project post has a clear permitted path.
- Ambiguous rules, moderator approval, account-history eligibility, authorship restrictions, or substantial moderation/safety risk produce `blocked-for-human-review`.
- Poor topical fit, prohibited promotion, or unacceptable piracy-adjacency produce `not-recommended`.
- The agent prepares research and bundles only. Publication remains a separate Hemanth decision.
- Near-identical burst cross-posting is not part of the approved campaign model.

## Scoring key

Positive dimensions use **1–5 where 5 is strongest**:

- `AF` audience fit
- `PR` product relevance
- `PC` self-promotion policy clarity
- `DC` developer-showcase compatibility
- `ME` media-embed compatibility
- `LC` external-link compatibility

Risk dimensions use **1–5 where 5 is riskier**:

- `PA` piracy-adjacency risk
- `MR` moderation risk
- `CR` cadence/cross-posting risk

Confidence is `high`, `medium`, or `low`.

---

## Executive recommendation

The deeper pass found **five policy-clean second-wave opportunities** after excluding the four communities already used:

1. **r/software** — strongest next target. It has a clearly defined self-promotion path for open-source software, an explicit anti-piracy rule, release guidance, and an account-karma gate that can be checked at submission time. The safest interpretation is to use its Wednesday self-promotion path with the correct flair and substantive context.
2. **r/IMadeThis** — the cleanest native-video showcase after the original four. Its own community description explicitly welcomes open-source projects and developers showing what they made.
3. **r/ShowYourApp** — a smaller but unusually policy-clean app-showcase community. Its rules explicitly exist for developers/founders sharing apps and permit app/demo links when accompanied by context.
4. **r/buildinpublic** — usable when framed as progress, lessons, decisions, or feedback rather than a naked product advertisement. This is a story/lessons route, not a duplicate launch post.
5. **r/github** — valid only through its pinned project-promotion megathread. It is not a trailer-post destination, but its policy is clear enough to mark the thread-only route `audited`.

The most important outcome is **not** the number five. It is the niche-route gap:

- **Manga/comics:** `r/comicbooks` is promising but requires genuine prior community participation before self-promotion.
- **Books/audiobooks:** `r/ebooks`, `r/audiobooks`, and `r/ereader` all retain moderation or policy ambiguity for software promotion.
- **Theatre/anime:** the obvious app/anime communities remain either approval-gated or piracy-adjacent.
- **Vault/privacy/local media:** `r/homelab` is conditional on eligibility/relevance; `r/degoogle` requires a truthful Google-service-replacement story; `r/privacy` currently blocks app promotion.

That means the implementation agent should **not manufacture a niche subreddit just to satisfy every route**.

### Recommended second-wave order

1. `r/software`
2. `r/IMadeThis`
3. `r/ShowYourApp`
4. `r/buildinpublic`
5. `r/github` — megathread only

These five should not receive near-identical copy or be treated as one burst. Their native formats are materially different.

---

## Candidate audit table

| Route | Subreddit | URL | Audience fit | Self-promo policy | Developer post allowed? | Flair/mod approval | Media/link support | Piracy risk | Cadence risk | Status | Confidence |
|---|---|---|---|---|---|---|---|---|---|---|---|
| General / software | r/software | https://www.reddit.com/r/software/ | 4/5 | Open-source path + Wednesday exception; [rules](https://www.reddit.com/r/software/about/rules/) | Yes, through compliant path | Appropriate flair; hidden karma threshold | Images + links; native video off | 1/5 | 4/5 | `audited` | High |
| General / maker | r/IMadeThis | https://www.reddit.com/r/IMadeThis/ | 4/5 | Community explicitly exists to show what users made; [about](https://www.reddit.com/r/IMadeThis/about.json) | Yes | No special approval found | Video, GIF, images, links | 1/5 | 1/5 | `audited` | High |
| General / apps | r/ShowYourApp | https://www.reddit.com/r/ShowYourApp/ | 3/5 | Explicit app/project showcase community; [rules](https://www.reddit.com/r/ShowYourApp/about/rules/) | Yes | No preapproval found | Video/GIF/images + links | 1/5 | 2/5 | `audited` | High |
| General / maker story | r/buildinpublic | https://www.reddit.com/r/buildinpublic/ | 3/5 | Share work openly, but not solely for promotion; [rules](https://www.reddit.com/r/buildinpublic/about/rules/) | Yes, story/progress-first | No preapproval found | Video/GIF/images + links | 1/5 | 2/5 | `audited` | High |
| General / GitHub | r/github | https://www.reddit.com/r/github/ | 2/5 | Promotion only in pinned project megathread; [rules](https://www.reddit.com/r/github/about/rules/) | Yes, megathread only | Appropriate flair where applicable | Link/comment-first | 1/5 | 2/5 | `audited` | High |
| Testing / feedback | r/TestMyApp | https://www.reddit.com/r/TestMyApp/ | 3/5 | Testing allowed; advertising prohibited; [rules](https://www.reddit.com/r/TestMyApp/about/rules/) | Only for genuine testing | Required title-stage/OS convention | Video/images + links | 1/5 | 3/5 | `blocked-for-human-review` | High |
| General / apps | r/apps | https://www.reddit.com/r/apps/ | 4/5 | Rules warn against accounts made to advertise apps; [about](https://www.reddit.com/r/apps/about.json) | Ambiguous for developer showcase | Flair expected | Images + links; native video off | 2/5 | 3/5 | `blocked-for-human-review` | Medium |
| Manga/comics | r/comicbooks | https://www.reddit.com/r/comicbooks/ | 5/5 | Self-promo allowed only after real community participation; [FAQ](https://www.reddit.com/r/comicbooks/wiki/faq) | Conditional | Title tag + link flair | Images/text/links; native video off | 1/5 | 4/5 | `blocked-for-human-review` | High |
| General / OSS | r/OpenSourceProjects | https://www.reddit.com/r/OpenSourceProjects/ | 3/5 | Explicit project-showcase purpose but subreddit is restricted; [about](https://www.reddit.com/r/OpenSourceProjects/about.json) | Conditional on posting access | Restricted posting | Video/GIF/images + links | 1/5 | 2/5 | `blocked-for-human-review` | Medium-high |
| General / founder | r/indiehackers | https://www.reddit.com/r/indiehackers/ | 3/5 | Self-promotion category exists; policy actively evolving; [2026 mod guidance](https://www.reddit.com/r/indiehackers/comments/1uzsfzy/community_reorganisation_new_posting_guidelines/) | Yes, but conditional | `Self Promotion`; new-account gate | Mixed media + links | 1/5 | 5/5 | `blocked-for-human-review` | High |
| General / AI-built software | r/vibecoding | https://www.reddit.com/r/vibecoding/ | 3/5 | Project sharing allowed with educational/process value; commercial shilling constrained; [mod update](https://www.reddit.com/r/vibecoding/comments/1mp8oyw/important_new_rules_update_on_selfpromotion/) | Conditional | Some commercial/dev-tool promotion requires approval | Video/GIF/images + links | 1/5 | 4/5 | `blocked-for-human-review` | High |
| Vault/privacy | r/degoogle | https://www.reddit.com/r/degoogle/ | 2/5 | Official weekly developer showcase exists | Only if product truthfully replaces/reduces a Google dependency | `[DEV]` convention in showcase | Thread/comment + repo/site | 1/5 | 5/5 | `blocked-for-human-review` | Medium-high |
| Vault/privacy | r/PrivacyGuides | https://www.reddit.com/r/PrivacyGuides/ | 2/5 | Developer discussion historically allowed with strict disclosure/threat-model requirements | Conditional | Strong disclosure/evidence burden | Discussion + links | 1/5 | 4/5 | `blocked-for-human-review` | Medium |
| Books / Biblio | r/ereader | https://www.reddit.com/r/ereader/ | 5/5 | Commercial/service promotion needs moderator approval; [rules](https://www.reddit.com/r/ereader/about/rules/) | Not without gate | Mod approval; current AI-content restriction | Links/images | 1/5 | 4/5 | `blocked-for-human-review` | High |
| Books / Biblio | r/ebooks | https://www.reddit.com/r/ebooks/ | 5/5 | Self-promo language clearly covers authors/publishers, not app developers; [rules](https://www.reddit.com/r/ebooks/about/rules/) | Ambiguous | `Self Promotion` exists | Video + direct links | 1/5 | 3/5 | `blocked-for-human-review` | Medium-high |
| Books / audio | r/audiobooks | https://www.reddit.com/r/audiobooks/ | 4/5 | Promotion language focuses authors/narrators/publishers; [rules](https://www.reddit.com/r/audiobooks/about/rules/) | Ambiguous | Moderator clarification advisable | Text/link-first | 1/5 | 3/5 | `blocked-for-human-review` | High |
| Vault / local media | r/homelab | https://www.reddit.com/r/homelab/ | 3/5 | Software showcases allowed under project/relevance/account conditions; [rules](https://www.reddit.com/r/homelab/about/rules/) | Conditional | Project flair; eligibility and disclosure gates | Screenshots + public repo | 1/5 | 3/5 | `blocked-for-human-review` | High |
| Privacy | r/privacy | https://www.reddit.com/r/privacy/ | 3/5 | Current rules prohibit advertising and say app-promotion approvals are on hold; [rules](https://www.reddit.com/r/privacy/about/rules/) | No | No viable current approval path | Irrelevant because promotion gate fails | 1/5 | 5/5 | `not-recommended` | High |
| Windows | r/windows | https://www.reddit.com/r/windows/ | 5/5 | App promotion requires mod approval and current rules say applicants are not being accepted; [rules](https://www.reddit.com/r/windows/about/rules/) | No current path | Approval route closed | Strong media support otherwise | 1/5 | 5/5 | `not-recommended` | High |
| Windows | r/Windows10 | https://www.reddit.com/r/Windows10/ | 5/5 | App-promotion approval route not currently accepting applicants; [rules](https://www.reddit.com/r/Windows10/about/rules/) | No current path | Approval route closed | N/A | 1/5 | 5/5 | `not-recommended` | High |
| Windows | r/Windows11 | https://www.reddit.com/r/Windows11/ | 5/5 | App-promotion approval route not currently accepting applicants; [rules](https://www.reddit.com/r/Windows11/about/rules/) | No current path | Approval route closed | N/A | 1/5 | 5/5 | `not-recommended` | High |
| FOSS | r/FreeSoftware | https://www.reddit.com/r/FreeSoftware/ | 4/5 | Current Rule 7 rejects software containing AI-generated code; [July 2026 mod clarification](https://www.reddit.com/r/freesoftware/comments/1uljopz/rule_7_updated_software_submissions_beware/) | Incompatible if project contains AI-generated code | Strict project rule | N/A | 1/5 | 5/5 | `not-recommended` | High |
| Vault/local | r/DataHoarder | https://www.reddit.com/r/DataHoarder/ | 4/5 | Software advertising/project promotion creates material moderation risk; [rules](https://www.reddit.com/r/DataHoarder/about/rules/) | Poor fit | Approval/eligibility concerns | N/A | 3/5 | 4/5 | `not-recommended` | High |
| Self-hosted | r/selfhosted | https://www.reddit.com/r/selfhosted/ | 2/5 | Projects must genuinely concern self-hosting; [rules](https://www.reddit.com/r/selfhosted/about/rules/) | Not for current Colosseum positioning | Project constraints | Links | 2/5 | 4/5 | `not-recommended` | High |
| Home server | r/HomeServer | https://www.reddit.com/r/HomeServer/ | 2/5 | Server-topic and advertising restrictions do not fit a native desktop client; [rules](https://www.reddit.com/r/HomeServer/about/rules/) | Poor fit | Mod exceptions needed for advertising | N/A | 2/5 | 4/5 | `not-recommended` | High |
| Manga | r/MangaCollectors | https://www.reddit.com/r/MangaCollectors/ | 4/5 | Personal-project/self-promotion prohibition; [rules](https://www.reddit.com/r/MangaCollectors/about/rules/) | No | N/A | N/A | 1/5 | 3/5 | `not-recommended` | High |
| Manga | r/manga | https://www.reddit.com/r/manga/ | 5/5 | Current moderation strongly disfavors drive-by self-promotion | Poor fit | Participation expectations | N/A | 2/5 | 5/5 | `not-recommended` | High |
| Comics | r/graphicnovels | https://www.reddit.com/r/graphicnovels/ | 4/5 | Self-promotion explicitly disallowed, including removal of mod exceptions; [mod clarification](https://www.reddit.com/r/graphicnovels/comments/y9xhhq/update_to_sub_rules_please_read/) | No | No exception route | N/A | 1/5 | 4/5 | `not-recommended` | High |
| Comics | r/comics | https://www.reddit.com/r/comics/ | 2/5 | Community is principally for posting comics/OC, not reader/library software; [rules](https://www.reddit.com/r/comics/about/rules/) | Poor topical fit | N/A | Media-first but wrong product category | 1/5 | 3/5 | `not-recommended` | High |
| Anime apps | r/animeapp | https://www.reddit.com/r/animeapp/ | 5/5 | App promotion requires prior moderator approval and community context is strongly piracy-adjacent; [rules](https://www.reddit.com/r/animeapp/about/rules/) | Approval-gated | Prior approval | Media/links | 5/5 | 5/5 | `not-recommended` | High |
| HTPC/video | r/htpc | https://www.reddit.com/r/htpc/ | 4/5 | Moderation has repeatedly rejected vested-interest/new-software promotion; [rules](https://www.reddit.com/r/htpc/about/rules/) | No useful campaign path | High mod risk | N/A | 2/5 | 4/5 | `not-recommended` | High |
| App founders | r/AppBusiness | https://www.reddit.com/r/AppBusiness/ | 2/5 | Rule explicitly says no promotion of own apps; feedback is relegated to a weekly thread; [rules](https://www.reddit.com/r/AppBusiness/about/rules/) | Not as marketing | Weekly feedback thread only | N/A | 1/5 | 4/5 | `not-recommended` | High |
| Beta testers | r/alphaandbetausers | https://www.reddit.com/r/alphaandbetausers/ | 2/5 | Designed for alpha/beta recruitment, not a stable 1.1.3 marketing campaign; [about](https://www.reddit.com/r/alphaandbetausers/about.json) | Only for genuine pre-release testing | Stage/OS conventions | Self-post/link-body format | 1/5 | 3/5 | `not-recommended` | High |
| Beta testers | r/BetaTesting | https://www.reddit.com/r/BetaTesting/ | 1/5 | Current rules focus Reddit-native apps and restrict external app promotion; [rules](https://www.reddit.com/r/BetaTesting/about/rules/) | No useful current path | N/A | N/A | 1/5 | 4/5 | `not-recommended` | High |
| QA/testing | r/softwaretesting | https://www.reddit.com/r/softwaretesting/ | 1/5 | Community moderation removes requests to test/review software and self-promotion | No | N/A | N/A | 1/5 | 4/5 | `not-recommended` | High |

---

# Per-community audit

## 1. r/software

**URL:** https://www.reddit.com/r/software/  
**Route:** General / software  
**Status:** `audited`  
**Research date:** 2026-08-24  
**Confidence:** High

### Audience

A broad software-usage community. It is less maker-centric than `r/IMadeThis` and `r/ShowYourApp`, but more directly populated by people evaluating desktop software.

### Exact policy sources

- [Current subreddit rules](https://www.reddit.com/r/software/about/rules/)
- [Current subreddit metadata/sidebar](https://www.reddit.com/r/software/about.json)

Both were retrieved on 2026-08-24 from Reddit's current endpoints.

### Self-promotion rule

The current rule states that users should not promote their own software unless it is open source and additionally defines a Wednesday exception requiring appropriate flair and something of value beyond merely dropping the software.

Because the language is unusually punitive about non-compliant promotion, the conservative Colosseum interpretation is: **use the Wednesday self-promotion path even though Colosseum is open source**.

### Developer/project-post rule

A relevant open-source software release/project post has a clear permitted path.

### Flair / approval

- Appropriate self-promotion flair is required for the Wednesday route.
- The subreddit has a hidden account-karma threshold. AutoModerator can remove accounts below it.
- The rule explicitly says a user who is caught by that threshold may send modmail for manual review. This is an eligibility fallback, not permission to game karma.

### Media support

- Native video submissions: disabled.
- Images: supported.
- Galleries: supported.
- Links: supported.

### Disclosure

The subreddit does not need to supply Colosseum's disclosure requirement. The approved campaign already does: Hemanth must identify himself as the developer.

### Piracy adjacency

**Very low if positioned correctly.** The subreddit explicitly bans content involving piracy and software primarily intended to facilitate it. That makes acquisition/source framing especially inappropriate, but gives a normal desktop-client/local-library post a clean trust boundary.

### Recommended eventual format

Image/text/link-led software showcase or substantive release/project discussion, not a trailer-first post.

### Claims/angles to avoid

- torrents or third-party source acquisition;
- video downloaders;
- copyrighted-content availability;
- “watch anything” / “read anything”;
- presenting Colosseum as content access rather than a client/library.

### Scorecard

`AF 4 | PR 5 | PC 5 | DC 4 | ME 2 | LC 5 | PA 1 | MR 2 | CR 4`

---

## 2. r/IMadeThis

**URL:** https://www.reddit.com/r/IMadeThis/  
**Route:** General / maker showcase  
**Status:** `audited`  
**Research date:** 2026-08-24  
**Confidence:** High

### Exact policy sources

- [Current rules endpoint](https://www.reddit.com/r/IMadeThis/about/rules/)
- [Current community metadata/sidebar](https://www.reddit.com/r/IMadeThis/about.json)

### Policy finding

The subreddit currently exposes no custom community rules beyond Reddit-wide rules. More importantly, the current community description explicitly exists for people to show what they made and specifically references open-source projects and game developers as examples.

That is direct first-party evidence that a transparent Colosseum developer showcase fits the community's purpose.

### Flair / approval

No special promotional flair or moderator pre-approval requirement was found.

### Media

- Native video: supported.
- Video GIF: supported.
- Images/galleries: supported.
- Standard link/self-post formats: supported.

### Disclosure

Use explicit developer identity despite the absence of a special subreddit disclosure rule.

### Piracy adjacency

Low. It is a maker community, not a media-source community.

### Recommended eventual format

Native short product demonstration with developer context and a specific feedback question.

### Avoid

Do not turn a general maker showcase into an acquisition/source pitch.

### Scorecard

`AF 4 | PR 4 | PC 4 | DC 5 | ME 5 | LC 4 | PA 1 | MR 1 | CR 1`

---

## 3. r/ShowYourApp

**URL:** https://www.reddit.com/r/ShowYourApp/  
**Route:** General / application showcase  
**Status:** `audited`  
**Research date:** 2026-08-24  
**Confidence:** High

### Exact policy sources

- [Current rules](https://www.reddit.com/r/ShowYourApp/about/rules/)
- [Current subreddit metadata](https://www.reddit.com/r/ShowYourApp/about.json)

### Self-promotion / developer rule

The current rules state that the community exists for makers, developers, and founders to share apps, SaaS products, and software projects.

A second rule explicitly allows an application's website, landing page, or demo link when accompanied by description/context rather than a bare link.

### Approval / flair

No moderator preapproval requirement was found for ordinary compliant app showcases.

### Media

Current subreddit configuration supports rich visual submissions, including video/GIF/image formats and external links.

### Disclosure

Developer disclosure remains required by Colosseum's internal campaign policy.

### Piracy adjacency

Low as long as the product is described as software rather than as a media-acquisition mechanism.

### Recommended eventual format

Video/GIF-first app demo with a short explanation of what Colosseum is.

### Scorecard

`AF 3 | PR 4 | PC 5 | DC 5 | ME 5 | LC 5 | PA 1 | MR 1 | CR 2`

---

## 4. r/buildinpublic

**URL:** https://www.reddit.com/r/buildinpublic/  
**Route:** General / maker story  
**Status:** `audited`  
**Research date:** 2026-08-24  
**Confidence:** High

### Exact policy sources

- [Current rules](https://www.reddit.com/r/buildinpublic/about/rules/)
- [Current subreddit metadata](https://www.reddit.com/r/buildinpublic/about.json)

### Self-promotion rule

Current rules allow users to share their work openly but say posts should not exist solely for self-promotion. The expected value is progress, lessons, decisions, feedback, wins, or challenges.

### Developer/project rule

A Colosseum post is permitted **when the post itself is substantively about building the product**, not simply a duplicate download pitch.

### Approval / flair

No moderator preapproval requirement was found.

### Media

Video, GIFs, images, and links are technically compatible.

### Disclosure

Transparent developer authorship is naturally aligned with the subreddit and is independently required by the Colosseum campaign.

### Piracy adjacency

Low when the story is architecture/product-development centered.

### Recommended eventual format

Story/lessons post with real product media as evidence, not as the entire post.

### Avoid

- disguised launch copy;
- fake questions;
- contrived founder narrative;
- traffic-only CTA;
- acquisition/source mechanics.

### Scorecard

`AF 3 | PR 4 | PC 5 | DC 4 | ME 5 | LC 5 | PA 1 | MR 2 | CR 2`

---

## 5. r/github

**URL:** https://www.reddit.com/r/github/  
**Route:** General / GitHub  
**Status:** `audited`  
**Research date:** 2026-08-24  
**Confidence:** High

### Exact policy sources

- [Current rules](https://www.reddit.com/r/github/about/rules/)
- [Current subreddit metadata](https://www.reddit.com/r/github/about.json)
- [Project-promotion megathread](https://www.reddit.com/r/github/comments/1jy8rea/promote_your_projects_here_selfpromotion/)

### Self-promotion rule

Current Rule 4 says self-promotion and project sharing should happen only in the dedicated pinned megathread.

### Developer/project rule

A Colosseum entry is permitted **inside that megathread**. A standalone Colosseum promotional post is not.

### Flair / approval

Use whatever active thread/post conventions apply. There is no reason to request an exception from the standalone-post rule.

### Media/link support

Treat this as a repository-link/comment placement, not a trailer destination.

### Piracy adjacency

Low in the community itself. Because the destination is likely GitHub, the public-facing description should remain disciplined even though the repository README contains more technical details about third-party integrations.

### Recommended eventual format

Megathread project entry only.

### Scorecard

`AF 2 | PR 4 | PC 5 | DC 3 | ME 2 | LC 5 | PA 1 | MR 1 | CR 2`

---

# Backup candidates requiring a gate

## 6. r/TestMyApp

**URL:** https://www.reddit.com/r/TestMyApp/  
**Route:** Testing / feedback  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High

### Sources

- [Current rules](https://www.reddit.com/r/TestMyApp/about/rules/)
- [Current community metadata](https://www.reddit.com/r/TestMyApp/about.json)

### Finding

The community is explicitly for developers seeking people to test apps and recognizes stages from pre-alpha through final/GA. It also says the subreddit is for development/testing rather than app advertising.

### Human gate

Hemanth must decide whether the objective is **genuinely user testing and bug/feedback collection**, or marketing. It should not be relabeled as testing merely to gain access to the audience.

### Media

Video, images, and links are supported. The community has title conventions that include revision/stage and operating system.

### Recommended format if the gate passes

A real testing request centered on what feedback is needed.

### Scorecard

`AF 3 | PR 4 | PC 5 | DC 2 | ME 5 | LC 5 | PA 1 | MR 3 | CR 3`

---

## 7. r/apps

**URL:** https://www.reddit.com/r/apps/  
**Route:** General / apps  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** Medium

### Source

- [Current community metadata/rules text](https://www.reddit.com/r/apps/about.json)

### Finding

The community covers apps across platforms including Windows, but current guidance warns users not to create an account to advertise their app. It does not provide a comparably explicit developer-showcase pathway for an established account.

Under the approved Colosseum gate, this ambiguity is enough to stop copy production.

### Human gate

Use only if Hemanth's existing account history and current subreddit guidance make transparent developer participation clearly compliant, or after moderator clarification.

### Media

Images and links are supported; native video is not the primary route.

### Scorecard

`AF 4 | PR 5 | PC 3 | DC 2 | ME 2 | LC 4 | PA 2 | MR 4 | CR 3`

---

## 8. r/comicbooks

**URL:** https://www.reddit.com/r/comicbooks/  
**Route:** Manga/comics — western comics  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High

### Exact policy sources

- [Current subreddit metadata and posting guidance](https://www.reddit.com/r/comicbooks/about.json)
- [Current FAQ](https://www.reddit.com/r/comicbooks/wiki/faq)
- [Self-promotion section](https://www.reddit.com/r/comicbooks/wiki/faq#wiki_rule_5.3A_self-promotion_and_spam)

### Audience

This is the strongest second-wave **actual reader audience** discovered in the deeper pass. The subreddit explicitly covers comic books, graphic novels, and digital comics, and its sidebar includes a resource area for digital-comics software/apps.

### Self-promotion rule

The FAQ permits creators to share their work only when they are genuinely active community members. Accounts whose own promotional activity outweighs their ordinary participation are treated as spam. The policy also limits own-content frequency.

### Account gate

Whether Hemanth currently satisfies the active-member expectation cannot be inferred from the product or from Reddit-wide account age.

This is a real eligibility condition, not an invitation to manufacture engagement.

### Posting guidance

Current submit guidance requires:
- title tagging;
- link flair after submission;
- accounts less than a day old are filtered;
- piracy links/material are ban-worthy.

### Media

- Native video: disabled.
- Images: supported.
- Text/link posts: supported.

### Piracy adjacency

Low because the community explicitly bans pirated material and piracy-site links.

### Recommended eventual format if eligibility passes

Image/text-first comic-library/reader showcase focused on Tankoban's western-comics experience and local reading.

### Avoid

GetComics, source acquisition, downloads from unauthorized sources, or any implication that Colosseum supplies copyrighted comics.

### Scorecard

`AF 5 | PR 5 | PC 5 | DC 3 | ME 2 | LC 4 | PA 1 | MR 4 | CR 4`

---

## 9. r/OpenSourceProjects

**URL:** https://www.reddit.com/r/OpenSourceProjects/  
**Route:** General / open source  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** Medium-high

### Sources

- [Current rules endpoint](https://www.reddit.com/r/OpenSourceProjects/about/rules/)
- [Current subreddit metadata](https://www.reddit.com/r/OpenSourceProjects/about.json)

### Finding

The community's stated purpose is showcasing and discussing open-source projects, which makes Colosseum topically excellent. However, current subreddit metadata identifies it as restricted rather than an ordinary public-posting community.

### Gate

Posting access/approval must be confirmed before drafting.

### Media

The subreddit configuration supports modern project media and links.

### Scorecard

`AF 3 | PR 5 | PC 4 | DC 4 | ME 5 | LC 5 | PA 1 | MR 4 | CR 2`

---

## 10. r/indiehackers

**URL:** https://www.reddit.com/r/indiehackers/  
**Route:** General / founder-builder  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High

### Exact current moderator source

- [Community Reorganisation & New Posting Guidelines](https://www.reddit.com/r/indiehackers/comments/1uzsfzy/community_reorganisation_new_posting_guidelines/), current moderator guidance retrieved 2026-08-24.
- [Current subreddit rules](https://www.reddit.com/r/indiehackers/about/rules/)

### Policy

The moderator guidance explicitly says:
- if a user is promoting a startup/product or asking people to use it, use the `Self Promotion` category;
- founder-story flair must actually be a story, not a disguised sales pitch;
- fake Q&A and vote manipulation are prohibited;
- new Reddit accounts face additional restrictions;
- the community is considering restricting self-promotion to particular weekdays.

### Why blocked

Self-promotion is clearly recognized, but the cadence policy is actively evolving and account eligibility can matter. That makes it a good future target but not agent-copy-ready without a current posting-time recheck.

### Recommended eventual format

Either an explicitly labeled self-promotion post, or a genuine founder/build story. Never blur the two.

### Scorecard

`AF 3 | PR 3 | PC 4 | DC 4 | ME 4 | LC 5 | PA 1 | MR 4 | CR 5`

---

## 11. r/vibecoding

**URL:** https://www.reddit.com/r/vibecoding/  
**Route:** General / AI-assisted development story  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High

### Exact sources

- [Self-promotion rules update](https://www.reddit.com/r/vibecoding/comments/1mp8oyw/important_new_rules_update_on_selfpromotion/)
- [Current subreddit rules](https://www.reddit.com/r/vibecoding/about/rules/)

### Policy

The moderators explicitly distinguish useful project-sharing from thinly disguised advertising. Project posts are expected to teach or explain something about process, workflow, tools, implementation, code, or design. Commercial/startup/dev-tool shilling receives tighter treatment and may require approval.

### Human gate

If Hemanth wants to discuss the AI-assisted development process publicly, this can be a strong story/lessons community. It should not be converted into a disguised product ad.

Any community-specific authorship/AI-text restriction in force at drafting time must be rechecked before agent-produced copy is used publicly.

### Recommended eventual format

Development-process retrospective with the app serving as evidence.

### Scorecard

`AF 3 | PR 3 | PC 5 | DC 3 | ME 5 | LC 4 | PA 1 | MR 4 | CR 4`

---

## 12. r/degoogle

**URL:** https://www.reddit.com/r/degoogle/  
**Route:** Vault/privacy/offline ownership  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** Medium-high

### Exact sources

- [Current subreddit rules](https://www.reddit.com/r/degoogle/about/rules/)
- [Official DeGoogle Showcase — week of 22 Aug 2026](https://www.reddit.com/r/degoogle/comments/1vvd8jy/degoogle_showcase_week_of_22_aug_2026/)

### Policy

The subreddit operates an official recurring showcase for developers/creators. The showcase expects open-source/public-repository projects, disclosure of affiliation, and a concrete explanation of which Google service or dependency the project replaces/reduces.

### Human/product-truth gate

Colosseum must **truthfully** fit the DeGoogle premise. It is not enough that Colosseum is local-first or open source.

Hemanth must decide whether there is a genuine Google-dependency replacement use case, such as an actual existing workflow the product replaces. Do not manufacture a “Google Play Books replacement” or similar angle just to qualify.

### Scorecard

`AF 2 | PR 2 | PC 5 | DC 2 | ME 3 | LC 5 | PA 1 | MR 4 | CR 5`

---

## 13. r/PrivacyGuides

**URL:** https://www.reddit.com/r/PrivacyGuides/  
**Route:** Vault/privacy  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** Medium

### Sources

- [Current subreddit rules](https://www.reddit.com/r/PrivacyGuides/about/rules/)
- [Moderator policy on developer participation](https://www.reddit.com/r/PrivacyGuides/comments/upcbc6/recent_changes_to_rprivacyguides_rules/)

### Evidence boundary

The developer-participation policy located during research is older than the other sources in this audit. It allowed developers to discuss projects provided they disclose their association and provide meaningful privacy/security context rather than fire-and-forget advertising.

Because that developer-policy source is old and the live current rule surface could not be independently expanded to equivalent detail in this research session, it is **not sufficient for `audited` status**.

### Product-fit gate

Colosseum is not primarily a privacy/security product. A local-files/privacy angle must be real and technically defensible, not bolted on for subreddit access.

### Status

Keep blocked unless current moderators/rules clearly reaffirm a relevant software-discussion path.

### Scorecard

`AF 2 | PR 3 | PC 2 | DC 2 | ME 3 | LC 4 | PA 1 | MR 5 | CR 4`

---

## 14. r/ereader

**URL:** https://www.reddit.com/r/ereader/  
**Route:** Books / Biblio  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High

### Sources

- [Current rules](https://www.reddit.com/r/ereader/about/rules/)
- [Current subreddit metadata](https://www.reddit.com/r/ereader/about.json)

### Policy

The subreddit is highly relevant to Biblio, but current rules prohibit shilling/advertising of commercial products/services without moderator approval and impose a current restriction on AI-generated content.

### Human gate

This is not suitable for agent-generated public copy without resolving both:
1. moderator/product-promotion approval; and
2. the community's current AI-content authorship rule.

### Piracy adjacency

Low. Piracy is separately prohibited.

### Recommended eventual format if approved

Reader-focused demonstration of Biblio's reading experience, typography, audiobook pairing, progress, and local/offline behavior.

### Scorecard

`AF 5 | PR 5 | PC 5 | DC 2 | ME 3 | LC 4 | PA 1 | MR 5 | CR 4`

---

## 15. r/ebooks

**URL:** https://www.reddit.com/r/ebooks/  
**Route:** Books / Biblio  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** Medium-high

### Sources

- [Current rules](https://www.reddit.com/r/ebooks/about/rules/)
- [Current subreddit metadata](https://www.reddit.com/r/ebooks/about.json)

### Policy

The subreddit directly covers ebooks and even recognizes ebook software as part of the ecosystem. Its self-promotion rules, however, are written clearly for authors/publishers rather than software developers.

It also prohibits piracy/torrent material and requires appropriate flair/link behavior.

### Why blocked

A `Self Promotion` flair exists, but the rules do not clearly establish that an ebook-reader/library developer is included in the allowed promotional class.

### Needed human action

Moderator clarification before community-specific copy.

### Scorecard

`AF 5 | PR 5 | PC 3 | DC 2 | ME 5 | LC 5 | PA 1 | MR 4 | CR 3`

---

## 16. r/audiobooks

**URL:** https://www.reddit.com/r/audiobooks/  
**Route:** Books / audiobooks  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High

### Sources

- [Current rules](https://www.reddit.com/r/audiobooks/about/rules/)
- [Current subreddit metadata](https://www.reddit.com/r/audiobooks/about.json)

### Policy

Current promotion rules explicitly address authors, narrators, and publishers. They do not clearly define an ordinary audiobook-player/library software promotion route.

The subreddit is strongly anti-piracy and expects external links to be contextualized inside text posts rather than acting as bare promotions.

### Human action

Moderator clarification before drafting.

### Recommended eventual format if approved

Text-first Biblio read/listen pairing and audiobook-progress story, not a trailer dump.

### Scorecard

`AF 4 | PR 4 | PC 3 | DC 2 | ME 1 | LC 3 | PA 1 | MR 4 | CR 3`

---

## 17. r/homelab

**URL:** https://www.reddit.com/r/homelab/  
**Route:** Vault / local media  
**Status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High

### Sources

- [Current rules](https://www.reddit.com/r/homelab/about/rules/)
- Current moderator software-project guidance reviewed in the research pass.

### Policy

The community has a software-project showcase path, but it is not a generic app-ad channel. Relevant projects must explain the homelab problem solved, use the required project flair, provide appropriate repository/screenshots/evidence, meet account/community eligibility requirements, and comply with AI-development disclosure rules.

### Human/account gate

Before copy:
- confirm Hemanth's account satisfies the subreddit-specific participation/karma gate;
- decide whether Colosseum's Vault/local-media workflow is actually homelab-relevant enough for the rule, rather than merely “software that can run on a computer.”

### Recommended eventual format

Technical local-library/Vault project discussion, not broad Colosseum marketing.

### Scorecard

`AF 3 | PR 3 | PC 5 | DC 3 | ME 4 | LC 5 | PA 1 | MR 4 | CR 3`

---

# High-confidence avoid list

## r/privacy

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/privacy/about/rules/)  
**Research date:** 2026-08-24

Current rules prohibit advertising/marketing ventures and state that app-promotion approvals are currently on hold, including for open-source projects. The rules also explicitly prohibit astroturfing and leading questions used to shill products.

Do not target this community.

---

## r/windows, r/Windows10, r/Windows11

**Status:** `not-recommended`  
**Sources:**
- [r/windows rules](https://www.reddit.com/r/windows/about/rules/)
- [r/Windows10 rules](https://www.reddit.com/r/Windows10/about/rules/)
- [r/Windows11 rules](https://www.reddit.com/r/Windows11/about/rules/)

The Windows communities' current app-promotion pathway requires moderator approval, and the reviewed rules state that applications are not currently being accepted.

Do not contact moderators hoping for an exception while that rule is in force.

---

## r/FreeSoftware

**Status:** `not-recommended`  
**Source:** [July 2026 Rule 7 clarification](https://www.reddit.com/r/freesoftware/comments/1uljopz/rule_7_updated_software_submissions_beware/)  
**Research date:** 2026-08-24

The moderator clarification says software with AI-written code, including vibe-coded software, is not allowed. It distinguishes AI explanation/debugging from AI code generation.

Do not conceal AI involvement to enter this community.

---

## r/DataHoarder

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/DataHoarder/about/rules/)  
**Research date:** 2026-08-24

The combination of software-advertising restrictions and current project/AI moderation makes it a poor campaign target despite superficially good local-media audience overlap.

---

## r/selfhosted

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/selfhosted/about/rules/)  
**Research date:** 2026-08-24

Colosseum is a native Windows desktop media environment, not a self-hosted server/service. Do not bend the product definition to fit the subreddit.

---

## r/HomeServer

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/HomeServer/about/rules/)  
**Research date:** 2026-08-24

Server topicality and advertising restrictions make a native desktop Colosseum showcase a weak fit.

---

## r/MangaCollectors

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/MangaCollectors/about/rules/)  
**Research date:** 2026-08-24

Current policy prohibits the personal-project/self-promotion route required by this campaign.

---

## r/manga

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/manga/about/rules/) plus current moderator enforcement reviewed during the research pass.  
**Research date:** 2026-08-24

The audience is excellent, but current moderation treats drive-by self-promotion and accounts dominated by their own material harshly. That is incompatible with a first/second-wave developer showcase.

---

## r/graphicnovels

**Status:** `not-recommended`  
**Source:** [Moderator rule clarification](https://www.reddit.com/r/graphicnovels/comments/y9xhhq/update_to_sub_rules_please_read/)  
**Research date:** 2026-08-24

Moderators clarified that self-promotion is not allowed and explicitly removed the former moderator-exception path.

---

## r/comics

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/comics/about/rules/)  
**Research date:** 2026-08-24

The community is primarily for comics/OC content rather than comic-reader/library software. `r/comicbooks` is the materially better conditional route.

---

## r/animeapp

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/animeapp/about/rules/)  
**Research date:** 2026-08-24

App promotion requires moderator approval, and the surrounding community context is strongly tied to sources, scraping, download providers, and other piracy-adjacent concepts. Even if technically approved, this is a poor positioning environment for the approved Colosseum campaign.

---

## r/htpc

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/htpc/about/rules/) and recent moderator removals reviewed during research.  
**Research date:** 2026-08-24

Recent moderation repeatedly rejects vested-interest/new-software promotion. Do not target it.

---

## r/AppBusiness

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/AppBusiness/about/rules/)  
**Research date:** 2026-08-24

The subreddit explicitly says not to promote your own app and confines app feedback to a recurring thread. It is also an app-business audience, not primarily prospective Colosseum users.

---

## r/alphaandbetausers

**Status:** `not-recommended` for the current stable campaign  
**Source:** [Current subreddit metadata](https://www.reddit.com/r/alphaandbetausers/about.json)  
**Research date:** 2026-08-24

The subreddit is designed for genuine alpha/beta recruitment. Colosseum's current public release is 1.1.3. It may become relevant for a real future pre-release test, but should not be used to market a stable release under a fake beta premise.

---

## r/BetaTesting

**Status:** `not-recommended`  
**Source:** [Current rules](https://www.reddit.com/r/BetaTesting/about/rules/)  
**Research date:** 2026-08-24

Current rules do not provide a useful path for an external Windows desktop application campaign.

---

## r/softwaretesting

**Status:** `not-recommended`  
**Source:** [Current subreddit](https://www.reddit.com/r/softwaretesting/) and current moderation/removal patterns reviewed in the research pass.  
**Research date:** 2026-08-24

The community is for software-testing practice/profession, not recruiting users to test or promoting a product.

---

# First-wave registry

Only communities that have passed the policy gate appear here. Conditional and rejected communities are intentionally absent.

```json
{
  "communities": [
    {
      "route": "general",
      "community": "r/software",
      "url": "https://www.reddit.com/r/software/",
      "audit_file": "marketing/audits/software.md",
      "post_file": "marketing/posts/software.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Current rules provide a defined open-source/self-promotion path, explicitly prohibit piracy, and support substantive software showcase posts."
    },
    {
      "route": "general",
      "community": "r/IMadeThis",
      "url": "https://www.reddit.com/r/IMadeThis/",
      "audit_file": "marketing/audits/imadethis.md",
      "post_file": "marketing/posts/imadethis.md",
      "status": "audited",
      "confidence": "high",
      "reason": "The community explicitly exists for people to show what they built, including open-source projects, and supports native video/GIF showcases."
    },
    {
      "route": "general",
      "community": "r/ShowYourApp",
      "url": "https://www.reddit.com/r/ShowYourApp/",
      "audit_file": "marketing/audits/showyourapp.md",
      "post_file": "marketing/posts/showyourapp.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Current rules explicitly invite makers and developers to share apps and permit app/demo links when accompanied by meaningful context."
    },
    {
      "route": "general",
      "community": "r/buildinpublic",
      "url": "https://www.reddit.com/r/buildinpublic/",
      "audit_file": "marketing/audits/buildinpublic.md",
      "post_file": "marketing/posts/buildinpublic.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Current rules permit sharing one's work when the post contributes progress, lessons, decisions, or feedback rather than existing solely as an advertisement."
    },
    {
      "route": "general",
      "community": "r/github",
      "url": "https://www.reddit.com/r/github/",
      "audit_file": "marketing/audits/github.md",
      "post_file": "marketing/posts/github.md",
      "status": "audited",
      "confidence": "high",
      "reason": "Current Rule 4 provides a clear project-promotion path in the pinned self-promotion megathread, while prohibiting standalone promotional posts."
    }
  ]
}
```

---

# Final recommendation

## 1. Communities the implementation agent should research/use first

After the already-used `r/windowsapps`, `r/SideProject`, `r/foss`, and `r/coolgithubprojects`:

1. `r/software`
2. `r/IMadeThis`
3. `r/ShowYourApp`
4. `r/buildinpublic`
5. `r/github` — pinned project megathread only

These are the only new candidates in this deeper pass that currently clear the strict `audited` gate without first needing a Hemanth decision, moderator decision, or account-eligibility determination.

## 2. Communities that should be avoided

High-confidence avoids for the current campaign:

- `r/privacy`
- `r/windows`
- `r/Windows10`
- `r/Windows11`
- `r/FreeSoftware`
- `r/DataHoarder`
- `r/selfhosted`
- `r/HomeServer`
- `r/MangaCollectors`
- `r/manga`
- `r/graphicnovels`
- `r/comics`
- `r/animeapp`
- `r/htpc`
- `r/AppBusiness`
- `r/alphaandbetausers` for a stable-release campaign
- `r/BetaTesting`
- `r/softwaretesting`

## 3. Communities requiring moderator contact or another human gate before drafting

### Moderator/policy clarification

- `r/ebooks`
- `r/audiobooks`
- `r/ereader`
- `r/OpenSourceProjects` — restricted-posting access must be established

### Account/community eligibility

- `r/comicbooks`
- `r/homelab`
- `r/apps`
- `r/indiehackers`

### Product-truth decision

- `r/degoogle`
- `r/PrivacyGuides`

### Campaign-objective decision

- `r/TestMyApp` — only if the objective is genuinely testing/feedback rather than marketing
- `r/vibecoding` — only if Hemanth wants a real AI-assisted development/process story rather than a product advertisement

## 4. Strongest video/GIF-first second-wave communities

- `r/IMadeThis`
- `r/ShowYourApp`
- `r/buildinpublic`

Conditional:
- `r/TestMyApp`, if used for genuine testing rather than marketing
- `r/vibecoding`, if used as a process/lessons story

Not trailer-first:
- `r/software`
- `r/github`
- `r/comicbooks`
- `r/audiobooks`

## 5. Communities better suited to story/lessons than a product demo

- `r/buildinpublic`
- `r/indiehackers`
- `r/vibecoding`
- `r/PrivacyGuides`, only if a genuine privacy/threat-model story exists
- `r/homelab`, only if the Vault/local-media architecture genuinely solves a homelab problem

## 6. Hemanth decisions required before niche-route copywriting

The deeper search exposes six genuine human choices:

1. **Comic-books eligibility:** Is Hemanth already an active enough `r/comicbooks` participant to meet its self-promotion rule? Do not artificially create activity to satisfy it.
2. **Testing route:** Should `r/TestMyApp` be used for a genuine user-testing objective separate from marketing?
3. **Books route:** Is moderator contact worth doing for `r/ebooks`, `r/audiobooks`, and/or `r/ereader`, or should Biblio remain out of this Reddit wave?
4. **DeGoogle route:** Does Colosseum truthfully replace/reduce a Google service in a way Hemanth actually wants to claim?
5. **AI-development story:** Does Hemanth want to discuss the AI-assisted development process publicly in communities such as `r/vibecoding`?
6. **Homelab route:** Does Hemanth's account satisfy `r/homelab` eligibility, and is Vault genuinely relevant enough to the homelab problem space?

Until those choices are resolved, none of the affected communities should receive community-specific copy.

---

# Research stopping point

The search stopped after:

- the obvious broad app/FOSS communities had been exhausted;
- direct Reddit rules/current moderator guidance had been checked for the strongest deeper candidates;
- manga/comics, books/audio, video/anime, privacy/local, testing, Windows, maker, GitHub, FOSS, and self-hosted lanes had each been probed;
- five new communities had a clear permitted developer/project path;
- the highest-value niche candidates had explicit gates rather than unresolved generic “maybe allowed” assumptions;
- additional candidates were increasingly either lower-fit variants of already rejected categories or communities without a current authoritative promotion path.

The principal unresolved issue is not lack of subreddit discovery. It is **eligibility and policy friction in the medium-specific communities**. Further broad searching is unlikely to change that without first resolving the human gates above.

## Research integrity note

No generic “best subreddit” lists, growth-hacking anecdotes, karma-building schemes, or Reddit Founder performance claims were used as policy evidence.

No recommendation requires:
- fake organic behavior;
- pretending not to be the developer;
- artificial karma-building;
- undisclosed promotion;
- piracy/source positioning; or
- circumventing moderator rules.

No Reddit post, comment, moderator message, or publication action was performed during this research.

---

# Appendix C — Ecosystem-Adjacent Deep Research

# Colosseum Ecosystem-Adjacent Subreddit Deep Research

**Research date:** 2026-08-24  
**Campaign stage:** Third-pass ecosystem-adjacent discovery  
**Scope:** Stremio and media-library/player ecosystems rather than generic app-promotion communities.  
**Prior waves intentionally excluded:** the broad communities already researched and/or used, including `r/windowsapps`, `r/SideProject`, `r/foss`, `r/coolgithubprojects`, plus the generic second-wave shortlist.  
**Action boundary:** Research only. No Reddit posts, comments, moderator messages, publishing, cross-posting, or community-specific marketing copy were produced.

## Executive recommendation

This pass found something much more valuable than another batch of generic “show your app” communities:

**`r/StremioMods` and `r/StremioAddons` are the strongest ecosystem-adjacent audiences discovered so far for Colosseum.**

They are unusually relevant because current accepted posts include independent Windows Stremio clients, modified Stremio shells, MPV-backed clients, and broader “community project” releases. Those are much closer to Colosseum than a normal software-showcase subreddit.

However, under Colosseum’s approved policy gate, neither can yet be marked `audited`:

- I could verify current accepted developer/community-project posts.
- I could verify `r/StremioAddons` currently enforces a mandatory “Read The Rules” acknowledgement before posting.
- I could verify a developer/community-project trust mechanism around verified developer flair.
- I could **not** retrieve current authoritative rule text that explicitly says a separate Stremio-compatible client may self-promote.
- Older community evidence conflicts with the permissive interpretation by describing a “no self-promotion or advertising” rule.
- Both communities are materially piracy-adjacent, especially `r/StremioAddons`.

Therefore the correct status is **`blocked-for-human-review`**, not `audited` and not automatically `not-recommended`.

### Priority ecosystem review queue

1. **r/StremioMods** — highest priority. Current accepted posts are near-direct precedents for independent Windows Stremio-adjacent clients, including a Windows client using MPV.
2. **r/StremioAddons** — extremely relevant because Colosseum implements Stremio-compatible extensions, and the community currently accepts “community project” announcements, but piracy-adjacency and rule ambiguity require extra care.
3. **r/mpv** — technically authentic route because Theatre’s current production player uses MpvQt/libmpv. Best for a technical player/client story, not the broad Colosseum pitch.
4. **r/Calibre** — excellent Biblio audience and current developer-reader posts exist, but Colosseum does not currently integrate with Calibre/OPDS. Do not imply interoperability that does not exist.
5. **r/audiobookshelf** — exceptional future Biblio ecosystem if Audiobookshelf integration is ever added; official project docs explicitly welcome third-party clients. It is not currently a truthful Colosseum integration route.
6. **r/Jellyfin** — current community allows third-party projects with strict AI/LLM disclosure and authorship rules, but Colosseum does not integrate Jellyfin today. Not recommended for the current campaign.
7. **r/PleX** — ecosystem users are visibly interested in alternative clients, but 2026 community discussion shows growing hostility to AI/vibe-coded self-promotion and there is no current Colosseum Plex integration. Do not target now.
8. **r/KavitaManga / r/trakt / r/kodi / r/emby / r/komga** — useful future ecosystem routes only if real interoperability is added.

### Strict outcome

**No new ecosystem-specific subreddit passes the full `audited` gate today.**

That is not a failed research result. It means the best next step is not to spray more generic communities. It is to resolve the policy gate for the two Stremio communities, because their audience relevance is far higher than the broad second-wave subs.

---

## Candidate audit table

| Route | Subreddit | Ecosystem relation | Audience fit | Current developer-project evidence | Policy clarity | Piracy adjacency | Status | Confidence |
|---|---|---|---|---|---|---|---|---|
| Theatre / extensions | r/StremioMods | Independent Stremio clients/mods | 5/5 | Multiple 2026 Windows client releases accepted | Medium-low | High | `blocked-for-human-review` | High |
| Theatre / extensions | r/StremioAddons | Stremio-compatible addon ecosystem | 5/5 | 2026 community-client/project releases accepted; rules acknowledgement enforced | Medium-low | Very high | `blocked-for-human-review` | High |
| Theatre / player | r/mpv | Colosseum Theatre uses MpvQt/libmpv | 5/5 technical | Current developer-built media-player showcase accepted | Medium | Low-moderate | `blocked-for-human-review` | Medium-high |
| Books / Biblio | r/Calibre | Ebook library/reader ecosystem | 5/5 audience, 2/5 integration | Current third-party reader developer posts accepted | Medium | Low | `blocked-for-human-review` | Medium-high |
| Books / Biblio | r/audiobookshelf | Audiobook/ebook library ecosystem | 5/5 audience, 1/5 integration | Official project encourages third-party clients; subreddit contains app launches | High ecosystem policy, unclear subreddit promo rule | Low-moderate | `not-recommended` now / high-value future | High |
| Vault / Theatre | r/Jellyfin | Local media server/client ecosystem | 5/5 audience, 1/5 integration | Current subreddit explicitly contemplates third-party tools | High AI policy; topical mismatch | Low | `not-recommended` | High |
| Vault / Theatre | r/PleX | Personal media server/client ecosystem | 5/5 audience, 1/5 integration | Alternative-client promotion is common but contentious | Low | Low | `not-recommended` | High |
| Manga/books | r/KavitaManga | Self-hosted books/comics/manga | 5/5 audience, 1/5 integration | Third-party companion apps discussed | Low | Low | `not-recommended` now | Medium-high |
| Theatre | r/trakt | Watch-history/progress ecosystem | 4/5 audience, 1/5 integration | Current developer Stremio-addon project accepted | Low | Moderate | `not-recommended` now | Medium |
| Theatre / local | r/kodi | Media-center ecosystem | 4/5 audience, 1/5 integration | No sufficient current promotion-policy evidence found | Low | Moderate | `not-recommended` | Medium |
| Theatre / local | r/emby | Personal media-server ecosystem | 4/5 audience, 1/5 integration | No sufficient current subreddit developer-promo policy found | Low | Low | `not-recommended` | Medium |
| Manga/comics | r/komga | Comic/manga server ecosystem | 4/5 audience, 1/5 integration | No sufficient current policy evidence found | Low | Low | `not-recommended` now | Medium |
| Theatre / device | r/firetvstick | TV media apps | 2/5 due Windows-only product | 2026 open-source Stremio-style media-hub launch accepted | Medium | High | `not-recommended` | High |
| General Stremio | r/Stremio | Core Stremio community | 4/5 audience | Current mod enforcement requires posts be directly about Stremio | Medium | Moderate | `not-recommended` for standalone Colosseum | High |

---

# Per-community audits

## r/StremioMods

**URL:** https://www.reddit.com/r/StremioMods/  
**Route:** Theatre / Stremio-compatible ecosystem  
**Final status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High  
**Priority:** **#1 ecosystem target for policy resolution**

### Why this community matters

This is the closest community match discovered so far.

Recent accepted posts are not merely addons. They include **independent Stremio-adjacent Windows desktop clients**, exactly the class of comparison Colosseum would naturally invite.

### Current evidence

#### StreamGo Community — accepted January 12, 2026

Source:

https://www.reddit.com/r/StremioMods/comments/1qax2zj/new_app_release_streamgo_community/

The developer openly described StreamGo as:

- a community project;
- unofficial and unsupported by Stremio;
- an optimized Electron wrapper around Stremio;
- a downloadable application with its own repository and release links.

The post remained an ordinary community project announcement rather than being removed merely for being a competing/modified client.

#### MyStremio 2.1.3 — accepted June 24, 2026

Source:

https://www.reddit.com/r/StremioMods/comments/1ue0snj/mystremio_213_update_a_customized_stremio_shell/

The post explicitly presents:

- an independent Windows desktop client;
- no official Stremio affiliation;
- custom UI/navigation;
- enhanced player tooling;
- community-project credits.

#### MyStremio 2.3.2 — accepted July 20, 2026

Source:

https://www.reddit.com/r/StremioMods/comments/1v20lk1/mystremio_232_update_a_customized_stremio_shell/

This is an especially strong precedent because the developer says the Windows client:

- uses **MPV as its native video player**;
- offers its own UI enhancements;
- integrates an addon manager;
- ships a Windows installer;
- links the GitHub repository and releases;
- openly disclaims affiliation with official Stremio.

That is remarkably close to several real Colosseum Theatre characteristics.

### Self-promotion / developer-post rule

The current accepted-post evidence strongly suggests community projects and independent Stremio client work are topically welcome.

However, I could not retrieve current authoritative subreddit-rule text that explicitly establishes the self-promotion conditions.

Under the approved Colosseum audit contract, precedent alone cannot upgrade an ambiguous rule to `audited`.

### Flair / moderator pre-approval

No reliable current mandatory flair or pre-approval rule was retrieved.

This is the unresolved gate.

### Media and links

Recent accepted project posts include:

- screenshots/images;
- GitHub repository links;
- release/download links;
- substantive feature descriptions.

### Disclosure expectations

Any Colosseum post here should plainly say:

- Hemanth built Colosseum;
- Colosseum is an independent project;
- it is **not affiliated with Stremio**;
- it supports Stremio-compatible extension protocols rather than being an official Stremio client.

### Piracy adjacency

**High**, because the Stremio mod ecosystem naturally intersects addons, streams, and acquisition.

That does **not** automatically disqualify this subreddit. The accepted MyStremio posts demonstrate that client/UI/player work itself has a legitimate community lane.

The campaign must nevertheless omit:

- Torrentio;
- debrid providers;
- torrent source details;
- “free streaming”;
- acquisition reliability;
- unauthorized source configuration;
- “watch anything” claims.

### Recommended eventual post format

If current rules/moderator guidance clear the policy gate:

**Independent Windows client / Stremio-compatible ecosystem project showcase**, with the Theatre player, UI, extensions compatibility, local media, and broader cross-media shell as the differentiators.

### Human decision required

Before any copy is drafted, current subreddit rules must be manually confirmed or moderators must clarify whether a separate Stremio-compatible application may be showcased.

---

## r/StremioAddons

**URL:** https://www.reddit.com/r/StremioAddons/  
**Route:** Theatre / extensions  
**Final status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** High  
**Priority:** **#2 ecosystem target for policy resolution**

### Why this community matters

Colosseum has a real, current Stremio ecosystem connection: Theatre supports **Stremio-compatible extensions**.

That makes this audience substantially more relevant than a generic “media app” subreddit.

### Current accepted developer/community-project evidence

#### StreamGo Community — January 12, 2026

Source:

https://www.reddit.com/r/StremioAddons/comments/1qawlz8/new_app_release_streamgo_community/

The accepted post openly announces an unofficial community Stremio desktop application, repository, releases, performance changes, plugins, and UI modifications.

This is direct evidence that at least some **client/community-project announcements**, not only manifests/addons, are accepted.

### Current rules-acknowledgement enforcement

Multiple posts in 2026 were automatically removed by the subreddit’s **Read The Rules** app because the author had not submitted acknowledgement that they had read the current rules.

Example source:

https://www.reddit.com/r/StremioAddons/comments/1tuusgt/removed/

The automated message says the user must:

- refresh themselves on community rules because they may change;
- submit a rules acknowledgement;
- then retry the post.

This makes one thing very clear: **current-rule acknowledgement is a real posting gate in this community.**

### Developer verification/trust signal

Current automated community-project notices advise readers to check whether the poster has a user flair identifying their addon/project, describing that as a way to confirm a developer is verified in the community.

That is useful evidence that developer identity and project legitimacy matter.

### Policy conflict

An older community discussion from 2025 quotes/describes a subreddit rule as prohibiting self-promotion/advertising while criticizing paid addon promotion.

Because the current accepted project precedent and the older rule description do not cleanly reconcile, the safe status is `blocked-for-human-review`.

### Piracy adjacency

**Very high.**

This is not theoretical. Recent 2026 posts openly discuss:

- Torrentio;
- P2P addons;
- torrent-backed addons;
- free HTTP sources;
- VPN use;
- debrid-oriented configurations.

Example current community post:

https://www.reddit.com/r/StremioAddons/comments/1uzygxu/the_ultimate_list_of_free_addons/

Another August 2026 post asks users to share a completely free addon setup:

https://www.reddit.com/r/StremioAddons/comments/1vov0ec/my_completely_free_stremio_setup_loving_it_so_far/

### What that means for Colosseum

This community should **not** be rejected merely because piracy-adjacent discussion exists. Colosseum genuinely implements a Stremio-compatible extension layer.

But the eventual post must be quarantined from acquisition language.

Lead with:

- native Windows application;
- Stremio-compatible extension protocol;
- extension management UI;
- Theatre player;
- local Vault;
- one shell across media.

Do **not** lead with or explain:

- torrent addons;
- debrid;
- third-party source availability;
- “free” catalogues/streams;
- Nyaa/LibGen/AudioBookBay;
- exact torrent source paths;
- how to obtain copyrighted media.

### Important ecosystem boundary

Stremio’s addon-catalog submission rules separately prohibit using addon listings to advertise competitor Stremio applications/addon platforms.

Therefore **do not treat Colosseum itself as something to advertise through an addon-catalog submission**. Reddit community discussion and addon-catalog distribution are separate policy surfaces.

### Human decision required

Before copywriting:

1. Hemanth should complete/read the subreddit’s current rules acknowledgement himself.
2. Confirm whether an independent Stremio-compatible application counts as a permitted “Community Project”.
3. If unclear after reading current rules, ask moderators before drafting.

---

## r/Stremio

**URL:** https://www.reddit.com/r/Stremio/  
**Route:** Theatre / general Stremio  
**Final status:** `not-recommended` for a standalone Colosseum launch  
**Research date:** 2026-08-24  
**Confidence:** High

### Current moderator evidence

A January 22, 2026 moderator action states:

> The main focus of your post should be directly related to Stremio.

The same moderator message directs help about other software and content acquisition to their own subreddits.

Source:

https://www.reddit.com/r/Stremio/comments/1qjzplt/after_dv_hdr_content_stremio_exits_and_causes/

### Assessment

A standalone “I built Colosseum” post is weaker topical fit here than in `r/StremioMods` or `r/StremioAddons`.

The only plausible safe use would be a narrowly Stremio-specific interoperability discussion, but the current campaign does not need to force this when two more appropriate ecosystem communities exist.

### Recommendation

Do not target the main Stremio subreddit for the current campaign.

---

## r/mpv

**URL:** https://www.reddit.com/r/mpv/  
**Route:** Theatre / native player engineering  
**Final status:** `blocked-for-human-review`  
**Research date:** 2026-08-24  
**Confidence:** Medium-high  
**Priority:** #3 ecosystem policy check

### Why it is genuinely relevant

Colosseum Theatre’s production player is a QML surface over **MpvQt/libmpv**.

This is not a stretched marketing association. MPV is a real implementation dependency.

### Current community precedent

A current August 21, 2026 developer post showcased a new media player built on MPV/libmpv and explicitly included a developer disclosure.

That demonstrates that developer-created MPV frontends are topical community content.

### Policy limitation

I did not retrieve authoritative current subreddit rule text establishing the allowed self-promotion cadence/flair.

Therefore this cannot be `audited`.

### Recommended eventual format if cleared

A technical development post centered on:

- embedding libmpv in a native Windows/QML environment;
- player controls and lifecycle;
- subtitles;
- local playback;
- media-session integration;
- lessons from building a dedicated frontend.

Colosseum’s broader manga/books universe should be context, not the pitch.

### Avoid

- “Stremio replacement” framing;
- streaming-source details;
- torrents/debrid;
- download acquisition;
- generic product advertising.

---

## r/Calibre

**URL:** https://www.reddit.com/r/Calibre/  
**Route:** Books / Biblio  
**Final status:** `blocked-for-human-review`, low current priority  
**Research date:** 2026-08-24  
**Confidence:** Medium-high

### Audience fit

Excellent. This is exactly the kind of audience that cares about:

- ebook libraries;
- metadata;
- readers;
- local ownership;
- desktop book workflows.

### Current developer-project precedent

In August 2026, the subreddit accepted a developer post for a reader built around Calibre-Web’s OPDS feed. The developer openly identified themselves and requested feedback.

This establishes that third-party reading clients can be topical.

### Critical product-truth problem

**Colosseum currently does not claim Calibre or Calibre-Web/OPDS integration.**

Therefore posting Colosseum simply because both products manage books would risk looking like competitor promotion rather than ecosystem participation.

### Status reason

Even if the community permits reader-app posts, a truthful ecosystem angle is presently weak.

Keep it blocked unless:

- current rules explicitly allow a broader ebook-reader showcase; or
- Colosseum later gains real Calibre/OPDS interoperability.

### Recommended future route

If Biblio gains OPDS/Calibre support, this immediately becomes one of the strongest niche audiences available.

---

## r/audiobookshelf

**URL:** https://www.reddit.com/r/audiobookshelf/  
**Route:** Books / audiobooks / Biblio  
**Final status:** `not-recommended` for current campaign; **high-value future integration route**  
**Research date:** 2026-08-24  
**Confidence:** High

### Official ecosystem policy

Audiobookshelf’s official documentation explicitly says:

- third-party apps are encouraged;
- developers are welcome to build third-party clients or integrate Audiobookshelf into an existing app;
- third-party clients do not have to be free/open source;
- developers must not imply official affiliation;
- the Audiobookshelf name/logo must not be used to promote piracy.

Official source:

https://audiobookshelf.org/docs/faq/app/

The official community-app directory also accepts stable third-party clients and currently lists Windows-capable community clients.

Source:

https://audiobookshelf.org/docs/documentation/community/community-apps/

### Reddit precedent

Current `r/audiobookshelf` discussion contains repeated developer-app announcements, including third-party client projects.

This shows the ecosystem itself is very receptive to companion clients.

### Why Colosseum should not target it today

Colosseum currently has no Audiobookshelf integration.

Without that, a Biblio promotion would be an adjacent competitor pitch rather than an Audiobookshelf ecosystem contribution.

### Future value

If Biblio gains genuine Audiobookshelf library/progress integration, this becomes a **top-tier niche route**, potentially better than the generic audiobook communities because:

- the audience already values local/self-hosted audiobook ownership;
- third-party clients are officially encouraged;
- cross-device/offline client features are normal ecosystem concepts.

---

## r/Jellyfin

**URL:** https://www.reddit.com/r/jellyfin/  
**Route:** Vault / Theatre / local media  
**Final status:** `not-recommended` for current campaign  
**Research date:** 2026-08-24  
**Confidence:** High

### Current subreddit posture

Current AutoModerator notices repeatedly state that:

- `r/jellyfin` is a community space rather than an official support channel;
- third-party client/tool bugs should go to those projects’ own support channels;
- people sharing something they made must follow Jellyfin’s LLM/AI policy;
- anything developed or created using LLM tooling requires disclosure and can be removed.

Example current source:

https://www.reddit.com/r/jellyfin/comments/1u2ptp2/i_built_a_guided_installer_for_a_full/

### Developer project precedent

The June 2026 “MediaStack” project post was accepted and openly shared a third-party Jellyfin-oriented project.

So third-party ecosystem tools are not automatically disallowed.

### Jellyfin’s current AI/LLM policy

Official source:

https://jellyfin.org/docs/general/contributing/llm-policies/

The policy applies to official projects and community spaces.

Important requirements include:

- verbatim LLM output is prohibited in direct community communication;
- primarily LLM-developed third-party tools must be disclosed;
- pure vibe coding is rejected for official contributions;
- developers remain responsible for understanding, testing, licensing, and explaining their work.

### Consequence for Colosseum marketing

An agent-generated Reddit post could **not** simply be pasted into this community. Hemanth would need to write the final public post in his own words, with appropriate AI-development disclosure.

### Topical mismatch

More importantly, Colosseum does **not currently integrate with Jellyfin**.

The README credits Jellyfin as a visual influence for some Vault card language, but visual influence is not an interoperability story.

Do not post in `r/jellyfin` merely because Vault also manages local media.

### Future route

If Colosseum later gains a real Jellyfin client/import/server integration, revisit immediately.

---

## r/PleX

**URL:** https://www.reddit.com/r/PleX/  
**Route:** Vault / Theatre / personal media  
**Final status:** `not-recommended` for current campaign  
**Research date:** 2026-08-24  
**Confidence:** High

### 2026 community environment

A March 2, 2026 meta discussion documented a dramatic increase in self-promotion for:

- Plex apps;
- utilities;
- websites;
- alternative UIs.

Community members debated:

- limiting self-promotion to one day;
- creating a megathread;
- vetting projects;
- rejecting low-quality/vibe-coded applications.

Source:

https://www.reddit.com/r/PleX/comments/1rig27n/selfpromotion_day/

This is strong evidence that third-party client promotion occurs, but is currently contentious.

### Audience opportunity

Some commenters specifically welcomed alternative clients because they were dissatisfied with Plex’s native apps.

So the audience concept is real.

### Why not current Colosseum

Colosseum currently has no Plex integration.

A generic “alternative media client” post would therefore be both:

- only weakly Plex-specific; and
- entering a community actively frustrated with app self-promotion.

### Recommendation

Do not target until Colosseum has real Plex interoperability or a narrowly Plex-specific capability.

---

## r/KavitaManga

**URL:** https://www.reddit.com/r/KavitaManga/  
**Route:** Manga/comics/books  
**Final status:** `not-recommended` for current campaign; future integration candidate  
**Research date:** 2026-08-24  
**Confidence:** Medium-high

### Audience fit

Excellent conceptual fit for Tankoban/Biblio:

- manga;
- comics;
- books;
- self-hosted library;
- reader clients;
- progress.

### Ecosystem evidence

A February 2026 discussion specifically asks about companion applications for Kavita, and community replies point to the official list of external apps and third-party reader work.

Source:

https://www.reddit.com/r/KavitaManga/comments/1rb129u/companion_apps/

Another recent thread discusses offline third-party reading clients and progress syncing.

Source:

https://www.reddit.com/r/KavitaManga/comments/1pafhri/offline_reading_and_annotations_that_sync_when_im/

### Product mismatch

Colosseum currently has no Kavita integration.

Therefore an unsolicited Tankoban/Biblio launch is not an ecosystem contribution yet.

### Future route

Kavita interoperability would create a very compelling manga/comics/books niche marketing lane.

---

## r/trakt

**URL:** https://www.reddit.com/r/trakt/  
**Route:** Theatre / progress  
**Final status:** `not-recommended` for current campaign  
**Research date:** 2026-08-24  
**Confidence:** Medium

### Current developer precedent

On June 1, 2026, `r/trakt` accepted a developer post for a free Stremio addon that reads Trakt history and exposes:

- Continue Watching;
- resume;
- up-next;
- episode progress.

Source:

https://www.reddit.com/r/trakt/comments/1ttdvld/i_built_a_free_stremio_addon_that_surfaces_your/

This is excellent evidence that interoperable developer tools can be discussed.

### Why Colosseum is not a current fit

Colosseum’s current approved product truth does not include a Trakt integration.

Do not market shared Continue/progress as though it were Trakt-connected.

### Future route

If Theatre later adds Trakt sync, this becomes a high-value ecosystem subreddit.

---

## r/kodi

**URL:** https://www.reddit.com/r/kodi/  
**Route:** Theatre / media center  
**Final status:** `not-recommended`  
**Research date:** 2026-08-24  
**Confidence:** Medium

### Assessment

Kodi is obvious conceptual adjacency, but the research did not produce a sufficiently current authoritative third-party self-promotion path.

More importantly, Colosseum does not currently interoperate with Kodi.

No need to incur policy/competitor risk for a merely conceptual similarity.

---

## r/emby

**URL:** https://www.reddit.com/r/emby/  
**Route:** Theatre / Vault / personal media  
**Final status:** `not-recommended`  
**Research date:** 2026-08-24  
**Confidence:** Medium

No current Colosseum Emby integration and no sufficiently strong current subreddit promotion policy was established.

Do not target.

---

## r/komga

**URL:** https://www.reddit.com/r/komga/  
**Route:** Manga/comics  
**Final status:** `not-recommended` for current campaign  
**Research date:** 2026-08-24  
**Confidence:** Medium

The audience would be excellent for a real Komga client/integration, but Colosseum currently has none and the research did not establish a current policy path for unrelated reader promotion.

---

## r/firetvstick

**URL:** https://www.reddit.com/r/firetvstick/  
**Route:** Theatre / media-app ecosystem  
**Final status:** `not-recommended`  
**Research date:** 2026-08-24  
**Confidence:** High

### Interesting precedent

A July 23, 2026 post successfully launched **ARVIO**, described as:

- free and open source;
- Stremio-style;
- a media hub;
- integrating Jellyfin/Plex/debrid-style ecosystems;
- not hosting media.

Source:

https://www.reddit.com/r/firetvstick/comments/1v4q09x/i_built_arvio_a_free_opensource_streaming_hub_for/

This proves there is audience appetite for independent media-hub projects.

### Why Colosseum should not use it

Colosseum is Windows 10/11 only.

A Fire TV subreddit is therefore a direct platform mismatch.

Do not target simply because another media hub succeeded there.

---

# Ecosystem policy conclusions

## 1. Stremio ecosystem is the real third-wave opportunity

The strongest adjacent route is not Plex/Jellyfin/Kodi.

It is **Stremio**, because Colosseum has an actual protocol-level relationship:

- Theatre supports Stremio-compatible extensions.
- Community projects are part of the current Stremio ecosystem.
- `r/StremioMods` currently accepts independent Windows clients.
- `r/StremioAddons` currently accepts at least some broader community-project announcements.

This is materially different from merely competing with Jellyfin/Plex/Calibre.

## 2. r/StremioMods is more attractive than r/StremioAddons for the first ecosystem post

Why:

- accepted precedent is almost exact;
- independent Windows clients are normal content;
- MPV-backed client precedent exists;
- the discussion centers client modifications rather than primarily addon/source availability.

Its piracy-adjacency is still real, but lower than `r/StremioAddons`.

## 3. r/StremioAddons could be enormous, but requires the strictest positioning

It has the strongest direct audience for Colosseum’s Stremio-compatible extension system.

It also has the highest probability that comments immediately ask about:

- Torrentio;
- debrid;
- sources;
- free streaming;
- addon compatibility.

The eventual Colosseum bundle therefore needs a safe first-comment/source answer **before publication**, as already required by the approved marketing spec.

## 4. Do not market into ecosystems Colosseum does not actually support

This pass found healthy developer ecosystems around:

- Audiobookshelf;
- Jellyfin;
- Calibre;
- Kavita;
- Plex;
- Trakt.

That is useful product-strategy information, but not permission to pretend Colosseum is already part of those ecosystems.

Audience similarity alone is insufficient.

## 5. Ecosystem integration could unlock better future marketing than generic subreddit hunting

Future integrations would immediately create high-quality native audiences:

| Future Colosseum integration | Strong community unlocked |
|---|---|
| Audiobookshelf client/progress integration | r/audiobookshelf |
| Calibre-Web / OPDS | r/Calibre |
| Kavita | r/KavitaManga |
| Jellyfin client/library integration | r/jellyfin |
| Plex client/library integration | r/PleX |
| Trakt sync | r/trakt |
| Kodi/Emby integrations | corresponding ecosystem communities |

This is a better long-term marketing loop than repeatedly finding unrelated “show your app” communities.

---

# Priority human-review queue

These are **not publication approvals**. They are the communities worth resolving next.

```json
{
  "communities": [
    {
      "route": "theatre-anime",
      "community": "r/StremioMods",
      "url": "https://www.reddit.com/r/StremioMods/",
      "audit_file": "marketing/audits/stremiomods.md",
      "post_file": "marketing/posts/stremiomods.md",
      "status": "blocked-for-human-review",
      "confidence": "high",
      "reason": "Recent accepted posts include independent Windows Stremio clients and an MPV-backed customized client, but current explicit self-promotion rules were not retrievable."
    },
    {
      "route": "theatre-anime",
      "community": "r/StremioAddons",
      "url": "https://www.reddit.com/r/StremioAddons/",
      "audit_file": "marketing/audits/stremioaddons.md",
      "post_file": "marketing/posts/stremioaddons.md",
      "status": "blocked-for-human-review",
      "confidence": "high",
      "reason": "Colosseum has real Stremio-extension compatibility and current community-project announcements are accepted, but rule ambiguity and high piracy-adjacency require manual policy review."
    },
    {
      "route": "theatre-anime",
      "community": "r/mpv",
      "url": "https://www.reddit.com/r/mpv/",
      "audit_file": "marketing/audits/mpv.md",
      "post_file": "marketing/posts/mpv.md",
      "status": "blocked-for-human-review",
      "confidence": "medium-high",
      "reason": "Theatre genuinely uses MpvQt/libmpv and current MPV frontend developer posts are topical, but explicit current promotional rules need confirmation."
    },
    {
      "route": "books-audiobooks",
      "community": "r/Calibre",
      "url": "https://www.reddit.com/r/Calibre/",
      "audit_file": "marketing/audits/calibre.md",
      "post_file": "marketing/posts/calibre.md",
      "status": "blocked-for-human-review",
      "confidence": "medium-high",
      "reason": "Third-party reader developer posts are accepted and the audience fits Biblio, but Colosseum has no current Calibre/OPDS integration and broader app-promotion policy is unclear."
    }
  ]
}
```

There is intentionally **no `audited` first-wave registry** in this ecosystem pass.

Under the approved Colosseum status definitions, current accepted precedent does not substitute for a clear current community rule when the self-promotion boundary itself remains uncertain.

---

# Final recommendation

## 1. Research first

The implementation agent should resolve current policy in this order:

1. **r/StremioMods**
2. **r/StremioAddons**
3. **r/mpv**
4. **r/Calibre**

The first two are vastly more important than adding another generic app subreddit.

## 2. Avoid for the current campaign

Do not currently target:

- `r/Stremio`
- `r/Jellyfin`
- `r/PleX`
- `r/audiobookshelf`
- `r/KavitaManga`
- `r/trakt`
- `r/kodi`
- `r/emby`
- `r/komga`
- `r/firetvstick`

Most are good audiences for **future real integrations**, not current Colosseum promotion.

## 3. Communities requiring moderator/current-rule confirmation before drafting

- `r/StremioMods`
- `r/StremioAddons`
- `r/mpv`
- `r/Calibre`

For `r/StremioAddons`, Hemanth should also personally complete/read the current rules acknowledgement rather than relying on this research summary.

## 4. Best future video/GIF showcase candidate

**r/StremioMods** is the strongest ecosystem-native visual showcase candidate.

Its current accepted Windows-client posts include detailed screenshots/product demonstrations and direct repository/release references.

## 5. Best technical/story candidate

**r/mpv**.

If cleared by current rules, it should receive an engineering/player story rather than the general Colosseum trailer.

## 6. Important Hemanth decision before ecosystem copywriting

The central decision is:

**Do we want the first Stremio-ecosystem post to present Colosseum primarily as an independent Windows media environment that supports Stremio-compatible extensions, or do we want to defer until its extension compatibility has been freshly runtime-verified for the release being marketed?**

The approved campaign rules say public claims must ultimately be runtime verified against the target build. Therefore the research can identify `r/StremioMods` and `r/StremioAddons` now, but copy should not claim broad Stremio compatibility beyond what the later claims gate proves.

---

# Research integrity / stopping point

The ecosystem pass examined:

- Stremio
- StremioAddons
- StremioMods
- MPV
- Jellyfin
- Plex
- Calibre
- Audiobookshelf
- Kavita
- Trakt
- Kodi
- Emby
- Komga
- Fire TV media-app communities

The search stopped when the pattern stabilized:

1. Stremio is the only major media ecosystem with a **current real Colosseum interoperability relationship**.
2. `r/StremioMods` and `r/StremioAddons` contain strong recent project precedents but lack sufficiently retrievable explicit self-promotion rules for automatic approval.
3. Other ecosystems contain attractive audiences and often welcome third-party apps, but Colosseum currently lacks the required interoperability, making promotion there topically weak or misleading.
4. Further generic ecosystem searching would mostly produce more “great audience if you integrate it later” communities rather than additional current campaign targets.

No generic subreddit-ranking lists, growth anecdotes, fake-karma strategies, or performance claims were used as policy evidence.

No moderator was contacted and no Reddit publication action occurred.
