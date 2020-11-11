import QtQuick 2.12
import QtQuick.Window 2.12
import OpenGLDemo 1.0

Window {
    visible: true
    width: 640
    height: 480
    title: qsTr("OpenGL Demo")

    VRRender{
        id: render
    }

    ImageView{
        id:imageView
        anchors.fill: parent
        image: render.frame
    }

    Timer {
        interval: 10;
        running: true;
        repeat: true
        onTriggered: render.renderImage()
    }
}
