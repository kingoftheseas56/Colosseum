$ErrorActionPreference = 'Stop'

$prototype = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content -Raw (Join-Path $prototype 'CMakeLists.txt')
$slotRing = Get-Content -Raw (Join-Path $prototype 'src\slot_ring.h')

function Require-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "contract failure: $Message"
    }
}

Require-Text $cmake 'add_executable\(slot_ring_test' 'isolated slot-ring harness target is required'
Require-Text $slotRing 'enum class SlotState' 'slot state must be explicit'
Require-Text $slotRing 'Displaying' 'displayed slots must be represented'
Require-Text $slotRing 'Retiring' 'consumer retirement must be represented'
Require-Text $slotRing 'consumerFenceValue' 'slot reuse must depend on a consumer fence value'

Write-Output 'prototype_contract_test: PASS'
