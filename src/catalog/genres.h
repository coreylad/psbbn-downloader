#ifndef GENRES_H
#define GENRES_H

/* ── Genre identifiers ───────────────────────────────────────────────────── */
typedef enum {
    GENRE_ALL = 0,   /* virtual "All Games" genre — no filter applied       */
    GENRE_ACTION,
    GENRE_RPG,
    GENRE_SPORTS,
    GENRE_RACING,
    GENRE_SHOOTER,
    GENRE_FIGHTING,
    GENRE_ADVENTURE,
    GENRE_PUZZLE,
    GENRE_SIMULATION,
    GENRE_STRATEGY,
    GENRE_HORROR,
    GENRE_PLATFORM,
    GENRE_MUSIC,
    GENRE_OTHER,
    GENRE_COUNT
} Genre;

/* ── Display label per genre ─────────────────────────────────────────────── */
static const char * const GENRE_NAMES[GENRE_COUNT] = {
    [GENRE_ALL]        = "All Games",
    [GENRE_ACTION]     = "Action",
    [GENRE_RPG]        = "RPG",
    [GENRE_SPORTS]     = "Sports",
    [GENRE_RACING]     = "Racing",
    [GENRE_SHOOTER]    = "Shooter",
    [GENRE_FIGHTING]   = "Fighting",
    [GENRE_ADVENTURE]  = "Adventure",
    [GENRE_PUZZLE]     = "Puzzle",
    [GENRE_SIMULATION] = "Simulation",
    [GENRE_STRATEGY]   = "Strategy",
    [GENRE_HORROR]     = "Horror",
    [GENRE_PLATFORM]   = "Platform",
    [GENRE_MUSIC]      = "Music / Rhythm",
    [GENRE_OTHER]      = "Other",
};

/* ── Archive.org subject keywords mapped to each genre ──────────────────── */
/* Used when building the search query sent to archive.org advancedsearch.  */
static const char * const GENRE_SEARCH_TERMS[GENRE_COUNT] = {
    [GENRE_ALL]        = "",
    [GENRE_ACTION]     = "action",
    [GENRE_RPG]        = "role-playing",
    [GENRE_SPORTS]     = "sports",
    [GENRE_RACING]     = "racing",
    [GENRE_SHOOTER]    = "shooter",
    [GENRE_FIGHTING]   = "fighting",
    [GENRE_ADVENTURE]  = "adventure",
    [GENRE_PUZZLE]     = "puzzle",
    [GENRE_SIMULATION] = "simulation",
    [GENRE_STRATEGY]   = "strategy",
    [GENRE_HORROR]     = "horror",
    [GENRE_PLATFORM]   = "platform",
    [GENRE_MUSIC]      = "music",
    [GENRE_OTHER]      = "",
};

/* ── Accent colour per genre (R, G, B) — used by home screen tiles ───────── */
typedef struct { unsigned char r, g, b; } GenreColour;
static const GenreColour GENRE_COLOURS[GENRE_COUNT] = {
    [GENRE_ALL]        = {0x00, 0xB4, 0xD8},
    [GENRE_ACTION]     = {0xE5, 0x39, 0x35},
    [GENRE_RPG]        = {0x7B, 0x1F, 0xA2},
    [GENRE_SPORTS]     = {0x26, 0x8B, 0x26},
    [GENRE_RACING]     = {0xFF, 0x8C, 0x00},
    [GENRE_SHOOTER]    = {0xB7, 0x1C, 0x1C},
    [GENRE_FIGHTING]   = {0x1A, 0x23, 0x7E},
    [GENRE_ADVENTURE]  = {0x00, 0x60, 0x64},
    [GENRE_PUZZLE]     = {0xF5, 0x6A, 0x00},
    [GENRE_SIMULATION] = {0x00, 0x79, 0x7F},
    [GENRE_STRATEGY]   = {0x37, 0x47, 0x4F},
    [GENRE_HORROR]     = {0x4A, 0x00, 0x00},
    [GENRE_PLATFORM]   = {0xFF, 0xCA, 0x28},
    [GENRE_MUSIC]      = {0xC2, 0x18, 0x5B},
    [GENRE_OTHER]      = {0x55, 0x55, 0x55},
};

/* ── Helper: map a free-text subject tag to a Genre enum ─────────────────── */
Genre genre_from_subject(const char *subject);

#endif /* GENRES_H */
