MoodAnchor Android MVP

功能：模拟/接收黄山派确认事件，记录事件，发出 Android 通知，点击进入舒缓对话页。

当前状态：可导入 Android Studio 的 Kotlin 工程；大模型使用 LocalStubGateway 占位，BLE 服务保留 WatchBleService 接入点。

运行步骤：
1. 安装 Android Studio（选择 Android SDK 35、Android SDK Build-Tools、Android SDK Platform-Tools）。
2. 用 Android Studio 打开本 android-app 目录，等待 Gradle 同步。
3. 在 Android 手机打开开发者选项和 USB 调试，USB 连接后运行 app。
4. 允许通知权限；点“模拟一次手表确认事件”，在状态栏点通知进入对话页。

后续 BLE 协议：黄山派为 GATT Server，手机为 GATT Client。黄山派通过 Event Notification 发 JSON 或二进制包：timestamp、score、threshold、signalQuality。收到后由 WatchBleService.onWatchConfirmed() 调用相同事件链路。

大模型接口：将 LargeLanguageModelGateway 的 LocalStubGateway 替换为 HTTPS 实现；API Key 不写入 APK，应由受控后端或用户安全存储提供。
