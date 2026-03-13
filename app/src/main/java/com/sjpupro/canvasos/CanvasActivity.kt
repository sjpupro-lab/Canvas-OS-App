package com.sjpupro.canvasos

import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.sjpupro.canvasos.databinding.ActivityCanvasBinding
import kotlinx.coroutines.*
import java.io.*

/**
 * CanvasActivity — CanvasOS 캔버스 시각화 화면
 *
 * 네이티브 프로세스를 GUI 모드로 실행하여 캔버스를 실시간 렌더링.
 * 5가지 시각화 모드 전환, 터치로 셀 편집, 타임라인 탐색 지원.
 */
class CanvasActivity : AppCompatActivity(), CanvasView.OnCellTouchListener {

    private lateinit var binding: ActivityCanvasBinding
    private var process: Process? = null
    private var processWriter: BufferedWriter? = null
    private var outputJob: Job? = null
    private var refreshJob: Job? = null
    private var currentMode = 0  // 0=ABGR, 1=Energy, 2=Opcode, 3=Lane, 4=Activity
    private val modeNames = arrayOf("ABGR", "Energy", "Opcode", "Lane", "Activity")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityCanvasBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val dataDir = NativeBridge.getDataDir()
        val bmpPath = "$dataDir/canvas_gui.bmp"

        binding.canvasView.cellTouchListener = this
        binding.canvasView.bmpPath = bmpPath

        // 시각화 모드 전환
        binding.btnMode.setOnClickListener {
            currentMode = (currentMode + 1) % modeNames.size
            val modeName = modeNames[currentMode]
            binding.btnMode.text = modeName
            binding.canvasView.visMode = modeName
            sendCommand("vis $modeName")
        }

        // 게이트 오버레이 토글
        binding.btnGate.setOnClickListener {
            sendCommand("gate toggle")
        }

        // 타임라인 제어
        binding.btnRewind.setOnClickListener { sendCommand("timewarp -10") }
        binding.btnPlay.setOnClickListener { sendCommand("tick") }
        binding.btnForward.setOnClickListener { sendCommand("timewarp +10") }

        // 스냅샷
        binding.btnSnapshot.setOnClickListener {
            sendCommand("snapshot gui_snap")
            Toast.makeText(this, "Snapshot saved", Toast.LENGTH_SHORT).show()
        }

        // 해시 검증
        binding.btnHash.setOnClickListener { sendCommand("hash") }

        // 터미널 전환
        binding.btnTerminal.setOnClickListener { finish() }

        // 네이티브 프로세스 시작
        startCanvasProcess(bmpPath)
    }

    private fun startCanvasProcess(bmpPath: String) {
        try {
            process = NativeBridge.startProcess("gui")
            processWriter = process!!.outputStream.bufferedWriter()

            // GUI 모드 초기화 — BMP 출력 경로 설정
            sendCommand("gui_output $bmpPath")
            sendCommand("vis ABGR")

            // stdout 로그 읽기
            outputJob = lifecycleScope.launch(Dispatchers.IO) {
                val reader = process!!.inputStream.bufferedReader()
                try {
                    val buf = CharArray(2048)
                    while (isActive) {
                        val n = reader.read(buf)
                        if (n == -1) break
                        val text = String(buf, 0, n)
                        // 상태 파싱
                        parseStatus(text)
                    }
                } catch (_: IOException) {}
            }

            // 자동 tick + BMP 갱신 요청
            refreshJob = lifecycleScope.launch(Dispatchers.IO) {
                while (isActive) {
                    delay(100)
                    sendCommand("gui_refresh")
                }
            }

        } catch (e: Exception) {
            Toast.makeText(this, "Error: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun parseStatus(text: String) {
        // 간단한 상태 파싱: "TICK:1234" "HASH:ABCD1234"
        for (line in text.lines()) {
            when {
                line.startsWith("TICK:") -> {
                    val tick = line.removePrefix("TICK:").trim().toLongOrNull() ?: 0
                    binding.canvasView.tickCount = tick
                }
                line.startsWith("HASH:") -> {
                    binding.canvasView.statusText = line.trim()
                }
                line.startsWith("STATUS:") -> {
                    binding.canvasView.statusText = line.removePrefix("STATUS:").trim()
                }
            }
        }
    }

    override fun onCellTouch(x: Int, y: Int) {
        // 터치 좌표를 네이티브에 전달
        sendCommand("touch $x $y")
        binding.tvCellInfo.text = "Cell($x,$y)"
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
        binding.canvasView.cleanup()
        try {
            processWriter?.write("exit\n")
            processWriter?.flush()
            processWriter?.close()
        } catch (_: Exception) {}
        process?.destroyForcibly()
    }
}
