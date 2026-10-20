# Argon assignment: Colosseum website UI refresh

Use skill: colosseum-argon-workspace.

Work from this imported GitHub repository and branch `kingoftheseas56/Colosseum:argon-source`. The real website is mirrored under `web/colosseum-site/`, originally `Brotherhood/agents/colosseum-site` at commit `4f33a13df11bbd4d54ad295dc5669c9282fb6908`. Read `ARGON_SITE_README.md` and `ARGON_SITE_MANIFEST.json` first. The `SOURCE_MANIFEST.json` at repo root describes a separate Colosseum app snapshot, not the website.

**Mission:** Redesign the public website's presentation and interactions to feel like Colosseum itself: cinematic, cohesive, responsive, exploratory, and worthy of its distinct Worlds and fan Tributes. Not a generic marketing grid and not a full native-to-Svelte conversion. Working Svelte components are welcome when they improve interaction, but do not needlessly rewrite the entire site.

Keep a clear top-level visitor journey: understand Colosseum → explore a World or a Tribute → interact with its existing demonstration → download the actual desktop application. Improve navigation, page hierarchy, transition quality, mobile presentation, keyboard focus and iframe/overlay entry/exit behavior.

**Mandatory preservation:**
- Existing Theatre, Tankoban, Biblio, Home, readers, player, Downloads, Extensions and Vault demonstrations; their illustrated/demo states must not be misrepresented as shipping runtime.
- Existing Tribute URLs `/tributes/one-piece/`, `/tributes/star-wars/`, `/tributes/stormlight/`; no overwrites of approved bespoke Tribute visual identities.
- The six approved `demos/release119-panels/` compositions and original assets. Improve only their surrounding site presentation, never recreate the panels from scratch.
- Windows/Linux v1.1.9 installer links and `/download/v1.1.9/windows`, `/download/v1.1.9/linux` redirect tracking behavior; exact shipped versus Coming Up status labels.
- Existing About voice, accessible links and appropriate copyright/data disclaimers.
- Source files and names used for the public site, except for an explicitly justified architectural change with an integration plan.

**Design language:** Colosseum's cold ripple, glass, Fraunces, restrained gold highlights, cinematic media-first framing. No generic equal-sized grid, placeholder stock imagery, gratuitous controls, visibly persistent scrollbars or fabricated data/APIs/logins. Reuse existing public artwork, including large media referenced by the preview server. Do not substitute AI posters.

**Deliverable:** working standalone candidate or coherent website-only diff with complete source, a concise before/after explanation, desktop and ~390 px phone screenshots, navigation/keyboard/mobile behavior checks, broken-link and iframe checks, and a list of genuinely tested versus unverified claims. Test local rendering on the exact candidate where tools allow. Avoid editing Colosseum app code.

**Authority:** no GitHub push/merge, production Cloudflare deployment, repo-wide cleanup, or native Colosseum edits. The canonical Brotherhood website is the integration target after review. Do not promote this derived branch as production.
