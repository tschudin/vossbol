package nz.scuttlebutt.tremolavossbol.tssb.ble

import android.Manifest
import android.bluetooth.*
import android.bluetooth.BluetoothGatt.GATT_SUCCESS
import android.bluetooth.BluetoothProfile.STATE_CONNECTED
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.bluetooth.le.ScanSettings.MATCH_MODE_STICKY
import android.content.Context
import android.content.pm.PackageManager
import android.location.LocationManager
import android.os.Build
import android.os.ParcelUuid
import android.util.Log
import android.widget.Toast
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import nz.scuttlebutt.tremolavossbol.MainActivity
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_BLE_REPL_SERVICE_2022
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_BLE_RX_CHARACTERISTIC
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_BLE_TX_CHARACTERISTIC
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions.Companion.toHex
import kotlin.collections.HashMap

class BlePeers(val act: MainActivity) {

    private val bluetoothManager = act.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter = bluetoothManager.adapter
    private val bleScanner = bluetoothAdapter.bluetoothLeScanner
    val peers = HashMap<BluetoothDevice,BluetoothGatt>(0)
    var isScanning = false

    private val scanSettings = ScanSettings.Builder()
        .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
        .setScanMode(MATCH_MODE_STICKY)
        .build()

    private val scanFilter = ScanFilter.Builder()
        .setServiceUuid(ParcelUuid(TINYSSB_BLE_REPL_SERVICE_2022))
        .build()

    private val isLocationPermissionGranted
        get() = ContextCompat.checkSelfPermission(act, Manifest.permission.ACCESS_FINE_LOCATION) ==
                PackageManager.PERMISSION_GRANTED

    private fun isLocationEnabled(): Boolean {
        val locationManager = act.getSystemService(Context.LOCATION_SERVICE) as LocationManager
        return locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)
    }

    fun write(buf: ByteArray) {
        for (p in peers) {
            // Log.d("ble", "sending (rx char) ${buf.size} bytes")
            val service = p.value.getService(TINYSSB_BLE_REPL_SERVICE_2022)
            val ch = service.getCharacteristic(TINYSSB_BLE_RX_CHARACTERISTIC)
            ch.value = buf
            p.value.writeCharacteristic(ch)
        }
    }

    fun startBleScan() {
        if (isScanning)
            return
        val pm: PackageManager = act.getPackageManager()
        if (!pm.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            Toast.makeText(act, "this device does NOT have Bluetooth LE - user Wifi to sync",
                Toast.LENGTH_LONG).show()
            return
        }
        if (!bluetoothAdapter.isEnabled) {
            Toast.makeText(act, "Bluetooth MUST be enabled for using BlueTooth-Low-Energy sync, then restart",
                Toast.LENGTH_LONG).show()
            /*
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            startActivityForResult(act, enableBtIntent, 444, null)
            */
            return
        }
        if (!isLocationEnabled()) {
            Toast.makeText(
                act, "Location MUST be enabled for using BlueTooth-Low-Energy sync, then restart",
                Toast.LENGTH_LONG
            ).show()
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !isLocationPermissionGranted) {
            ActivityCompat.requestPermissions(act, arrayOf(Manifest.permission.ACCESS_FINE_LOCATION), 555)
            return
        }
        Log.d("ble", "starting scan")
            // scanResults.clear()
            // scanResultAdapter.notifyDataSetChanged()
        if (bleScanner != null) {
            bleScanner.startScan(mutableListOf(scanFilter), scanSettings, scanCallback)
            isScanning = true
        }
    }

    fun stopBleScan() {
        if (bleScanner != null)
            bleScanner.stopScan(scanCallback)
        isScanning = false
    }

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onCharacteristicChanged(gatt: BluetoothGatt?, ch: BluetoothGattCharacteristic?) {
            // Log.d("ble", "change..")
            super.onCharacteristicChanged(gatt, ch)
            if (ch != null) {
                // Log.d("ble", "${ch.uuid.toString()} changed: ${ch.value.toHex()}, ${ch.value.size}")
                act.ioLock.lock()
                val rc = act.tinyDemux.on_rx(ch.value)
                act.ioLock.unlock()
                if (!rc)
                    Log.d("ble rx", "not dmx entry for ${ch.value.toHex()}")
            }
        }

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            super.onConnectionStateChange(gatt, status, newState)
            if (status == GATT_SUCCESS && newState == STATE_CONNECTED) {
                Log.d("ble", "connected! ${gatt.device.address.toString()}")
                val rc = gatt.requestMtu(128)
                Log.d("ble mtu", "request rc=$rc")
            } else if (gatt != null && gatt.device in peers)
                peers.remove(gatt.device)
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            super.onServicesDiscovered(gatt, status)
            if (gatt != null)
                for (s in gatt.services) {
                    Log.d("ble discovered services", "${s.uuid.toString()}")
                    if (s.uuid.toString() == TINYSSB_BLE_REPL_SERVICE_2022.toString()) {
                        for (c in s.characteristics)
                            Log.d("ble discovered ch", c.uuid.toString())
                        val ch = s.getCharacteristic(TINYSSB_BLE_TX_CHARACTERISTIC)
                        if (ch == null)
                            Log.d("ble disc", "ch still is null")
                        else {
                            Log.d("ble disc", "ch ${ch.uuid.toString()} found, enable notif")
                            gatt.setCharacteristicNotification(ch, true)
                            val lst = ch.getDescriptors(); //find the descriptors on the characteristic
                            // val ndx = lst.indexOfFirst { it.uuid == device.address == result.device.address }
                            // if (indexQuery != -1) { // A scan result already exists with the same address
                            // for (d in lst)
                            //    Log.d("ble", "descriptor ${d.uuid.toString()}")
                            val descr = lst.get(0); // there should be only one descriptor
                            descr.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                            gatt.writeDescriptor(descr); //apply these changes to the ble chip to tell it we are ready for the data
                        }
                    } else
                        Log.d("ble", "hm, service is ${s.uuid.toString()} vs ${TINYSSB_BLE_REPL_SERVICE_2022.toString()}")
                }
            /*
            characteristic = gatt.getService(UUID.fromString(SERVICE_UUID)).getCharacteristic(UUID.fromString(CHARACTERISTIC_UUID)); //Find you characteristic
            mGatt.setCharacteristicNotification(characteristic, true); //Tell you gatt client that you want to listen to that characteristic
            List<BluetoothGattDescriptor> descriptors = characteristic.getDescriptors(); //find the descriptors on the characteristic
            BluetoothGattDescriptor descriptor = descriptors.get(1); //get the right descriptor for setting notifications
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            mGatt.writeDescriptor(descriptor); //apply these changes to the ble chip to tell it we are ready for the data

             */
        }

        override fun onMtuChanged(gatt: BluetoothGatt?, mtu: Int, status: Int) {
            super.onMtuChanged(gatt, mtu, status)
            Log.d("ble mtu", "status=$status, mtu=$mtu")
            if (status == GATT_SUCCESS && gatt != null) {
                Log.d("ble gatt", "${gatt.discoverServices()}")
                for (s in gatt.services) {
                    Log.d("ble services", "${s.uuid.toString()}")
                }
                val service = gatt.getService(TINYSSB_BLE_REPL_SERVICE_2022)
                if (service != null) {
                    val ch = service.getCharacteristic(TINYSSB_BLE_TX_CHARACTERISTIC)
                    if (ch != null)
                        gatt.setCharacteristicNotification(ch, true);
                    else
                        Log.d("ble", "ch is null")
                } else
                    Log.d("ble", "service is null")
            }
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            super.onScanResult(callbackType, result)
            if (!(result.device in peers)) {
                Log.d("ble", "Found BLE tinySSB device! adding address: ${result.device.address}")
                val g = result.device.connectGatt(act, true, gattCallback)
                peers[result.device] = g
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.d("ble","onScanFailed: code $errorCode")
        }
    }

}
