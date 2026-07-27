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
