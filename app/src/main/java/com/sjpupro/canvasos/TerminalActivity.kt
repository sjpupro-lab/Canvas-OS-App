package com.sjpupro.canvasos

import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.util.Log
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.*
import java.io.*

/**
 * TerminalActivity — CanvasOS 터미널 화면
 */
class TerminalActivity : AppCompatActivity() {

    private val TAG = "TerminalActivity"
    private var tvOutput: TextView? = null
    private var etInput: EditText? = null
    private var process: Process? = null
    private var processWriter: BufferedWriter? = null
    private var outputJob: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        try {
            setContentView(R.layout.activity_terminal)
        } catch (e: Exception) {
            Log.e(TAG, "Layout failed", e)
            val tv = TextView(this)
            tv.text = "Layout error:\n${e.stackTraceToString()}"
            tv.textSize = 11f
            tv.setTextColor(0xFFFF4444.toInt())
            tv.setBackgroundColor(0xFF000000.toInt())
            tv.setPadding(16, 48, 16, 16)
            setContentView(tv)
            return
        }

        tvOutput = findViewById(R.id.tvOutput)
        etInput = findViewById(R.id.etInput)

        tvOutput?.movementMethod = ScrollingMovementMethod()

        val mode = intent.getStringExtra("mode") ?: "launcher"

        // 디버그 정보 먼저 표시
        tvOutput?.text = "Starting CanvasOS ($mode)...\n" +
            "Binary: ${NativeBridge.getBinaryPath()}\n" +
            "Installed: ${NativeBridge.isInstalled()}\n\n"

        startCanvasOS(mode)

        // 입력
        findViewById<Button>(R.id.btnSend)?.setOnClickListener { sendInput() }
        etInput?.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_SEND ||
                actionId == EditorInfo.IME_ACTION_DONE) {
                sendInput(); true
            } else false
        }

        // 퀵 버튼
        findViewById<Button>(R.id.btnHome)?.setOnClickListener { sendCommand("home") }
        findViewById<Button>(R.id.btnMap)?.setOnClickListener { sendCommand("map") }
        findViewById<Button>(R.id.btnStat)?.setOnClickListener { sendCommand("stat") }
        findViewById<Button>(R.id.btnHelp)?.setOnClickListener { sendCommand("help") }
        findViewById<Button>(R.id.btnHash)?.setOnClickListener { sendCommand("hash") }
        findViewById<Button>(R.id.btnSave)?.setOnClickListener { sendCommand("save") }

        // 방향키
        findViewById<Button>(R.id.btnUp)?.setOnClickListener { sendCommand("^") }
        findViewById<Button>(R.id.btnDown)?.setOnClickListener { sendCommand("v") }
        findViewById<Button>(R.id.btnLeft)?.setOnClickListener { sendCommand("<") }
        findViewById<Button>(R.id.btnRight)?.setOnClickListener { sendCommand(">") }
    }

    private fun startCanvasOS(mode: String) {
        try {
            val proc = NativeBridge.startProcess(mode)
            if (proc == null) {
                appendOutput("[ERROR] Failed to start native process\n" +
                    "Binary: ${NativeBridge.getBinaryPath()}\n" +
                    "NativeDir: ${applicationInfo.nativeLibraryDir}\n")
                return
            }

            process = proc
            processWriter = proc.outputStream.bufferedWriter()
            appendOutput("[OK] Process started\n\n")

            outputJob = lifecycleScope.launch(Dispatchers.IO) {
                val reader = proc.inputStream.bufferedReader()
                val ansiRegex = Regex("\u001B\\[[0-9;]*[a-zA-Z]")

                try {
                    val charBuf = CharArray(4096)
                    while (isActive) {
                        val n = reader.read(charBuf)
                        if (n == -1) {
                            appendOutput("\n[Process exited]")
                            break
                        }
                        val raw = String(charBuf, 0, n)
                        val cleaned = raw.replace(ansiRegex, "")
                            .replace("\u001B[2J\u001B[H", "\n--- screen ---\n")
                        appendOutput(cleaned)
                    }
                } catch (e: IOException) {
                    appendOutput("\n[IO Error: ${e.message}]")
                }
            }
        } catch (e: Exception) {
            appendOutput("[CRASH] ${e.message}\n${e.stackTraceToString()}")
            Log.e(TAG, "startCanvasOS failed", e)
        }
    }

    private fun appendOutput(text: String) {
        runOnUiThread {
            tvOutput?.append(text)
            // 자동 스크롤
            tvOutput?.let { tv ->
                val scrollAmount = tv.layout?.let {
                    it.getLineTop(tv.lineCount) - tv.height
                } ?: 0
                if (scrollAmount > 0) tv.scrollTo(0, scrollAmount)
            }
        }
    }

    private fun sendInput() {
        val text = etInput?.text?.toString() ?: return
        if (text.isNotEmpty()) {
            sendCommand(text)
            etInput?.text?.clear()
        }
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
        outputJob?.cancel()
        try {
            processWriter?.write("exit\n")
            processWriter?.flush()
            processWriter?.close()
        } catch (_: Exception) {}
        process?.destroyForcibly()
    }
}
