package com.example.project1
import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.Environment
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.*
class LocationActivity : AppCompatActivity(), LocationListener {
    private lateinit var locmanager: LocationManager
    private lateinit var tvLatitude: TextView
    private lateinit var tvLongitude: TextView
    private lateinit var tvAltitude: TextView
    private lateinit var tvTime: TextView
    private lateinit var back: Button
    companion object {
        private const val PERMISSION_REQUEST_ACCESS_LOCATION = 100
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_location)
        view()
        locmanager = getSystemService(Context.LOCATION_SERVICE) as LocationManager
    }
    override fun onResume() {
        super.onResume()
        updlocation()
    }
    private fun view() {
        tvLatitude = findViewById(R.id.tvLatitude)
        tvLongitude = findViewById(R.id.tvLongitude)
        tvAltitude = findViewById(R.id.tvAltitude)
        tvTime = findViewById(R.id.tvTime)
        back = findViewById(R.id.btnBack)
        back.setOnClickListener {
            finish()
        }
    }
    private fun updlocation() {
        if (checkperms()) {
            if (provider()) {
                if (ActivityCompat.checkSelfPermission(
                        this,
                        Manifest.permission.ACCESS_FINE_LOCATION
                    ) != PackageManager.PERMISSION_GRANTED && ActivityCompat.checkSelfPermission(
                        this,
                        Manifest.permission.ACCESS_COARSE_LOCATION
                    ) != PackageManager.PERMISSION_GRANTED
                ) {
                    requestperms()
                    return
                }
                locmanager.requestLocationUpdates(
                    LocationManager.GPS_PROVIDER,
                    1000L,
                    1f,
                    this
                )
                Toast.makeText(this, "обновление данных...", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(applicationContext, "включи gps", Toast.LENGTH_SHORT).show()
            }
        } else {
            requestperms()
        }
    }
    private fun checkperms(): Boolean {
        return ActivityCompat.checkSelfPermission(
            this,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED && ActivityCompat.checkSelfPermission(
            this,
            Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
    }
    private fun requestperms() {
        ActivityCompat.requestPermissions(
            this,
            arrayOf(
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.ACCESS_FINE_LOCATION
            ),
            PERMISSION_REQUEST_ACCESS_LOCATION
        )
    }
    private fun provider(): Boolean {
        return locmanager.isProviderEnabled(LocationManager.GPS_PROVIDER)
    }
    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSION_REQUEST_ACCESS_LOCATION) {
            if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                updlocation()
            } else {
                Toast.makeText(this, "нет прав", Toast.LENGTH_SHORT).show()
            }
        }
    }
    override fun onLocationChanged(location: Location) {
        updlocinfo(location)
        save(location)
        println("местоположение обновлено:${location.latitude}, ${location.longitude}")
    }
    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
    private fun updlocinfo(location: Location) {
        val latitude = location.latitude
        val longitude = location.longitude
        val altitude = location.altitude
        val time = location.time
        val dateformat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
        val currenttime = dateformat.format(Date(time))
        tvLatitude.text = "Широта: $latitude"
        tvLongitude.text = "Долгота: $longitude"
        tvAltitude.text = "Высота: ${"%.2f".format(altitude)}"
        tvTime.text = "Время: $currenttime"
    }
    private fun save(location: Location) {
        try {
            val data = JSONObject().apply {
                put("latitude", location.latitude)
                put("longitude", location.longitude)
                put("altitude", location.altitude)
                put("current_time", SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date(location.time)))
            }
            val jsonstring = data.toString()
            val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            val appdir = File(dir, "location")
            if (!appdir.exists()) {
                appdir.mkdirs()
            }
            val timestamp = SimpleDateFormat("HHmmss", Locale.getDefault()).format(Date())
            val file = File(appdir, "location_$timestamp.json")
            FileOutputStream(file).use { outputStream ->
                outputStream.write(jsonstring.toByteArray())
            }
        } catch (e: Exception) {
            println("ошибка при сохранении файла:${e.message}")
        }
    }
    private var updcount = 0
    private fun getupdcount(): Int {
        updcount++
        return updcount
    }
    override fun onDestroy() {
        super.onDestroy()
        locmanager.removeUpdates(this)
    }
}