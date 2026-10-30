package account

import (
	"bytes"
	"encoding/base64"
	"errors"
	"image"
	"image/jpeg"
	"image/png"
	"regexp"
	"strings"
	"unicode"
	"unicode/utf8"

	"golang.org/x/text/unicode/norm"
)

var (
	ErrCollectionInvalid            = errors.New("invalid collection publication")
	ErrCollectionNotFound           = errors.New("collection publication not found")
	ErrCollectionConflict           = errors.New("collection revision conflict")
	ErrCollectionImmutable          = errors.New("gift edition is immutable")
	ErrCollectionIdempotency        = errors.New("collection operation conflict")
	ErrCollectionPassphrase         = errors.New("collection passphrase invalid")
	ErrCollectionPassphraseRequired = errors.New("collection passphrase required")
	ErrCollectionLimit              = errors.New("collection publication limit")
	collectionNamespace             = regexp.MustCompile(`^[a-z][a-z0-9._-]{0,63}$`)
	collectionIdentity              = regexp.MustCompile(`^[a-zA-Z0-9][a-zA-Z0-9._:-]{0,255}$`)
	collectionShareID               = regexp.MustCompile(`^[a-zA-Z0-9_-]{43}$`)
	collectionCanonicalID           = regexp.MustCompile(`^ct1:[0-9a-f-]{36}$`)
	collectionIMDbID                = regexp.MustCompile(`^tt[0-9]{1,20}$`)
)

const CollectionMaxBodyBytes int64 = 768 * 1024

type CollectionPresentation struct {
	Preset     string `json:"preset"`
	Dedication string `json:"dedication"`
	Signature  string `json:"signature"`
	CoverData  string `json:"cover_data"`
}
type CollectionMember struct {
	World             string   `json:"world"`
	Kind              string   `json:"kind"`
	Source            string   `json:"source"`
	SourceID          string   `json:"source_id"`
	EditionID         string   `json:"edition_id"`
	CanonicalID       string   `json:"canonical_id"`
	CanonicalSourceID string   `json:"canonical_source_id"`
	AlbumSourceID     string   `json:"album_source_id"`
	AlbumBrowseID     string   `json:"album_browse_id"`
	ComicPostIDs      []string `json:"comic_post_ids"`
	Title             string   `json:"title"`
	Creator           string   `json:"creator"`
	Year              string   `json:"year"`
	Note              string   `json:"note"`
}
type CollectionSnapshot struct {
	SchemaVersion int                    `json:"schema_version"`
	Name          string                 `json:"name"`
	Description   string                 `json:"description"`
	Presentation  CollectionPresentation `json:"presentation"`
	Members       []CollectionMember     `json:"members"`
}

func collectionText(value string, max int) bool {
	if !utf8.ValidString(value) {
		return false
	}
	units := 0
	for _, r := range value {
		units++
		if r > 0xffff {
			units++
		}
		if units > max {
			return false
		}
		if unicode.IsControl(r) && r != '\n' && r != '\r' && r != '\t' {
			return false
		}
	}
	return true
}
func validPublicIdentity(value string, optional bool) bool {
	if value == "" {
		return optional
	}
	if len(value) >= 2 && value[1] == ':' {
		return false
	}
	if !collectionIdentity.MatchString(value) {
		return false
	}
	lower := strings.ToLower(value)
	for _, prefix := range []string{"file:", "http:", "https:", "content:", "data:", "magnet:", "ftp:"} {
		if strings.HasPrefix(lower, prefix) {
			return false
		}
	}
	return true
}
func ValidateCollectionSnapshot(s CollectionSnapshot) error {
	if s.SchemaVersion != 1 || strings.TrimSpace(s.Name) == "" || s.Name != norm.NFC.String(strings.Join(strings.Fields(s.Name), " ")) || !collectionText(s.Name, 100) || !collectionText(s.Description, 300) || len(s.Members) > 500 || s.Members == nil {
		return ErrCollectionInvalid
	}
	p := s.Presentation
	if p.Preset != "midnight" && p.Preset != "paper" && p.Preset != "sea" {
		return ErrCollectionInvalid
	}
	if !collectionText(p.Dedication, 2000) || !collectionText(p.Signature, 100) {
		return ErrCollectionInvalid
	}
	if p.CoverData != "" {
		prefix, raw, ok := strings.Cut(p.CoverData, ",")
		if !ok || (prefix != "data:image/png;base64" && prefix != "data:image/jpeg;base64") || len(raw) > 350000 {
			return ErrCollectionInvalid
		}
		b, err := base64.StdEncoding.Strict().DecodeString(raw)
		if err != nil || len(b) > 256*1024 {
			return ErrCollectionInvalid
		}
		cfg, format, err := image.DecodeConfig(bytes.NewReader(b))
		if err != nil || cfg.Width < 1 || cfg.Height < 1 || cfg.Width > 2048 || cfg.Height > 2048 || cfg.Width*cfg.Height > 4*1024*1024 {
			return ErrCollectionInvalid
		}
		if (format == "png" && prefix != "data:image/png;base64") || (format == "jpeg" && prefix != "data:image/jpeg;base64") || (format != "png" && format != "jpeg") {
			return ErrCollectionInvalid
		}
		// Full bounded decode rejects truncated files. Viewers never execute these
		// bytes: only PNG/JPEG data URLs are accepted, with no external fetch.
		if _, _, err = image.Decode(bytes.NewReader(b)); err != nil {
			return ErrCollectionInvalid
		}
	}
	seen := map[string]bool{}
	for _, m := range s.Members {
		switch m.World {
		case "theatre", "tankoban", "biblio", "vinyl", "vault":
		default:
			return ErrCollectionInvalid
		}
		switch m.Kind {
		case "movie", "film", "series", "manga", "comic", "book", "album", "song", "artist", "other":
		default:
			return ErrCollectionInvalid
		}
		if !collectionNamespace.MatchString(m.Source) || !validPublicIdentity(m.SourceID, false) || !validPublicIdentity(m.EditionID, true) || !collectionText(m.Title, 300) || !collectionText(m.Creator, 200) || !collectionText(m.Year, 32) || !collectionText(m.Note, 500) {
			return ErrCollectionInvalid
		}
		if m.CanonicalID != "" && (m.CanonicalID == "ct1:00000000-0000-0000-0000-000000000000" || !collectionCanonicalID.MatchString(m.CanonicalID) || !IsUUID(strings.TrimPrefix(m.CanonicalID, "ct1:"))) {
			return ErrCollectionInvalid
		}
		if m.CanonicalSourceID != "" && (m.World != "theatre" || !collectionIMDbID.MatchString(m.CanonicalSourceID)) {
			return ErrCollectionInvalid
		}
		if !validPublicIdentity(m.AlbumSourceID, true) || !validPublicIdentity(m.AlbumBrowseID, true) || (m.World != "vinyl" && (m.AlbumSourceID != "" || m.AlbumBrowseID != "")) {
			return ErrCollectionInvalid
		}
		if len(m.ComicPostIDs) > 500 || ((m.Source != "getcomics" && m.Source != "gcd") && len(m.ComicPostIDs) > 0) {
			return ErrCollectionInvalid
		}
		posts := map[string]bool{}
		for _, id := range m.ComicPostIDs {
			if len(id) < 1 || len(id) > 20 || id[0] == '0' || posts[id] {
				return ErrCollectionInvalid
			}
			for _, r := range id {
				if r < '0' || r > '9' {
					return ErrCollectionInvalid
				}
			}
			posts[id] = true
		}
		key := m.World + "\x00" + m.Kind + "\x00" + m.Source + "\x00" + m.SourceID + "\x00" + m.EditionID
		if seen[key] {
			return ErrCollectionInvalid
		}
		seen[key] = true
		if m.CanonicalID != "" {
			canonicalKey := "canonical\x00" + m.CanonicalID + "\x00" + m.EditionID
			if seen[canonicalKey] {
				return ErrCollectionInvalid
			}
			seen[canonicalKey] = true
		}
	}
	return nil
}

// Republish only decoded pixels, stripping EXIF and ancillary metadata from
// the user-selected image. The resulting permitted asset is the gift edition.
func sanitizeCollectionSnapshot(s CollectionSnapshot) (CollectionSnapshot, error) {
	s.Members = append([]CollectionMember{}, s.Members...)
	for i := range s.Members {
		if s.Members[i].ComicPostIDs == nil {
			s.Members[i].ComicPostIDs = []string{}
		}
	}
	if s.Presentation.CoverData == "" {
		return s, nil
	}
	prefix, raw, _ := strings.Cut(s.Presentation.CoverData, ",")
	b, err := base64.StdEncoding.Strict().DecodeString(raw)
	if err != nil {
		return s, ErrCollectionInvalid
	}
	img, format, err := image.Decode(bytes.NewReader(b))
	if err != nil {
		return s, ErrCollectionInvalid
	}
	var out bytes.Buffer
	if format == "jpeg" {
		err = jpeg.Encode(&out, img, &jpeg.Options{Quality: 85})
	} else {
		err = png.Encode(&out, img)
	}
	if err != nil || out.Len() > 256*1024 {
		return s, ErrCollectionInvalid
	}
	s.Presentation.CoverData = prefix + "," + base64.StdEncoding.EncodeToString(out.Bytes())
	return s, nil
}
