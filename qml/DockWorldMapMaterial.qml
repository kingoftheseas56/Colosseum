pragma ComponentBehavior: Bound
import QtQuick

Canvas {
    id: surface

    // The chart keeps its own logical coordinates across dock resizes.
    property var lands: null
    readonly property int chartWidth: 932
    readonly property int chartHeight: 544

    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function randomFrom(seed) {
        return () => {
            seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0
            return seed / 4294967296
        }
    }

    function mottling(ctx, random, count, light, dark, radius) {
        for (let i = 0; i < count; ++i) {
            const x = random() * chartWidth
            const y = random() * chartHeight
            const size = radius * (0.25 + random())
            const shade = ctx.createRadialGradient(x, y, 0, x, y, size)
            const color = i % 3 === 0 ? dark : light
            shade.addColorStop(0, color)
            shade.addColorStop(1, "transparent")
            ctx.fillStyle = shade
            ctx.fillRect(x - size, y - size, size * 2, size * 2)
        }
    }

    function veins(ctx, random, count, edgeColor, centerColor) {
        for (let i = 0; i < count; ++i) {
            let x = random() * chartWidth - 85
            let y = random() * chartHeight - 45
            const angle = (random() - 0.4) * Math.PI * 0.9
            const step = 3 + random() * 7
            const length = 14 + Math.floor(random() * 34)
            const startWidth = i % 5 === 0 ? 2.5 : 1.1
            ctx.beginPath()
            ctx.moveTo(x, y)
            for (let j = 0; j < length; ++j) {
                const direction = angle + Math.sin(j * 0.21) * 0.47 + (random() - 0.5) * 1.6
                x += Math.cos(direction) * step
                y += Math.sin(direction) * step
                ctx.lineTo(x, y)
            }
            ctx.lineJoin = "round"
            ctx.strokeStyle = edgeColor
            ctx.lineWidth = startWidth
            ctx.stroke()
            ctx.strokeStyle = centerColor
            ctx.lineWidth = startWidth * 0.28
            ctx.stroke()
        }
    }

    function chartOutline(ctx, path) {
        const commands = path.match(/[MLZ][^MLZ]*/g)
        for (const command of commands) {
            if (command[0] === "Z") {
                ctx.closePath()
            } else {
                const point = command.substring(1).split(",")
                if (command[0] === "M")
                    ctx.moveTo(Number(point[0]), Number(point[1]))
                else
                    ctx.lineTo(Number(point[0]), Number(point[1]))
            }
        }
    }

    function stone(ctx) {
        const random = randomFrom(1833)
        const base = ctx.createLinearGradient(0, 0, chartWidth, chartHeight)
        base.addColorStop(0, "#202126")
        base.addColorStop(0.46, "#111317")
        base.addColorStop(1, "#191a1e")
        ctx.fillStyle = base
        ctx.fillRect(0, 0, chartWidth, chartHeight)
        mottling(ctx, random, 135, "#12657883", "#5d050609", 89)
        mottling(ctx, random, 380, "#155c6777", "#45020407", 22)
        veins(ctx, random, 80, "#1211151e", "#2067727a")
        veins(ctx, random, 23, "#3d5a3c1e", "#859f7644")
        veins(ctx, random, 4, "#514d3420", "#c9ce9561")
    }

    function foil(ctx, locked) {
        const random = randomFrom(locked ? 38 : 1445)
        const pigment = ctx.createLinearGradient(120, 10, 760, 570)
        pigment.addColorStop(0, locked ? "#b99259" : "#f8d994")
        pigment.addColorStop(0.35, locked ? "#bd9559" : "#eed095")
        pigment.addColorStop(0.62, locked ? "#ab814a" : "#e3b965")
        pigment.addColorStop(0.8, locked ? "#d3a974" : "#f5d894")
        pigment.addColorStop(1, locked ? "#997043" : "#dcb261")
        ctx.fillStyle = pigment
        ctx.fillRect(0, 0, chartWidth, chartHeight)
        mottling(ctx, random, 65, "#20fff2c9", "#157a451a", 69)
        for (let i = 0; i < 260; ++i) {
            const x = random() * chartWidth
            const y = random() * chartHeight
            const length = 6 + random() * 31
            const width = 1 + random() * 9
            const angle = random() * 2 * Math.PI
            ctx.save()
            ctx.translate(x, y)
            ctx.rotate(angle)
            const sheen = ctx.createLinearGradient(0, -width, 0, width)
            sheen.addColorStop(0, "transparent")
            sheen.addColorStop(0.46, "#25fff9d3")
            sheen.addColorStop(0.53, "#1a7c471c")
            sheen.addColorStop(1, "transparent")
            ctx.fillStyle = sheen
            ctx.beginPath()
            ctx.moveTo(-length, 0)
            ctx.lineTo(-length / 4, -width)
            ctx.lineTo(length * 0.68, -width * 0.4)
            ctx.lineTo(length, 0)
            ctx.lineTo(length / 3, width)
            ctx.closePath()
            ctx.fill()
            ctx.restore()
        }
        veins(ctx, random, 66, "#11fff7db", "#1c8d5a28")
    }

    function land(ctx, locked) {
        ctx.beginPath()
        for (const island of lands) {
            if (island.locked === locked) {
                ctx.save()
                ctx.translate(island.x, island.y)
                chartOutline(ctx, island.path)
                ctx.restore()
            }
        }
        ctx.strokeStyle = "#801b1009"
        ctx.lineWidth = 2.4
        ctx.lineJoin = "round"
        ctx.stroke()
        ctx.save()
        ctx.clip()
        foil(ctx, locked)
        ctx.restore()
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        ctx.scale(width / chartWidth, height / chartHeight)
        if (lands) {
            land(ctx, false)
            land(ctx, true)
        } else {
            // The sea fills the rectangular pane with the two exposed corners
            // rounded to match the picker shell.
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(chartWidth - 18, 0)
            ctx.quadraticCurveTo(chartWidth, 0, chartWidth, 18)
            ctx.lineTo(chartWidth, chartHeight - 18)
            ctx.quadraticCurveTo(chartWidth, chartHeight, chartWidth - 18, chartHeight)
            ctx.lineTo(0, chartHeight)
            ctx.closePath()
            ctx.clip()
            stone(ctx)
        }
    }
}
