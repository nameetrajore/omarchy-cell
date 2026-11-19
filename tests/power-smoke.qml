import QtQuick
import Quickshell
import "Power" as Power

// Compile the real integrated plugin without opening a window or changing power settings.
ShellRoot {
    Power.Panel { id: battery; bar: null }
    Timer {
        interval: 800; running: true
        onTriggered: {
            if (battery.moduleName !== "omarchy.power") { console.error("Lost stock battery IPC identity"); Qt.exit(1); return }
            Qt.exit(0)
        }
    }
}
