package com.example.project1

import android.content.Intent
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class ControlActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_control)

        val tvStatus = findViewById<TextView>(R.id.tvStatus)
        val etIp = findViewById<EditText>(R.id.etServerIp)
        val btnStart = findViewById<Button>(R.id.btnStart)
        val btnStop = findViewById<Button>(R.id.btnStop)

        btnStart.setOnClickListener {
            val ip = etIp.text.toString().trim()
            if (ip.isNotEmpty()) {
                val intent = Intent(this, MyService::class.java)
                intent.putExtra("SERVER_IP", ip)

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    startForegroundService(intent)
                } else {
                    startService(intent)
                }

                tvStatus.text = "СЕРВИС: ЗАПУЩЕН"
                tvStatus.setTextColor(Color.GREEN)
            }
        }

        btnStop.setOnClickListener {
            stopService(Intent(this, MyService::class.java))
            tvStatus.text = "СЕРВИС: ОСТАНОВЛЕН"
            tvStatus.setTextColor(Color.RED)
        }
    }
}