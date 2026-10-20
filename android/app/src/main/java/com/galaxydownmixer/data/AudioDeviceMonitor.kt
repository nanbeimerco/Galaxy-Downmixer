package com.galaxydownmixer.data

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class DeviceType {
    BLUETOOTH,
    USB,
    WIRED,
    SPEAKER,
    OTHER,
}

data class ConnectedAudioDevice(
    val key: String,
    val name: String,
    val type: DeviceType,
    val rawType: Int,
) {
    companion object {
        val SPEAKER_DEFAULT = ConnectedAudioDevice(
            key = "SPEAKER",
            name = "内蔵スピーカー",
            type = DeviceType.SPEAKER,
            rawType = AudioDeviceInfo.TYPE_BUILTIN_SPEAKER,
        )
    }
}

class AudioDeviceMonitor(private val context: Context) {

    private val audioManager = context.getSystemService(AudioManager::class.java)
    private val mainHandler = Handler(Looper.getMainLooper())

    private val _currentDevice = MutableStateFlow(resolveActiveDevice())
    val currentDevice: StateFlow<ConnectedAudioDevice> = _currentDevice.asStateFlow()

    private val callback = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>?) {
            updateActiveDevice()
        }

        override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>?) {
            updateActiveDevice()
        }
    }

    init {
        audioManager.registerAudioDeviceCallback(callback, mainHandler)
    }

    fun updateActiveDevice(): ConnectedAudioDevice {
        val dev = resolveActiveDevice()
        _currentDevice.value = dev
        return dev
    }

    private fun resolveActiveDevice(): ConnectedAudioDevice {
        val outputs = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        if (outputs.isNullOrEmpty()) return ConnectedAudioDevice.SPEAKER_DEFAULT

        // Priority order for media output
        // 1. Bluetooth A2DP / BLE
        val bt = outputs.firstOrNull {
            it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                it.type == 26 /* TYPE_BLE_HEADSET */ ||
                it.type == 27 /* TYPE_BLE_SPEAKER */
        }
        if (bt != null) {
            val name = bt.productName?.toString()?.ifBlank { null } ?: "Bluetooth オーディオ"
            return ConnectedAudioDevice(
                key = "BT:$name",
                name = name,
                type = DeviceType.BLUETOOTH,
                rawType = bt.type,
            )
        }

        // 2. USB Headset / USB DAC
        val usb = outputs.firstOrNull {
            it.type == AudioDeviceInfo.TYPE_USB_HEADSET ||
                it.type == AudioDeviceInfo.TYPE_USB_DEVICE
        }
        if (usb != null) {
            val name = usb.productName?.toString()?.ifBlank { null } ?: "USB DAC"
            return ConnectedAudioDevice(
                key = "USB:$name",
                name = name,
                type = DeviceType.USB,
                rawType = usb.type,
            )
        }

        // 3. Wired Headphone / Headset (3.5mm)
        val wired = outputs.firstOrNull {
            it.type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES ||
                it.type == AudioDeviceInfo.TYPE_WIRED_HEADSET
        }
        if (wired != null) {
            val name = wired.productName?.toString()?.ifBlank { null } ?: "有線イヤホン / ヘッドホン"
            return ConnectedAudioDevice(
                key = "WIRED",
                name = name,
                type = DeviceType.WIRED,
                rawType = wired.type,
            )
        }

        // 4. Built-in Speaker
        val spk = outputs.firstOrNull { it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER }
        if (spk != null) {
            return ConnectedAudioDevice.SPEAKER_DEFAULT
        }

        // Fallback to first output
        val first = outputs.first()
        val name = first.productName?.toString()?.ifBlank { null } ?: "オーディオ出力"
        return ConnectedAudioDevice(
            key = "OTHER:$name",
            name = name,
            type = DeviceType.OTHER,
            rawType = first.type,
        )
    }

    fun release() {
        audioManager.unregisterAudioDeviceCallback(callback)
    }
}
