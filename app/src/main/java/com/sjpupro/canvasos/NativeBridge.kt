package com.sjpupro.canvasos

import android.content.Context
import android.util.Log
import java.io.File

/**
 * NativeBridge — CanvasOS 네이티브 바이너리 관리
 *
 * jniLibs에 포함된 libcanvasos_launcher.so를 nativeLibraryDir에서 직접 실행.
 * Android 10+의 SELinux W^X 정책을 우회 (nativeLibraryDir은 실행 허용).
 */
object NativeBridge {

    private const val TAG = "NativeBridge"
    private const val LIB_NAME = "libcanvasos_launcher.so"

    private var binaryPath: String = ""
    private var dataDir: String = ""
    private var installed = false

    fun install(context: Context): Boolean {
        return try {
            // nativeLibraryDir에서 바이너리 경로 획득 (실행 가능한 경로)
            val nativeDir = context.applicationInfo.nativeLibraryDir
            val binFile = File(nativeDir, LIB_NAME)
            binaryPath = binFile.absolutePath

            // 데이터 디렉토리 생성
            val dataPath = File(context.filesDir, "data")
            dataPath.mkdirs()
            dataDir = dataPath.absolutePath

            if (!binFile.exists()) {
                Log.e(TAG, "Binary not found: $binaryPath")
                Log.e(TAG, "nativeLibraryDir contents: ${File(nativeDir).list()?.joinToString()}")
                installed = false
                return false
            }

            if (!binFile.canExecute()) {
                Log.w(TAG, "Binary not executable, attempting chmod")
                binFile.setExecutable(true, true)
            }

            installed = true
            Log.i(TAG, "Binary ready: $binaryPath (${binFile.length()} bytes)")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Install failed: ${e.message}", e)
            installed = false
            false
        }
    }

    /**
     * CanvasOS 프로세스 시작
     */
    fun startProcess(mode: String = "launcher"): Process? {
        if (!installed || binaryPath.isEmpty()) {
            Log.e(TAG, "Not installed, cannot start process")
            return null
        }

        return try {
            val cmd = mutableListOf(binaryPath)

            val pb = ProcessBuilder(cmd)
            pb.directory(File(dataDir))
            pb.environment().apply {
                put("HOME", dataDir)
                put("TERM", "xterm-256color")
                put("LANG", "en_US.UTF-8")
                put("TMPDIR", dataDir)
            }
            pb.redirectErrorStream(true)

            val process = pb.start()
            Log.i(TAG, "Process started: mode=$mode, binary=$binaryPath")
            process
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start process: ${e.message}", e)
            null
        }
    }

    fun isInstalled(): Boolean = installed
    fun getBinaryPath(): String = binaryPath
    fun getDataDir(): String = dataDir
}
