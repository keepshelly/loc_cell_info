package com.example.project1
import android.graphics.Color
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import com.example.project1.R
class CalculatorActivity : AppCompatActivity() {
    private lateinit var tvdisplay: TextView
    private var input = "0"
    private var operator = ""
    private var firstnumber = 0.0
    private var secondnumber = 0.0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_calculator)
        tvdisplay = findViewById(R.id.tvdisplay)
        number()
        operation()
        val btnclear: Button = findViewById(R.id.btnclear)
        btnclear.setOnClickListener {
            clear()
        }
        val btnravno: Button = findViewById(R.id.btnravno)
        btnravno.setOnClickListener {
            result()
        }
    }
    private fun number() {
        val buttons = listOf(
            R.id.btn0, R.id.btn1, R.id.btn2, R.id.btn3, R.id.btn4,
            R.id.btn5, R.id.btn6, R.id.btn7, R.id.btn8, R.id.btn9
        )
        buttons.forEach { buttonId ->
            val button: Button = findViewById(buttonId)
            button.setOnClickListener {
                val number = (it as Button).text.toString()
                addnumber(number)
            }
        }
    }
    private fun operation() {
        val btnsum: Button = findViewById(R.id.btnsum)
        val btnminus: Button = findViewById(R.id.btnminus)
        val btnumnozh: Button = findViewById(R.id.btnumnozh)
        val btndel: Button = findViewById(R.id.btndel)
        btnsum.setOnClickListener { setoperator("+") }
        btnminus.setOnClickListener { setoperator("-") }
        btnumnozh.setOnClickListener { setoperator("*") }
        btndel.setOnClickListener { setoperator("/") }
    }
    private fun addnumber(number: String) {
        if (input == "0") input = number else input += number
        display()
    }
    private fun setoperator(op: String) {
        if (operator == "") {
            firstnumber = input.toDouble()
            operator = op
            input = "0"
            display()
        }
    }
    private fun result() {
        if (operator != "") {
            secondnumber = input.toDouble()
            var result = 0.0
            when (operator) {
                "+" -> result = firstnumber + secondnumber
                "-" -> result = firstnumber - secondnumber
                "*" -> result = firstnumber * secondnumber
                "/" -> {
                    if (secondnumber != 0.0) {
                        result = firstnumber / secondnumber
                    } else {
                        tvdisplay.text = "Ошибка"
                        return
                    }
                }
            }
            input = if (result % 1 == 0.0) result.toInt().toString() else result.toString()
            operator = ""
            display()
        }
    }
    private fun clear() {
        input = "0"
        operator = ""
        firstnumber = 0.0
        secondnumber = 0.0
        display()
    }
    private fun display() {
        tvdisplay.text = input
    }
}
