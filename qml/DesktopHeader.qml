import QtQuick

import ThemeEngine

Rectangle {
    id: desktopHeader
    width: parent.width
    height: headerHeight
    z: 10

    color: Theme.colorHeader
    property int headerHeight: isHdpi ? 52 : 56

    ////////////////

    signal scannerButtonClicked()
    signal advertiserButtonClicked()
    signal ubertoothButtonClicked()
    signal settingsButtonClicked()

    ////////////////

    DragHandler {
        // make that surface draggable
        // also, prevent clicks below this area
        onActiveChanged: if (active) appWindow.startSystemMove()
        target: null
    }

    ////////////////

    Rectangle { // left menus
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter

        width: rowleft.width
        height: 32
        radius: Theme.componentRadius

        clip: true
        color: Theme.colorHeader
        border.width: 2
        border.color: Theme.colorHeaderHighlight

        visible: (appContent.state === "Scanner" ||
                  appContent.state === "Advertiser" ||
                  appContent.state === "Ubertooth")

        Row {
            id: rowleft
            height: 32
            spacing: 0

            RoundButtonIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 48
                sourceSize: 30

                backgroundVisible: false
                backgroundColor: Theme.colorHeaderHighlight
                opacity: 1
                highlightMode: "off"
                iconColor: Theme.colorIcon
                source: {
                    if (appContent.state === "Advertiser") return "qrc:/assets/icons_material/duotone-wifi_tethering-24px.svg"
                    if (appContent.state === "Ubertooth") return "qrc:/assets/icons_material/duotone-microwave-48px.svg"
                    return "qrc:/assets/icons_material/duotone-devices-24px.svg"
                }
            }

            RoundButtonIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 48
                sourceSize: 32

                enabled: deviceManager.bluetooth
                highlightMode: "off"
                iconColor: Theme.colorHeaderContent
                source: (deviceManager.scanningPaused) ?
                            "qrc:/assets/icons_material/baseline-play_arrow-24px.svg" :
                            "qrc:/assets/icons_material/baseline-play_arrow-24px.svg"

                backgroundVisible: {
                    if (appContent.state === "Scanner" && deviceManager.scanning) return true
                    if (appContent.state === "Advertiser" && deviceManager.advertising) return true
                    if (appContent.state === "Ubertooth" && ubertooth.running) return true
                    return false
                }
                backgroundColor: Theme.colorHeaderHighlight

                opacity: {
                    if (appContent.state === "Scanner" && deviceManager.scanning) return 1
                    if (appContent.state === "Advertiser" && deviceManager.advertising) return 1
                    if (appContent.state === "Ubertooth" && ubertooth.running) return 1
                    return 0.4
                }
                onClicked: {
                    if (appContent.state === "Scanner") deviceManager.scanDevices_start()
                    if (appContent.state === "Advertiser") deviceManager.advertise_start()
                    if (appContent.state === "Ubertooth") ubertooth.startWork()
                }
            }

            RoundButtonIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 48
                sourceSize: 32

                enabled: deviceManager.bluetooth
                highlightMode: "color"
                iconColor: Theme.colorHeaderContent
                source: "qrc:/assets/icons_material/baseline-stop-24px.svg"

                opacity: {
                    if (appContent.state === "Scanner" && !deviceManager.scanning) return 1
                    if (appContent.state === "Advertiser" && !deviceManager.advertising) return 1
                    if (appContent.state === "Ubertooth" && !ubertooth.running) return 1
                    return 0.4
                }

                onClicked: {
                    if (appContent.state === "Scanner") deviceManager.scanDevices_stop()
                    if (appContent.state === "Advertiser") deviceManager.advertise_stop()
                    if (appContent.state === "Ubertooth") ubertooth.stopWork()
                }
            }
        }
    }

    ////////////

    Rectangle { // center indicator
        anchors.centerIn: parent
        width: parent.width * 0.5
        height: 32
        radius: 8

        clip: true
        color: Theme.colorHeaderHighlight
        border.width: 2
        border.color: Qt.darker(Theme.colorHeaderHighlight, 1.01)

        Row {
            anchors.top: parent.top
            anchors.topMargin: 0
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
            spacing: 8

            IconSvg {
                height: 20; width: 20;
                anchors.verticalCenter: parent.verticalCenter

                source: {
                    if (deviceManager.scanningPaused) return "qrc:/assets/icons_material/baseline-pause-24px.svg"
                    if (deviceManager.scanning) return "qrc:/assets/icons_material/baseline-autorenew-24px.svg"
                    return "qrc:/assets/icons_material/baseline-stop-24px.svg"
                }
                color: Theme.colorText

                NumberAnimation on rotation {
                    running: (deviceManager.scanning && !deviceManager.scanningPaused)
                    alwaysRunToEnd: true
                    loops: Animation.Infinite

                    duration: 2000
                    from: 0
                    to: 360
                    easing.type: Easing.Linear
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (deviceManager.scanningPaused) return qsTr("Scanning paused")
                    if (deviceManager.scanning) return qsTr("Scanning for Bluetooth devices nearby")
                    return qsTr("Not scanning")
                }
                color: Theme.colorText
            }

            ////

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: deviceManager.advertising
                text: "  |  "
                color: Theme.colorText
            }

            ////

            IconSvg {
                height: 20; width: 20;
                anchors.verticalCenter: parent.verticalCenter

                visible: deviceManager.advertising
                source: "qrc:/assets/icons_material/duotone-wifi_tethering-24px.svg"
                color: Theme.colorText

                SequentialAnimation on opacity {
                    running: deviceManager.advertising
                    alwaysRunToEnd: true
                    loops: Animation.Infinite

                    PropertyAnimation { to: 0.5; duration: 666; }
                    PropertyAnimation { to: 1; duration: 666; }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: deviceManager.advertising ? qsTr("Virtual device is running") : ""
                color: Theme.colorText
            }

            ////

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: ubertooth.running
                text: "  |  "
                color: Theme.colorText
            }

            ////

            IconSvg {
                height: 20; width: 20;
                anchors.verticalCenter: parent.verticalCenter

                visible: ubertooth.running
                source: "qrc:/assets/icons_material/duotone-microwave-48px.svg"
                color: Theme.colorText

                SequentialAnimation on opacity {
                    running: ubertooth.running
                    alwaysRunToEnd: true
                    loops: Animation.Infinite

                    PropertyAnimation { to: 0.5; duration: 666; }
                    PropertyAnimation { to: 1; duration: 666; }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: ubertooth.running ? qsTr("Ubertooth is running") : ""
                color: Theme.colorText
            }
        }

        ////////

        Row {
            anchors.top: parent.top
            anchors.topMargin: 0
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0

            Text {
                anchors.verticalCenter: parent.verticalCenter

                visible: deviceManager.scanning
                text: qsTr("%n device(s) found", "", deviceManager.deviceCount)
                color: Theme.colorText
            }
        }
    }

    ////////////

    Row { // right
        id: rowright
        anchors.top: parent.top
        anchors.topMargin: 0
        anchors.right: parent.right
        anchors.rightMargin: 0
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 0

        spacing: isHdpi ? 4 : 12
        visible: true

        // MAIN MENU

        Row {
            id: menuMain

            spacing: 0

            DesktopHeaderItem {
                id: menuScanner
                width: headerHeight
                height: headerHeight

                source: "qrc:/assets/icons_material/duotone-devices-24px.svg"
                colorContent: Theme.colorHeaderContent
                colorHighlight: Theme.colorHeaderHighlight

                selected: (appContent.state === "Scanner")
                onClicked: scannerButtonClicked()

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 6

                    width: 12
                    height: 12
                    radius: 12
                    color: Theme.colorGreen

                    opacity: deviceManager.scanning ? 0.8 : 0
                    Behavior on opacity { OpacityAnimator { duration: 333 } }
                }
            }
            DesktopHeaderItem {
                id: menuAdvertiser
                width: headerHeight
                height: headerHeight

                source: "qrc:/assets/icons_material/duotone-wifi_tethering-24px.svg"
                colorContent: Theme.colorHeaderContent
                colorHighlight: Theme.colorHeaderHighlight

                selected: (appContent.state === "Advertiser")
                onClicked: advertiserButtonClicked()

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 6

                    width: 12
                    height: 12
                    radius: 12
                    color: Theme.colorGreen

                    opacity: deviceManager.advertising ? 0.8 : 0
                    Behavior on opacity { OpacityAnimator { duration: 333 } }
                }
            }
            DesktopHeaderItem {
                id: menuUbertooth
                width: headerHeight
                height: headerHeight

                source: "qrc:/assets/icons_material/duotone-microwave-48px.svg"
                colorContent: Theme.colorHeaderContent
                colorHighlight: Theme.colorHeaderHighlight

                visible: ubertooth.toolsAvailable
                selected: (appContent.state === "Ubertooth")
                onClicked: ubertoothButtonClicked()

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 6

                    width: 12
                    height: 12
                    radius: 12
                    color: Theme.colorGreen

                    opacity: ubertooth.running ? 0.8 : 0
                    Behavior on opacity { OpacityAnimator { duration: 333 } }
                }
            }
            DesktopHeaderItem {
                id: menuSettings
                width: headerHeight
                height: headerHeight

                source: "qrc:/assets/icons_material/duotone-tune-24px.svg"
                colorContent: Theme.colorHeaderContent
                colorHighlight: Theme.colorHeaderHighlight

                selected: (appContent.state === "Settings")
                onClicked: settingsButtonClicked()
            }
        }
    }

    ////////////

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        height: 2
        opacity: 1
        color: Theme.colorHeaderHighlight
    }

    ////////////
}
