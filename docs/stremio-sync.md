# Stremio Sync

Stremio Sync is an optional Theatre connection. It works with either an account-backed Colosseum profile or a profile that has no Colosseum account.

Open Theatre and select the official Stremio icon between Search and the Profile/Device icon. **Connect Stremio** opens Stremio's official browser sign-in; Colosseum never asks for or stores a Stremio password. Each Colosseum profile connects its own Stremio account.

The first sync safely combines the two libraries and compatible addon collections. It deletes nothing. For progress and watched state, the newest real activity wins, including a newer completed state over an older partial position. Later changes to library membership, progress, watched state and addons continue syncing quietly.

Removing a linked Theatre title offers two choices:

1. **Remove from Colosseum** keeps the Stremio copy.
2. **Remove from Colosseum & Stremio** records the explicit remote removal and retries it if the provider is temporarily unavailable.

Imported viewing appears in History with a Stremio label and its latest known date. It does not add Colosseum viewing hours, sessions or other Your Colosseum statistics.

The Stremio credential and configured addon collection stay on the device. A new device shows **Reconnect Stremio** and retrieves the actual addon collection from Stremio after reconnection. Disconnecting removes the device credential and stops sync while keeping everything already merged into Colosseum.

Stremio Sync is Theatre-only. It does not appear in Tankoban or Biblio, does not sync Stremio player/subtitle preferences, and does not make local playback or persistence depend on Stremio being online. Only one external main-sync provider can be connected to a profile; this release adds Stremio only.
