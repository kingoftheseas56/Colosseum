// GuidedCameraHost — the ONE place that imports the native Colosseum.Guided module.
//
// MangaReader loads this through a Loader instead of importing Colosseum.Guided directly,
// so the reader keeps loading even where the C++ type isn't registered yet (the app before
// Agent 0 wires main.cpp, or a plain qml.exe harness). When the module is absent the Loader
// simply fails and Guided degrades to a static whole-page view — reading is never blocked.

import QtQuick
import Colosseum.Guided

GuidedCameraController {
}
