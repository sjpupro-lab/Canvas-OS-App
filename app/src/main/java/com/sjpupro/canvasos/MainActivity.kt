package com.sjpupro.canvasos

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.io.File

/**
 * CanvasOS Main Launcher — 홈 화면
 */
class MainActivity : AppCompatActivity() {

    private val TAG = "MainActivity"
    private val PERM_REQUEST = 1001

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        try {
            setContentView(R.layout.activity_main)
        } catch (e: Exception) {
            Log.e(TAG, "setContentView failed", e)
            showCrashScreen("Layout inflate error:\n${e.message}\n\n${e.stackTraceToString()}")
            return
        }

        // 권한 요청
        requestPermissions()

        // 바이너리 설치
        var installOk = false
        try {
            installOk = NativeBridge.install(this)
        } catch (e: Exception) {
            Log.e(TAG, "Install failed", e)
        }

        val tvVersion = findViewById<TextView>(R.id.tvVersion)

        // 디버그 정보 표시
        val nativeDir = applicationInfo.nativeLibraryDir
        val libs = File(nativeDir).list()?.joinToString(", ") ?: "empty"
        val status = if (installOk) "Ready" else "FAILED"
        tvVersion?.text = "v1.1.0 | $status | libs: $libs"

        if (!installOk) {
            Toast.makeText(this, "Engine: $status\nPath: ${NativeBridge.getBinaryPath()}\nNativeDir: $nativeDir", Toast.LENGTH_LONG).show()
        }

        // 버튼 연결 (안전하게)
        setupButtons(installOk)
    }

    private fun setupButtons(installOk: Boolean) {
        try {
            findViewById<android.view.View>(R.id.btnLauncher)?.setOnClickListener {
                safeStartTerminal("launcher")
            }
            findViewById<android.view.View>(R.id.btnCli)?.setOnClickListener {
                safeStartTerminal("cli")
            }
            findViewById<android.view.View>(R.id.btnTervas)?.setOnClickListener {
                safeStartTerminal("tervas")
            }
            findViewById<android.view.View>(R.id.btnTest)?.setOnClickListener {
                safeStartTerminal("test")
            }
            findViewById<android.view.View>(R.id.btnCanvas)?.setOnClickListener {
                safeStartActivity(CanvasActivity::class.java)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Button setup failed", e)
        }
    }

    private fun safeStartTerminal(mode: String) {
        try {
            if (!NativeBridge.isInstalled()) {
                NativeBridge.install(this)
            }
            val intent = Intent(this, TerminalActivity::class.java).apply {
                putExtra("mode", mode)
            }
            startActivity(intent)
        } catch (e: Exception) {
            Toast.makeText(this, "Error: ${e.message}", Toast.LENGTH_LONG).show()
            Log.e(TAG, "startTerminal failed", e)
        }
    }

    private fun safeStartActivity(cls: Class<*>) {
        try {
            if (!NativeBridge.isInstalled()) {
                NativeBridge.install(this)
            }
            startActivity(Intent(this, cls))
        } catch (e: Exception) {
            Toast.makeText(this, "Error: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun requestPermissions() {
        val perms = mutableListOf<String>()

        if (Build.VERSION.SDK_INT <= 32) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
                perms.add(Manifest.permission.READ_EXTERNAL_STORAGE)
            }
        }
        if (Build.VERSION.SDK_INT <= 29) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
                perms.add(Manifest.permission.WRITE_EXTERNAL_STORAGE)
            }
        }

        if (perms.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, perms.toTypedArray(), PERM_REQUEST)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERM_REQUEST) {
            Log.i(TAG, "Permissions result: ${permissions.zip(grantResults.toList())}")
        }
    }

    private fun showCrashScreen(msg: String) {
        val tv = TextView(this)
        tv.text = msg
        tv.textSize = 12f
        tv.setPadding(32, 64, 32, 32)
        tv.setTextColor(0xFFFF4444.toInt())
        tv.setBackgroundColor(0xFF000000.toInt())
        setContentView(tv)
    }
}
