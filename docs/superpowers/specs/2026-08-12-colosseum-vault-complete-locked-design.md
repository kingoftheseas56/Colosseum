# Colosseum Vault — Complete Locked Design

> Combined canonical design containing locked Sections 1–6.
>
> **Jellyfin evidence status:** Reference excerpts in this design are behavioral evidence, not porting instructions. This revision was checked against the uploaded `jellyfin-master.zip`. Newly added excerpts from `BookResolver.cs`, `Emby.Naming/TV/EpisodeResolver.cs`, `CoreResolutionIgnoreRule.cs`, `FFProbeVideoInfo.cs`, and `ILocalImageProvider.cs` were blob-matched against Jellyfin commit `4e2fc33a61391ded79fc4353f0fd9090952bd130`; the core resolver/provider/media-source paths cited elsewhere were also inspected at that pin.

**Status:** Approved design. Product decisions are locked. Implementation planning and runtime verification have not been performed.

## Locked review amendments

- **Vault Browse survives:** the existing door/shelves/folders/tiles/file-first browsing capability remains first-class. The UI may be completely overhauled; the capability may not be silently removed. The new control room is the management layer.
- **Very likely is suggestion-only:** only Certain identities auto-adopt. Very likely remains unpublished until more evidence makes it Certain or the user confirms it, converting authority to Manual.
- **Regrouping is an implementation invariant:** material package regrouping re-evaluates automatic group-derived identity; this is not a new user-facing product feature.
- **Collection consumes Vault facts:** ownership, local quality, availability, and drive/root facts may feed the real Collection without creating another ownership authority.
- **Scheduled scans are supplementary:** useful for reliability/convenience, never the fundamental correctness path.
- **Functional empty states only:** no title-plus-tagline empty-state pattern.
- **Migration rehearsal is mandatory:** real VaultIndex/schema restructures must be rehearsed on a representative database copy with backup/rollback verified first.

# Vault Design — Section 1: Experience Promise and Scope

## 1. Experience Promise and Scope

Vault should feel like **the control room for everything you physically own**.

A user points it at real folders and drives. Vault figures out what is there, what belongs together, what each copy actually contains, which canonical Theatre/Tankoban/Biblio object it belongs to, and whether anything needs human judgment.

When Vault understands everything, it mostly gets out of the way.

For canonical media browsing, the user still browses *Alien* in Theatre. Theatre simply knows:

> **Owned locally — 3 copies**  
> Play — Local 4K Dolby Vision

At the same time, the **existing Vault file-first browsing surface remains first-class**: the door, shelves, tiles, folders, and file-oriented navigation remain a legitimate way to browse the user's local collection. Existing user-visible Vault capabilities are preserved unless this locked design explicitly refines them. The UI may be overhauled, but those capabilities are not silently removed or demoted. The new control room is an added management layer, not a replacement for Vault's browsing face.

If they care about the physical collection, Vault can explain the entire truth:

> 4K theatrical copy on Drive D:  
> 1080p backup on Drive E:  
> Director’s Cut on archive storage  
> local subtitles  
> artwork override  
> one drive currently away  
> all identities certain

If Vault does **not** understand something, it does not manufacture confidence. It puts the case into **Needs Attention**, explains what is uncertain, proposes the safest repair, and can learn a folder/root-scoped rule from the decision.

The product boundary stays strict:

**Vault owns:** files, roots, physical copies, storage availability, file relationships, technical media facts, fingerprints, local metadata/artwork evidence, identification evidence, ownership history, learned local rules, and repair state.

**Theatre / Tankoban / Biblio own:** the canonical media objects, normal browsing, consumption UX, progress semantics, and the presentation of media-level information.

**DLNA may expose Vault-owned media**, but Vault does not grow into a multi-user remote media server.

**Explicit local-only objects are permitted** when no outside catalog can represent something the user genuinely owns. Those objects enter the appropriate Colosseum world intentionally and can later be linked to a canonical object without losing their history.

The key rule underneath the whole design is:

> **The file tells Vault what you physically have. The canonical world tells Colosseum what the media is. Your explicit decisions outrank automation.**

And the Jellyfin reference atlas will sit beside these behaviors throughout the final specification, separating proven prior art from Colosseum-original design.

---

# Vault Design — Section 2: Primary User Journey

## 2. Primary User Journey

The normal Vault journey should feel like this:

> **Add a folder → Vault understands it → the media quietly appears where it belongs → Vault only bothers me when judgment is actually required.**

Vault should not make importing a library feel like configuring a server.

### 2.1 Adding a root

The user opens Vault and chooses **Add storage**.

They select something like:

`D:\Movies`

Vault asks only for the small root profile we already locked:

- **Automatic**
- **Movies & TV**
- **Manga & Comics**
- **Books**

Plus basic behavior such as whether Colosseum should watch the location automatically and include its subfolders.

There is no giant metadata-provider configuration screen.

The root immediately appears in **Storage**:

> **D:\Movies**  
> Scanning…  
> 1,482 files found

The user can leave Vault immediately. Scanning does not hold the application hostage.

### Jellyfin control case — specialized library interpretation

**Analogue strength:** Direct for the idea, different ownership model.

**Confirmed Jellyfin location:**  
`Emby.Server.Implementations/Library/Resolvers/Movies/MovieResolver.cs`

**Relevant mechanism:** Jellyfin does not treat every filesystem entry as the same generic thing. `MovieResolver` examines folders/files, collection type, video naming information, extras, alternate versions, disc structures, and other surrounding evidence before deciding what media object the files represent.

A verified representative excerpt is:

```csharp
var videoInfos = files
    .Select(i => VideoResolver.Resolve(i.FullName, i.IsDirectory, NamingOptions, parseName, parent.ContainingFolderPath))
    .Where(f => f is not null)
    .ToList();

var resolverResult = _videoListResolver.Resolve(
    videoInfos, supportMultiEditions, parseName, parent.ContainingFolderPath, collectionType);
```

Later, the same resolver preserves additional parts and local alternate versions:

```csharp
AdditionalParts = additionalParts,
LocalAlternateVersions = video.AlternateVersions.Select(av => av.Files[0].Path).ToArray()
```

**What Jellyfin proves:** A mature local-media scanner needs a resolving layer between “filesystem entry” and “media object.” Raw paths are not enough.

**What Vault adopts:** Different media families may use specialized parsers/resolvers.

**What Vault changes:** Vault does not make the resolver output the final canonical media object. It produces physical-media evidence which is then reconciled against Theatre, Tankoban, or Biblio.

## 2.2 Fast census first

Vault performs the first-stage scan.

Its immediate job is deliberately narrow:

> What files exist?  
> Which ones appear related?  
> What broad kind of media are they?  
> What obvious sidecars/extras belong with them?  
> What changed since Vault last looked?

At this stage Vault should be cheap.

It does not need to fully decode every video stream, fingerprint every byte, refresh every poster, and call every identity provider before the user sees progress.

The control room should update continuously:

> **D:\Movies**  
> 1,482 files found  
> 1,311 grouped  
> Identification running

Known junk/support material is ignored or attached appropriately rather than creating fake media items.

Examples:

`poster.jpg` → artwork companion  
`Alien.en.srt` → subtitle companion  
`Deleted Scenes\...` → extras  
temporary/incomplete file → ignored according to rule  
`Alien.1979.2160p.DV.mkv` → primary/version candidate

If the user has previously taught Vault a scoped rule for this root, it applies during this stage.

Example:

> Ignore `Samples` in `D:\Movies`.

The rule is visible later under **Rules**. It is not an invisible heuristic the user can never undo.

### Jellyfin control case — ignore rules before resolution

**Analogue strength:** Direct for the resolver-stage idea; Vault adds user-scoped learning.

**Confirmed Jellyfin location:**  
`Emby.Server.Implementations/Library/CoreResolutionIgnoreRule.cs`

Verified excerpt:

```csharp
if (IgnorePatterns.ShouldIgnore(fileInfo.FullName))
{
    return true;
}
```

**What Jellyfin proves:** Mature local-library resolution needs an explicit way to exclude known non-media/support paths before those files become library objects.

**What Vault adopts:** Ignore/exclusion rules participate early in physical interpretation.

**What Vault changes:** Vault's learned exclusions are visible and scoped to a folder/root rather than becoming invisible global behavior.


## 2.3 Physical grouping

Before canonical identification, Vault works out what belongs together physically.

For a movie folder such as:

```text
Alien (1979)/
    Alien.1979.2160p.DV.mkv
    Alien.1979.1080p.mkv
    Alien.en.srt
    Alien.ja.ass
    poster.jpg
    fanart.jpg
    trailer.mkv
    Deleted Scenes/
        Cocoon Scene.mkv
```

Vault should form one physical package:

> probable main media  
> two versions  
> subtitle companions  
> artwork companions  
> extras

It should **not** create nine unrelated media objects.

At this stage the package may still be called something provisional internally. That provisional filesystem identity never leaks into Theatre as a fake canonical item.

### Jellyfin control case — multiple versions and extras

**Analogue strength:** Direct for grouping mechanics; Colosseum extends the model.

**Confirmed Jellyfin location:**  
`Emby.Server.Implementations/Library/Resolvers/Movies/MovieResolver.cs`

Jellyfin explicitly asks its video-list resolver to support multiple editions/versions:

```csharp
private MultiItemResolverResult ResolveVideos<T>(
    Folder parent,
    IEnumerable<FileSystemMetadata> fileSystemEntries,
    bool supportMultiEditions,
    CollectionType? collectionType,
    bool parseName)
    where T : Video, new()
```

and places unresolved/supporting material separately:

```csharp
ExtraFiles = leftOver
```

It also avoids promoting extras as the primary movie:

```csharp
return movie?.ExtraType is null ? movie : null;
```

**What Jellyfin proves:** Main media, alternate versions, extras, and leftover/supporting files need different treatment.

**What Vault adopts:** Related physical files are grouped before publication.

**What Vault adds:** A stricter distinction:

- **Edition** — a meaningfully different cut/version of the work.
- **Version/copy** — another physical representation of the same edition.
- **Companion** — subtitle, artwork, NFO, external audio, chapter data, etc.
- **Extra** — trailer, interview, deleted scene, featurette, and similar actual bonus content.

Vault must never collapse those four concepts into one generic “alternate file” bucket.

### Jellyfin control case — conservative book-folder resolution

**Analogue strength:** Direct for media-specific physical interpretation.

**Confirmed Jellyfin location:**  
`Emby.Server.Implementations/Library/Resolvers/Books/BookResolver.cs`

Jellyfin explicitly refuses to treat a directory as one book when it contains more than one supported book file:

```csharp
if (bookFiles.Count != 1)
{
    return null;
}
```

**What Jellyfin proves:** A mature resolver sometimes needs to refuse an attractive folder-level interpretation rather than flatten several real items into one.

**What Vault adopts:** Biblio-oriented physical parsing must be conservative about folder grouping and supported book/comic containers.

**What Vault changes:** The result remains physical evidence; Biblio still owns the canonical book object.


## 2.4 Identification

Once a physical package is understood well enough, Vault asks:

> **What canonical Colosseum object does this belong to?**

Evidence may include:

- parsed filename/folder title;
- year;
- edition clues;
- embedded metadata;
- local NFO/provider IDs;
- runtime;
- existing known identity;
- scoped rules;
- canonical catalog matches;
- previous manual decisions.

Identification produces one of the locked confidence states.

### Certain

Evidence agrees strongly and there is effectively one plausible object.

Vault identifies automatically.

### Very likely

Evidence is extremely strong but not sufficient for automatic canonical adoption.

Vault keeps the candidate as a **strong suggestion but does not publish it**. It can become publishable in one of two ways:

- new evidence raises the result to **Certain**; or
- the user confirms the suggestion, at which point the authority becomes **Manual**.

This preserves the shipped **“one exact match or stay honest”** law while making strong unresolved candidates less painful to confirm. A false canonical link is more expensive than a temporarily unresolved item because ownership, versions, extras, progress relationships, Collection facts, and source behavior can all attach to that identity.

### Ambiguous

Several plausible objects remain.

Vault does not guess.

The item enters **Needs Attention**.

### Conflicting evidence

Strong sources disagree.

Example:

> Folder says *The Thing (1982)*  
> NFO says *The Thing (2011)*

Vault enters **Needs Attention** and explains the conflict.

### Manual

The user chose the identity.

That choice outranks future automatic matching unless the user unlocks it.

The key user-facing rule is:

> **Vault should be able to tell me why it thinks this is Alien.**

Example:

> **Identity: Certain**  
> Filename matches *Alien (1979)*  
> Folder year matches  
> Local NFO agrees  
> One canonical candidate found

Not:

> Matched: 98.7%

A number by itself is confidence theatre.

## 2.5 TV and anime episode resolution

TV follows the same physical-evidence model, but its parser can understand several naming cultures.

Examples:

`Show S02E04.mkv`  
`Show 2x04.mkv`  
`Anime - 27.mkv`  
`Show S01E01-E03.mkv`  
`Show Special 01.mkv`

Vault determines what the **file claims to contain**.

Theatre determines where those episodes belong canonically.

This distinction is especially important for anime.

Vault should not invent its own anime season ontology just because filenames use absolute numbering.

### Jellyfin control case — episode resolver

**Analogue strength:** Direct for specialized episode resolution; Adjacent for anime.

**Confirmed Jellyfin location:**  
`Emby.Server.Implementations/Library/Resolvers/TV/EpisodeResolver.cs`

Jellyfin has a dedicated episode resolver rather than expecting the generic movie resolver to understand television structure.

Representative server-side behavior:

```csharp
var episode = ResolveVideo<Episode>(args, false);
```

Jellyfin's naming layer also carries an explicit absolute-number capability:

**Confirmed Jellyfin location:**  
`Emby.Naming/TV/EpisodeResolver.cs`

```csharp
var parsingResult = new EpisodePathParser(_options)
    .Parse(path, isDirectory, isNamed, isOptimistic, supportsAbsoluteNumbers, fillExtendedInfo);
```

It then connects the resolved episode to its series/season context.

**What Jellyfin proves:** Episode parsing deserves specialized logic, and absolute-number support is a first-class parsing concern rather than a generic movie heuristic.

**What Vault adopts:** TV/anime filename parsing is specialized.

**What Vault changes:** The parser owns physical claims only. Theatre owns canonical series/season/episode truth.

A scoped rule can teach a root:

> `E:\Anime` uses absolute episode numbering.

That rule does not become a global assumption about every anime file on the machine.

## 2.6 Publication into the Colosseum worlds

An item becomes visible in Theatre, Tankoban, or Biblio only when canonical identity is good enough.

The publication gate is:

> **Confident about the object; technical analysis may still be incomplete.**

So a newly added movie may progress like this:

```text
Found
→ Grouped
→ Identified
→ Published to Theatre
→ Deep Analysis continues
```

Theatre might initially show:

> **Owned locally**

Then a little later, after analysis:

> **Owned locally — 4K Dolby Vision**

The user never sees:

> `Alien.1979.REMASTERED.2160p.DV.TrueHD-GROUP.mkv`

temporarily appearing as a fake movie in Theatre.

If identity is ambiguous, it stays in Vault.

### Colosseum-specific design

**Jellyfin analogue:** Adjacent only.

Jellyfin's resolved server-library item is itself the library object.

Vault deliberately does something else:

> **Physical recognition and canonical publication are separate events.**

That is necessary because Theatre, Tankoban, and Biblio already own the actual media ontology.

## 2.7 Normal consumption after publication

Once published, Vault should mostly disappear.

The Theatre page for *Alien* might show:

> **Owned locally — 3 copies**

The primary action becomes:

> **Play — Local 4K Dolby Vision**

assuming that local copy passes the quality sanity check.

Opening the source/version chooser might reveal:

```text
Local — 4K Dolby Vision
Local — 1080p
Local — Director's Cut
Torrentio — 4K
Other addon...
```

The Director's Cut remains a separate edition. It is not silently substituted because it happens to have the highest resolution.

Opening a local copy reveals progressively deeper information:

> 2160p HEVC  
> Dolby Vision  
> TrueHD Atmos 7.1  
> English / Japanese audio  
> 4 subtitle tracks  
> 68.2 GB  
> Drive D: — Online

The full physical history, fingerprints, file path, identity evidence, duplicates, and repair controls stay in Vault.

The rule is:

> **World pages show what helps you choose. Vault shows what helps you manage.**

## 2.8 Deep analysis

After publication, or earlier when convenient, Vault performs the second-stage analysis.

For video this should learn physical facts such as:

- container;
- duration;
- resolution;
- video codec;
- bitrate;
- HDR type;
- audio codecs;
- channel layouts;
- audio languages;
- embedded subtitle languages/types;
- chapters;
- file size;
- other relevant stream properties.

Equivalent physical analysis should exist for books/comics where useful.

This information belongs to the **copy**, not the canonical work.

*Alien* is not “Dolby Vision.”

Your particular MKV is.

That distinction must survive the entire design.

Deep analysis runs in the background and does not block normal library use.

If the user opens an unanalyzed item, Vault prioritizes it.

Visible Vault state can therefore say:

> 18,432 found  
> 18,201 identified  
> 14,670 analyzed  
> 23 need attention

### Jellyfin control case — deep stream and chapter probing

**Analogue strength:** Direct for deep physical analysis.

**Confirmed Jellyfin location:**  
`MediaBrowser.Providers/MediaInfo/FFProbeVideoInfo.cs`

Verified excerpt:

```csharp
mediaStreams.AddRange(mediaInfo.MediaStreams);
video.TotalBitrate = mediaInfo.Bitrate;
video.RunTimeTicks = mediaInfo.RunTimeTicks;
video.Container = mediaInfo.Container;
chapters = mediaInfo.Chapters ?? [];
```

**What Jellyfin proves:** Detailed playback/source facts are learned from the physical media source and include streams, bitrate, runtime, container, and chapters.

**What Vault adopts:** Deep analysis produces structured, copy-scoped technical facts.

**What Vault changes:** Analysis remains downstream of canonical publication and never becomes canonical metadata authority.


## 2.9 Needs Attention journey

If Vault cannot safely complete a decision, the user does not receive a generic error.

They get a guided case.

Example:

> **17 episodes need help**  
> These files look like they use absolute episode numbering.  
> Vault can map them if that is the rule for this folder.

Actions:

> **Use absolute numbering for this folder**  
> Review individually

Another:

> **6 files may have moved**  
> Their physical fingerprints match files previously stored on Drive D:.

Actions:

> **Confirm all 6 moves**  
> Review

Another:

> **Two possible identities**  
> *The Thing (1982)*  
> *The Thing (2011)*

Vault explains which evidence supports each candidate.

A safe repeated decision may become a scoped learned rule.

It never silently becomes global policy.

### Colosseum-original design

**Jellyfin analogue:** No meaningful direct analogue identified for the full experience.

Jellyfin contains mature resolver and metadata infrastructure, but the **guided Attention queue + explicit explanation + scoped learned repair rule** is a Colosseum product design.

Execution agents should therefore treat Jellyfin as evidence for the lower-level parsing problem, **not** as the UX authority for this workflow.

## 2.10 Move, copy, disappearance, and return

Physical location is not physical identity.

If:

`D:\Movies\Alien.mkv`

becomes:

`E:\Films\Alien\Alien.mkv`

Vault should attempt to establish what happened.

Possible outcomes:

- **Moved** — same physical copy, new path.
- **Copied** — the old copy still exists and another equivalent copy appeared.
- **Changed** — the file at a known location materially changed.
- **Away** — the storage containing the known copy is unavailable.
- **Missing** — storage is available, but this file no longer exists.
- **Duplicate** — several copies appear materially equivalent.

Obvious cases reconcile automatically.

Uncertain cases enter Attention.

History remains visible.

Example:

> **Alien — Local 4K**  
> Moved from `D:\Movies` to `E:\Films`  
> August 9

If Drive E disappears later:

> **Owned locally — currently unavailable**

The canonical *Alien* object does not disappear.

If the drive returns, Vault reconciles the physical copy again instead of inventing a new owned movie.

Vault never automatically forgets a long-away root.

## 2.11 Local-only media

If Vault reaches the end of identification and genuinely no canonical catalog object exists, it does not automatically create junk.

The item stays unresolved until the user deliberately chooses:

> **Create local-only item**

The user can then supply or approve:

- title;
- year/date;
- edition;
- artwork;
- description;
- appropriate destination world.

It appears in Theatre/Tankoban/Biblio with a visible **Local-only** identity.

Later, if a canonical object becomes available, the user can link it.

Linking must preserve:

- progress;
- ownership history;
- copies;
- editions;
- extras;
- companions;
- local artwork/metadata overrides;
- learned rules;
- file history.

The operation is a reconciliation, not delete-and-recreate.

### Colosseum-original design

**Jellyfin analogue:** No direct analogue is being adopted here.

This exists because Colosseum's canonical-world model must still be able to represent genuinely obscure, custom, fan-made, self-created, or otherwise uncatalogued media.

## 2.12 What the user experiences when everything works

The ideal Vault experience is actually very quiet.

A user adds a drive once.

After that:

> They copy a movie into it.

Vault notices.

Vault understands the file.

The movie page in Theatre gains:

> **Owned locally**

The best local version becomes the normal playback choice.

The user never opens Vault.

That is success.

Vault becomes visible when the user wants to understand their storage or when Vault needs their judgment.

So the primary journey has two equally important endings:

### Happy path

> **I added files and Colosseum just understood them.**

### Recovery path

> **Colosseum wasn't sure, and it showed me exactly what was uncertain instead of making something up.**

Those are the two experiences this entire Vault design must protect.

## Reference-atlas rule for later specification

The final specification should repeat this pattern for every major Jellyfin-derived mechanism:

**Jellyfin control case**  
**Analogue strength:** Direct / Adjacent / None  
**Pinned source path + symbol**  
**Minimal source excerpt**  
**Plain-language mechanism**  
**What Vault adopts**  
**What Vault rejects or changes**  
**Colosseum-specific contract**

The excerpts are evidence, not implementation instructions.

Jellyfin's C# class structure must never become a reason by itself to reproduce the same architecture in Colosseum's C++/QML code.

---

# Vault Design — Section 3: States, Interruptions, Recovery, and Edge Cases

## 3. States, Interruptions, Recovery, and Edge Cases

Vault must be designed around one simple reality:

> **Local media libraries are not stable. Drives disappear. Files move. Downloads are incomplete. Metadata conflicts. Scans get interrupted. Users correct the app. None of those events should corrupt the user's idea of what they own.**

This section defines the durable states Vault must distinguish and the recovery behavior expected when normal flow breaks.

---

## 3.1 Core state model

Vault must not reduce every file to “present” or “missing.”

A physical copy can be in several materially different states:

- **Found** — the filesystem entry exists and has been observed.
- **Grouped** — Vault understands which physical package the entry belongs to.
- **Identified** — Vault has mapped the package to a canonical or explicit local-only media object.
- **Analyzed** — deep technical properties have been collected.
- **Published** — the canonical media world is allowed to expose ownership.
- **Away** — the storage containing the copy is unavailable.
- **Missing** — the storage is available, but the previously known file is gone.
- **Changed** — the known path exists, but the physical file no longer matches the previously known copy.
- **Moved** — the same physical copy has been reconciled to a new location.
- **Copied** — another materially equivalent physical copy now exists.
- **Duplicate** — two or more physical copies are probably equivalent enough to warrant a diagnostic warning.
- **Needs Attention** — Vault cannot safely resolve an identity, relationship, movement, or classification without user judgment.
- **Suppressed / user-overridden** — Vault has an explicit user decision that automation must respect.

These are not cosmetic labels. They represent different ownership facts and require different recovery behavior.

---

## 3.2 A root becomes unavailable

Example:

`E:\Archive` is configured in Vault.

The user unplugs the drive.

Vault must not reinterpret that as deletion.

The root becomes:

> **Away**

All physical copies owned by that root become unavailable for playback/opening, but their ownership records remain.

The associated Theatre/Tankoban/Biblio objects remain published.

The world may show:

> **Owned locally — currently unavailable**

The Vault control room may show:

> **E:\Archive**  
> Away for 4 hours  
> 3,241 owned items currently unavailable

No identity, progress, edition relationship, local artwork choice, learned rule, copy history, or ownership history is destroyed.

### Recovery

When the root returns:

1. Vault detects that the storage is available again.
2. Vault does not blindly trust every old path.
3. A reconciliation scan confirms which known copies still exist.
4. Unchanged copies return to **Available**.
5. Moved/changed/missing cases are resolved individually.
6. The root returns to healthy state only after reconciliation is complete.

### Colosseum-specific contract

**Jellyfin analogue:** Adjacent only.

Jellyfin maintains durable server-library state across refreshes, but Vault makes **storage availability itself** a first-class ownership fact.

The important rule is:

> **Away is not missing, and missing is not deleted.**

---

## 3.3 A known file disappears while its root is online

If:

`D:\Movies\Alien.mkv`

vanishes while `D:\Movies` itself remains available, Vault must not label the root away.

The copy becomes:

> **Missing**

Vault should then look for evidence that the copy:

- moved elsewhere;
- was renamed;
- was replaced;
- was duplicated to another root;
- was deliberately removed.

If a high-confidence move is found, Vault reconciles automatically.

If not, the copy remains missing and the canonical object can still remain **Owned locally** if another copy exists.

If this was the only owned copy, the world may show:

> **Owned locally — copy missing**

until the user explicitly decides to forget that ownership record or the copy is reconciled.

### Design rule

> **The absence of one path is an event to explain, not permission to erase history.**

---

## 3.4 A file changes in place

A known path may still exist while the file itself changes.

Examples:

- a download finishes and replaces a partial file;
- a remux is overwritten with a new encode;
- a comic archive is repacked;
- an EPUB is replaced with a corrected edition;
- the same filename now points to different content.

Vault must compare current physical evidence against the stored copy identity.

If the file materially changed:

> **Changed**

Vault invalidates technical analysis that belonged to the old file.

It must not blindly carry forward:

- codec/stream facts;
- duration;
- chapter data;
- fingerprints;
- duplicate status;
- file-specific errors;
- companion relationships that depended on old evidence.

Canonical identity may survive if evidence still supports it, but the physical copy itself must be re-evaluated.

---

## 3.5 Move and rename recovery

A rename or move should preserve the physical copy's history when Vault has enough evidence.

Example:

`D:\Movies\Alien.mkv`

becomes:

`E:\Films\Alien (1979)\Alien.mkv`

High-confidence evidence may include:

- stable fingerprint;
- same size;
- same duration;
- same stream structure;
- same container metadata;
- disappearance and appearance occurring in a compatible time window;
- strong canonical identity agreement.

Vault may then record:

> **Moved**  
> `D:\Movies\Alien.mkv` → `E:\Films\Alien (1979)\Alien.mkv`

The user should not see:

> Alien deleted  
> Alien added

if Vault can prove it was one move.

### Uncertain move

If evidence is not strong enough:

> **Needs Attention — possible move**

The user sees both old and new locations and the evidence tying them together.

A confirmed decision updates history instead of replacing it.

---

## 3.6 Copy versus move

Vault must distinguish:

> **The old copy disappeared and the new copy appeared**

from:

> **The old copy still exists and another equivalent copy appeared**

The second case is a copy.

A copied file should create another physical copy underneath the same edition/canonical object when appropriate.

It should not erase the original copy's history.

If the two copies are materially equivalent, Vault may flag:

> **Likely duplicate**

but does not treat that as an error.

The user can mark the relationship as:

- intentional backup;
- intentional second location;
- duplicate warning ignored.

Deletion is not an automatic repair action.

---

## 3.7 Duplicate uncertainty

Duplicate detection must be evidence-based.

Vault should not decide two things are duplicates merely because:

`Alien.mkv`

exists in two folders.

Signals can include:

- same canonical object;
- same edition;
- same duration;
- same stream layout;
- same or near-identical size;
- matching strong fingerprint;
- matching embedded IDs;
- matching chapter/track structure.

The control room should explain why it thinks two copies are duplicates.

Example:

> **Likely duplicate**  
> Same edition  
> Same duration  
> Same video/audio structure  
> Strong fingerprint matches

### Recovery actions

The user may:

- show both locations;
- mark as intentional backup;
- suppress duplicate warning;
- open containing folder;
- later perform an explicit delete action if Colosseum supports one.

The normal Vault repair path never deletes automatically.

---

## 3.8 Interrupted scan

Vault scanning is two-stage, so interruption must be safe at each stage.

### Census interruption

If filesystem census is cancelled, crashes, or loses its root partway through:

- the previously published index remains authoritative;
- partial new truth must not replace complete old truth;
- the root may show a scan interruption/partial-refresh state;
- the user can resume/retry later.

### Identification interruption

Already identified items stay identified.

Unfinished items remain pending.

No partially constructed canonical publication should leak into Theatre/Tankoban/Biblio.

### Deep-analysis interruption

Already analyzed copies retain their analysis.

Unanalyzed copies remain queued.

The user can continue using the library.

### Design rule

> **An interrupted refresh may delay new truth, but it must not destroy old truth.**

This aligns with Vault's existing candidate-before-publish philosophy.

---

## 3.9 App closes during background work

If Colosseum exits during scanning, identification, or analysis, Vault should recover without pretending every job completed.

On next startup:

- unfinished roots resume or restart safely;
- already durable results remain;
- pending analysis can be reconstructed;
- active Attention cases remain;
- learned rules remain;
- no completed user decision is silently forgotten.

The user should see a simple state such as:

> **Refreshing resumed**

rather than a scary recovery wizard.

Low-level recovery details belong in diagnostics.

---

## 3.10 Root disappears during an active scan

This is distinct from a root simply being offline before a scan begins.

If the drive disappears in the middle of scanning:

- stop reading from that root;
- do not publish a partial destructive census;
- preserve the previous known state;
- transition the root to **Away**;
- resume/reconcile when the root returns.

The event may be recorded in history:

> Scan interrupted because Drive E: became unavailable.

The user does not need to repair anything unless reconciliation later finds real inconsistencies.

---

## 3.11 Files that are still being written

Vault should not eagerly publish media that is obviously incomplete.

Examples:

- active downloads;
- temporary files;
- download-client partial extensions;
- a file whose size is still changing;
- archives still being created.

The default behavior is:

> **Wait until the file appears settled before normal identification/publication.**

This is an ordinary implementation rule, not a user-facing decision.

A scoped learned/ignore rule may override unusual folder conventions when needed.

---

## 3.12 Permission failure

A root or file can exist but be unreadable.

This is not **Away**.

Vault should represent:

> **Access blocked**

with a plain explanation:

> Vault can see this storage but does not currently have permission to read it.

Known ownership remains.

The user can:

- retry;
- open system permissions/location;
- remove the root;
- leave it unresolved.

Permission failure should not destroy index state.

---

## 3.13 Corrupt or unreadable media

If a physical file exists but technical analysis fails:

- canonical identity can remain if already established;
- the copy becomes **Problematic** or equivalent;
- the failure is tied to the physical copy, not the canonical media object.

Example:

> **Owned locally — playback problem detected**

Vault diagnostics may say:

> Container could not be parsed.

or:

> Archive is corrupt.

A bad copy does not poison other copies.

If another good local copy exists, normal playback can choose it instead.

---

## 3.14 Conflicting companions

Examples:

- two `poster.jpg` files from conflicting folder scopes;
- multiple NFO files disagreeing;
- subtitle sidecar appears to match two different video versions;
- an external audio track could belong to either edition.

Vault must not silently attach ambiguous companions.

The companion relationship enters **Needs Attention** when the ambiguity materially affects behavior.

The guided repair should explain:

> This subtitle could belong to either the theatrical or Director's Cut copy.

Once resolved, the relationship can become a scoped rule if the pattern repeats.

---

## 3.15 Conflicting canonical identity evidence

If filename, NFO, embedded metadata, and provider search disagree, Vault must preserve the disagreement.

Example:

> Filename: *The Thing (1982)*  
> NFO provider ID: *The Thing (2011)*  
> Runtime: closer to 1982 release

Vault does not reduce this to an opaque score.

It presents the conflict and likely candidates.

If the user chooses one:

> **Manual identity**

That choice becomes authoritative until deliberately unlocked.

---

## 3.16 Canonical metadata changes

A canonical source may later change:

- title;
- poster;
- synopsis;
- year;
- episode mapping;
- edition relationship.

Refreshing metadata must not trigger a filesystem rescan.

A canonical metadata refresh must respect:

- user-chosen local artwork;
- locked fields;
- manual identity;
- local-only status;
- progress;
- physical-copy history.

### Design rule

> **Changing what Colosseum knows about the work must not make Vault rediscover the disk.**

---

## 3.17 Provider failure

An identity/metadata provider may be unavailable.

Vault should distinguish:

> **No match found**

from:

> **Could not ask the provider**

A temporary provider outage should not turn known media into unidentified media.

Existing canonical identity remains.

New identification can remain pending and retry later.

If enough other evidence/providers can establish identity confidently, Vault may continue.

The Attention queue should only involve the user if human judgment is actually useful.

---

## 3.18 Learned-rule conflict

Scoped learned rules can eventually conflict.

Example:

Root rule:

> `E:\Anime` uses absolute numbering.

Nested folder rule:

> `E:\Anime\Classic Show` uses season/episode numbering.

The narrower rule wins in its scope.

The Vault Rules view should make inheritance understandable:

> Root rule inherited  
> Overridden in this folder

If two rules at the same effective scope conflict, Vault must not choose invisibly.

That becomes a repairable rules conflict.

---

## 3.19 User changes or removes a learned rule

Removing a learned rule does not immediately erase media.

Instead:

1. the rule stops influencing future decisions;
2. affected items can be offered for re-identification/re-grouping;
3. already manual/locked decisions remain;
4. the user decides whether to re-evaluate existing items now.

This prevents:

> Delete one rule → entire library instantly reshuffles.

---

## 3.20 User changes a manual identity

If the user remaps a physical package from one canonical object to another:

- ownership association moves;
- physical-copy history stays;
- technical analysis stays with the copy;
- copy-specific companions stay when still compatible;
- canonical-world progress is handled according to the destination world's progress rules;
- the prior automatic identity evidence remains visible in history/diagnostics.

A remap is not a delete/reimport.

---

## 3.21 Local-only object later gains a canonical match

This is a major recovery path.

A local-only object may later become representable by Theatre/Tankoban/Biblio's canonical catalog.

The user can choose:

> **Link to canonical object**

Vault must migrate the relationship without losing:

- ownership;
- physical copies;
- edition/version relationships;
- extras;
- companions;
- artwork overrides;
- field locks;
- progress;
- history;
- learned rules;
- Attention resolutions.

The result should feel like:

> The thing finally got a proper canonical identity.

Not:

> Old thing deleted, new thing appeared.

---

## 3.22 Canonical object disappears or changes provider identity

If an external catalog stops exposing a previously known object, Vault must not immediately orphan the user's ownership.

The last known canonical identity can remain durable while Colosseum attempts reconciliation.

Possible outcomes:

- provider ID changed and can be migrated;
- another provider now owns the canonical match;
- object becomes temporarily unresolved;
- user converts/preserves it as local-only.

Ownership survives the provider problem.

### Product rule

> **A catalog provider is not allowed to erase something the user physically owns.**

---

## 3.23 Edition ambiguity

Vault must not silently flatten a suspected different cut into an ordinary quality version.

If two copies look like:

- same movie;
- materially different runtimes;
- edition/cut markers;
- different chapter structure;

Vault should consider:

> **Possible separate edition**

If confidence is insufficient, it goes to Attention.

The user can choose:

- same edition, different version;
- separate edition;
- leave unresolved.

This is load-bearing because the primary Play action must never silently substitute a different edition.

---

## 3.24 Multi-episode ambiguity

A file may appear to contain several episodes.

Examples:

`Show.S01E01-E03.mkv`

or an anime release covering absolute episodes 1–4.

Vault records the physical claim:

> This file appears to cover episodes 1–3.

Theatre maps those claims into canonical episodes.

If the mapping cannot be made safely:

> **Needs Attention — multi-episode mapping**

The physical file is not duplicated into three unrelated copies.

---

## 3.25 Extras without a primary object

Vault may discover:

`Deleted Scenes/scene01.mkv`

without being able to find the main movie.

Those extras should not become standalone movies automatically.

They remain:

> **Unattached extras**

inside Attention or an equivalent physical-package state.

The user can:

- attach them to an existing canonical object;
- create/link a local-only object;
- mark the folder as intentionally standalone if a future feature supports that.

---

## 3.26 Sidecar without media

A subtitle, NFO, artwork file, or chapter sidecar may exist without its main media.

Vault should not publish it.

It can remain:

> **Unattached companion**

If a matching primary appears later, Vault may associate it automatically when evidence is strong.

---

## 3.27 One canonical object across several roots

Example:

- 4K copy on `D:\Movies`
- 1080p copy on `E:\Archive`
- another copy on NAS storage

All may belong to one canonical object.

Root availability is tracked per physical copy.

The world-level ownership state is derived from the copies:

- at least one good online copy → **Owned locally / available**
- all copies away → **Owned locally / currently unavailable**
- some online, some away → **Owned locally**, with copy-level availability shown when expanded

One root going away must not make the canonical object unavailable if another good copy exists.

---

## 3.28 Poor local copy versus better online source

Local-first playback is not absolute.

If the only local copy is:

- corrupt;
- incomplete;
- unsupported;
- path unavailable;
- clearly inferior under the locked quality sanity rules;

Colosseum may choose an online/addon source as the primary playback recommendation.

The local copy remains visible.

A manual source choice always wins.

### Design rule

> **Ownership has priority, not immunity from quality checks.**

---

## 3.29 Duplicate roots or overlapping folders

Users may accidentally configure:

`D:\Media`

and:

`D:\Media\Movies`

as separate roots.

Vault should detect overlap and avoid double-indexing the same physical files.

This is an ordinary safety behavior and does not require another product decision.

The root UI should explain the overlap and recommend the simpler configuration.

---

## 3.30 Case changes and path normalization

Filesystem path differences that are not meaningful on the host platform must not create fake copies.

Examples:

- case-only path changes on case-insensitive filesystems;
- slash normalization;
- drive-letter normalization;
- equivalent canonical paths.

This is implementation-level normalization, but the user-visible contract is:

> **Vault should not create duplicates because Windows spelled the same path differently.**

---

## 3.31 Root deliberately removed by the user

Removing a root is different from the root being away.

Vault should ask what ownership action the user intends.

Two conceptual outcomes exist:

### Remove storage location, preserve ownership/history

The location is no longer actively watched/scanned, but the ownership history remains.

Useful for retired/archive storage.

### Forget this storage and its ownership records

The user deliberately asks Vault to stop treating those physical copies as owned.

This is destructive from an ownership-record perspective and must be explicit.

No timeout automatically makes this decision.

---

## 3.32 Health-state derivation

Vault's top-level health summary should be derived from the above states.

Examples:

### Healthy

> **Vault is healthy**  
> All roots available  
> 18,432 files known  
> 22 items still analyzing  
> No action required

### Attention required

> **23 things need attention**  
> 9 ambiguous identities  
> 6 possible moves  
> 5 companion conflicts  
> 3 episode mappings

### Storage degraded

> **1 root is away**  
> 3,241 owned items currently unavailable  
> Nothing has been forgotten

### Background work

> **Deep analysis running**  
> 14,670 / 18,432 analyzed  
> Colosseum remains usable

The health summary must never equate:

> background work still running

with:

> library broken.

---

## 3.33 History and auditability

Important ownership transitions should have lightweight durable history.

Useful events include:

- root added/removed;
- root became away/returned;
- copy moved;
- copy changed;
- duplicate confirmed/ignored;
- manual identity chosen;
- local-only object created;
- local-only object linked to canonical;
- scoped rule created/removed;
- local artwork/field override locked/unlocked.

History exists to answer:

> **Why does Vault believe this now?**

It is not intended to become an enterprise audit log.

---

## 3.34 Reference atlas — state and recovery

### Jellyfin control case: specialized resolution

**Analogue strength:** Direct for media-specific parsing, Adjacent for recovery state.

**Confirmed source:**  
`Emby.Server.Implementations/Library/Resolvers/Movies/MovieResolver.cs`

**Relevant behavior:** Jellyfin separates physical video resolution, alternate versions, extras, and supporting files rather than treating every path as an independent media object.

**Vault implication:** Vault should preserve distinctions between primary media, editions, copies, companions, and extras before canonical publication.

### Jellyfin control case: episode resolution

**Analogue strength:** Direct for specialized episode parsing.

**Confirmed source:**  
`Emby.Server.Implementations/Library/Resolvers/TV/EpisodeResolver.cs`

**Relevant behavior:** Jellyfin gives TV episodes their own resolver instead of relying on the generic movie path.

**Vault implication:** TV/anime parsing deserves specialized physical parsers, while Theatre remains canonical authority.

### No direct Jellyfin analogue adopted

The following are treated as Colosseum-specific product contracts unless later repository evidence establishes a closer direct control case:

- **Away** as distinct from missing/deleted;
- guided Attention workflow;
- scoped learned repair rules;
- preserving explicit ownership through long-offline roots;
- local-only Colosseum objects;
- canonical-world publication gate;
- world-level ownership versus copy-level availability;
- ownership-history reconciliation rather than delete/recreate semantics.

These must not be “normalized” toward Jellyfin merely because Jellyfin is the reference repository.

---

## 3.35 Section acceptance criteria

This section is satisfied when the eventual implementation can observably demonstrate all of the following:

1. Unplugging a configured root does not erase ownership or canonical publication.
2. Returning the root reconciles old copies rather than blindly re-importing them.
3. A missing file under an online root is distinguishable from an unavailable root.
4. A moved file can preserve copy identity/history when evidence is strong.
5. An uncertain move enters Attention rather than being silently guessed.
6. Copy and duplicate states do not cause automatic deletion.
7. Interrupted scans cannot replace complete known truth with partial new truth.
8. Deep-analysis interruption does not block normal use of already identified media.
9. Provider outages do not erase existing canonical identities.
10. Manual identity and locked local overrides survive refresh.
11. Local-only objects can later link to canonical identities without losing state/history.
12. Different editions are never silently substituted as ordinary quality variants.
13. Unattached extras/companions do not become fake canonical media.
14. Health status distinguishes healthy/background-work/attention/storage-away states.
15. The user can inspect why a repair, match, move, or rule exists.

---

## 3.36 Section conclusion

The recovery philosophy is:

> **Vault should assume that storage changes are normal, uncertainty is normal, and interruption is normal. What is not normal is losing the user's ownership truth because one of those things happened.**

The happy path should remain invisible.

The broken path should remain understandable.

And whenever Vault cannot prove what happened, it should preserve the evidence and ask for judgment rather than rewriting history.

---

# Vault Design — Section 4: Controls, Feedback, Accessibility, and Integration

## 4. Controls, Feedback, Accessibility, and Integration

Vault has **two first-class faces**:

1. **Browse Vault** — the existing file-first browsing capability: door, shelves, folders, tiles, and local-file navigation.
2. **Manage Vault** — the new ownership control room.

The entire Vault UI may be visually/navigation-wise overhauled as part of the feature, but the existing browse capability is preserved as a product behavior. The management layer is added beside/beneath it rather than replacing it.

The management interface should answer four questions quickly:

1. **Is my collection okay?**
2. **What storage do I have?**
3. **What needs my judgment?**
4. **What exactly do I own, and where is it?**

Everything else should stay one level deeper.

The interface must not turn into a server dashboard, metadata laboratory, or duplicate version of Theatre/Tankoban/Biblio.

---

## 4.1 Primary Vault navigation

Vault should expose **Browse** and **Manage** as first-class destinations or modes. The exact visual navigation may change during the UI overhaul.

The **Manage** surface contains six primary areas:

- **Overview**
- **Storage**
- **Attention**
- **Owned Media**
- **Rules**
- **History**

**Diagnostics** is available from context and settings, but should not sit beside the six Manage areas as though ordinary users are expected to live there.

### Overview

Answers:

> **Is my Vault healthy?**

Shows:

- overall health;
- roots online/away;
- active scan/analysis work;
- items needing attention;
- recently added ownership;
- recent important changes.

### Storage

Answers:

> **Where is my media physically stored?**

Shows:

- configured roots;
- drive/root availability;
- root profile;
- file/item counts;
- scan state;
- last successful refresh;
- learned-rule count;
- Attention count for that root.

### Attention

Answers:

> **What actually needs me?**

This is the guided repair workflow already locked.

### Owned Media

Answers:

> **What does Vault physically know that I own?**

This is management search, not another Netflix-style browsing page.

### Rules

Answers:

> **What has Vault learned about my collection?**

Shows scoped rules, inheritance, overrides, source scope, and removal controls.

### History

Answers:

> **What changed, and why does Vault believe this now?**

Shows meaningful ownership and repair events without becoming an enterprise audit log.

---

## 4.2 Overview layout

The Overview should lead with one plain-language health statement.

Examples:

> **Vault is healthy**  
> 4 roots online • 18,432 files known • 23 still analyzing

or:

> **23 things need attention**  
> Your collection is usable. Vault needs your help with a few uncertain items.

or:

> **1 drive is away**  
> 3,241 owned items are currently unavailable. Nothing has been forgotten.

The first screen should not lead with:

- database row counts;
- provider status;
- codec statistics;
- filesystem event queues;
- worker/thread state.

Those belong in drill-downs or diagnostics.

### Primary cards

Overview may expose compact cards for:

**Storage**
> 4 roots • 1 away

**Attention**
> 23 items

**Analysis**
> 14,670 / 18,432 complete

**Recently owned**
> 18 new items this week

**Recent changes**
> 6 moved • 2 new copies • 1 root returned

Each card opens the relevant filtered view.

---

## 4.3 Global Vault action: Refresh

Vault has one obvious normal action:

> **Refresh**

The default action is context-aware and does the cheapest sensible work.

A menu attached to Refresh exposes advanced operations:

- **Rescan files**
- **Re-identify**
- **Re-analyze files**
- **Refresh metadata**
- **Repair selected issues**

These actions may be scoped to:

- whole Vault;
- one root;
- one folder/package;
- one canonical ownership group;
- one physical copy,

where meaningful.

### Product rule

> **Normal users get one Refresh. Power users can choose exactly what truth they want Vault to reconsider.**

### Scheduled scans

Scheduled scans are **useful, not fundamental**.

Vault may periodically run a reconciliation scan as a reliability/convenience layer, especially for roots where filesystem watching is imperfect. But correctness must not depend on waiting for a schedule:

- watcher-driven refresh is a normal path;
- manual Refresh is always available;
- root-return reconciliation is explicit;
- scheduled scanning provides eventual extra coverage.

The schedule must not become a second ownership authority or a reason to delay obvious watcher/manual work.

### Jellyfin control case — independent library operations

**Analogue strength:** Direct for separation of operations; different UX.

**Confirmed Jellyfin location:**  
`MediaBrowser.Controller/Library/ILibraryManager.cs`

Relevant interface methods include:

```csharp
Task ValidateMediaLibrary(IProgress<double> progress, CancellationToken cancellationToken);
```

and:

```csharp
void QueueLibraryScan();
```

Jellyfin also exposes image refresh and other item-level operations independently.

**What Jellyfin proves:** Mature media libraries do not have to treat every refresh as one indivisible operation.

**What Vault adopts:** Filesystem scan, identification, technical analysis, and metadata refresh are separable responsibilities.

**What Vault changes:** The normal UI still presents one simple Refresh action and only exposes the separate operations when the user asks for control.

---

## 4.4 Storage controls

Each root should have a compact card.

Example:

> **E:\Anime**  
> Online  
> Movies & TV  
> 4,281 files • 3 learned rules • 2 need attention  
> Refreshed 12 minutes ago

Primary controls:

- **Open**
- **Refresh**
- overflow menu

Overflow actions may include:

- Rescan files
- Re-identify
- Re-analyze
- Refresh metadata
- Edit root profile
- View learned rules
- Open folder in system file manager
- Remove root

### Root profile

The editable profile remains intentionally small:

- **Automatic**
- **Movies & TV**
- **Manga & Comics**
- **Books**
- Watch automatically
- Include subfolders

Do not expose dozens of parser/provider toggles here.

Collection-specific weirdness belongs to learned rules.

---

## 4.5 Adding storage

The **Add storage** flow should fit in one compact dialog/page.

1. Choose folder/root.
2. Choose approximate profile.
3. Confirm watching/subfolder behavior.
4. Add.

The user should immediately see:

> **Scanning started**

and be free to leave.

No mandatory metadata-provider setup.

No requirement to wait for the first scan to finish.

No requirement to understand what a “library type” means in server terminology.

---

## 4.6 Attention queue

Attention is the most important unique Vault interaction.

It should be organized by **problem**, not merely by path.

Examples:

> **17 episodes need numbering help**

> **6 files may have moved**

> **3 movies have conflicting identity evidence**

> **4 subtitle files could belong to more than one version**

Each group shows:

- plain-language problem;
- affected count;
- Vault's current interpretation;
- strongest evidence;
- safest recommended action;
- alternative actions;
- scope if a learned rule can be created.

### Example

> **17 episodes need numbering help**  
> These files look like they use absolute episode numbers.  
> `Frieren - 27.mkv` appears to mean absolute episode 27.

Actions:

> **Use absolute numbering for this folder**  
> Review individually  
> Ignore for now

If the first action creates a learned rule, say so before applying it:

> This will remember the rule for `E:\Anime\Frieren`.

### Batch behavior

Batch actions are allowed only when the same evidence/rule genuinely applies to the full group.

Vault must never use “Resolve all” as a euphemism for “guess all.”

---

## 4.7 Decision explanations

Every material automatic decision should be inspectable.

For identity:

> **Why Vault thinks this is Alien (1979)**  
> Folder title matches  
> Year matches  
> Local NFO agrees  
> One canonical result found

For a move:

> **Why Vault thinks this file moved**  
> Previous copy disappeared  
> Strong fingerprint matches  
> Duration and stream structure match  
> New file appeared on Drive E:

For a duplicate:

> **Why Vault flagged this as a duplicate**  
> Same canonical object  
> Same edition  
> Same duration  
> Same stream structure  
> Strong fingerprint matches

Do not show an unexplained percentage as the primary explanation.

Confidence tiers can exist internally, but the user-facing explanation is evidence.

---

## 4.8 Owned Media view

Owned Media is not a second Theatre/Tankoban/Biblio.

Its default presentation should emphasize ownership facts.

Useful rows/cards can show:

- canonical title;
- destination world;
- owned-copy count;
- best local copy;
- storage availability;
- edition count;
- Attention marker;
- local-only marker.

Example:

> **Alien (1979)**  
> Theatre  
> 3 copies • 2 editions • 2 available  
> Best: 4K Dolby Vision  
> Drive E: away

The user can open the ownership detail without being sent into a consumer-style hero page.

### Filters

Useful management filters include:

- World
- Root/drive
- Available / Away / Missing
- Needs Attention
- Unidentified
- Local-only
- Duplicate
- Not analyzed
- Added date
- Resolution
- HDR
- Audio language
- Subtitle language
- File size
- Edition count
- Copy count

The exact filter UI may evolve, but these facts belong to Vault's management search contract.

---

## 4.9 Ownership detail

Opening one ownership group should show progressive levels:

### Media identity

> Alien (1979)  
> Theatre  
> Identity: Certain

### Editions

> Theatrical Cut  
> Director's Cut

### Copies

> **4K Dolby Vision**  
> Drive D: • Online • 68.2 GB

> **1080p**  
> Drive E: • Away • 12.4 GB

### Companions

> 2 external subtitles  
> local poster  
> local NFO

### Extras

> Trailer  
> 4 deleted scenes  
> 1 interview

### History

> Moved from D:\Movies to E:\Films on August 9

### Actions

- Open in destination world
- Open containing folder
- Re-identify
- Re-analyze
- Refresh metadata
- Edit local metadata override
- View identity evidence
- Mark duplicate as intentional
- Create/remove locks where permitted

Destructive actions, if ever supported, must be visually and behaviorally separated from normal ownership management.

---

## 4.10 Physical copy detail

A physical copy may expose full technical truth.

For video:

> 2160p  
> HEVC  
> Dolby Vision  
> 64.8 Mbps  
> TrueHD Atmos 7.1  
> English / Japanese audio  
> 4 subtitle tracks  
> MKV  
> 2h 1m  
> 68.2 GB

Then:

> **Location**  
> `D:\Movies\Alien\Alien.mkv`

> **Storage**  
> Drive D: • Online

> **Identity**  
> Strong fingerprint available

> **History**  
> Added June 12  
> Moved August 9

This is where MediaInfo-style detail belongs.

It should not spill into the normal Theatre page.

---

## 4.11 Rules interface

Rules must remain understandable.

Each rule shows:

- what it does;
- its scope;
- whether it was user-created or learned from a repair;
- when it was created;
- whether a narrower rule overrides it.

Example:

> **Use absolute episode numbering**  
> Scope: `E:\Anime`  
> Learned from Attention repair  
> Active

Nested override:

> **Use season/episode numbering**  
> Scope: `E:\Anime\Classic Show`  
> Overrides root rule here

Actions:

- Edit scope where safe
- Disable
- Remove
- See affected items

Removing a rule must not instantly reshuffle the library without warning.

Vault should offer:

> Rule removed. Re-evaluate affected items now?

---

## 4.12 History controls

History should be filterable by:

- root;
- canonical object;
- physical copy;
- event type;
- date.

Meaningful event types:

- root added;
- root removed;
- root away;
- root returned;
- copy moved;
- copy changed;
- duplicate diagnosed;
- duplicate intentionally accepted;
- identity changed;
- manual identity locked/unlocked;
- local-only object created;
- local-only object linked;
- learned rule created/removed;
- local field/artwork override changed.

History entries should be short and human-readable.

Example:

> **Alien — 4K copy moved**  
> `D:\Movies` → `E:\Films`  
> August 9

Not:

> `VaultIdentityMigrationEventType::PhysicalPathRelink`

---

## 4.13 World integration — Owned locally

The destination worlds should expose ownership without becoming Vault.

For Theatre:

> **Owned locally — 3 copies**

For Tankoban:

> **Owned locally — Volumes 1–12**

For Biblio:

> **Owned locally**

The exact media-specific expression can differ because each world has different units.

Clicking ownership detail may open a compact ownership sheet first, with **Open in Vault** available for full management.

---

## 4.14 World integration — primary action

A good local copy should affect the primary action.

Example:

> **Play — Local 4K Dolby Vision**

If the local copy is unavailable:

> **Play**

may fall back to the normal online/addon path while ownership still shows:

> **Owned locally — currently unavailable**

If a bad/problematic local copy exists, local-first sanity checks may choose another source.

The user can always choose explicitly from Sources.

---

## 4.15 World integration — source/version chooser

Local sources and addon sources share the same chooser, but remain identifiable.

Example:

```text
Local — 4K Dolby Vision
Local — 1080p
Local — Director's Cut
Torrentio — 4K
Other addon — 1080p
```

Edition boundaries must remain visible.

A Director's Cut is not silently treated as the next-best technical version of the theatrical cut.

### Jellyfin control case — alternate versions

**Analogue strength:** Direct for multiple playable local versions.

**Confirmed Jellyfin location:**  
`MediaBrowser.Controller/Library/ILibraryManager.cs`

Representative methods include:

```csharp
IEnumerable<Guid> GetLocalAlternateVersionIds(Video video);
```

and:

```csharp
IEnumerable<Video> GetLinkedAlternateVersions(Video video);
```

Jellyfin's comments explicitly distinguish local alternate quality versions from linked content variants such as a Director's Cut.

**What Jellyfin proves:** Mature local libraries need explicit relationships between one work and several playable versions.

**What Vault adopts:** Multiple local playable copies stay grouped under one media identity where appropriate.

**What Vault changes:** Colosseum makes edition boundaries product-visible and combines local copies with addon sources in the destination world's source chooser.

---

## 4.16 World integration — Extras

Extras appear only when they exist.

A media page may gain:

> **Extras**

with entries such as:

- Trailer
- Deleted Scenes
- Interviews
- Featurettes

Companions do not appear here.

An `.srt`, `poster.jpg`, or NFO is not an Extra.

The destination world owns the visual treatment, while Vault owns the physical relationship.

---

## 4.17 Search integration

Normal Colosseum search may expose consumer-useful ownership filters:

- Owned locally
- Available offline
- Local only
- 4K
- HDR

Search results may show a compact ownership indicator.

Example:

> **Alien**  
> Owned locally • 4K Dolby Vision

Vault's management search exposes deeper physical filters.

Both surfaces must read the same underlying ownership facts.

### Jellyfin control case — queryable library facts

**Analogue strength:** Direct for the need to query/filter library state; different consumer model.

**Confirmed Jellyfin location:**  
`MediaBrowser.Controller/Library/ILibraryManager.cs`

The interface exposes general library querying:

```csharp
QueryResult<BaseItem> QueryItems(InternalItemsQuery query);
```

and helpers such as media-stream language queries.

**What Jellyfin proves:** Once a media library understands rich physical/media facts, those facts need to be queryable rather than trapped inside item-detail pages.

**What Vault adopts:** Ownership and analysis facts become filterable/queryable.

**What Vault changes:** Consumer-facing search and management search use different presentations over one shared truth.

---

### Collection integration

Vault must feed physical-ownership facts into Colosseum's real **Collection** rather than forcing Collection to rediscover them.

Useful Collection facts include:

- Owned locally
- local copy count
- best local quality, such as 4K/HDR
- current local availability
- storage/root such as **on Drive X** where management context calls for it

Collection may filter, badge, or surface these facts.

**Authority rule:** Collection consumes Vault facts. It does not become another writer of physical ownership truth.


## 4.18 DLNA integration boundary

DLNA is allowed, but it must remain an exposure layer.

The conceptual path is:

> **Vault-owned physical copy → DLNA exposure**

not:

> DLNA server → new canonical media database → Vault sync

Vault remains authoritative for:

- which physical files are owned;
- their identity;
- edition/version grouping;
- local technical facts;
- availability.

DLNA may expose supported local media to other devices.

DLNA-specific device capability negotiation must not redefine Vault's ownership model.

If DLNA later needs transcoding or compatibility work, that belongs to the DLNA delivery path, not to canonical identity.

---

## 4.19 Notifications and background feedback

Vault should avoid noisy notifications.

Normal background events do not need toast spam.

Do not notify for:

- every discovered file;
- every analyzed file;
- every metadata refresh;
- every successful filesystem event.

Useful notifications include:

> **Vault needs attention**  
> 23 items need your judgment.

> **Drive E: is away**  
> Your media is still remembered.

> **Drive E: is back**  
> Vault is reconciling 3,241 items.

> **Refresh failed**  
> Previous library state was preserved.

Background progress should be visible inside Vault and optionally in Colosseum's existing task/progress surface if one exists.

---

## 4.20 Progress feedback

Long-running work must show:

- what operation is running;
- its scope;
- progress where measurable;
- whether Colosseum remains usable;
- cancel/pause where safe;
- what will happen if cancelled.

Example:

> **Deep analysis running**  
> 14,670 / 18,432 complete  
> You can keep using Colosseum.

For operations where an accurate percentage is impossible, show activity and concrete counts instead of fake precision.

---

## 4.21 Error feedback

Errors must distinguish:

- user action needed;
- temporary external failure;
- inaccessible storage;
- corrupt file;
- internal Vault failure.

Examples:

> **Drive unavailable**  
> Nothing was deleted.

> **Metadata provider unavailable**  
> Existing identities are unchanged. Vault will retry later.

> **This file could not be analyzed**  
> The movie identity is still known.

> **Refresh stopped unexpectedly**  
> Your previous Vault state was preserved.

The UI should state what is safe before exposing technical details.

---

## 4.22 Accessibility

Vault is information-dense, so accessibility must be structural rather than decorative.

### Keyboard

Every primary workflow must be fully keyboard-operable:

- navigate top-level areas;
- search/filter;
- open root/item;
- move through Attention choices;
- approve/reject recommended repair;
- open overflow actions;
- manage learned rules.

Focus order must follow the visual reading order.

Batch Attention actions must not require drag-and-drop.

### Screen readers

Status information must have semantic labels.

Do not expose:

> red dot

Expose:

> **Drive E: away**

Do not expose:

> yellow 23

Expose:

> **23 items need attention**

Technical copy properties should read in a sensible grouped order rather than as an unstructured wall of values.

### Color

Color is never the only indication of:

- healthy;
- away;
- warning;
- error;
- manual lock;
- local-only;
- analysis pending.

Use text/icon/state labels as well.

### Motion

Scanning/analyzing indicators should respect reduced-motion settings.

Progress changes must not constantly steal focus or produce screen-reader chatter for every increment.

### Scaling

Vault must remain usable at larger text/UI scaling.

Dense technical rows should reflow or stack rather than truncate critical state.

### Destructive actions

Any destructive operation must have:

- explicit text;
- clear target;
- keyboard-accessible confirmation;
- no dependence on red color alone.

---

## 4.23 Empty states

Empty states use **functional state/action copy**, not title-plus-tagline presentation.

### No roots

> **No storage configured.**

Action:

> **Add storage**

### No Attention items

> **No items need attention.**

### No learned rules

> **No learned rules.**

### No history

> **No ownership changes recorded.**

No marketing/tagline copy.

---

## 4.24 Local-only object controls

When an unresolved package genuinely has no canonical match, Attention may offer:

> **Create local-only item**

This must be a deliberate action.

The creation flow should ask only for the media facts needed to present the object:

- destination world;
- title;
- date/year where relevant;
- edition/volume information where relevant;
- artwork optional;
- description optional.

The resulting object is visibly:

> **Local-only**

Later:

> **Link to canonical object**

must be available without losing the local object's ownership/history.

---

## 4.25 Metadata and artwork controls

Canonical metadata stays default.

Where local alternatives exist, the UI can expose:

> Use local poster  
> Use canonical poster  
> Lock this poster

For editable fields:

> Use local title  
> Use canonical title  
> Lock field

Do not place dozens of lock icons permanently on every world page.

Detailed override management belongs in Vault ownership detail.

The world may expose the currently chosen artwork normally.

### Jellyfin control case — local artwork as a separate provider concern

**Analogue strength:** Direct for local-image discovery; different authority policy.

**Confirmed Jellyfin location:**  
`MediaBrowser.Controller/Providers/ILocalImageProvider.cs`

Verified excerpt:

```csharp
public interface ILocalImageProvider : IImageProvider
{
    IEnumerable<LocalImageInfo> GetImages(BaseItem item, IDirectoryService directoryService);
}
```

**What Jellyfin proves:** Local artwork discovery can be a provider concern separate from the canonical media object itself.

**What Vault adopts:** Local artwork is discoverable evidence/presentation material.

**What Vault changes:** Discovery does not make local artwork authoritative. The user or a scoped rule chooses authority, and locks survive metadata refresh.

---

## 4.26 Destructive-action boundary

Vault is diagnostic by default.

Normal workflows may:

- classify;
- identify;
- regroup;
- reconcile;
- suppress warnings;
- lock identity;
- create local-only objects;
- remove logical root configuration.

They must not casually delete physical files.

If physical deletion is ever supported later:

- it is an explicit separate capability;
- it names the exact paths affected;
- it never hides behind “Fix duplicate”;
- it requires clear confirmation;
- it must respect external/removable storage state.

This design does not require physical file deletion.

---

## 4.27 Reference atlas — controls and integration

### Jellyfin control case: library operation boundaries

**Analogue strength:** Direct mechanism, different UI.

**Confirmed source:**  
`MediaBrowser.Controller/Library/ILibraryManager.cs`

Relevant responsibilities include:

- queued scans;
- library validation;
- path resolution;
- ignore rules;
- library queries;
- alternate-version relationships;
- item updates;
- image updates.

**Vault implication:** Rich library intelligence benefits from explicit operation boundaries rather than one giant “refresh everything” function.

### Jellyfin control case: alternate versions

**Analogue strength:** Direct.

**Confirmed source:**  
`MediaBrowser.Controller/Library/ILibraryManager.cs`

The interface distinguishes local alternate quality versions and linked alternate content variants.

**Vault implication:** The UI needs to preserve meaningful relationships between copies/versions/editions instead of presenting a flat file list.

### Jellyfin control case: queryable library state

**Analogue strength:** Direct concept.

**Confirmed source:**  
`MediaBrowser.Controller/Library/ILibraryManager.cs`

General library queries and media-stream-language queries exist at the library layer.

**Vault implication:** Physical ownership facts must be queryable so Colosseum search and Vault management search can expose different views over the same truth.

### No Jellyfin UI authority

The Jellyfin server repository is **not** a direct control case for Vault's visual experience.

The following are Colosseum design decisions:

- Overview / Storage / Attention / Owned Media / Rules / History structure;
- guided Attention UI;
- scoped learned-rule management;
- progressive world-versus-Vault detail;
- compact **Owned locally** integration;
- local/addon source coexistence;
- local-only object creation;
- plain-language evidence explanations.

Execution agents must not infer Jellyfin-web UI requirements from server-side source references.

---

## 4.28 Section acceptance criteria

The design is satisfied when the eventual product can observably demonstrate:

1. Vault's normal landing view answers whether the collection is healthy without requiring technical interpretation.
2. Roots can be added with a small profile rather than a large server-style settings flow.
3. One normal Refresh action exists while advanced refresh operations remain accessible.
4. Attention groups related decisions and supports safe batch repair.
5. Automatic identity/move/duplicate decisions can explain their evidence in plain language.
6. Learned rules show their scope and can be inspected, disabled, or removed.
7. Vault's existing file-first Browse surface remains first-class while Owned Media provides management-oriented search/filtering rather than duplicating Browse or Theatre/Tankoban/Biblio.
8. Physical-copy detail exposes full technical facts without cluttering normal world pages.
9. Destination worlds expose compact local-ownership state.
10. Good local copies can become the default action while addon sources remain available.
11. Editions remain visibly distinct from technical versions.
12. Extras appear only when present and companions do not become Extras.
13. Consumer search and Vault management search read the same ownership truth.
14. DLNA exposes Vault-owned media without becoming a second ownership authority.
15. Long-running operations communicate scope/progress/safety without blocking normal use.
16. Errors state what happened and what existing truth remains safe.
17. All primary workflows are keyboard-operable.
18. State is never communicated by color alone.
19. Local-only creation is explicit and later canonical linking remains available.
20. No normal repair action silently deletes physical media.

---

## 4.29 Section conclusion

The control-room rule is:

> **Vault should be powerful when you open it and almost invisible when you do not.**

Normal Colosseum surfaces should only inherit the ownership facts that help the user watch or read something.

Vault itself is where those facts become inspectable, repairable, searchable, and explainable.

The interface should make a 20,000-file collection feel understood, not administered.

---

# Vault Design — Section 5: Technical Shape and Implementable Contracts

## 5. Technical Shape and Implementable Contracts

### Status

**Approved design — product decisions locked; implementation planning not yet performed.**

### Evidence base

**Colosseum pinned commit:** `3c55300278d2410c19626c4aca513ea4a606da65`  
**Jellyfin pinned commit:** `4e2fc33a61391ded79fc4353f0fd9090952bd130`

This section is a design contract, not an implementation plan.

It deliberately distinguishes:

- **Confirmed existing owners** — directly inspected in the current Colosseum repository.
- **Recommended responsibility changes** — design decisions for existing owners.
- **New seams** — capabilities the design requires, without inventing final class/file names.
- **Jellyfin control cases** — behavioral evidence, not code-port instructions.

> **Global translation rule:** Jellyfin's C# architecture is evidence for mature media-library behavior. It is not an instruction to reproduce Jellyfin's class hierarchy, server lifecycle, dependency graph, user model, or .NET patterns inside Colosseum.

---

## 5.1 Technical objective

Vault needs enough structure to answer five different questions without mixing them together:

1. **Filesystem truth** — what physically exists right now?
2. **Physical identity** — is this the same copy, a moved copy, a changed copy, or another copy?
3. **Media structure** — which files belong together, and what roles do they play?
4. **Canonical identity** — what Theatre/Tankoban/Biblio object does the package represent?
5. **Ownership publication** — what local-ownership facts should the destination world expose?

The pipeline must therefore remain conceptually:

```text
Configured roots
    ↓
Filesystem observation
    ↓
Physical census
    ↓
Physical-copy reconciliation
    ↓
Package / relationship resolution
    ↓
Canonical identification
    ↓
Ownership publication
    ↓
Deep analysis continues independently
```

No stage is allowed to silently take ownership of the stage above or below it.

---

# 5.2 Current Colosseum responsibility map

The current repository already contains a strong foundation.

## Confirmed owner: `VaultConfig`

**Likely path confirmed through current references:**  
`native/engine/VaultConfig.*`

Current code is already treated by `VaultWatcher` as the source of truth for configured roots, kind overrides, and scan-ignore rules.

### Design responsibility

`VaultConfig` remains the durable authority for:

- configured roots;
- root confirmation/removal;
- root profile;
- watch preference;
- include-subfolder preference;
- explicit root/folder kind overrides;
- explicit scan-ignore rules.

It should **not** become the storage place for:

- canonical identities;
- technical media analysis;
- copy fingerprints;
- user Attention history;
- metadata-provider results.

Scoped learned rules may be persisted alongside configuration or through a dedicated rule store, but the design contract is:

> **Configuration describes what the user told Vault to do. Learned rules describe what Vault learned from the user's repairs.**

Those concepts must remain distinguishable even if they share persistence infrastructure.

---

## Confirmed owner: `VaultWatcher`

**Confirmed path:**  
`native/engine/VaultWatcher.cpp`

Current implementation already:

- uses `QFileSystemWatcher`;
- debounces event storms;
- periodically probes configured roots;
- distinguishes root absence from watcher degradation;
- emits root-availability changes;
- preserves dirty roots while immersive work defers upsert;
- re-arms watches after roots return;
- performs incremental arrival processing;
- uses configured ignore rules and kind overrides.

Representative current intent from the code:

```cpp
m_probe->setInterval(1000);
```

and the implementation explicitly distinguishes:

> filesystem watcher degradation from filesystem absence.

### Design responsibility

`VaultWatcher` remains the **event detector**, not the truth database.

It owns:

- noticing that something changed;
- noticing that a root became unavailable/available;
- coalescing noisy events;
- requesting the narrowest safe refresh/reconciliation.

It does **not** own:

- durable physical identity;
- canonical matching;
- grouping decisions;
- metadata-provider policy;
- Attention decisions.

### Required contract

Watcher output should conceptually be requests such as:

```text
RootChanged(root)
RootAvailabilityChanged(root, available)
SubtreeDirty(root, subtree)
```

The downstream scan/reconciliation pipeline determines what those events mean.

---

## Confirmed owner: `VaultScanner`

**Confirmed path:**  
`native/engine/VaultScanner.cpp`

Current code already performs a candidate census rather than treating every event as an authoritative mutation.

The existing design is important and should survive:

> **candidate first → publish only complete safe truth**

### Design responsibility

`VaultScanner` remains the **physical census owner**.

It answers:

- what media-like files exist;
- basic physical facts;
- broad media kind;
- root/subtree association;
- scan-ignore policy;
- provisional grouping evidence.

It should produce physical observations, not canonical media objects.

### Scanner output contract

A physical observation should contain enough cheap truth to support later stages:

```text
path
root
subtree
filename
extension/container hint
size
mtime
broad media kind
settled/incomplete state
obvious sidecar role hint
```

The census should avoid expensive probing unless required to classify safely.

### Non-goal

The scanner does not decide:

> "This is Alien (1979)."

It decides:

> "This is a stable video-like file at this location with these cheap facts."

---

## Confirmed owner: `VaultIdentity`

**Confirmed path:**  
`native/engine/VaultIdentity.cpp`

Current code already contains important physical-copy reconciliation behavior:

- path normalization;
- durable identity entries;
- aliases;
- path aliases;
- changed-content ceremonies;
- likely-copy ceremonies;
- persisted user choices;
- whole-scan reconciliation;
- unique signature-based migration;
- parked ambiguous relationships.

Current identity begins with a cheap computed identity based on normalized path, size, and modification time:

```cpp
return QStringLiteral("vault:") + QString::fromLatin1(digest);
```

The current reconciler also distinguishes:

- changed content;
- likely copy;
- unambiguous move;
- ambiguous relationships requiring a ceremony.

### Design responsibility

`VaultIdentity` should evolve into the authoritative **physical-copy identity/reconciliation owner**.

It must remain separate from canonical media identity.

It answers:

> Is this physical file the same known copy, a changed file, a moved copy, another copy, or unresolved?

### Required identity tiers

The new design should support progressively stronger evidence:

**Tier 0 — cheap observation identity**
- normalized path;
- size;
- mtime.

**Tier 1 — structural fingerprint**
- duration/page count;
- container/archive structure;
- stream/track summary;
- selected stable embedded identifiers.

**Tier 2 — strong content fingerprint**
- partial-content fingerprint;
- chunk hash;
- another bounded content signature.

**Tier 3 — full hash**
- only when actually required.

The exact algorithms are implementation details.

The design contract is:

> **Use the cheapest evidence that safely settles the relationship. Escalate only when ambiguity remains.**

This protects very large libraries from turning every refresh into full-file hashing.

---

## Confirmed owner: `VaultIndex`

**Confirmed path:**  
`native/engine/VaultIndex.cpp`

Current implementation is SQLite-backed and acts as the durable queryable file index.

It already supports batch publication/upsert patterns used by scanner/watcher/enricher.

### Design responsibility

`VaultIndex` remains the durable **physical ownership read model**, but its schema must grow enough to represent the locked design without flattening concepts.

It should store/query physical truth.

It must **not** become a duplicate canonical Theatre/Tankoban/Biblio database.

### Required conceptual records

The implementation does not have to use these exact table names, but it must represent the concepts independently.

#### Root

```text
rootId
path
profile
availability
watch state
last successful census
last reconciliation
```

#### Physical copy

```text
copyId
rootId
current path
broad media kind
availability state
size
mtime
fingerprint state
analysis state
problem state
first seen
last seen
```

#### Physical package

Groups related files before/around canonical identity.

```text
packageId
primary copy/copies
companions
extras
provisional title/year evidence
```

#### Edition

Represents a meaningful cut/edition boundary when known.

```text
editionId
ownership group
label
evidence
manual/automatic authority
```

#### Copy-to-edition relationship

Allows:

```text
Alien
  Theatrical
    4K copy
    1080p copy
  Director's Cut
    4K copy
```

#### Canonical ownership link

```text
destination world
canonical object id
confidence tier
authority source
manual lock
local-only flag
```

#### Companion relationship

```text
subtitle
artwork
NFO
external audio
chapter sidecar
other supported companion
```

#### Extra relationship

```text
trailer
deleted scene
interview
featurette
other bonus content
```

#### Analysis facts

Copy-scoped technical facts.

#### Attention case

Unresolved user-meaningful problem.

#### Learned rule

Folder/root-scoped durable repair rule.

#### Ownership/history event

Meaningful state transitions.

### Core index rule

> **Rows representing physical truth may reference canonical IDs, but they must not duplicate the canonical media world's full object graph.**

---

## Confirmed owner: `VaultEnricher`

**Confirmed path:**  
`native/engine/VaultEnricher.cpp`

Current code already performs second-stage physical enrichment:

- comic archive inspection;
- EPUB parsing;
- cover discovery;
- EPUB metadata extraction;
- video duration probing via `ffprobe`;
- media-admission probing;
- batched result commit;
- cancellation between files;
- owner-thread database commit.

Current code explicitly buffers results and commits them in one owner-thread batch rather than writing from the worker per file.

That boundary is worth preserving.

### Design responsibility

`VaultEnricher` becomes or feeds the **deep physical analyzer**.

Its job is copy-scoped facts:

- video/audio/container properties;
- streams/tracks;
- chapters;
- duration;
- page counts;
- embedded/local metadata evidence;
- embedded/local artwork evidence;
- archive health;
- technical error state.

### Important expansion

Today video enrichment is relatively narrow.

The design requires full structured media analysis sufficient to support:

- resolution;
- codec;
- HDR/Dolby Vision where detectable;
- bitrate;
- audio codecs/channels/languages;
- subtitle languages/types;
- chapters;
- container;
- duration;
- file size;
- other source-choice facts.

### Analyzer output rule

Deep analysis writes facts about:

> **this physical copy**

not facts about:

> **the canonical movie itself.**

---

## Confirmed owner: `VaultIdentifier`

**Confirmed path:**  
`native/engine/VaultIdentifier.cpp`

Current code already performs canonical identification using media-specific sources and conservative adoption behavior.

### Design responsibility

`VaultIdentifier` remains the **canonical identity policy owner**, but should no longer directly absorb every future catalog integration.

It should orchestrate provider evidence and decide:

- confidence state;
- automatic adoption;
- ambiguity;
- conflict;
- manual lock;
- identity suppression;
- local-only eligibility.

It remains the place where Vault's cautious product policy lives.

---

# 5.3 New seam: Media resolver layer

Jellyfin demonstrates a useful separation here.

It has specialized resolvers such as:

- `Resolvers/Movies/MovieResolver.cs`
- `Resolvers/TV/EpisodeResolver.cs`

Vault needs the same **idea**, not the same class hierarchy.

## Purpose

Resolvers translate physical filesystem evidence into **media structure claims**.

They answer questions such as:

- which file is primary media;
- which files are alternate copies;
- which files appear to be different editions;
- which are companions;
- which are extras;
- which episodes a file claims to contain;
- whether a folder looks like one title, a season, a multi-volume set, or mixed material.

## Resolver contract

Conceptually:

```text
resolve(PhysicalCensus + scoped rules)
    -> PhysicalPackageClaims
```

A claim carries:

```text
claim type
files involved
parsed title/year/numbering evidence
confidence
explanation/evidence
```

### Resolver families

At minimum:

- Movies
- TV
- Anime naming
- Comics/manga
- Books
- Generic companion/extra detection

The exact implementation may share parser libraries.

### Authority limit

Resolvers never assign final canonical IDs.

Example:

Anime resolver:

> `Frieren - 27.mkv` claims absolute episode 27.

Theatre canonical resolver:

> absolute 27 corresponds to this canonical episode.

That boundary is mandatory.

---

# 5.4 New seam: Identity-provider contract

This is the strongest Jellyfin architecture idea worth borrowing.

## Jellyfin control case

**Confirmed path:**  
`MediaBrowser.Controller/Providers/IRemoteMetadataProvider.cs`

Jellyfin has an explicit remote-provider contract with operations equivalent to:

```csharp
Task<MetadataResult<TItemType>> GetMetadata(
    TLookupInfoType info,
    CancellationToken cancellationToken);
```

and:

```csharp
Task<IEnumerable<RemoteSearchResult>> GetSearchResults(
    TLookupInfoType searchInfo,
    CancellationToken cancellationToken);
```

It separately defines local metadata providers in:

`MediaBrowser.Controller/Providers/ILocalMetadataProvider.cs`

with a contract equivalent to:

```csharp
Task<MetadataResult<TItemType>> GetMetadata(
    ItemInfo info,
    IDirectoryService directoryService,
    CancellationToken cancellationToken);
```

### What this proves

A mature metadata system benefits from separating:

> **provider knows how to ask one source**

from:

> **library decides which answer to trust.**

## Vault provider contract

Vault should introduce a small provider seam that can represent:

- canonical remote catalogs;
- bundled/offline catalogs;
- embedded metadata;
- NFO/sidecar metadata;
- world-owned canonical lookup services.

Conceptually:

```text
IdentityProvider
    supports(mediaKind)
    search(evidence) -> candidates
    fetch(stableProviderId) -> metadata/evidence
```

A candidate should carry:

```text
provider id
canonical/world id when known
title
year/date
media type
external identifiers
evidence fields
provider confidence facts
```

### Important constraint

Providers do **not** decide the final Vault confidence tier.

They report candidates/evidence.

`VaultIdentifier` owns:

- reconciliation;
- conflict;
- confidence;
- manual authority;
- auto-adoption threshold.

### Why this matters

Adding:

- another movie catalog;
- another comics provider;
- better anime identity;
- book metadata;
- future local metadata formats

should not require turning `VaultIdentifier.cpp` into a giant provider-specific switchboard.

---

# 5.5 Local metadata is evidence, not canonical authority

Jellyfin explicitly distinguishes local and remote metadata providers.

Vault should preserve that useful separation while applying Colosseum's stronger authority rule.

## Evidence sources

Local identity evidence can include:

- NFO;
- EPUB metadata;
- archive metadata;
- embedded tags;
- local artwork naming conventions;
- folder/file naming;
- stable external IDs embedded locally.

## Authority order

The product contract is:

```text
explicit user decision
    >
locked field / locked identity
    >
high-confidence canonical identity
    >
strong local evidence
    >
weaker filename heuristics
```

This is not a literal scoring formula.

It defines who is allowed to overrule whom.

### Local presentation

Local metadata can become presentation authority only when:

- the user explicitly chooses it; or
- a scoped rule says local presentation is preferred.

That decision is stored separately from the raw local evidence.

---

# 5.6 New seam: Technical analyzer contract

Deep analysis should not remain a bag of ad-hoc fields attached directly to scan logic.

## Conceptual contract

```text
analyze(PhysicalCopy)
    -> PhysicalAnalysis
```

For video/audio, `PhysicalAnalysis` can contain:

```text
container
duration
bitrate
video streams
audio streams
subtitle streams
chapters
HDR/DV properties
resolution
frame rate
language facts
corruption/admission result
```

For comics/books:

```text
archive/container health
page count
embedded title/creator
cover candidates
EPUB structure
chapter/spine facts
```

### Caching rule

Analysis is valid only while the physical-copy facts it depends on still match.

At minimum, cache validity must be tied to a physical-copy identity/version.

If the file changes materially:

> old technical analysis becomes stale.

### Queue behavior

Analyzer work is background work.

Priority order should conceptually allow:

1. currently requested/opened item;
2. newly identified item;
3. normal background backlog.

Exact scheduling is implementation detail.

---

# 5.7 New seam: Attention service

The current `VaultIdentity` already has the beginnings of user-facing unresolved ceremonies.

The new design generalizes that idea.

## Purpose

Attention owns **unresolved product decisions requiring human judgment**.

It does not own ordinary technical errors.

### Attention case examples

- ambiguous canonical identity;
- conflicting identity evidence;
- uncertain move;
- uncertain copy/duplicate relationship;
- edition ambiguity;
- episode numbering ambiguity;
- companion attachment ambiguity;
- learned-rule conflict.

### Attention case contract

Each case should expose:

```text
case type
affected items
plain-language problem
evidence
recommended action
alternative actions
batchability
possible learned rule
scope of learned rule
created time
resolution history
```

### Resolution output

A user repair can produce:

- one-time decision;
- manual canonical identity;
- relationship decision;
- explicit suppression;
- scoped learned rule.

The resolution must be durable.

---

# 5.8 New seam: Scoped rule engine

Rules are an input into interpretation, not a post-processing decoration.

## Rule scope

At minimum:

```text
root
folder/subtree
```

Narrower scope wins over broader scope.

## Rule examples

```text
use absolute episode numbering
ignore Samples
prefer local artwork
treat folder as a specific edition
kind override
companion naming convention
```

## Contract

Given a path/context, the rule engine returns the **effective rules**, preserving provenance.

Example:

```text
E:\Anime
  absolute numbering

E:\Anime\Classic Show
  season/episode numbering
```

The second rule overrides the first only inside that subtree.

### Required provenance

The UI must be able to answer:

> Why is this rule active here?

Therefore each effective rule needs:

```text
source scope
origin
created by user vs learned repair
overridden rule if any
```

---

# 5.9 Ownership graph

Vault needs a stable conceptual graph linking physical copies to canonical media without becoming the canonical media database.

## Model

```text
Canonical object (owned by Theatre/Tankoban/Biblio)
    ↑
Ownership link
    ↑
Edition
    ↑
Physical copy / version
    ├── companions
    └── extras
```

A physical copy also belongs to:

```text
Root → current path/location
```

### Why this graph matters

It allows one canonical object to answer:

> How many copies do I own?

while preserving:

> Which edition?  
> Which location?  
> Which copy is away?  
> Which copy has HDR?  
> Which copy has these subtitles?

### Non-goal

Vault should not duplicate:

- Theatre's complete movie metadata;
- Tankoban's canonical series/volume ontology;
- Biblio's canonical book ontology.

Vault stores only enough foreign identity to maintain the ownership link.

---

# 5.10 Canonical publication contract

The destination worlds need a narrow ownership interface.

Conceptually:

```text
OwnershipSnapshot getOwnership(world, canonicalId)
```

The snapshot should expose consumer-useful facts:

```text
owned
currentlyAvailable
copyCount
editionCount
bestLocalSource
local source summaries
extras available
local-only status
```

It should **not** expose the entire Vault index.

### Publication gate

A canonical ownership link may publish when:

- identity is **Certain**; or
- identity is **Manual** because the user explicitly confirmed/chose it; or
- the user explicitly created a local-only object.

**Very likely does not publish automatically.** It remains a suggested candidate until additional evidence makes it Certain or the user confirms it, converting authority to Manual.

Ambiguous/conflicting unresolved packages do not publish.

### Update behavior

Deep analysis updates the ownership snapshot incrementally.

The canonical object does not have to be unpublished while analysis is incomplete.

---

# 5.11 Local source contract

A local physical copy should enter Theatre's source selection as a first-class source.

## Source summary

For source choice, the destination world needs only:

```text
copy id
edition
availability
quality summary
resolution
HDR
audio summary
subtitle summary
problem state
path/open token
```

The full file path may be withheld from normal world presentation if the UI does not need it.

### Source selection rule

Local-source preference remains a **Theatre/source-selection policy**, not a Vault identity decision.

Vault supplies truthful source facts.

Theatre decides:

> which source should be recommended for playback?

This prevents physical ownership logic from growing player policy.

---

# 5.12 Jellyfin control case: media-source separation

**Confirmed path:**  
`Emby.Server.Implementations/Library/MediaSourceManager.cs`

Jellyfin explicitly has a media-source owner separate from the canonical item/library manager.

Representative behavior:

```csharp
var sources = hasMediaSources.GetMediaSources(enablePathSubstitution);
```

It then handles media streams and alternate sources independently.

### What Jellyfin proves

One media object can have several actual playable media sources with source-specific technical facts.

### What Vault adopts

Physical copies are source objects underneath a canonical media identity.

### What Vault rejects

Vault does not adopt:

- Jellyfin user permissions;
- transcoding capability flags as ownership facts;
- live-stream lifecycle;
- server path substitution;
- server playback-session ownership.

Those belong to Jellyfin's server problem.

---

# 5.13 Edition versus version contract

The design needs an explicit semantic rule.

## Version/copy

Same meaningful content/edition, different physical representation.

Examples:

```text
Alien theatrical — 1080p
Alien theatrical — 2160p Dolby Vision
```

## Edition

Meaningfully different content/cut.

Examples:

```text
Alien theatrical
Alien Director's Cut
```

### Detection

Automatic edition suggestions may use:

- filename markers;
- NFO/local metadata;
- canonical edition information;
- runtime difference;
- chapter structure;
- explicit folder rule.

But uncertain edition classification enters Attention.

### Playback constraint

Automatic "best source" logic must only rank sources **within the requested/default edition** unless the user explicitly selects another edition.

This is a hard contract.

---

# 5.14 Companion contract

Companions are physical files associated with a primary copy/package but are not themselves canonical media.

Examples:

- external subtitles;
- local artwork;
- NFO;
- external audio;
- chapter files;
- other supported sidecars.

## Relationship authority

Companion association can be:

- automatic and high-confidence;
- scoped-rule-derived;
- manual.

Ambiguous attachment enters Attention.

### Playback exposure

The appropriate player/reader may consume compatible companions.

Vault remains the owner of:

> this sidecar belongs to this copy/package.

### Jellyfin control case — external subtitle and audio companions

**Analogue strength:** Direct for recognizing external media companions.

**Confirmed Jellyfin location:**  
`MediaBrowser.Providers/MediaInfo/FFProbeVideoInfo.cs`

External subtitles are resolved and attached to the video's stream set:

```csharp
video.SubtitleFiles = externalSubtitleStreams.Select(i => i.Path).Distinct().ToArray();
currentStreams.InsertRange(0, externalSubtitleStreams);
```

External audio is treated similarly:

```csharp
video.AudioFiles = externalAudioStreams.Select(i => i.Path).Distinct().ToArray();
currentStreams.AddRange(externalAudioStreams);
```

**What Jellyfin proves:** External subtitle/audio files can be modeled as attached source-specific media facts instead of standalone library objects.

**Vault translation:** They are **Companions** owned by the relevant physical package/copy; the player consumes the relationship.

---

# 5.15 Extras contract

Extras are real consumable bonus media associated with a canonical object/package.

Examples:

- trailer;
- deleted scene;
- interview;
- featurette.

They are not companions.

### Publication

Vault publishes an extras summary to the destination world only when extras exist.

The destination world owns:

- visual presentation;
- playback/reader UX.

Vault owns:

- physical path/copy;
- extra type;
- association.

---

# 5.16 Local-only object contract

A local-only object is a deliberate bridge when no canonical catalog can represent something.

## Creation gate

Only explicit user action creates one.

Automatic identification failure does not.

## Minimum identity

A local-only object needs:

```text
destination world
durable local-only id
title
media-kind-specific minimum structure
optional date/year
optional description/artwork
```

### Authority

The local-only object behaves as the canonical identity **for this user's local collection** until linked.

### Later linking

Linking to a canonical object must preserve:

- ownership links;
- copies;
- editions;
- companions;
- extras;
- progress handoff where the world supports it;
- local presentation overrides;
- history;
- learned rules.

The operation is an identity reconciliation.

It is not delete + create.

---

# 5.17 World ownership interface

Theatre/Tankoban/Biblio should not query raw Vault tables.

A narrow API/bridge should answer world-level ownership questions.

Conceptually:

```text
ownershipFor(world, canonicalId)
bestLocalSource(world, canonicalId, edition?)
localSources(world, canonicalId, edition?)
extrasFor(world, canonicalId)
```

### Eventing

Worlds need update signals such as:

```text
ownershipChanged(world, canonicalId)
localSourceChanged(world, canonicalId)
extrasChanged(world, canonicalId)
```

The exact Qt interface is implementation detail.

The design contract is:

> **Worlds consume ownership facts; they do not reconstruct Vault state themselves.**

---

# 5.18 Search/query contract

Vault needs one physical-ownership query surface that both UI layers can use.

## Consumer query

World/global search and the real Collection may request:

```text
owned locally
available offline
4K
HDR
local-only
on root / drive
copy count
```

Collection is a **consumer/projection of these Vault facts**, not a second ownership authority.

## Management query

Vault may additionally request:

```text
root
away
missing
duplicate
needs attention
unanalyzed
audio language
subtitle language
file size
added date
copy count
edition count
```

### Single-truth rule

Both query modes derive from the same durable physical/ownership facts.

Do not create a second consumer-only ownership cache with independent truth unless it is a rebuildable projection.

---

# 5.19 Scan / identify / analyze / refresh contracts

These operations remain independent.

## Rescan

Re-evaluate filesystem census and relationships affected by physical change.

Does not automatically force all canonical metadata to refresh.

## Re-identify

Re-run canonical identity policy/provider lookup over selected physical packages.

Does not require rereading unchanged large files.

## Re-analyze

Invalidate/re-run technical physical analysis.

Does not change canonical identity unless new evidence explicitly triggers a separate identity evaluation.

## Refresh metadata

Refresh canonical/presentation metadata from allowed sources.

Does not scan the disk.

## Repair

Resolve selected Attention cases.

### Normal Refresh

The user-facing default orchestrates the cheapest necessary subset.

The implementation must retain these operation boundaries even if the UI normally hides them.

### Scheduled reconciliation

Scheduled scans may invoke the same rescan/reconciliation machinery as an **optional reliability layer**.

They must not define separate scan semantics or separate truth. Watcher events, manual Refresh, root-return reconciliation, and scheduled scans all converge on the same census/reconciliation contracts.

---

# 5.20 Concurrency and threading contract

The current Vault code already contains good evidence that thread ownership matters.

`VaultEnricher` explicitly avoids per-file database writes from a possible worker thread and hops the batch commit back to the index owner thread.

That should become a general rule.

## Required concurrency model

Expensive read-only work may run off the UI/DB owner thread:

- filesystem census;
- fingerprints;
- ffprobe/media analysis;
- provider requests;
- archive parsing.

Authoritative state publication happens through controlled owners.

### Rules

1. Worker tasks do not mutate QML objects.
2. Worker tasks do not independently write authoritative SQLite state unless the database layer explicitly supports and owns that model.
3. Cancellation is checked at safe boundaries.
4. Results are committed in coherent batches.
5. A cancelled/failed pass cannot publish a partial destructive view.
6. User-visible progress is derived from job state, not worker-side UI mutation.

---

# 5.21 Transaction and publication contract

Vault already benefits from conservative publication.

The new system must preserve:

> **No partial truth.**

## Census publication

A failed complete-root census must not replace the previous complete root state with a partial result.

## Reconciliation

Move/copy/identity relationship changes that affect several records should commit atomically where practical.

## Analysis

Analysis may publish per-copy or in batches because incomplete analysis does not invalidate ownership.

## Manual decisions

Manual identity, locks, Attention resolutions, and learned rules must be durable before the UI reports success.

---

# 5.22 Confidence contract

Confidence must not be one unexplained floating-point number exposed as product truth.

## Product states

- **Certain** — safe for automatic canonical adoption/publication.
- **Very likely** — strong suggestion only; remains unpublished.
- **Ambiguous** — several plausible candidates; requires Attention.
- **Conflicting** — material evidence disagrees; requires Attention.
- **Manual** — user-selected/confirmed authority; publishable and persistent.

Internal systems may use numeric scores.

The UI and publication gate use the semantic states. **There is no numeric Very-Likely threshold that is itself permission to publish.**

### Evidence record

An identity result should retain enough explanation to answer:

> Why did Vault decide this?

For example:

```text
filename title matched
year matched
local NFO agreed
one canonical candidate
```

### Manual authority

Manual beats automatic.

Automatic matching does not silently overturn a Manual identity.

### Implementation safety invariant — regrouping

This is an implementation safeguard, not a new product surface.

If the membership/structure of a physical package materially changes, an **automatic group-derived canonical link must be re-evaluated** rather than blindly carried forward from the old group. A Manual link persists, but the changed grouping may be flagged for review if the new shape conflicts with the user's chosen identity.

This exists to prevent stale per-file identity evidence from poisoning a newly formed group.

---

# 5.23 Metadata-lock contract

Locks apply to **authority**, not merely UI values.

## Identity lock

Stops automatic canonical re-identification.

## Field lock

Prevents refresh from replacing a chosen presentation field.

Examples:

```text
poster locked to local
title locked to user value
synopsis unlocked
```

### Storage

Raw provider/local evidence should remain separately inspectable from the effective chosen value.

This lets the user unlock later without losing provenance.

---

# 5.24 History contract

History should record user-meaningful state transitions, not every filesystem event.

Useful durable events:

```text
root added
root away
root returned
copy moved
copy changed
manual identity chosen
identity relinked
duplicate ignored/accepted
local-only created
local-only linked
rule created
rule removed
field override changed
```

### History ownership

History belongs to Vault's ownership model.

It is not canonical media history and not playback history.

---

# 5.25 Diagnostics contract

Diagnostics may expose deeper technical state than the normal Vault UI.

Useful diagnostic information:

- last scan timing;
- parser/resolver chosen;
- rules applied;
- identity providers consulted;
- provider errors;
- candidate counts;
- fingerprint tier used;
- analysis tool/probe failure;
- transaction failure;
- watcher degradation;
- root probe transitions.

### Security/privacy rule

Diagnostics intended for bug reports must avoid casually dumping:

- full personal filesystem layouts;
- private URLs/tokens;
- credentials;
- unnecessary local metadata.

Redaction/export behavior can be specified later if a bug-report feature is designed.

---

# 5.26 DLNA boundary

DLNA is allowed, but architecturally downstream.

Conceptually:

```text
Vault physical ownership
        ↓
DLNA catalog/exposure adapter
        ↓
DLNA clients
```

DLNA may ask Vault:

```text
which copies are available?
what is their media type?
what technical properties are known?
how can this file be opened?
```

DLNA does not become an identity authority.

### Explicit non-adoption from Jellyfin

Jellyfin's DLNA/server concerns may involve:

- multi-user permissions;
- device profiles;
- transcoding policy;
- server URLs;
- remote session management.

Those are not implied by Vault's DLNA support.

If Colosseum later needs compatibility/transcoding for DLNA, that is a **delivery capability**, not a reason to redesign Vault as a server.

---

# 5.27 Existing-owner evolution versus new owners

The execution plan must later confirm the smallest safe implementation shape.

The design does **not** mandate a proliferation of new classes.

A reasonable ownership target is:

| Responsibility | Current confirmed owner | Design direction |
|---|---|---|
| Roots/config | `VaultConfig` | Extend |
| FS observation | `VaultWatcher` | Preserve/extend |
| Census | `VaultScanner` | Preserve/extend |
| Physical identity/reconcile | `VaultIdentity` | Substantially extend |
| Durable physical query state | `VaultIndex` | Schema/query extension |
| Deep physical facts | `VaultEnricher` | Expand or split behind analyzer seam |
| Canonical matching policy | `VaultIdentifier` | Preserve policy, add provider seam |
| Media structure resolving | Existing scanner/kit logic partial | New explicit seam |
| Attention | Existing identity ceremonies partial | Generalize behind explicit seam |
| Scoped learned rules | Kind overrides/ignore partial | New generalized seam |
| World publication | Existing Vault/world integration to verify | Narrow ownership bridge |
| DLNA exposure | Future | Downstream adapter |

The plan phase must inspect repository drift before assigning exact new filenames.

---

# 5.28 Jellyfin reference atlas for this section

## A. Specialized media resolvers

**Analogue strength:** Direct concept.

**Confirmed Jellyfin locations:**

- `Emby.Server.Implementations/Library/Resolvers/Movies/MovieResolver.cs`
- `Emby.Server.Implementations/Library/Resolvers/TV/EpisodeResolver.cs`

**Mechanism:** Dedicated resolvers interpret different physical-media structures.

**Vault adoption:** Explicit resolver seam for physical structure claims.

**Vault divergence:** Resolvers do not create the final canonical world object.

---

## B. Remote metadata provider seam

**Analogue strength:** Direct.

**Confirmed Jellyfin location:**  
`MediaBrowser.Controller/Providers/IRemoteMetadataProvider.cs`

**Minimal reference excerpt:**

```csharp
Task<MetadataResult<TItemType>> GetMetadata(
    TLookupInfoType info,
    CancellationToken cancellationToken);
```

**Mechanism:** Provider obtains metadata/search results; library code owns integration policy.

**Vault adoption:** Small identity-provider interface.

**Vault divergence:** `VaultIdentifier` retains cautious confidence/manual-authority policy.

---

## C. Local metadata provider seam

**Analogue strength:** Direct.

**Confirmed Jellyfin location:**  
`MediaBrowser.Controller/Providers/ILocalMetadataProvider.cs`

**Minimal reference excerpt:**

```csharp
Task<MetadataResult<TItemType>> GetMetadata(
    ItemInfo info,
    IDirectoryService directoryService,
    CancellationToken cancellationToken);
```

**Mechanism:** Local metadata is handled through a separate provider family.

**Vault adoption:** Treat embedded/NFO/local metadata as explicit evidence sources.

**Vault divergence:** Local evidence does not automatically outrank canonical identity or presentation.

---

## D. Media-source manager

**Analogue strength:** Direct for multi-source media.

**Confirmed Jellyfin location:**  
`Emby.Server.Implementations/Library/MediaSourceManager.cs`

**Minimal reference excerpt:**

```csharp
var sources = hasMediaSources.GetMediaSources(enablePathSubstitution);
```

**Mechanism:** One library item can expose multiple media sources with source-specific stream facts.

**Vault adoption:** One canonical media identity may have several local physical sources.

**Vault divergence:** Editions remain explicit, and local sources coexist with addon sources in Colosseum's world/player architecture.

---

## E. Alternate-version handling

**Analogue strength:** Direct.

**Confirmed Jellyfin evidence:**  
`MediaSourceManager.cs` contains explicit alternate-version resume/source handling.

The current code states conceptually:

> for a video, multiple sources mean alternate versions.

**Vault adoption:** Preserve multiple playable local copies.

**Vault divergence:** Vault adds stronger edition-versus-quality-version semantics and never silently crosses edition boundaries.

---

## F. No direct Jellyfin analogue adopted

The following remain Colosseum-specific contracts:

- Vault as ownership control room instead of canonical media library;
- canonical publication into separate worlds;
- **Owned locally** versus **currently available**;
- Away/Missing/Changed distinction as an ownership product model;
- guided Attention;
- scoped learned repair rules;
- explicit confidence explanations;
- local-only Colosseum objects;
- user authority over automatic identity;
- local-first source choice combined with addon sources;
- preserving one physical ownership history through canonical relinking.

Execution agents should not search Jellyfin for a design authority that does not exist.

---

# 5.29 Discovery gates for planning

These are repository questions, not product questions.

They do not reopen the brainstorm.

Before implementation planning, the execution agent must verify:

1. **Current Vault-world publication path**  
   Confirm exactly how current Vault rows become Theatre/Tankoban/Biblio-visible state.

2. **Current VaultIndex schema/migrations**  
   Identify which existing columns already cover copy, identity, away, enrichment, and admission truth.

3. **Current `VaultIdentifier` provider coupling**  
   Map every direct catalog dependency before extracting a provider seam.

4. **Current resolver logic distribution**  
   Determine what lives in `VaultScanner`, `VaultKit`, media-specific helpers, and QML today.

5. **Current Attention/ceremony UI path**  
   Trace `VaultIdentity::pendingCeremonies()` through the current UI and persistence.

6. **Current local source/player integration**  
   Confirm how Vault-owned video currently becomes a playable source and where addon/local source policy belongs.

7. **Existing search integration**  
   Determine whether Vault facts already feed global/world search.

8. **Existing DLNA/cast infrastructure**  
   Inspect current `caststore`, streaming server, or related owners before designing any duplicate transport.

9. **Thread/database ownership**  
   Pin the SQLite connection ownership and existing background-worker conventions.

10. **Repository drift**  
    Re-pin Colosseum immediately before planning; this design is based on `3c553002`.

11. **VaultIndex migration rehearsal**  
    Any schema/data migration that restructures real ownership data must first be rehearsed against a copy of a representative real Vault database. Verify backup creation and rollback/recovery before touching the live library database.

No exact implementation path should be invented when one of these gates changes the owner.

---

# 5.30 Hard stop conditions

Planning or implementation must stop and return to design review if repository evidence shows any of the following:

1. Theatre/Tankoban/Biblio currently duplicate Vault ownership state in ways that would create two authoritative writers.
2. Local-source selection cannot represent edition boundaries without a product-visible behavioral change.
3. A required world cannot represent local-only objects without inventing a new ontology.
4. The current database cannot migrate ownership identity without destructive loss of existing progress/history.
5. DLNA requires Vault itself to become a long-running remote canonical server rather than a downstream exposure adapter.
6. The only feasible provider design would let provider-specific code decide canonical authority outside `VaultIdentifier`.
7. Repository drift materially invalidates the confirmed current-owner map.

A stop condition produces a new design question or discovery report, not an improvised architecture.

---

# 5.31 Section acceptance criteria

Section 5 is design-complete when an implementation planner can map the product requirements without inventing ownership.

The eventual implementation must preserve these observable/architectural contracts:

1. Filesystem observation, physical identity, media structure, canonical identity, and world publication are separate responsibilities.
2. `VaultWatcher` detects change but does not become canonical truth.
3. `VaultScanner` performs physical census without inventing canonical media identity.
4. `VaultIdentity` owns physical-copy reconciliation independently of canonical matching.
5. Fingerprinting escalates from cheap to strong evidence rather than hashing every large file by default.
6. `VaultIndex` stores physical ownership truth without duplicating complete world ontologies.
7. Deep analysis remains copy-scoped and can continue after canonical publication.
8. Canonical providers report evidence/candidates; `VaultIdentifier` owns confidence and authority policy.
9. Local metadata is modeled as evidence and user-selectable presentation authority.
10. Specialized resolvers interpret physical structure without owning final canonical IDs.
11. Attention cases are durable, evidence-backed, and separable from ordinary technical errors.
12. Scoped learned rules have provenance, inheritance, and narrower-scope precedence.
13. One canonical object can own several editions and several physical copies.
14. Editions and technical versions remain distinct.
15. Companions and Extras remain distinct.
16. The destination worlds consume a narrow ownership interface rather than querying raw Vault state.
17. Local physical copies can become player/reader sources without making Vault own playback policy.
18. Consumer and management search use one physical-ownership truth.
19. Rescan, re-identify, re-analyze, metadata refresh, and repair remain distinct operations.
20. Worker tasks cannot publish partial destructive truth or mutate authoritative UI/DB state unsafely.
21. Manual identity and locks outrank automatic refresh.
22. Local-only objects can later reconcile into canonical objects without deleting ownership history.
23. DLNA remains downstream of Vault ownership.
24. Jellyfin code remains reference evidence rather than a porting template.

---

# 5.32 Technical design conclusion

The technical shape should remain very simple to explain even if the backend becomes sophisticated:

> **Scanner tells us what is on disk.**
>
> **Identity tells us whether we have seen that physical copy before.**
>
> **Resolvers tell us how the files belong together.**
>
> **Providers tell us what canonical things might match.**
>
> **VaultIdentifier decides what we are actually willing to believe.**
>
> **VaultIndex remembers the physical ownership truth.**
>
> **Theatre, Tankoban, and Biblio consume that truth without surrendering ownership of their media objects.**

Jellyfin is useful here because it proves mature libraries need specialized resolvers, metadata-provider seams, rich media-source facts, and alternate-version handling.

Colosseum's unique move is where those mechanisms stop:

> **Vault never becomes the media universe. It becomes the layer that proves what parts of that universe the user physically owns.**

---

# Vault Design — Section 6: Acceptance Criteria, Non-Goals, Deferred Work, and Design Closure

## 6. Acceptance Criteria, Non-Goals, Deferred Work, and Design Closure

### Status

**Approved design — closure decisions locked; specification/planning follow separately.**

This section closes the Vault/Jellyfin Brotherhood design.

It does not add new implementation tasks.

It freezes:

- what the finished Vault experience must prove;
- what Vault must not become;
- what is intentionally postponed;
- which design decisions are now authoritative;
- which Jellyfin ideas are being adopted;
- which Jellyfin ideas are explicitly rejected;
- what the later specification must preserve.

---

# 6.1 Experience acceptance criteria

The design is successful when a user can point Vault at real-world media storage and Colosseum behaves as though it understands **ownership**, not merely files.

The following outcomes must be observable.

## 0. Existing Vault browsing capability survives

The current Vault door/shelves/folders/tiles/file-first browsing behavior remains first-class.

The visual design, navigation model, and layout may all be overhauled.

### Pass condition

The overhaul may change **how** Vault Browse looks and navigates, but does not silently remove the user's ability to browse the local collection through Vault's file-first surface. The new management control room is additive rather than a replacement.

---

## A. Adding storage

A user can add a folder or drive with a small root profile.

They are not forced through a server-style setup wizard.

The user can leave Vault while the first scan continues.

### Pass condition

The user understands:

> **Vault is learning what I own.**

Not:

> **I am configuring a media server.**

---

## B. Fast first usefulness

Vault performs a fast physical census first.

It does not require full deep analysis before useful media can appear.

### Pass condition

A large library begins becoming usable before every codec, subtitle, chapter, fingerprint, artwork source, and metadata field has been analyzed.

---

## C. Canonical publication stays clean

A provisional filename never becomes a fake Theatre/Tankoban/Biblio object merely because Vault saw the file.

### Pass condition

A media item enters its destination world only when:

- identity is **Certain**;
- identity is **Manual** because the user confirmed/chose it; or
- the user explicitly created a local-only object.

**Very likely remains unpublished** until more evidence makes it Certain or the user confirms it.

Ambiguous/conflicting files remain in Vault.

---

## D. Ownership appears in the real worlds

Once published, the destination world exposes ownership compactly.

Example:

> **Owned locally — 3 copies**

A good local source can become the default action.

Example:

> **Play — Local 4K Dolby Vision**

### Pass condition

The user does not have to browse Vault to consume owned media.

Vault provides ownership intelligence.

The worlds remain the media experience.

---

## E. Ownership and availability remain separate

If a configured storage root goes offline, the canonical object remains.

Example:

> **Owned locally — currently unavailable**

### Pass condition

Unplugging a drive does not:

- remove the media from its world;
- erase ownership;
- erase identity;
- erase progress;
- erase copy history;
- erase local presentation choices.

---

## F. Multiple physical copies are understood as copies

If several files represent the same canonical work/edition, Vault groups them.

Example:

```text
Alien theatrical
    4K Dolby Vision — Drive D:
    1080p — Drive E:
```

### Pass condition

The system does not invent multiple canonical movies merely because several local files exist.

---

## G. Editions remain meaningful

A Director's Cut is not merely a higher/lower-quality version of a theatrical cut.

### Pass condition

Automatic best-source selection never silently crosses a known edition boundary.

The user can intentionally choose another edition.

---

## H. Companions and Extras remain distinct

Vault understands that:

- subtitles;
- artwork;
- NFO;
- external audio;
- chapter sidecars

are companions.

And:

- trailers;
- deleted scenes;
- interviews;
- featurettes

are Extras.

### Pass condition

Companions do not appear as standalone canonical media or as Extras.

Extras appear on the canonical media page only when they exist.

---

## I. Anime/TV naming can be understood without a second ontology

Vault can understand naming such as:

- `S02E04`;
- `2x04`;
- absolute anime numbering;
- multi-episode files;
- specials;
- common anime release conventions.

### Pass condition

Vault determines what the physical file claims to contain.

Theatre remains the authority on where those episodes belong canonically.

---

## J. Identification uncertainty is visible

Vault uses semantic confidence states:

- Certain;
- Very likely;
- Ambiguous;
- Conflicting;
- Manual.

### Pass condition

Only **Certain** is automatically adopted. **Very likely** is presented as a strong suggested match but remains unpublished until confirmed or promoted by additional evidence.

A user can inspect why Vault made an automatic identity decision.

Vault does not rely on an unexplained percentage as the primary explanation.

---

## K. Human judgment is respected

When Vault cannot safely decide, it asks.

A manual identity becomes authoritative.

### Pass condition

Automatic refresh never silently overrides a Manual identity.

---

## L. Attention is a guided workflow

Needs Attention is not a folder full of errors.

Cases are grouped by shared problem where safe.

Example:

> **17 episodes need numbering help**

### Pass condition

The user sees:

- what is uncertain;
- what evidence Vault has;
- the recommended action;
- alternatives;
- whether the decision can safely apply to a group.

---

## M. Repeated repairs can become scoped knowledge

A repair can create a learned rule at an appropriate scope.

Example:

> `E:\Anime` uses absolute episode numbering.

### Pass condition

The rule:

- is visible;
- shows its scope;
- can be disabled/removed;
- does not become a hidden global assumption;
- can be overridden by a narrower scoped rule.

---

## N. Moves and renames preserve continuity

If the same physical copy moves to a new location and evidence is strong, Vault reconciles it.

### Pass condition

The user sees:

> **Moved**

rather than:

> deleted + newly added

when the relationship can be proven.

---

## O. Copying is not moving

If the original remains and another equivalent copy appears, Vault preserves both.

### Pass condition

The new file becomes another physical copy instead of replacing the original.

---

## P. Duplicate diagnosis is informative, not destructive

Vault may diagnose probable duplicate copies.

### Pass condition

The user can:

- inspect both copies;
- see why they appear equivalent;
- mark one as intentional backup;
- suppress the warning.

No normal repair silently deletes physical files.

---

## Q. File changes invalidate only the truth that changed

If a file changes materially in place, copy-specific technical facts are re-evaluated.

### Pass condition

Vault does not discard unrelated canonical ownership truth merely because a copy changed.

---

## R. Scans are interruption-safe

If scanning is cancelled, crashes, or loses a drive:

### Pass condition

The previously complete known state survives.

Partial new truth does not destructively replace complete old truth.

---

## S. Deep analysis is progressive

After identification/publication, Vault can continue learning:

- codec;
- resolution;
- HDR;
- audio tracks;
- subtitle tracks;
- chapters;
- duration;
- bitrate;
- file size;
- container;
- other physical facts.

### Pass condition

The user can use the media while deep analysis continues.

Opening an unanalyzed item can prioritize its analysis.

---

## T. Local metadata is respected without replacing canonical identity

Local NFO/embedded metadata/artwork can provide evidence.

Canonical media identity remains canonical.

### Pass condition

Local presentation becomes authoritative only through:

- explicit user choice; or
- an approved scoped rule.

---

## U. User-selected local overrides persist

A user can deliberately choose/lock local fields or artwork.

### Pass condition

Metadata refresh does not silently replace a locked value.

---

## V. Refresh operations remain distinct

The system must conceptually preserve:

- Rescan;
- Re-identify;
- Re-analyze;
- Refresh Metadata;
- Repair.

### Pass condition

Changing a poster does not require rescanning a 20 TB disk.

Improving a filename parser does not require deep technical re-analysis of unchanged files.

---

## W. Vault has a simple health answer

The user can quickly understand:

> **Am I good?**

### Pass condition

Overview distinguishes:

- healthy;
- background work;
- Attention required;
- storage away/degraded.

Background analysis is not presented as failure.

---

## X. Physical truth is progressively inspectable

A world page stays compact.

Vault can expose full copy detail.

### Pass condition

Theatre/Tankoban/Biblio never need to look like MediaInfo just because Vault knows detailed technical facts.

---

## Y. Consumer search and management search share one truth

Colosseum search can use consumer-useful ownership facts.

Vault search can use management facts.

### Pass condition

They do not maintain two independently authoritative ownership databases.

---

## Z. Local-first means local-first, not local-only

A good owned copy should normally win.

A bad/problematic local copy does not have to beat a clearly better source.

### Pass condition

The user can still explicitly choose addon/online sources.

Manual source choice wins.

---

## AA. Collection receives Vault ownership facts

The real Collection can consume Vault-owned facts such as:

- Owned locally;
- best local quality;
- local availability;
- storage/root context where useful.

### Pass condition

Collection can surface/filter those facts without becoming a second writer of physical ownership state.

---

## AB. Scheduled scans are supplementary

A scheduled reconciliation scan may improve coverage.

### Pass condition

Disabling or delaying the schedule does not make watcher/manual/root-return paths semantically incorrect. All paths converge on the same Vault census/reconciliation truth.

---


# 6.2 Local-only media acceptance criteria

Local-only objects exist for media that genuinely cannot be represented by an external canonical catalog.

They are a deliberate user action.

## Creation

The user may create a local-only object after Vault fails to find a suitable canonical identity.

### Pass condition

Vault never silently creates local-only canonical objects as a fallback for failed matching.

---

## Presentation

The object appears in the appropriate destination world.

It is visibly:

> **Local-only**

### Pass condition

The object participates in normal consumption without pretending it came from a canonical catalog.

---

## Later canonical linking

If a canonical object becomes available later, the user may link the local-only object.

### Pass condition

Linking preserves:

- progress;
- ownership;
- copies;
- editions;
- companions;
- Extras;
- local overrides;
- learned rules;
- history.

The operation does not feel like deleting the old object and creating a new one.

---

# 6.3 Root and storage acceptance criteria

## Root profile remains small

A newly added root only asks for the settings needed immediately.

Example:

- Automatic;
- Movies & TV;
- Manga & Comics;
- Books;
- watch automatically;
- include subfolders.

### Pass condition

Users are not forced to preconfigure every parser, metadata source, naming convention, artwork preference, or repair rule.

---

## Root availability is durable

Away roots remain configured until the user explicitly changes that.

### Pass condition

No time-based cleanup policy automatically forgets a root.

---

## Root removal is explicit

Removing a root must distinguish:

- stop managing this storage location while preserving ownership/history;
- deliberately forget its ownership records.

### Pass condition

A destructive forget action cannot be triggered by an ordinary disconnect timeout.

---

## Overlapping roots are safe

Vault detects accidental overlapping roots.

### Pass condition

The same physical files are not double-indexed because both a parent and child path were configured.

---

# 6.4 Attention and learned-rule acceptance criteria

## Attention cases are user-meaningful

Technical implementation errors do not automatically become user decisions.

### Pass condition

Attention is reserved for cases where human judgment can materially resolve uncertainty.

---

## Batch repair is evidence-bound

Vault may batch cases only when one decision genuinely applies to all of them.

### Pass condition

"Resolve all" cannot mean "guess all."

---

## Rules have provenance

Every learned rule can answer:

- what it does;
- where it applies;
- how it was created;
- what broader rule it overrides, if any.

### Pass condition

The user can understand why a rule is active.

---

## Removing a rule is non-destructive

Removing a rule stops future application.

### Pass condition

Existing library state does not instantly reshuffle without a deliberate re-evaluation action.

---

# 6.5 Accessibility acceptance criteria

Vault is information-dense, so accessibility must remain a design requirement.

## Keyboard

All primary workflows are keyboard-operable.

This includes:

- root management;
- search;
- filters;
- Attention decisions;
- batch repair;
- learned-rule management;
- ownership detail;
- copy detail.

---

## State semantics

Color is never the only indicator.

### Pass condition

A screen reader can distinguish:

- Away;
- Missing;
- Attention;
- Local-only;
- Locked;
- Analysis pending;
- Healthy.

---

## Progress

Background progress does not constantly steal focus or spam assistive technologies.

---

## Scaling

Important state and controls remain understandable at larger UI/text scaling.

---

# 6.6 DLNA acceptance criteria

DLNA is allowed.

But DLNA does not redefine Vault.

## Product boundary

Conceptually:

```text
Vault physical ownership
    ↓
DLNA exposure
```

### Pass condition

DLNA consumes Vault's physical ownership facts.

It does not become:

- a second canonical library;
- a second identity system;
- a remote ownership authority.

---

## Server-boundary safeguard

DLNA-related compatibility work may require delivery/transcoding mechanisms later.

### Pass condition

Those mechanics do not by themselves justify:

- multi-user Vault accounts;
- remote administration;
- Jellyfin-style server lifecycle;
- device-management architecture as Vault's core model.

---

# 6.7 Explicit non-goals

The following are **not** part of this Vault design.

## Vault is not a Jellyfin replacement server

Do not add because Jellyfin has them:

- multi-user library ownership;
- remote user accounts;
- remote administration;
- household permissions;
- server-side user policy;
- server-client canonical authority;
- web-admin architecture;
- general remote session control.

---

## Vault is not a fourth canonical media world

Do not create:

- a separate canonical movie universe;
- a separate canonical manga universe;
- a separate canonical book universe;
- a second canonical metadata graph.

Normal canonical-media browsing remains:

- Theatre;
- Tankoban;
- Biblio.

**This does not remove Vault's existing first-class file-first Browse surface.** Vault may continue to browse shelves/folders/files while the new control room manages ownership truth.

---

## Vault does not own playback policy

Vault reports truthful local-source facts.

Theatre/player/source-selection logic decides which source should be recommended.

---

## Vault does not automatically delete files

Duplicate diagnosis is allowed.

Automatic cleanup is not.

---

## Vault does not guess through serious ambiguity

Better automation does not mean pretending uncertainty disappeared.

---

## Vault does not require full hashing of every file

Fingerprinting should escalate only when cheaper evidence cannot safely settle the question.

---

## Vault does not make providers authoritative

Metadata providers return evidence/candidates.

They do not own final identity policy.

---

## Vault does not turn local metadata into automatic canonical truth

A local NFO can be strong evidence.

It does not silently replace a known canonical object.

---

## Vault does not expose every technical property everywhere

Technical depth belongs in Vault/copy detail.

Normal media pages remain media pages.

---

# 6.8 Deferred work

These ideas are intentionally outside the current design.

Deferred does not mean rejected forever.

## Music

Jellyfin has a mature music ontology.

Vault must not invent a music world merely because it can scan audio files.

**Deferred until Colosseum has a proper music world.**

---

## Photos

Jellyfin supports photo libraries.

Vault must not become the canonical photo product by accident.

**Deferred until Colosseum has a proper photo concept/world.**

---

## Full remote media server

Remote multi-user Jellyfin-style service architecture is outside this design.

---

## Remote administration

Not part of Vault.

---

## Device profiles as Vault ontology

DLNA compatibility may need device-specific delivery rules, but they remain delivery concerns.

---

## Automatic duplicate deletion / storage cleanup

Not part of the approved ownership-control-room model.

A future storage-cleanup feature would require its own design.

---

## Advanced file-organizer behavior

Vault is not being designed to automatically rename/reorganize the user's files.

A future optional organizer would require separate product approval.

---

## Arbitrary global learned heuristics

Scoped learning is approved.

Unbounded global behavior inferred from one user's repair is not.

---

# 6.9 Rejected directions

These alternatives were considered during brainstorming and are now rejected for this design.

## Rejected: Vault replacing Theatre/Tankoban/Biblio as a full canonical media library UI

Reason:

That would duplicate the worlds and create competing media ontologies.

**Not rejected:** Vault's existing first-class file-first shelf/folder browsing capability. That surface survives, even if its entire UI is overhauled.

---

## Rejected: Vault as an almost invisible settings backend

Reason:

The system needs a real place to manage:

- ownership;
- drives;
- Attention;
- duplicates;
- rules;
- copy detail;
- history.

---

## Rejected: every physical version visible as a separate canonical item

Reason:

Physical copies belong underneath one canonical object/edition where appropriate.

---

## Rejected: hide all source complexity

Reason:

A smart default is desirable, but users still need access to their local versions and addon alternatives.

---

## Rejected: local metadata always wins

Reason:

Local evidence is useful, but stale/bad sidecars must not silently replace canonical truth.

---

## Rejected: canonical metadata always ignores local presentation

Reason:

Users with curated local artwork/metadata need explicit control.

---

## Rejected: global learning from one repair

Reason:

One folder's naming convention must not become a machine-wide assumption.

---

## Rejected: full deep analysis before publication

Reason:

Large libraries should become useful quickly.

---

## Rejected: publish raw filesystem names immediately

Reason:

The canonical worlds should never show temporary garbage identities.

---

## Rejected: automatically forgetting long-offline storage

Reason:

Offline archive storage may still represent real ownership.

---

## Rejected: automatic duplicate cleanup

Reason:

Vault explains ownership; it does not decide which copies the user is allowed to keep.

---

# 6.10 Jellyfin-derived capabilities approved for adoption

The design deliberately adopts the **library-intelligence concepts** below.

These are not instructions to copy Jellyfin's architecture.

## Specialized media resolvers

**Jellyfin analogue strength:** Direct.

Use separate media-aware interpretation for:

- movies;
- TV;
- episode naming;
- books;
- other relevant media families.

### Colosseum translation

Resolvers produce physical structure claims.

They do not become canonical object owners.

---

## Multiple media sources / alternate versions

**Jellyfin analogue strength:** Direct.

### Colosseum translation

One canonical object/edition can own several physical local copies.

Local copies can coexist with addon sources.

---

## Extras awareness

**Jellyfin analogue strength:** Direct.

### Colosseum translation

Extras remain associated bonus media and appear on the canonical page only when present.

---

## Metadata provider separation

**Jellyfin analogue strength:** Direct.

### Colosseum translation

Identity sources implement a provider seam.

`VaultIdentifier` keeps final confidence/authority policy.

---

## Local metadata provider separation

**Jellyfin analogue strength:** Direct.

### Colosseum translation

NFO/embedded/local metadata becomes explicit evidence.

Local evidence does not automatically become final authority.

---

## Rich media-source technical facts

**Jellyfin analogue strength:** Direct.

### Colosseum translation

Technical facts belong to physical copies and inform source choice.

---

## Queryable library intelligence

**Jellyfin analogue strength:** Direct concept.

### Colosseum translation

Consumer and management search use shared ownership facts.

---

## Independent scan/refresh responsibilities

**Jellyfin analogue strength:** Direct concept.

### Colosseum translation

Rescan, identify, analyze, metadata refresh, and repair remain separate responsibilities under one simple user-facing Refresh entry point.

---

# 6.11 Colosseum-original design decisions

The following must not be weakened because Jellyfin does something differently.

## Vault as ownership control room

This is the core product boundary.

---

## Separate canonical worlds

Theatre/Tankoban/Biblio remain canonical media experiences.

---

## Guided Attention

Uncertainty becomes explainable, repairable work.

---

## Scoped learned rules

Vault can learn the user's collection without globalizing one repair.

---

## Ownership versus availability

A user can still own something whose drive is currently away.

---

## Explicit physical-copy history

Move/copy/reconciliation history belongs to Vault.

---

## Local-only media objects

Media without a catalog identity can still become intentional Colosseum objects.

---

## Canonical publication gate

Filesystem recognition and world publication remain distinct.

---

## Semantic confidence states with evidence

Vault explains decisions instead of hiding behind a score.

---

## Strong user authority

Manual decisions and locks outrank automatic matching.

---

## Local-first plus addon coexistence

Owned files become first-class sources without shutting out online/addon choices.

---

# 6.12 Locked product decision ledger

The Brotherhood brainstorm produced the following authoritative product decisions.

## Vault identity

- Vault is an **ownership control room**.
- Vault is not another media library.
- Theatre/Tankoban/Biblio remain the normal browsing/consumption worlds.
- Vault does not become a Jellyfin-style server.
- DLNA support is allowed as a downstream exposure capability.

## Physical ownership

- One canonical object can own several physical copies.
- Meaningful editions remain distinct from quality versions.
- Physical location is not identity.
- Missing storage is not deletion.
- Long-away roots are never automatically forgotten.
- Obvious moves/copies may reconcile automatically.
- Uncertain reconciliation enters Attention.
- Copy history is preserved.

## Identification

- Identification uses cautious semantic confidence tiers.
- **Certain** may auto-adopt and publish.
- **Very likely** is suggestion-only and remains unpublished until additional evidence makes it Certain or the user confirms it.
- Ambiguous/Conflicting require Attention.
- Uncertainty remains visible.
- Manual identity is authoritative.
- Local metadata can inform identification.
- Canonical identity remains canonical.
- Explicit local-only objects are allowed when catalogs cannot represent the media.

## Analysis

- Scanning is two-stage.
- Useful identification/publication comes first.
- Deep technical analysis continues afterward.
- Deep details belong to the physical copy.

## Media relationships

- Companions and Extras are distinct.
- Extras appear on the media page only when present.
- TV/anime share a physical episode model with specialized parsers.
- Theatre remains canonical episode authority.

## Attention and learning

- Needs Attention is a guided repair queue.
- Safe batch decisions are allowed.
- Repeated repair decisions may create scoped learned rules.
- Learned rules remain visible/removable.
- Folder scope is preferred over global assumptions.

## Presentation

- Vault's existing file-first Browse surface remains first-class.
- The Vault UI may be overhauled, but Browse capability is preserved.
- The new Overview/Storage/Attention/Owned Media/Rules/History control room is the management layer, not a replacement for Browse.
- World pages use progressive detail.
- Vault exposes full physical truth.
- “Owned locally” is compact but first-class.
- Ownership and availability remain separate.
- Consumer search, Collection, and Vault management search use shared Vault-owned facts.

## Playback/source behavior

- One smart local default is preferred.
- All local copies remain available underneath.
- A good local source is normally preferred.
- Quality sanity checks prevent blind local preference.
- Explicit user source choice wins.
- Editions are never silently substituted as ordinary versions.

## Metadata/artwork

- Canonical presentation is default.
- Local presentation can be explicitly preferred.
- Scoped rules may prefer local presentation.
- Per-field/user locks persist across refresh.

## Duplicates

- Vault diagnoses duplicates.
- Vault does not automatically clean/delete them.

## Refresh behavior

- One simple normal Refresh exists.
- Advanced operations remain distinct:
  - Rescan;
  - Re-identify;
  - Re-analyze;
  - Refresh Metadata;
  - Repair.
- Scheduled scans are useful as a supplementary reliability/convenience layer but are not fundamental to correctness.

## Health

- Vault provides a simple health summary.
- Detail increases progressively through Attention and diagnostics.

---

# 6.13 Constraints that all later work must respect

The later specification and implementation plan must preserve these constraints.

1. **One ownership authority for physical media:** Vault.
2. **One canonical authority per media world:** Theatre/Tankoban/Biblio.
3. **No second canonical media graph inside Vault.**
4. **No silent ambiguity resolution where evidence is materially conflicting.**
5. **No partial destructive scan publication.**
6. **No automatic forgetting of ownership due to storage absence.**
7. **No global learned behavior from a local repair without explicit user choice.**
8. **No automatic duplicate deletion.**
9. **No automatic edition substitution.**
10. **No local metadata automatically overruling explicit canonical/user authority.**
11. **No provider-specific identity authority outside the Vault identity policy owner.**
12. **No Jellyfin server architecture imported merely because Jellyfin is the control case.**
13. **No implementation path that requires Vault to become a multi-user server to support DLNA.**
14. **No raw filesystem/provisional identity leaking into canonical worlds.**
15. **No user-visible success claim until the corresponding durable state is actually committed.**
16. **No automatic publication of Very likely identity suggestions.**
17. **No UI overhaul may remove Vault's first-class file-first Browse capability unless a later explicit product decision changes that law.**
18. **No destructive VaultIndex/schema migration reaches the live library before rehearsal on a representative database copy with backup/rollback verified.**

---

# 6.14 Reference-atlas contract for the final specification

The final durable specification must include a Jellyfin reference atlas for every major borrowed capability.

Each entry should contain:

```text
Capability
Analogue strength: Direct / Adjacent / None
Pinned Jellyfin source path
Relevant symbol
Minimal source excerpt where useful
Plain-language mechanism
What Jellyfin proves
What Vault adopts
What Vault changes/rejects
Colosseum-specific contract
```

### Copyright/practical constraint

Source excerpts stay minimal but **dense enough to prove the important seams**.

Where one Jellyfin capability has several relevant mechanics, the atlas may include several small excerpts instead of one large block—for example resolver grouping, alternate versions, extras, absolute episode parsing, ignore rules, provider search/fetch, stream probing, and external companions.

The agent receives:

- exact pinned path;
- exact symbol where known;
- one or more small verified excerpts where they materially improve understanding;
- enough code to prove the mechanism;
- detailed paraphrase;
- explicit **What Vault adopts / What Vault rejects** translation.

It does not need large Jellyfin source dumps inside the spec.

---

## Jellyfin verification note

For this revision, the uploaded `jellyfin-master.zip` was inspected directly. Newly added excerpts from the following files were blob-matched to the pinned Jellyfin commit `4e2fc33a61391ded79fc4353f0fd9090952bd130`:

- `Emby.Server.Implementations/Library/Resolvers/Books/BookResolver.cs`
- `Emby.Naming/TV/EpisodeResolver.cs`
- `Emby.Server.Implementations/Library/CoreResolutionIgnoreRule.cs`
- `MediaBrowser.Providers/MediaInfo/FFProbeVideoInfo.cs`
- `MediaBrowser.Controller/Providers/ILocalImageProvider.cs`

The existing movie/TV resolver, provider, library-manager, and media-source references in this design were also inspected against the same pinned commit during design research.

This is **source verification**, not runtime verification of Jellyfin behavior.

---

# 6.15 Translation rule for the execution agent

The execution agent must read every Jellyfin reference as:

> **This is evidence that the problem exists and one mature system has solved it this way.**

Not:

> **Recreate this C# class in C++.**

The coding agent must preserve Colosseum's existing ownership seams where they are sound.

A Jellyfin structure should only influence Colosseum structure when:

- the underlying responsibility is genuinely the same;
- the current Colosseum repository has no stronger existing owner;
- the change does not violate the locked Vault boundary.

---

# 6.16 Design verification pass

This design has been checked against the Brotherhood brainstorming requirements.

## First use

Covered:

- add storage;
- small root profile;
- fast census;
- progressive background work.

## Normal use

Covered:

- automatic watching;
- ownership publication;
- local-first source choice;
- consumer-world integration.

## Interruption

Covered:

- app exit;
- cancelled scan;
- root disappearance;
- provider outage;
- changed files.

## Recovery

Covered:

- root return;
- move reconciliation;
- ambiguous repair;
- learned rules;
- local-only canonical linking.

## Completion

Covered:

- healthy Vault;
- media published into the correct world;
- deep analysis completion;
- zero Attention as a valid steady state.

## Control

Covered:

- Refresh;
- advanced operations;
- root management;
- Attention;
- learned rules;
- local overrides;
- source selection.

## Feedback

Covered:

- health;
- progress;
- ownership availability;
- evidence explanations;
- history;
- diagnostics.

## Accessibility

Covered:

- keyboard;
- semantic state;
- color independence;
- reduced-motion consideration;
- scalable dense information.

## Integration

Covered:

- Theatre;
- Tankoban;
- Biblio;
- global search;
- local source selection;
- DLNA boundary.

---

# 6.17 Open product questions

**None remain that are consequential enough to block specification.**

Repository-specific technical questions remain discovery gates for specification/planning, but they do not reopen the product brainstorm unless they expose a contradiction with a locked requirement.

---

# 6.18 Deferred product questions

The following intentionally remain outside this design:

- What is Colosseum's future music world?
- What is Colosseum's future photo world?
- Should Vault ever gain a separate file-organizer feature?
- Should Colosseum ever support remote multi-user media serving?
- Should duplicate diagnosis ever grow into an explicit storage-cleanup product?
- Should DLNA later support compatibility transcoding, and if so where should that delivery system live?

Each requires its own design if pursued.

---

# 6.19 Final design statement

Vault should become one of the deepest parts of Colosseum without becoming another media application.

Its job is simple to say:

> **Vault knows what you physically own.**

That means it understands:

- where the files are;
- whether the storage is available;
- whether a file moved or changed;
- which files belong together;
- which are copies;
- which are editions;
- which are companions;
- which are Extras;
- what technical properties each copy has;
- which canonical media object the package belongs to;
- why Vault believes that identity;
- what is uncertain;
- what rules the user has taught it;
- what history that physical ownership has.

The destination worlds then do what they are already supposed to do:

> **Theatre knows the movie/show.**  
> **Tankoban knows the manga/comic.**  
> **Biblio knows the book/audiobook.**

Vault simply lets those worlds say something they could not say reliably before:

> **You own this. Here is the best copy you have. And if something about your collection is weird, Colosseum can explain it instead of guessing.**

That is the final product design.
