package com.sjpupro.canvasos

import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.*
import java.io.*

/**
 * CanvasActivity — CanvasOS 캔버스 시각화 화면
 */
class CanvasActivity : AppCompatActivity(), CanvasView.OnCellTouchListener {

    private val TAG = "CanvasActivity"
    private var canvasView: CanvasView? = null
    private var tvCellInfo: TextView? = null
    private var process: Process? = null
    private var processWriter: BufferedWriter? = null
    private var outputJob: Job? = null
    private var refreshJob: Job? = null
    private var currentMode = 0
    private val modeNames = arrayOf("ABGR", "Energy", "Opcode", "Lane", "Activity")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        try {
            setContentView(R.layout.activity_canvas)
        } catch (e: Exception) {
            Log.e(TAG, "Layout failed", e)
            val tv = TextView(this)
            tv.text = "Layout error:\n${e.stackTraceToString()}"
            tv.setTextColor(0xFFFF4444.toInt())
            tv.setBackgroundColor(0xFF000000.toInt())
            setContentView(tv)
            return
        }

        canvasView = findViewById(R.id.canvasView)
        tvCellInfo = findViewById(R.id.tvCellInfo)

        val dataDir = NativeBridge.getDataDir()
        val bmpPath = "$dataDir/canvas_gui.bmp"

        canvasView?.cellTouchListener = this
        canvasView?.bmpPath = bmpPath

        val btnMode = findViewById<Button>(R.id.btnMode)
        btnMode?.setOnClickListener {
            currentMode = (currentMode + 1) % modeNames.size
            val name = modeNames[currentMode]
            btnMode.text = name
            canvasView?.visMode = name
            sendCommand("vis $name")
        }

        findViewById<Button>(R.id.btnGate)?.setOnClickListener { sendCommand("gate toggle") }
        findViewById<Button>(R.id.btnRewind)?.setOnClickListener { sendCommand("timewarp -10") }
        findViewById<Button>(R.id.btnPlay)?.setOnClickListener { sendCommand("tick") }
        findViewById<Button>(R.id.btnForward)?.setOnClickListener { sendCommand("timewarp +10") }
        findViewById<Button>(R.id.btnSnapshot)?.setOnClickListener {
            sendCommand("snapshot gui_snap")
            Toast.makeText(this, "Snapshot saved", Toast.LENGTH_SHORT).show()
        }
        findViewById<Button>(R.id.btnHash)?.setOnClickListener { sendCommand("hash") }
        findViewById<Button>(R.id.btnTerminal)?.setOnClickListener { finish() }

        startCanvasProcess(bmpPath)
    }

    private fun startCanvasProcess(bmpPath: String) {
        try {
            val proc = NativeBridge.startProcess("gui")
            if (proc == null) {
                Toast.makeText(this, "Failed to start engine", Toast.LENGTH_LONG).show()
                return
            }
            process = proc
            processWriter = proc.outputStream.bufferedWriter()

            sendCommand("gui_output $bmpPath")
            sendCommand("vis ABGR")

            outputJob = lifecycleScope.launch(Dispatchers.IO) {
                val reader = proc.inputStream.bufferedReader()
                try {
                    val buf = CharArray(2048)
                    while (isActive) {
                        val n = reader.read(buf)
                        if (n == -1) break
                        parseStatus(String(buf, 0, n))
                    }
                } catch (_: IOException) {}
            }

            refreshJob = lifecycleScope.launch(Dispatchers.IO) {
                while (isActive) {
                    delay(200)
                    sendCommand("gui_refresh")
                }
            }
        } catch (e: Exception) {
            Toast.makeText(this, "Error: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun parseStatus(text: String) {
        for (line in text.lines()) {
            when {
                line.startsWith("TICK:") -> canvasView?.tickCount = line.removePrefix("TICK:").trim().toLongOrNull() ?: 0
                line.startsWith("HASH:") -> canvasView?.statusText = line.trim()
                line.startsWith("STATUS:") -> canvasView?.statusText = line.removePrefix("STATUS:").trim()
            }
        }
    }

    override fun onCellTouch(x: Int, y: Int) {
        sendCommand("touch $x $y")
        tvCellInfo?.text = "Cell($x,$y)"
    }

    private fun sendCommand(cmd: String) {
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                processWriter?.write("$cmd\n")
                processWriter?.flush()
            } catch (_: IOException) {}
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        refreshJob?.cancel()
        outputJob?.cancel()
        canvasView?.cleanup()
        try { processWriter?.close() } catch (_: Exception) {}
        process?.destroyForcibly()
    }
}
