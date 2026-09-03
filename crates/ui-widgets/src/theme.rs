//! Design tokens for the widget kit.
//!
//! Every value here is a frozen contract number ported from the QML design
//! language (`qml/Theme.qml` and `qml/CatalogueVisualMetrics.js`, gallery
//! profile). Widgets read from these modules instead of keeping local copies,
//! so the skin has one source of truth. Where the QML spells a color as a hex
//! or `Qt.rgba(…)` literal that cannot be written as a `const` directly, the
//! equivalent `Hsla` is reproduced here with the source value in the doc
//! comment.

/// Color tokens. Gold is a SPARING accent (active/focus/progress/primary CTA);
/// the ink ramp is the body text; glass edge/tint/hi are white-alpha overlays
/// on the dark stage.
pub mod colors {
    use gpui::Hsla;

    /// Accent — `#f0c44a`. Active pill, focus ring, progress, primary CTA.
    pub const GOLD: Hsla = Hsla {
        h: 0.122490,
        s: 0.846939,
        l: 0.615686,
        a: 1.0,
    };

    /// Hover/focus halo — `rgba(240, 196, 74, 0.55)` (Bookshelf gallery edge).
    pub const GOLD_EDGE: Hsla = Hsla {
        h: 0.122490,
        s: 0.846939,
        l: 0.615686,
        a: 0.55,
    };

    /// Text on gold surfaces — `#1a1408` (CarouselSlide primary button).
    pub const ON_GOLD: Hsla = Hsla {
        h: 0.111111,
        s: 0.529412,
        l: 0.066667,
        a: 1.0,
    };

    /// Body ink — `#f7f7f5`.
    pub const INK: Hsla = Hsla {
        h: 0.166667,
        s: 0.111111,
        l: 0.964706,
        a: 1.0,
    };

    /// Dimmed ink — `#c9c8d0`.
    pub const INK_DIM: Hsla = Hsla {
        h: 0.687500,
        s: 0.078431,
        l: 0.800000,
        a: 1.0,
    };

    /// Dimmest ink — `#9a99a5`.
    pub const INK_DIMMER: Hsla = Hsla {
        h: 0.680556,
        s: 0.062500,
        l: 0.623529,
        a: 1.0,
    };

    /// Glass edge — `rgba(1, 1, 1, 0.18)`.
    pub const EDGE: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 1.0,
        a: 0.18,
    };

    /// Resting poster edge — `rgba(1, 1, 1, 0.08)` (Bookshelf resting edge).
    pub const EDGE_REST: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 1.0,
        a: 0.08,
    };

    /// Glass tint — `rgba(1, 1, 1, 0.10)`.
    pub const GLASS_TINT: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 1.0,
        a: 0.10,
    };

    /// Glass highlight — `rgba(1, 1, 1, 0.14)`.
    pub const GLASS_HI: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 1.0,
        a: 0.14,
    };

    /// Dark stage — the app's global background wash (matches the GPUI shell's
    /// `hsla(0.6, 0.08, 0.08, 1.0)`).
    pub const STAGE: Hsla = Hsla {
        h: 0.6,
        s: 0.08,
        l: 0.08,
        a: 1.0,
    };

    /// Deeper stage inset (player pane) — `hsla(0.6, 0.05, 0.05, 1.0)`.
    pub const STAGE_DEEP: Hsla = Hsla {
        h: 0.6,
        s: 0.05,
        l: 0.05,
        a: 1.0,
    };

    /// Biblio wash top — `#0c0f18`.
    pub const BIBLIO_WASH_TOP: Hsla = Hsla {
        h: 0.625000,
        s: 0.333333,
        l: 0.070588,
        a: 1.0,
    };

    /// Biblio wash bottom — `#06070b`.
    pub const BIBLIO_WASH_BOTTOM: Hsla = Hsla {
        h: 0.633333,
        s: 0.294118,
        l: 0.033333,
        a: 1.0,
    };

    /// Carousel slide gradient start — `#241433`.
    pub const SLIDE_C1: Hsla = Hsla {
        h: 0.752688,
        s: 0.436620,
        l: 0.139216,
        a: 1.0,
    };

    /// Carousel slide gradient end — `#120b1a`.
    pub const SLIDE_C2: Hsla = Hsla {
        h: 0.744444,
        s: 0.405405,
        l: 0.072549,
        a: 1.0,
    };

    /// Cover tint start — `#532f49` (Bookshelf manga fan placeholder).
    pub const COVER_C1: Hsla = Hsla {
        h: 0.879630,
        s: 0.276923,
        l: 0.254902,
        a: 1.0,
    };

    /// Cover tint end — `#1d121b`.
    pub const COVER_C2: Hsla = Hsla {
        h: 0.863636,
        s: 0.234043,
        l: 0.092157,
        a: 1.0,
    };

    /// Classic card scrim deep — `#06050c` (combine with
    /// [`Hsla::alpha`](gpui::Hsla::alpha) for the scrim gradient stops).
    pub const SCRIM_DEEP: Hsla = Hsla {
        h: 0.690476,
        s: 0.411765,
        l: 0.033333,
        a: 1.0,
    };

    /// Gallery card scrim deep — `#040508`.
    pub const SCRIM_GALLERY_DEEP: Hsla = Hsla {
        h: 0.625000,
        s: 0.333333,
        l: 0.023529,
        a: 1.0,
    };

    /// Glass adaptive scrim — `rgba(0, 0, 0, 0.14)` (`Glass.qml` `scrim`).
    /// Keeps text legible over any wallpaper; layered under panel content.
    pub const GLASS_SCRIM: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 0.0,
        a: 0.14,
    };

    /// Hairline top-edge highlight — `Glass.qml`'s bright edge intent
    /// (`glassHi` value reused as a 1px top hairline on glass chrome).
    pub const EDGE_HI: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 1.0,
        a: 0.14,
    };

    /// Focus halo (outer ring) — `rgba(240, 196, 74, 0.18)`
    /// (`CataloguePosterCard.qml` double gold focus halo).
    pub const GOLD_HALO: Hsla = Hsla {
        h: 0.122490,
        s: 0.846939,
        l: 0.615686,
        a: 0.18,
    };

    /// Chrome scrim top stop — `rgba(0, 0, 0, 0.55)` (`ChromeScrim.qml`).
    pub const SCRIM_CHROME: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 0.0,
        a: 0.55,
    };

    /// Detail hero bottom scrim — `rgba(0, 0, 0, 0.92)`
    /// (`TheatreSeries.qml` banner bottom stop).
    pub const SCRIM_HERO_BOTTOM: Hsla = Hsla {
        h: 0.0,
        s: 0.0,
        l: 0.0,
        a: 0.92,
    };
}

/// Spacing tokens (gallery profile). These are [`Pixels`], so consumers can
/// drop them straight into `w(…)`, `gap(…)`, `px(…)`-style setters.
pub mod spacing {
    use gpui::{px, Pixels};

    /// App layout margin — `Theme.qml` `margin`.
    pub const MARGIN: Pixels = px(54.0);

    /// Poster width — `CatalogueVisualMetrics.gallery.posterWidth`.
    pub const POSTER_WIDTH: Pixels = px(148.0);

    /// Poster height — `posterWidth × posterRatio` (148 × 1.5).
    pub const POSTER_HEIGHT: Pixels = px(222.0);

    /// Horizontal gap between cards — `gallery.cardGap`.
    pub const CARD_GAP: Pixels = px(20.0);

    /// Gap between shelves/rails — `gallery.shelfGap`.
    pub const SHELF_GAP: Pixels = px(46.0);

    /// Gap between a rail header and its row — `gallery.headerGap`.
    pub const HEADER_GAP: Pixels = px(18.0);

    /// Gap between a poster plane and its title — the gallery title top gap.
    pub const TITLE_GAP: Pixels = px(10.0);

    /// Reserved two-line title measure — `gallery.titleMinHeight`.
    pub const TITLE_MIN_HEIGHT: Pixels = px(35.0);
}

/// Type-scale tokens (port of `Theme.qml` + the QML widget font sizes).
/// GPUI ships only the system font face in this POC, so the QML `display`
/// (Fraunces) serif is approximated with the same sizes in the system face.
pub mod typography {
    use gpui::{px, Pixels};

    /// Hero display title — `TheatreSeries.qml` / `TheatreCinemaHero.qml`.
    pub const HERO_TITLE_PX: Pixels = px(64.0);

    /// Rail/section header — `WidgetHeader.qml` (22px Fraunces).
    pub const RAIL_TITLE_PX: Pixels = px(22.0);

    /// Rail sub-caption — `WidgetHeader.qml` `sub`.
    pub const RAIL_SUB_PX: Pixels = px(12.0);

    /// Body / description text.
    pub const BODY_PX: Pixels = px(15.0);

    /// Secondary body text.
    pub const BODY_SM_PX: Pixels = px(13.0);

    /// Meta line (year · genres · runtime).
    pub const META_PX: Pixels = px(14.0);

    /// Eyebrow / kicker (`TheatreSeries.qml` gold hero kicker).
    pub const EYEBROW_PX: Pixels = px(11.0);

    /// Primary CTA button label.
    pub const BUTTON_PX: Pixels = px(14.0);

    /// Poster title (gallery profile).
    pub const POSTER_TITLE_PX: Pixels = px(13.0);

    /// Nav-rail item label.
    pub const NAV_PX: Pixels = px(14.0);
}

/// Corner-radius tokens. These are [`Pixels`], for `rounded(…)`.
pub mod radius {
    use gpui::{px, Pixels};

    /// Poster corner radius — `gallery.posterRadius`.
    pub const POSTER: Pixels = px(12.0);

    /// Classic card corner radius — `classic.posterRadius`.
    pub const CARD: Pixels = px(8.0);

    /// Carousel slide radius — `CarouselSlide.qml`.
    pub const SLIDE: Pixels = px(20.0);

    /// Glass panel radius — `Bookshelf.qml`.
    pub const PANEL: Pixels = px(18.0);

    /// Primary CTA / pill radius — `TheatreSeries.qml` Watch button.
    pub const BUTTON: Pixels = px(11.0);
}
