import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    property int orientation: Qt.Horizontal
    property bool fillAvailable: true

    Layout.fillWidth: fillAvailable && orientation === Qt.Horizontal
    Layout.fillHeight: fillAvailable && orientation === Qt.Vertical
    implicitWidth: orientation === Qt.Vertical ? Theme.separatorWidth : 0
    implicitHeight: orientation === Qt.Horizontal ? Theme.separatorWidth : 0
    color: Theme.border
}
