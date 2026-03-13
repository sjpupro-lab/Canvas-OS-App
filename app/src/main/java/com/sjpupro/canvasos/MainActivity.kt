package com.sjpupro.canvasos

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.sjpupro.canvasos.databinding.ActivityMainBinding

/**
 * CanvasOS Main Launcher — 홈 화면
 *
 * 앱 아이콘 터치 → 이 화면 → 모드 선택
 */
class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // 바이너리 설치
        NativeBridge.install(this)

        binding.btnLauncher.setOnClickListener {
            startTerminal("launcher")
        }

        binding.btnCli.setOnClickListener {
            startTerminal("cli")
        }

        binding.btnTervas.setOnClickListener {
            startTerminal("tervas")
        }

        binding.btnTest.setOnClickListener {
            startTerminal("test")
        }

        binding.btnCanvas.setOnClickListener {
            val intent = Intent(this, CanvasActivity::class.java)
            startActivity(intent)
        }

        // 버전 표시
        binding.tvVersion.text = "v1.1.0 | aarch64"
    }

    private fun startTerminal(mode: String) {
        val intent = Intent(this, TerminalActivity::class.java).apply {
            putExtra("mode", mode)
        }
        startActivity(intent)
    }
}
