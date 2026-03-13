package com.sjpupro.canvasos

import android.content.Context
import java.io.File
import java.io.FileOutputStream

/**
 * NativeBridge — CanvasOS 네이티브 바이너리 관리
 *
 * assets/canvasos_launcher를 앱 내부 저장소로 추출하고 실행 권한 부여.
 * ProcessBuilder로 네이티브 프로세스를 관리.
 */
object NativeBridge {

    private const val BINARY_NAME = "canvasos_launcher"
    private var binaryPath: String = ""
    private var dataDir: String = ""

    fun install(context: Context) {
        val binDir = File(context.filesDir, "bin")
        binDir.mkdirs()

        val dataPath = File(context.filesDir, "data")
        dataPath.mkdirs()
        dataDir = dataPath.absolutePath

        val targetFile = File(binDir, BINARY_NAME)
        binaryPath = targetFile.absolutePath

        // 바이너리가 없거나 버전이 다르면 재설치
        if (!targetFile.exists() || shouldUpdate(context)) {
            extractBinary(context, targetFile)
        }
    }

    private fun shouldUpdate(context: Context): Boolean {
        val versionFile = File(context.filesDir, "bin/.version")
        val currentVersion = "1.1.0"
        if (!versionFile.exists()) return true
        return versionFile.readText().trim() != currentVersion
    }

    private fun extractBinary(context: Context, target: File) {
        context.assets.open(BINARY_NAME).use { input ->
            FileOutputStream(target).use { output ->
                input.copyTo(output)
            }
        }
        target.setExecutable(true, true)

        // 버전 기록
        val versionFile = File(target.parentFile, ".version")
        versionFile.writeText("1.1.0")
    }

    /**
     * CanvasOS 프로세스 시작
     * @return Process 객체 (stdin/stdout/stderr 접근 가능)
     */
    fun startProcess(mode: String = "launcher"): Process {
        val cmd = mutableListOf(binaryPath)

        val env = mapOf(
            "HOME" to dataDir,
            "TERM" to "xterm-256color",
            "LANG" to "en_US.UTF-8"
        )

        val pb = ProcessBuilder(cmd)
        pb.directory(File(dataDir))
        pb.environment().putAll(env)
        pb.redirectErrorStream(true)

        return pb.start()
    }

    fun getBinaryPath(): String = binaryPath
    fun getDataDir(): String = dataDir
}
