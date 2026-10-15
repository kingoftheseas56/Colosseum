# Arc 41 Settings local-only seed

The Arc 41 Settings acceptance uses `QT_SCALE_FACTOR=1.5`. That produces the
real `853x480` logical viewport needed to overflow Settings, but the onboarding
Continue action is below that viewport. Colosseum stores the onboarding
bootstrap in Windows `QSettings`, so Lanista's AppData seed copy cannot carry
this state.

Prepare a disposable per-tag registry key before launching the scenario:

```powershell
powershell -ExecutionPolicy Bypass -File .\prepare.ps1 -Tag arc41-wave5-family4-green-<unique>
```

Then run `tests/lanista_scenarios/arc41_settings_overflow.json` with the same
tag and `QT_SCALE_FACTOR=1.5`. The helper rejects tags outside the Arc 41
family and writes only `HKCU\Software\Brotherhood\Colosseum-dltest-<tag>`.
Pass `-Clean` with the same tag after the run. The scenario asserts that the
account host is absent before opening Settings, so the seed is observable in
the receipt rather than silently assumed.
