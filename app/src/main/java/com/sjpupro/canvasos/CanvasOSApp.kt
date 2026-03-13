package com.sjpupro.canvasos

import android.app.Application
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter

/**
 * CanvasOS Application — 글로벌 크래시 핸들러
 */
class CanvasOSApp : Application() {

    override fun onCreate() {
        super.onCreate()

        // 크래시 로그를 파일로 저장
        val defaultHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                val sw = StringWriter()
                throwable.printStackTrace(PrintWriter(sw))
                val crashLog = "=== CanvasOS Crash ===\n" +
                    "Thread: ${thread.name}\n" +
                    "Exception: ${throwable.message}\n" +
                    "Stack:\n${sw}\n"

                Log.e("CanvasOS", crashLog)

                // 파일로 저장
                val logFile = File(filesDir, "crash.log")
                logFile.writeText(crashLog)
            } catch (_: Exception) {}

            defaultHandler?.uncaughtException(thread, throwable)
        }
    }
}
