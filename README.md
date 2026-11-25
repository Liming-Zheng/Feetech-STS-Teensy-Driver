# Teensy Driver for Feetech STS Servo Control

This repository contains a driver for the Feetech STS Servos dpecifically designed to run on the Teensy 4.0. It uses its hardware features for the single wire serial communication so no additional Multiplexer or similar is required. The implementation for this is taken from [Teensy_actuators_PPZ](https://github.com/tudelft/Teensy_actuators_PPZ/tree/main). 

To drive the servos the Arduino STS library has been modified [STS_Servos](https://github.com/matthieuvigne/STS_servos). Some addititonaly functions such as mode control and torque enabling/disabling have been implemented.

The driver can be controlled over serial where a number of commands are implemented. The structure of these can be seen in cmd_structs.h and all the available commands can be seen in the enum at the top of main.cpp.

An example controller for this can be seen in the ROS node [squirrel_servo_control](https://github.com/alang321/squirrel_servo_control). The file teensy_python_interface.py implements easy to use functions to send and retreive data from the driver and thus the servos.

## Issues
Currently, the data read from the gfeedback functions is incorrect. Only when the getAll function is used correct feedback values can be obtained. This is due to implementation differing, this has not been implemented yet in the other feedback functions due to time pressure.

## Building and uploading

The code is compiled using the platformio extension running in VSCode. To upload simply connect the Teensy over USB and hit upload, after finishing press the program button on the Teensy. The code has been tested only on the Teensy 4.0.

## Wiring

To connect the Teensy 4.0 to the servos see the schematic in [Teensy_actuators_PPZ](https://github.com/tudelft/Teensy_actuators_PPZ/tree/main).

Briefly summarized: connect both the TX and RX pin of the chosen port together and then connect these combined pins through a 150 ohm resistor to the servo communication pin.

## Configuration

To configure which serial ports and PWM pins to use, se the defines at the top of main.cpp.

## macOS 控制界面 · Teensy 4.1 + STS3032

Mac 通过 Teensy 的 USB 虚拟串口发送指令，Teensy 通过 `Serial2` 以 1 Mbps 驱动 STS3032。原有 `Serial3` 二进制接口仍可使用。

### 接线与首次运行

1. 用 USB 数据线连接 Mac 和 Teensy 4.1。舵机由独立的 4.8–6 V 电源供电，舵机电源地与 Teensy 的 GND 相连。
2. STS3032 使用单线半双工数据。沿用本仓库的接线，将 UART2 的 RX2（引脚 7）和 TX2（引脚 8）并接，再经 150 Ω 电阻接舵机 DATA。进入 Teensy 的信号必须满足 3.3 V 电平要求，确认总线电平后再接线。 [PJRC Teensy 4.1 引脚图](https://www.pjrc.com/teensy/card11a_rev4_web.pdf) · [Feetech STS3032 规格](https://www.feetechrc.com/Data/feetechrc/upload/file/20211019/6377025719620052024350972.pdf)
3. 用 VS Code 的 PlatformIO **Upload** 烧录 `teensy41` 环境。若 Teensy Loader 等待进入编程模式，按一下板上的 Program 键。烧录后关闭串口监视器。
4. 在仓库根目录运行下面的命令。需要 macOS 13 或更高版本，以及可用的 Swift 编译器（Xcode Command Line Tools 即可）。

   ```sh
   sh macos/run.sh
   ```

5. 在界面选择 `/dev/cu.usbmodem…` 并连接。舵机 ID 默认填 1。先点 **读取状态**，确认有位置、电压和温度反馈，并确认模式为 0。
6. 让舵臂周围留出活动空间。把目标角度设在实际位置附近，速度可先用 300，加速度可先用 10。点 **执行定位** 后再次读取状态，核对舵机实际位置。**释放扭矩** 后舵机不再保持位置，有负载时应先托住机构。关闭窗口或拔掉 USB 时，舵机可继续执行最后的目标。

### 参数含义

- 目标角度使用舵机自身的零点，范围为 0° 至小于 360°。一圈对应 4096 个计数，180° 对应 2048。
- 速度是本次定位的运行速度设定值，使用 STS 寄存器原始单位。实际速度受型号、舵机配置和负载影响。加速度也使用寄存器原始值，一档约为 8.79°/s²。
- 界面使用位置模式（模式 0、角度分辨率设为 1），速度和加速度随目标角度一起下发。连接和读取状态时不会主动开启扭矩。点执行定位后，固件先写入目标参数，再在需要时开启扭矩。 [Feetech STS 内存表](https://ftservo.github.io/ftservo-wiki/reference/parameter/memory-sts/)

### 60° ↔ 120° 往复实验

烧录更新后的固件，再用 `sh macos/run.sh` 打开界面。在 **往复实验** 区输入频率，先从 1 Hz 开始。一次完整往返计为一圈。舵机会先低速到 60°，再开始计时。Teensy 按去程占周期的 1/3、回程占 2/3 排程。以 1 Hz 为例，去程分配约 333 ms，回程约 667 ms。

固件把去程速度寄存器设为回程的两倍，实验使用舵机的最快加速度设置。每段末尾检查实际位置，容差约 1°。界面显示完成圈数，并在到位超时或舵机通信失败时停止切换方向。速度比和频率均为指令设定，负载、加减速和端点停留会影响实际速度曲线。

界面接受 1–10 Hz 的输入，固件会拒绝大于 3.7 Hz 的设定。厂家标称 6 V 空载最快约 0.09 s/60°，按快段 0.09 s、慢段 0.18 s 估算，空载理想上限约为 3.7 Hz。10 Hz 要求去程约 33 ms，超过这款舵机的标称能力。 [Feetech STS3032 规格书](https://www.feetechrc.com/Data/feetechrc/upload/file/20211019/6377025719620052024350972.pdf)

点击 **停止摆动** 或 USB 控制中断后，Teensy 会停止发送新的摆动目标，舵机仍可能走完最后一段并保持位置。需要解除保持时，使用 **释放扭矩**。
