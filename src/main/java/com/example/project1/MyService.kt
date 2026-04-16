package com.example.project1

import android.annotation.SuppressLint
import android.app.*
import android.content.*
import android.location.*
import android.net.wifi.WifiManager
import android.os.*
import android.telephony.*
import android.util.Log
import androidx.core.app.NotificationCompat
import org.json.*
import org.zeromq.*
import java.net.*
import java.text.SimpleDateFormat
import java.util.*

class MyService : Service() {
    private var isRunning = false
    private var serverIP = "127.0.0.1"
    private val CHANNEL_ID = "monitoring_channel"
    private var currentLat = 0.0
    private var currentLon = 0.0
    private var currentRsrp = -120
    private var currentRawCellInfo = "rssi=-120"

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        Log.d("MY_SERVICELOG", "Служба создана")

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(CHANNEL_ID, "Monitoring", NotificationManager.IMPORTANCE_LOW)
            getSystemService(NotificationManager::class.java)?.createNotificationChannel(channel)
        }

        val pm = getSystemService(POWER_SERVICE) as PowerManager
        pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "P1:W").acquire()

        val wm = applicationContext.getSystemService(WIFI_SERVICE) as WifiManager
        wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "P1:WF").acquire()

        setupListeners()
    }

    @SuppressLint("MissingPermission")
    private fun setupListeners() {
        Log.d("MY_SERVICELOG", "Настройка слушателей GPS и сотовой сети")

        val lm = getSystemService(LOCATION_SERVICE) as LocationManager
        try {
            lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f) { l ->
                currentLat = l.latitude
                currentLon = l.longitude
            }
        } catch (e: Exception) {
            Log.e("MY_SERVICELOG", "Ошибка GPS: ${e.message}")
        }

        val tm = getSystemService(TELEPHONY_SERVICE) as TelephonyManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            tm.registerTelephonyCallback(mainExecutor, object : TelephonyCallback(), TelephonyCallback.SignalStrengthsListener {
                override fun onSignalStrengthsChanged(sg: SignalStrength) {
                    val lte = sg.cellSignalStrengths.filterIsInstance<CellSignalStrengthLte>().firstOrNull()
                    if (lte != null) {
                        currentRsrp = lte.rsrp

                        currentRawCellInfo = lte.toString()
                        Log.d("MY_SERVICELOG", "Обновлен сигнал: RSRP=$currentRsrp")
                    }
                }
            })
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val newIp = intent?.getStringExtra("SERVER_IP")
        if (!newIp.isNullOrEmpty()) {
            serverIP = newIp
            Log.d("MY_SERVICELOG", "целевой IP: $serverIP")
        }
        if (!isRunning) {
            isRunning = true
            val notification = NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("Signal Monitor")
                .setContentText("Отправка данных на $serverIP")
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setOngoing(true)
                .build()

            startForeground(1, notification)
            Thread { telemetryLoop() }.start()
        }
        return START_STICKY
    }

    private fun telemetryLoop() {
        Log.d("MY_SERVICELOG", "Цикл телеметрии запущен")
        val zCtx = ZContext()
        val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())

        while (isRunning) {
            var sock: ZMQ.Socket? = null
            try {
                val json = JSONObject().apply {
                    put("latitude", currentLat)
                    put("longitude", currentLon)
                    put("rsrp", currentRsrp)
                    put("cellInfo", currentRawCellInfo)
                    put("time", dateFormat.format(Date()))
                }

                val jsonString = json.toString()
                Log.d("MY_SERVICELOG", "Сформирован пакет: $jsonString")
                sock = zCtx.createSocket(SocketType.REQ)
                sock.connect("tcp://$serverIP:5554")
                sock.sendTimeOut = 1500
                sock.receiveTimeOut = 1500

                val sent = sock.send(jsonString.toByteArray(ZMQ.CHARSET), 0)
                if (sent) {
                    Log.d("MY_SERVICELOG", "Данные ушли на $serverIP, ждем ответ...")
                    val reply = sock.recv(0)
                    if (reply != null) {
                        Log.d("MY_SERVICELOG", "Сервер ответил: ${String(reply, ZMQ.CHARSET)}")
                    } else {
                        Log.w("MY_SERVICELOG", "Timeout")
                    }
                } else {
                    Log.e("MY_SERVICELOG", "Не удалось отправить пакет (ошибка сокета)")
                }

            } catch (e: Exception) {
                Log.e("MY_SERVICELOG", "Ошибка в telemetryLoop: ${e.message}")
            } finally {
                try { sock?.close() } catch (e: Exception) {}
            }
            try { Thread.sleep(1000) } catch (e: Exception) {}
        }

        Log.d("MY_SERVICELOG", "Цикл телеметрии остановлен")
        zCtx.destroy()
    }

    override fun onDestroy() {
        Log.d("MY_SERVICELOG", "Служба была выключена")
        isRunning = false
        super.onDestroy()
    }
}