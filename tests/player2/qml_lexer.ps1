# qml_lexer.ps1 - shared comment stripper for the Player 2 contracts.
#
# WHY THIS EXISTS: every one of these contracts is a text grep, so a facade made entirely of
# commented-out declarations passes unless the comments are removed first (cross-model review,
# 2026-07-27, produced exactly that false negative against the facade contract). Lifted from
# player2_shell_contract.ps1, which grew the original after a 2026-07-26 review caught the mirror
# bug: a naive s|//.*|| also erases real code - "qrc:///qml/PlayerPage.qml", any https:// URL - so
# the walk has to honour quotes and treat a "//" inside a string as code.
#
# ONE FIX ON LIFT: the original's escape arm compared a [char] against the 3-character literal
# '`\`', which no char can ever equal, so it never fired - `function f() { return "a\"b//c" }` was
# truncated at the "//" because the escaped quote closed the string. Verified empirically before
# and after. Both contracts now dot-source this, so the lexer cannot drift between them again.
#
# SURVIVES TASK 9: the shell contract dies with the isolated shell, the FACADE contract does not -
# it is the port's definition of done and outlives the whole lab. This file must not be swept away
# with the rest of tests/player2/.
#
# Known limits, unreachable in every current input, recorded so the next reader does not re-derive
# them: (1) the block-comment pass is a plain regex, so a literal "/*" inside a string is eaten with
# it; (2) a regex literal holding an unbalanced quote - /["']/ - leaves the quote state wrong for
# the rest of that line, which would hide a trailing comment. Same class, same fix if either lands:
# the walk would need to track regex-literal context, not just quotes.

function Remove-QmlComments([string]$Source) {
    # Block comments first: they span lines, so the per-line walk below cannot see them.
    $text = [regex]::Replace($Source, '(?s)/\*.*?\*/', '')
    $out = New-Object System.Text.StringBuilder
    foreach ($line in ($text -split "`n")) {
        $inSingle = $false; $inDouble = $false; $cut = -1
        for ($i = 0; $i -lt $line.Length; $i++) {
            $ch = $line[$i]
            # Inside a string a backslash escapes the NEXT character outright. This is the arm that
            # keeps "a\"b//c" one string instead of a string plus a phantom comment.
            if (($inSingle -or $inDouble) -and $ch -eq '\') { $i++; continue }
            if ($ch -eq "'" -and -not $inDouble) { $inSingle = -not $inSingle; continue }
            if ($ch -eq '"' -and -not $inSingle) { $inDouble = -not $inDouble; continue }
            if (-not $inSingle -and -not $inDouble -and $ch -eq '/' -and $i + 1 -lt $line.Length -and $line[$i + 1] -eq '/') { $cut = $i; break }
        }
        $kept = if ($cut -ge 0) { $line.Substring(0, $cut) } else { $line }
        [void]$out.AppendLine($kept)
    }
    return $out.ToString()
}

# A contract that searches QML must not let a quoted decoy manufacture code. Preserve the fact
# that a real string literal exists, but encode its whole value as one opaque token. Thus
# `property string x: "readonly property ..."` cannot satisfy a declaration regex, while a real
# object key/value pair can still be matched through Get-QmlStringToken(). This scanner is also
# comment-aware before it ever recognizes a quote, unlike the older comment-only view above.
# Template interpolation remains opaque: it is string content, not QML code for these contracts.
function Get-QmlStringToken([string]$Value) {
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Value))
    return "__QML_STRING_${encoded}__"
}

function ConvertTo-QmlCodeView([string]$Source) {
    $out = New-Object System.Text.StringBuilder
    $length = $Source.Length
    for ($i = 0; $i -lt $length;) {
        $ch = $Source[$i]

        # Line comments: retain the line break so nearby constructs cannot merge into one match.
        if ($ch -eq '/' -and $i + 1 -lt $length -and $Source[$i + 1] -eq '/') {
            $i += 2
            while ($i -lt $length -and $Source[$i] -ne "`n") { $i++ }
            continue
        }
        # Block comments: retain every line break for the same structural reason.
        if ($ch -eq '/' -and $i + 1 -lt $length -and $Source[$i + 1] -eq '*') {
            $i += 2
            while ($i -lt $length -and -not ($Source[$i] -eq '*' -and $i + 1 -lt $length -and $Source[$i + 1] -eq '/')) {
                if ($Source[$i] -eq "`n") { [void]$out.Append("`n") }
                $i++
            }
            if ($i + 1 -lt $length) { $i += 2 }
            continue
        }
        if ($ch -eq "'" -or $ch -eq '"' -or $ch -eq [char]96) {
            $quote = $ch
            $value = New-Object System.Text.StringBuilder
            $i++
            while ($i -lt $length) {
                $current = $Source[$i]
                if ($current -eq '\' -and $i + 1 -lt $length) {
                    # The decoded spelling is only a token key; contracts never execute it.
                    [void]$value.Append($Source[$i + 1])
                    $i += 2
                    continue
                }
                if ($current -eq $quote) { $i++; break }
                [void]$value.Append($current)
                $i++
            }
            [void]$out.Append((Get-QmlStringToken $value.ToString()))
            continue
        }
        [void]$out.Append($ch)
        $i++
    }
    return $out.ToString()
}
