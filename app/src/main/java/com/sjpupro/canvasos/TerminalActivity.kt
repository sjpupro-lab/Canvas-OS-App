package com.sjpupro.canvasos

import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.KeyEvent
import android.view.inputmethod.EditorInfo
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.sjpupro.canvasos.databinding.ActivityTerminalBinding
import kotlinx.coroutines.*
import java.io.*

/**
 * TerminalActivity — CanvasOS 터미널 화면
 *
 * 네이티브 canvasos_launcher 프로세스를 실행하고
 * stdin/stdout을 터치 UI와 연결.
 */
class TerminalActivity : AppCompatActivity() {

    private lateinit var binding: ActivityTerminalBinding
    private var process: Process? = null
    private var processWriter: BufferedWriter? = null
    private var outputJob: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityTerminalBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.tvOutput.movementMethod = ScrollingMovementMethod()

        val mode = intent.getStringExtra("mode") ?: "launcher"
        startCanvasOS(mode)

        // 입력 전송
        binding.btnSend.setOnClickListener { sendInput() }
        binding.etInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_SEND ||
                actionId == EditorInfo.IME_ACTION_DONE) {
                sendInput()
                true
            } else false
        }

        // 퀵 버튼
        binding.btnHome.setOnClickListener { sendCommand("home") }
        binding.btnMap.setOnClickListener { sendCommand("map") }
        binding.btnStat.setOnClickListener { sendCommand("stat") }
        binding.btnHelp.setOnClickListener { sendCommand("help") }
        binding.btnHash.setOnClickListener { sendCommand("hash") }
        binding.btnSave.setOnClickListener { sendCommand("save") }

        // 방향키
        binding.btnUp.setOnClickListener { sendCommand("^") }
        binding.btnDown.setOnClickListener { sendCommand("v") }
        binding.btnLeft.setOnClickListener { sendCommand("<") }
        binding.btnRight.setOnClickListener { sendCommand(">") }
    }

    private fun startCanvasOS(mode: String) {
        try {
            process = NativeBridge.startProcess(mode)
            processWriter = process!!.outputStream.bufferedWriter()

            // stdout 읽기 (코루틴)
            outputJob = lifecycleScope.launch(Dispatchers.IO) {
                val reader = process!!.inputStream.bufferedReader()
                val ansiRegex = Regex("\u001B\\[[0-9;]*[a-zA-Z]")
                val buffer = StringBuilder()

                try {
                    val charBuf = CharArray(4096)
                    while (isActive) {
                        val n = reader.read(charBuf)
                        if (n == -1) break

                        val raw = String(charBuf, 0, n)
                        // ANSI 이스케이프 코드 제거 (터미널 UI에서는 별도 처리)
                        val cleaned = raw.replace(ansiRegex, "")
                            .replace("\u001B[2J\u001B[H", "\n--- screen ---\n")

                        buffer.append(cleaned)

                        // UI 업데이트
                        withContext(Dispatchers.Main) {
                            binding.tvOutput.append(cleaned)
                            // 자동 스크롤
                            val scrollAmount = binding.tvOutput.layout?.let {
                                it.getLineTop(binding.tvOutput.lineCount) -
                                    binding.tvOutput.height
                            } ?: 0
                            if (scrollAmount > 0) {
                                binding.tvOutput.scrollTo(0, scrollAmount)
                            }
                        }
                    }
                } catch (e: IOException) {
                    // 프로세스 종료
                }
            }
        } catch (e: Exception) {
            binding.tvOutput.text = "Error: ${e.message}"
        }
    }

    private fun sendInput() {
        val text = binding.etInput.text.toString()
        if (text.isNotEmpty()) {
            sendCommand(text)
            binding.etInput.text?.clear()
        }
    }

    private fun sendCommand(cmd: String) {
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                processWriter?.write("$cmd\n")
                processWriter?.flush()
            } catch (e: IOException) {
                // 프로세스 종료됨
            }
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
