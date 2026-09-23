$ErrorActionPreference = 'Stop'
. 'C:\b\Colosseum-Server-1.0-sol\INT-W2-repaired\artifacts\server1\K10\K10-A\controlled_peer_startup.ps1'
$log = 'C:\b\_k10diag2\neg-wire.log'
Remove-Item -ErrorAction SilentlyContinue $log, ($log + '.stderr.txt')
$peer = Start-ControlledPeer -Log $log -Arguments @('C:\b\Colosseum-Server-1.0-sol\INT-W2-repaired\artifacts\server1\K10\K10-A\controlled_peer.py','--port','0','--info-hash','zz','--log',$log)
$start = Get-Date
try { Wait-ControlledPeers -Case 'NEGATIVE' -Peers @($peer) -Logs @($log) ; 'UNEXPECTED PASS' } catch { "CAUGHT after $([int]((Get-Date)-$start).TotalMilliseconds) ms: $($_.Exception.Message)" }
