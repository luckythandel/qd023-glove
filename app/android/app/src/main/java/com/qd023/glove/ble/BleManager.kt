package com.qd023.glove.ble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import java.util.UUID

/**
 * BLE central for the QD023 glove (Nordic UART Service).
 *
 * Lines arrive on [lines] as a hot flow so no notification payload is dropped
 * when several lines are packed into one packet.
 */
class BleManager(private val ctx: Context) {
    companion object {
        val SVC = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val TX = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E") // notify
        val RX = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E") // write
        val CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        const val DEVICE_NAME = "QD023-Glove"

        /**
         * The firmware advertises the active control mode in its BLE name:
         * "-W" windows, "-L" LED strip, "-A" android. This app only grabs the
         * glove while it is in Android mode, so the PC bridge can keep the link
         * the rest of the time. The bare name is accepted for older firmware.
         */
        const val TARGET_NAME = "$DEVICE_NAME-A"
        /** Music mode: the phone is the instrument, so it claims this tag too. */
        const val MUSIC_NAME = "$DEVICE_NAME-M"
        /** Company id used by the firmware for its mode-tagged advertisement. */
        const val MODE_COMPANY_ID = 0xFFFF
        /** Mode byte values carried in the manufacturer data. */
        const val MODE_WINDOWS = 0
        const val MODE_LED = 1
        const val MODE_ANDROID = 2
        const val MODE_MUSIC = 3
        private const val RECONNECT_MS = 3000L
        private const val WATCHDOG_MS = 8000L
    }

    private val adapter: BluetoothAdapter? =
        (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private var gatt: BluetoothGatt? = null
    private var txChar: BluetoothGattCharacteristic? = null
    private var rxChar: BluetoothGattCharacteristic? = null

    private val handler = Handler(Looper.getMainLooper())
    private var scanning = false
    private var active = true

    val connected = MutableStateFlow(false)

    private val _lines = MutableSharedFlow<String>(extraBufferCapacity = 256)
    val lines = _lines.asSharedFlow()

    private val reconnectRunnable = Runnable {
        if (active && gatt == null) startScan()
    }

    /**
     * Some stacks silently drop a long-running scan (battery savers, OEM scan
     * throttling). Re-arm an idle scan every few seconds so a mode change on
     * the glove is picked up without restarting the service by hand.
     */
    private val watchdogRunnable = object : Runnable {
        override fun run() {
            if (!active) return
            if (gatt == null) {
                if (scanning) stopScan()
                startScan()
            }
            handler.postDelayed(this, WATCHDOG_MS)
        }
    }

    init {
        handler.postDelayed(watchdogRunnable, WATCHDOG_MS)
    }

    private fun hasScanPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.BLUETOOTH_SCAN) ==
            PackageManager.PERMISSION_GRANTED

    private fun hasConnectPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.BLUETOOTH_CONNECT) ==
            PackageManager.PERMISSION_GRANTED

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (matchesTarget(result)) {
                stopScan()
                connect(result.device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            scheduleReconnect()
        }
    }

    /**
     * The firmware advertises company id 0xFFFF followed by the active control
     * mode byte. That byte is authoritative and, unlike the device name, can
     * never be a stale cached value from an earlier connection.
     */
    private fun matchesTarget(result: ScanResult): Boolean {
        val mode = try {
            result.scanRecord?.getManufacturerSpecificData(MODE_COMPANY_ID)
        } catch (e: Exception) {
            null
        }
        if (mode != null && mode.isNotEmpty()) {
            // Android mode for remote control, music mode to play the synth.
            val m = mode[0].toInt()
            return m == MODE_ANDROID || m == MODE_MUSIC
        }
        // Firmware without mode tags: fall back to the advertised name.
        val advName = result.scanRecord?.deviceName
        val cached = try {
            result.device.name
        } catch (e: SecurityException) {
            null
        }
        val name = advName ?: cached ?: return false
        return name == TARGET_NAME || name == MUSIC_NAME || name == DEVICE_NAME
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (!active || scanning || gatt != null || !hasScanPermission()) return
        val scanner = adapter?.bluetoothLeScanner ?: return
        if (adapter?.isEnabled != true) {
            scheduleReconnect()
            return
        }
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanning = true
        try {
            // No name filter: the scanned name also carries the glove's mode.
            scanner.startScan(null, settings, scanCallback)
        } catch (e: SecurityException) {
            scanning = false
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: SecurityException) {
            // adapter went away
        }
    }

    @SuppressLint("MissingPermission")
    private fun connect(device: BluetoothDevice) {
        if (!active || !hasConnectPermission()) return
        gatt?.close()
        gatt = try {
            device.connectGatt(ctx, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } catch (e: SecurityException) {
            null
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                connected.value = true
                g.requestMtu(512)
            } else {
                connected.value = false
                txChar = null
                rxChar = null
                try {
                    g.close()
                } catch (e: SecurityException) {
                    // ignore
                }
                if (gatt === g) gatt = null
                scheduleReconnect()
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            // Discover regardless: some stacks reject the MTU request.
            g.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(SVC) ?: return
            txChar = svc.getCharacteristic(TX)
            rxChar = svc.getCharacteristic(RX)
            txChar?.let { c ->
                g.setCharacteristicNotification(c, true)
                val desc = c.getDescriptor(CCCD) ?: return@let
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    g.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                } else {
                    @Suppress("DEPRECATION")
                    desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    @Suppress("DEPRECATION")
                    g.writeDescriptor(desc)
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            // Notifications are live now: ask which control mode the glove is in.
            if (status == BluetoothGatt.GATT_SUCCESS) write("GETMODE\n")
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            c: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            emitLines(value)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            emitLines(c.value ?: byteArrayOf())
        }
    }

    private fun emitLines(value: ByteArray) {
        val text = String(value, Charsets.UTF_8)
        for (raw in text.split('\n')) {
            val line = raw.trim()
            if (line.isNotEmpty()) _lines.tryEmit(line)
        }
    }

    @SuppressLint("MissingPermission")
    fun write(text: String) {
        if (!hasConnectPermission()) return
        val c = rxChar ?: return
        val g = gatt ?: return
        val bytes = text.toByteArray(Charsets.UTF_8)
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                g.writeCharacteristic(c, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
            } else {
                @Suppress("DEPRECATION")
                c.value = bytes
                @Suppress("DEPRECATION")
                c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                @Suppress("DEPRECATION")
                g.writeCharacteristic(c)
            }
        } catch (e: SecurityException) {
            // permission revoked mid-flight
        }
    }

    private fun scheduleReconnect() {
        if (!active) return
        handler.removeCallbacks(reconnectRunnable)
        handler.postDelayed(reconnectRunnable, RECONNECT_MS)
    }

    /** Stop scanning, drop the link and cancel any pending reconnect. */
    @SuppressLint("MissingPermission")
    fun disconnect() {
        active = false
        handler.removeCallbacksAndMessages(null)
        stopScan()
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (e: SecurityException) {
            // ignore
        }
        gatt = null
        txChar = null
        rxChar = null
        connected.value = false
    }
}
