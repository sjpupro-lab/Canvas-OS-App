package com.sjpupro.canvasos

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import java.io.File
import java.io.FileInputStream

/**
 * CanvasView — CanvasOS 캔버스 실시간 시각화
 *
 * 네이티브 프로세스가 출력한 BMP 파일을 주기적으로 읽어
 * SurfaceView에 렌더링. 터치 이벤트를 캔버스 좌표로 변환.
 *
 * 5가지 시각화 모드: ABGR, Energy, Opcode, Lane, Activity
 */
class CanvasView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : SurfaceView(context, attrs, defStyleAttr), SurfaceHolder.Callback {

    companion object {
        const val CANVAS_SIZE = 1024
        const val REFRESH_MS = 100L  // 10 FPS
    }

    interface OnCellTouchListener {
        fun onCellTouch(x: Int, y: Int)
    }

    var cellTouchListener: OnCellTouchListener? = null
    var bmpPath: String = ""
        set(value) {
            field = value
            bmpFile = if (value.isNotEmpty()) File(value) else null
        }

    private var bmpFile: File? = null
    private var renderThread: Thread? = null
    private var running = false
    private val srcRect = Rect()
    private val dstRect = Rect()
    private var lastModified = 0L
    private var cachedBitmap: Bitmap? = null

    // 상태 정보
    var statusText: String = ""
    var visMode: String = "ABGR"
    var tickCount: Long = 0

    private val statusPaint = Paint().apply {
        color = Color.parseColor("#39D353")
        textSize = 28f
        isAntiAlias = true
        typeface = Typeface.MONOSPACE
    }

    private val bgPaint = Paint().apply {
        color = Color.parseColor("#0D1117")
        style = Paint.Style.FILL
    }

    private val borderPaint = Paint().apply {
        color = Color.parseColor("#30363D")
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }

    private val gridPaint = Paint().apply {
        color = Color.parseColor("#1A1F26")
        style = Paint.Style.STROKE
        strokeWidth = 1f
    }

    init {
        holder.addCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        running = true
        renderThread = Thread {
            while (running) {
                try {
                    drawFrame()
                    Thread.sleep(REFRESH_MS)
                } catch (_: InterruptedException) {
                    break
                }
            }
        }.apply {
            name = "CanvasView-Render"
            isDaemon = true
            start()
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, w: Int, h: Int) {}

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        running = false
        renderThread?.interrupt()
        renderThread?.join(500)
        renderThread = null
    }

    private fun drawFrame() {
        val canvas = holder.lockCanvas() ?: return
        try {
            canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)

            // BMP 파일 로드
            val file = bmpFile
            if (file != null && file.exists()) {
                val mod = file.lastModified()
                if (mod != lastModified) {
                    lastModified = mod
                    cachedBitmap?.recycle()
                    cachedBitmap = loadBmp(file)
                }
            }

            val bmp = cachedBitmap
            if (bmp != null) {
                // 캔버스 영역 계산 (정사각형, 중앙 상단)
                val canvasSize = minOf(width, height - 80)
                val left = (width - canvasSize) / 2
                val top = 4

                srcRect.set(0, 0, bmp.width, bmp.height)
                dstRect.set(left, top, left + canvasSize, top + canvasSize)
                canvas.drawBitmap(bmp, srcRect, dstRect, null)
                canvas.drawRect(dstRect, borderPaint)

                // 그리드 (64x64 타일 경계)
                val tileSize = canvasSize / 16f
                for (i in 1 until 16) {
                    val gx = left + (i * tileSize)
                    val gy = top + (i * tileSize)
                    canvas.drawLine(gx, top.toFloat(), gx, (top + canvasSize).toFloat(), gridPaint)
                    canvas.drawLine(left.toFloat(), gy, (left + canvasSize).toFloat(), gy, gridPaint)
                }
            } else {
                // BMP 없으면 빈 캔버스 표시
                val canvasSize = minOf(width, height - 80)
                val left = (width - canvasSize) / 2
                val top = 4
                dstRect.set(left, top, left + canvasSize, top + canvasSize)
                canvas.drawRect(dstRect, borderPaint)

                val waitPaint = Paint(statusPaint).apply { textSize = 24f; color = Color.parseColor("#484F58") }
                canvas.drawText("Waiting for canvas data...", left + 20f, top + canvasSize / 2f, waitPaint)
            }

            // 하단 상태바
            val sy = height - 36f
            statusPaint.color = Color.parseColor("#8B949E")
            statusPaint.textSize = 22f
            canvas.drawText("Mode: $visMode  |  Tick: $tickCount", 12f, sy, statusPaint)

            if (statusText.isNotEmpty()) {
                statusPaint.color = Color.parseColor("#39D353")
                canvas.drawText(statusText, width - statusPaint.measureText(statusText) - 12f, sy, statusPaint)
            }

        } finally {
            holder.unlockCanvasAndPost(canvas)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (event.action == MotionEvent.ACTION_DOWN || event.action == MotionEvent.ACTION_MOVE) {
            // 터치 좌표 → 캔버스 좌표 변환
            val canvasSize = minOf(width, height - 80)
            val left = (width - canvasSize) / 2
            val top = 4

            val cx = ((event.x - left) / canvasSize * CANVAS_SIZE).toInt()
            val cy = ((event.y - top) / canvasSize * CANVAS_SIZE).toInt()

            if (cx in 0 until CANVAS_SIZE && cy in 0 until CANVAS_SIZE) {
                cellTouchListener?.onCellTouch(cx, cy)
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private fun loadBmp(file: File): Bitmap? {
        return try {
            FileInputStream(file).use { fis ->
                BitmapFactory.decodeStream(fis)
            }
        } catch (_: Exception) {
            null
        }
    }

    fun cleanup() {
        running = false
        cachedBitmap?.recycle()
        cachedBitmap = null
    }
}
