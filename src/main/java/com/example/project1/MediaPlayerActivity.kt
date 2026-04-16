package com.example.project1
import android.Manifest
import android.content.pm.PackageManager
import android.media.MediaPlayer
import android.os.Bundle
import android.os.Environment
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import java.io.File
class MediaPlayerActivity : AppCompatActivity() {
    private var mp: MediaPlayer? = null
    private lateinit var play: Button
    private lateinit var pause: Button
    private lateinit var next: Button
    private lateinit var prev: Button
    private lateinit var bar: SeekBar
    private lateinit var title: TextView
    private var songs = mutableListOf<File>()
    private var current = 0
    private val handler = android.os.Handler()
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_media_player)
        play = findViewById(R.id.playbutton)
        pause = findViewById(R.id.pausebutton)
        next = findViewById(R.id.nextbutton)
        prev = findViewById(R.id.prevbutton)
        bar = findViewById(R.id.bar)
        title = findViewById(R.id.tracktitle)
        if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE), 123)
        } else {
            initPlayer()
        }
        play.setOnClickListener {
            if (mp == null) {
                initPlayer()
            }
            mp?.start()
        }
        pause.setOnClickListener {
            mp?.pause()
        }
        next.setOnClickListener {
            if (songs.isEmpty())
                mp?.stop()
            mp?.release()
            current++
            if (current >= songs.size) current = 0
            setup()
            mp?.start()
        }
        prev.setOnClickListener {
            if (songs.isEmpty())
                mp?.stop()
            mp?.release()
            current--
            if (current < 0) current = songs.size - 1
            setup()
            mp?.start()
        }
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) mp?.seekTo(progress)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })
    }
    private fun initPlayer() {
        loadMusic()
        if (songs.isNotEmpty()) {
            setup()
        } else {
            Toast.makeText(this, "Нет музыки в папке", Toast.LENGTH_LONG).show()
        }
    }
    private fun loadMusic() {
        songs.clear()
        val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC)
        val files = dir?.listFiles()
        if (files != null) {
            for (f in files) {
                if (f.isFile && (f.name.endsWith(".mp3") || f.name.endsWith(".wav"))) {
                    songs.add(f)
                }
            }
        }
    }
    private fun setup() {
        val song = songs[current]
        title.text = song.name
        mp = MediaPlayer()
        mp?.setDataSource(song.path)
        mp?.prepare()
        bar.max = mp?.duration?:0
        bar.progress = 0
        updatebar()
        mp?.setOnCompletionListener {
            next.performClick()
        }
    }
    private fun updatebar() {
        handler.post(object : Runnable {
            override fun run() {
                if (mp != null && mp!!.isPlaying) {
                    bar.progress = mp!!.currentPosition
                }
                handler.postDelayed(this, 500)
            }
        })
    }
    override fun onDestroy() {
        super.onDestroy()
        mp?.stop()
        mp?.release()
    }
}