# Adopted Qt Quick view input ownership

`6.11.1-adopted-input-owner.patch` targets QtWebEngine 6.11.1
`eb0793cc4b76e93cf669f586fd68c76019f40ec9`,
`src/webenginequick/api/qquickwebengineview.cpp`.

Qt's Quick delegate retained the creation view in `m_parentView` even after
`Bind()` adopted its WebContents into another view. `ActiveFocusOnPress()`
could then dereference that view after opener retirement. The patch rebinds
the input owner from the current adapter client, following the corresponding
ownership update in Qt's widget delegate, and guards a missing owner.

The constructor marker `_colosseumAdoptionInputRebound` identifies this
patched runtime. Colosseum retains the opener dependency guard on stock Qt;
a matching version string alone never removes it. No virtual interface or
public Qt ABI changes are introduced.

The codec workflow checks and applies this patch before configuring 6.11.1.
Its installed artifact records the patch hash. Other versions retain the
stock-runtime guard and need a separately reviewed patch.

Validation so far: clean application against the pinned source; native
capability-gate tests; stock-runtime hosting regressions. **The modified Qt
module has not yet been compiled or runtime-qualified.** After building it,
run `FeriaHosting::test_repairedEngineClosesOpenerAndKeepsChildInput` in the
functional QtWebEngine harness. It must pass without skipping: close the
opener, type into its adopted child, return it to the embedded host, type
again, then close it and verify view retirement.

The modified Qt file retains its upstream license:
`LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`.
