# Downloads essential fixes — eyes-on gate

Code checks are green, but only Hemanth's real-app check closes these:

- A failed manga/book job shows its real reason, offers its owning world plus Dismiss, and never offers Retry.
- Theatre alone offers Play, Retry, Pause, and Resume when the row can actually perform them.
- Deleting a landed item or audiobook, and cancelling a group/season, asks for confirmation first.
- A forced file-deletion failure leaves the row visible and shows the backend error.
- With only an audiobook downloading, Now arriving stays visible; summary items/bytes include completed audiobooks.
- Unequal known-size jobs use byte-weighted progress; unknown-size jobs show no guessed percentage.
- Open book on a completed audiobook opens the paired local book directly on its Audio panel.

Do not mark the audit findings closed from harness results alone.
