package account

import (
	"encoding/json"
	"strconv"
	"strings"
)

// JSON numbers are bounded by the sync request size, but an exponent can
// still ask an arbitrary-precision parser to allocate an enormous value. The
// materialized sync fields are int64, so a small bounded decimal parser is
// sufficient and keeps admission work proportional to the token size.
const maxSyncIntegerTokenLength = 128

func syncIntegerNumber(value any) (int64, bool) {
	number, ok := value.(json.Number)
	if !ok {
		return 0, false
	}
	return parseSyncIntegerToken(string(number))
}

func parseSyncIntegerToken(token string) (int64, bool) {
	if token == "" || len(token) > maxSyncIntegerTokenLength {
		return 0, false
	}

	negative := false
	if token[0] == '-' {
		negative = true
		token = token[1:]
	} else if token[0] == '+' {
		return 0, false
	}
	if token == "" {
		return 0, false
	}

	exponent := 0
	if index := strings.IndexAny(token, "eE"); index >= 0 {
		if strings.IndexAny(token[index+1:], "eE") >= 0 {
			return 0, false
		}
		exponentText := token[index+1:]
		if exponentText == "" || len(exponentText) > 3 {
			return 0, false
		}
		parsedExponent, err := strconv.Atoi(exponentText)
		if err != nil || parsedExponent < -19 || parsedExponent > 19 {
			return 0, false
		}
		exponent = parsedExponent
		token = token[:index]
	}

	dot := strings.IndexByte(token, '.')
	if dot >= 0 && strings.IndexByte(token[dot+1:], '.') >= 0 {
		return 0, false
	}
	whole := token
	fracDigits := 0
	if dot >= 0 {
		whole = token[:dot] + token[dot+1:]
		fracDigits = len(token) - dot - 1
	}
	if whole == "" {
		return 0, false
	}
	for _, digit := range whole {
		if digit < '0' || digit > '9' {
			return 0, false
		}
	}

	// Drop leading zeroes before applying the decimal scale. This keeps the
	// length checks small while preserving exactness for values such as 0e19.
	whole = strings.TrimLeft(whole, "0")
	if whole == "" {
		return 0, true
	}

	scale := exponent - fracDigits
	if scale >= 0 {
		if scale > 19 || len(whole)+scale > 19 {
			return 0, false
		}
		whole += strings.Repeat("0", scale)
	} else {
		remove := -scale
		if remove >= len(whole) {
			// A nonzero mantissa cannot become an integer when any significant
			// digit remains to the right of the decimal point.
			return 0, false
		}
		for _, digit := range whole[len(whole)-remove:] {
			if digit != '0' {
				return 0, false
			}
		}
		whole = whole[:len(whole)-remove]
	}
	if len(whole) > 19 {
		return 0, false
	}

	if negative {
		whole = "-" + whole
	}
	parsed, err := strconv.ParseInt(whole, 10, 64)
	return parsed, err == nil
}
