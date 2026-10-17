// 图形验收用：坐标必须先从目标应用的辅助功能树或截图读取。
import CoreGraphics
import Foundation

let point = CGPoint(x: Double(CommandLine.arguments[1])!, y: Double(CommandLine.arguments[2])!)
for kind: CGEventType in [.mouseMoved, .leftMouseDown, .leftMouseUp] {
    let event = CGEvent(mouseEventSource: nil, mouseType: kind, mouseCursorPosition: point, mouseButton: .left)!
    event.flags = []
    event.setIntegerValueField(.mouseEventClickState, value: 1)
    event.post(tap: .cghidEventTap)
    Thread.sleep(forTimeInterval: 0.1)
}
