import QtQuick
import ".."

Item {
    id: root
    objectName: "vinylBarcode"
    property string digits: ""
    readonly property string displayDigits: digits
    readonly property bool valid: {
        if (!/^\d{12}$/.test(digits))
            return false;
        var total = 0;
        for (var i = 0; i < 11; i++)
            total += Number(digits[i]) * (i % 2 === 0 ? 3 : 1);
        return (10 - total % 10) % 10 === Number(digits[11]);
    }
    readonly property string bars: {
        if (!valid)
            return "";
        var left = ["0001101", "0011001", "0010011", "0111101", "0100011", "0110001", "0101111", "0111011", "0110111", "0001011"];
        var result = "101";
        for (var i = 0; i < 6; i++)
            result += left[Number(digits[i])];
        result += "01010";
        for (i = 6; i < 12; i++)
            result += left[Number(digits[i])].replace(/[01]/g, function (v) {
                return v === "0" ? "1" : "0";
            });
        return result + "101";
    }
    readonly property real symbolWidth: symbol.childrenRect.width
    Theme {
        id: theme
    }
    width: 160
    height: valid ? 56 : 25
    Row {
        id: symbol
        width: parent.width
        height: 34
        Repeater {
            model: root.bars.length
            delegate: Rectangle {
                required property int index
                width: root.width / 95
                height: 34
                color: theme.ink
                opacity: root.bars[index] === "1" ? 1 : 0
            }
        }
    }
    Text {
        y: root.valid ? 40 : 0
        text: root.displayDigits
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 13
        font.letterSpacing: 2
    }
}
