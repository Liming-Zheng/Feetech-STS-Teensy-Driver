import Darwin
import SwiftUI

final class ServoConnection: ObservableObject {
    @Published private(set) var ports: [String] = []
    @Published private(set) var connected = false
    @Published private(set) var ready = false
    @Published private(set) var busy = false
    @Published private(set) var message = "请选择 Teensy 的 USB 串口。"
    @Published private(set) var feedback: Feedback?
    @Published private(set) var experimentID: Int?
    @Published private(set) var experimentStatus = "待开始"

    struct Feedback {
        let position: Int
        let speed: Int
        let voltage: Int
        let temperature: Int
        let mode: Int
        let torque: Int
        let resolution: Int
    }

    private var descriptor: Int32 = -1
    private var incoming = Data()
    private var pending: (reply: String, deadline: TimeInterval)?
    private var timer: Timer?
    private var startingExperimentID: Int?
    private var nextPulse = 0.0

    func refreshPorts() {
        ports = ((try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? [])
            .filter { $0.hasPrefix("cu.usbmodem") }
            .sorted()
            .map { "/dev/" + $0 }
    }

    func connect(_ path: String) {
        let device = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard device >= 0 else {
            message = "无法打开串口（系统错误 \(errno)）。"
            return
        }
        var settings = termios()
        guard ioctl(device, TIOCEXCL) == 0, tcgetattr(device, &settings) == 0 else {
            Darwin.close(device)
            message = "无法独占串口。请关闭串口监视器。"
            return
        }
        cfmakeraw(&settings)
        settings.c_cflag |= tcflag_t(CLOCAL | CREAD)
        guard cfsetspeed(&settings, speed_t(B115200)) == 0,
              tcsetattr(device, TCSANOW, &settings) == 0,
              tcflush(device, TCIOFLUSH) == 0 else {
            Darwin.close(device)
            message = "无法初始化 USB 串口。"
            return
        }

        descriptor = device
        connected = true
        timer = Timer.scheduledTimer(withTimeInterval: 0.03, repeats: true) { [weak self] _ in
            self?.receive()
        }
        send("HELLO", expecting: "STS1")
    }

    func disconnect(_ reason: String = "USB 已断开。") {
        timer?.invalidate()
        timer = nil
        if descriptor >= 0 { Darwin.close(descriptor) }
        descriptor = -1
        connected = false
        ready = false
        busy = false
        pending = nil
        if experimentID != nil || startingExperimentID != nil {
            experimentStatus = "USB 断开，Teensy 将停止发出新的摆动目标。"
        }
        experimentID = nil
        startingExperimentID = nil
        incoming.removeAll()
        feedback = nil
        message = reason
    }

    func clearFeedback() {
        feedback = nil
    }

    func startExperiment(id: Int, milliHz: Int) {
        startingExperimentID = id
        send("SWING \(id) \(milliHz)", expecting: "SWING")
    }

    func stopExperiment() {
        guard let experimentID else { return }
        send("STOP \(experimentID)", expecting: "STOP")
    }

    func send(_ command: String, expecting reply: String) {
        guard connected, !busy else { return }
        let bytes = Array((command + "\n").utf8)
        let count = bytes.withUnsafeBytes { Darwin.write(descriptor, $0.baseAddress, $0.count) }
        guard count == bytes.count else {
            disconnect("写入串口失败。请重新连接。")
            return
        }
        if reply == "STATUS" { clearFeedback() }
        pending = (reply, ProcessInfo.processInfo.systemUptime + 2)
        busy = true
        if reply != "PULSE" { message = "等待回复…" }
    }

    private func receive() {
        var buffer = [UInt8](repeating: 0, count: 256)
        let count = buffer.withUnsafeMutableBytes {
            Darwin.read(descriptor, $0.baseAddress, $0.count)
        }
        if count > 0 {
            incoming.append(contentsOf: buffer.prefix(count))
            while let newline = incoming.firstIndex(of: 10) {
                let line = String(decoding: incoming[..<newline], as: UTF8.self)
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                incoming.removeSubrange(...newline)
                handle(line)
            }
            if incoming.count > 512 { incoming.removeAll() }
        } else if count == 0 || (errno != EAGAIN && errno != EINTR) {
            disconnect("串口连接中断。")
            return
        }
        let now = ProcessInfo.processInfo.systemUptime
        if let pending, now >= pending.deadline {
            disconnect("Teensy 回复超时。请检查固件、USB 连接和接线。")
        } else if let experimentID, !busy, now >= nextPulse {
            nextPulse = now + 0.2
            send("PULSE \(experimentID)", expecting: "PULSE")
        }
    }

    private func handle(_ line: String) {
        let parts = line.split(separator: " ")
        if parts.count == 4, parts[0] == "OSC", parts[1] == "END" {
            experimentID = nil
            let reason: String
            switch parts[2] {
            case "LATE120": reason = "去程未按时到达 120°"
            case "LATE60": reason = "回程未按时到达 60°"
            case "HOME": reason = "起始定位未到达 60°"
            case "LINK": reason = "控制连接中断"
            case "PROFILE": reason = "无法确认舵机运动参数"
            default: reason = "舵机位置读取失败"
            }
            experimentStatus = "已停止。\(reason)，完成 \(parts[3]) 圈。"
            return
        }
        guard let request = pending else { return }
        if parts.first == "ERR" {
            pending = nil
            busy = false
            startingExperimentID = nil
            if request.reply == "PULSE" {
                if experimentID != nil { experimentStatus = "Teensy 已结束本次摆动。" }
                experimentID = nil
                return
            }
            switch parts.last {
            case "RATE": message = "设定频率超过此舵机约 3.7 Hz 的理想空载上限。"
            case "ACTIVE": message = "实验正在运行，请先停止摆动。"
            case "COMMAND" where request.reply == "SWING":
                message = "请烧录支持往复实验的新固件。"
            case "SERVO": message = "舵机读取失败。请检查 ID、供电和 UART2 接线。"
            case "MODE": message = "请将舵机设为位置模式，角度分辨率设为 1。"
            case "PROFILE": message = "无法确认运动参数。请读取状态确认舵机位置。"
            case "TORQUE": message = "无法确认扭矩状态。"
            default: message = "Teensy 拒绝了指令。"
            }
            if !ready { disconnect(message) }
            return
        }
        guard parts.count >= 2, parts[0] == "OK", parts[1] == request.reply else { return }

        switch request.reply {
        case "STS1":
            ready = true
            message = "USB 已连接。请先读取舵机状态。"
        case "STATUS":
            let values = parts.dropFirst(2).compactMap { Int($0) }
            guard parts.count == 9, values.count == 7 else { return }
            feedback = Feedback(position: values[0], speed: values[1], voltage: values[2],
                                temperature: values[3], mode: values[4], torque: values[5],
                                resolution: values[6])
            message = "已读取舵机状态。"
        case "MOVE":
            clearFeedback()
            message = "舵机已接受目标。可读取状态查看实际位置。"
        case "SWING":
            experimentID = startingExperimentID
            startingExperimentID = nil
            nextPulse = 0
            clearFeedback()
            experimentStatus = "正在移到起点 60°…"
            message = "Teensy 已接受实验指令。"
        case "PULSE":
            guard parts.count == 4 else { return }
            if experimentID != nil {
                experimentStatus = parts[2] == "1" ? "正在移到起点 60°…" : "摆动中 · 已完成 \(parts[3]) 圈"
            }
        case "STOP":
            experimentID = nil
            experimentStatus = "已停止发出新的摆动目标。"
            message = "舵机会完成并保持最后一个目标。"
        case "RELEASE":
            if experimentID != nil {
                experimentID = nil
                experimentStatus = "已停止摆动并释放扭矩。"
            }
            clearFeedback()
            message = "已关闭舵机扭矩。"
        default:
            return
        }
        pending = nil
        busy = false
    }
}

struct ServoPanel: View {
    @StateObject private var connection = ServoConnection()
    @State private var port = ""
    @State private var servoID = "1"
    @State private var angle = "180"
    @State private var speed = "300"
    @State private var acceleration = "10"
    @State private var frequency = "1.0"

    private var id: Int? {
        guard let value = Int(servoID), (0...253).contains(value) else { return nil }
        return value
    }

    private var move: String? {
        guard let id, let angle = Double(angle), (0..<360).contains(angle),
              let speed = Int(speed), (1...32767).contains(speed),
              let acceleration = Int(acceleration), (1...254).contains(acceleration)
        else { return nil }
        let position = min(4095, Int((angle * 4096 / 360).rounded()))
        return "MOVE \(id) \(position) \(speed) \(acceleration)"
    }

    private var frequencyMilliHz: Int? {
        guard let value = Double(frequency), (1...10).contains(value) else { return nil }
        return Int((value * 1000).rounded())
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("STS3032 舵机控制").font(.largeTitle.bold())
            HStack {
                Picker("USB 串口", selection: $port) {
                    Text("请选择").tag("")
                    ForEach(connection.ports, id: \.self) { Text($0).tag($0) }
                }
                .disabled(connection.connected)

                Button("刷新", action: refresh).disabled(connection.connected)
                if connection.connected {
                    Button("断开") { connection.disconnect() }
                } else {
                    Button("连接") { connection.connect(port) }.disabled(port.isEmpty)
                }
            }

            GroupBox("运动参数") {
                VStack(alignment: .leading, spacing: 10) {
                    field("舵机 ID", value: Binding(
                        get: { servoID },
                        set: {
                            servoID = $0
                            connection.clearFeedback()
                        }
                    ), range: "0–253")
                    .disabled(connection.busy || connection.experimentID != nil)
                    field("目标角度", value: $angle, range: "0° ≤ 角度 < 360°")
                    field("速度", value: $speed, range: "1–32767 · 寄存器值")
                    field("加速度", value: $acceleration, range: "1–254 · 寄存器值")
                    HStack {
                        Button("执行定位") {
                            if let move { connection.send(move, expecting: "MOVE") }
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(move == nil || connection.experimentID != nil)

                        Button("读取状态") {
                            if let id { connection.send("STATUS \(id)", expecting: "STATUS") }
                        }
                        .disabled(id == nil)
                        Button("释放扭矩") {
                            if let id { connection.send("RELEASE \(id)", expecting: "RELEASE") }
                        }
                        .disabled(id == nil)
                    }
                    .disabled(!connection.ready || connection.busy)
                }
                .padding(8)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            GroupBox("往复实验 · 60° ↔ 120°") {
                VStack(alignment: .leading, spacing: 8) {
                    field("往复频率", value: $frequency, range: "1–10 Hz · 选定值")
                        .disabled(connection.experimentID != nil)
                    Text("去程速度设为回程的 2 倍。STS3032 理想空载上限约 3.7 Hz。")
                        .foregroundStyle(.secondary)
                    HStack {
                        Button("开始摆动") {
                            if let id, let frequencyMilliHz {
                                connection.startExperiment(id: id, milliHz: frequencyMilliHz)
                            }
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(id == nil || frequencyMilliHz == nil || connection.experimentID != nil)
                        Button("停止摆动", action: connection.stopExperiment)
                            .disabled(connection.experimentID == nil)
                    }
                    .disabled(!connection.ready || connection.busy)
                    Text(connection.experimentStatus).monospacedDigit()
                }
                .padding(8)
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            GroupBox("舵机状态") {
                VStack(alignment: .leading, spacing: 6) {
                    Text(connection.message)
                    if let feedback = connection.feedback {
                        if feedback.mode == 0 && feedback.resolution == 1 {
                            let degrees = Double(feedback.position) * 360 / 4096
                            Text("实际位置 \(degrees, specifier: "%.2f")° · \(feedback.position) counts")
                        } else {
                            Text("位置反馈 \(feedback.position) counts")
                        }
                        Text("电压 \(Double(feedback.voltage) / 10, specifier: "%.1f") V · 温度 \(feedback.temperature) °C")
                        Text("实际速度 \(feedback.speed) · 模式 \(feedback.mode) · 扭矩状态 \(feedback.torque)")
                    }
                }
                .frame(maxWidth: .infinity, minHeight: 85, alignment: .topLeading)
                .padding(8)
                .monospacedDigit()
            }
        }
        .padding(24)
        .frame(minWidth: 580)
        .fixedSize(horizontal: false, vertical: true)
        .onAppear(perform: refresh)
        .onDisappear { connection.disconnect() }
    }

    private func field(_ label: String, value: Binding<String>, range: String) -> some View {
        HStack {
            Text(label).frame(width: 84, alignment: .leading)
            TextField(label, text: value).labelsHidden().frame(width: 96)
            Text(range).foregroundStyle(.secondary)
        }
    }

    private func refresh() {
        connection.refreshPorts()
        if !connection.ports.contains(port) {
            port = connection.ports.count == 1 ? connection.ports[0] : ""
        }
    }
}

@main
struct ServoControlApp: App {
    var body: some Scene {
        WindowGroup { ServoPanel() }
            .windowResizability(.contentSize)
    }
}
