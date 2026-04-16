package com.example.project1

import android.annotation.SuppressLint
import android.os.*
import android.widget.*
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.*
import org.json.*
import org.zeromq.*

class SocketsActivity : AppCompatActivity() {
    private lateinit var tvLog: TextView
    private lateinit var etIp: EditText
    private lateinit var btnSend: Button
    private lateinit var handler: Handler

    @SuppressLint("MissingInflatedId")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_sockets)

        handler = Handler(Looper.getMainLooper())
        tvLog = findViewById(R.id.tvlog)
        etIp = findViewById(R.id.etServerIp)
        btnSend = findViewById(R.id.btntoserv)

        btnSend.setOnClickListener {
            val ip = etIp.text.toString().trim()
            Thread {
                val json = JSONObject().apply {
                    put("location", JSONObject().apply { put("lat", 55.03); put("lon", 82.92) })
                    put("telephony_data", JSONArray().apply {
                        put(JSONObject().apply {
                            put("CellInfoLte", JSONObject().apply {
                                put("CellSignalStrengthLte", JSONObject().apply { put("RSRP", -85) })
                            })
                        })
                    })
                }
                toserv(ip, json.toString())
            }.start()
        }
    }

    private fun toserv(ip: String, mes: String) {
        val zCtx = ZContext()
        try {
            val sock = zCtx.createSocket(SocketType.REQ)
            sock.connect("tcp://$ip:5554")
            sock.sendTimeOut = 2000
            sock.send(mes.toByteArray(ZMQ.CHARSET), 0)
            handler.post { tvLog.append("\nOUT: $mes") }
            val reply = sock.recv(0)
            if (reply != null) {
                val resp = String(reply, ZMQ.CHARSET)
                handler.post { tvLog.append("\nIN: $resp") }
            }
            sock.close()
        } catch (e: Exception) {
            handler.post { tvLog.append("\nERR: ${e.message}") }
        } finally {
            zCtx.destroy()
        }
    }
}