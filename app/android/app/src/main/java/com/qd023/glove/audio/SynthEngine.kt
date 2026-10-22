package com.qd023.glove.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import kotlin.math.PI
import kotlin.math.pow
import kotlin.math.sin
import kotlin.concurrent.thread

/**
 * Five-voice synth for music mode.
 *
 * Each glove finger gates one note of a C-major pentatonic scale, so any
 * combination of fingers is consonant. Wrist twist bends the pitch within
 * +/-2 semitones and, past 45 degrees, shifts the whole hand an octave.
 *
 * Audio is generated on a dedicated thread straight into an AudioTrack
 * (streaming mode) which keeps latency around 40 ms - low enough to feel
 * played rather than triggered.
 */
class SynthEngine {
    private companion object {
        const val SR = 48000
        const val VOICES = 5
        const val CHUNK = 480              // 10 ms of audio per write
        const val TABLE_SIZE = 2048
        const val MASTER = 0.22f

        // Attack/release smoothing coefficients (per sample).
        const val ATTACK = 0.010f
        const val RELEASE = 0.0006f

        /** C-major pentatonic starting at C5: C D E G A. */
        val BASE_HZ = doubleArrayOf(523.25, 587.33, 659.25, 783.99, 880.00)
        val NOTE_NAMES = arrayOf("C", "D", "E", "G", "A")
    }

    private val sine = FloatArray(TABLE_SIZE + 1).also { t ->
        for (i in 0 until TABLE_SIZE) {
            t[i] = sin(2.0 * PI * i / TABLE_SIZE).toFloat()
        }
        t[TABLE_SIZE] = t[0]
    }

    private val gate = FloatArray(VOICES)      // envelope state
    private val target = FloatArray(VOICES)    // 1 = key down, 0 = key up
    private val phase = DoubleArray(VOICES)
    private val inc = DoubleArray(VOICES)

    @Volatile private var running = false
    @Volatile private var bend = 0f            // -1 .. +1
    @Volatile private var octave = 0           // -1, 0, +1
    @Volatile private var mask = 0

    private var track: AudioTrack? = null
    private var worker: Thread? = null

    val isRunning: Boolean get() = running

    fun start() {
        if (running) return
        val minBuf = AudioTrack.getMinBufferSize(
            SR,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        ).coerceAtLeast(CHUNK * 4)

        val t = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(SR)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build()
            )
            .setBufferSizeInBytes(minBuf * 2)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        track = t
        running = true
        t.play()
        worker = thread(name = "qd023-synth", priority = Thread.MAX_PRIORITY) { pump(t) }
    }

    fun stop() {
        running = false
        worker?.let { runCatching { it.join(400) } }
        worker = null
        track?.let {
            runCatching { it.pause() }
            runCatching { it.flush() }
            runCatching { it.stop() }
            runCatching { it.release() }
        }
        track = null
        for (i in 0 until VOICES) {
            target[i] = 0f
            gate[i] = 0f
            phase[i] = 0.0
        }
        mask = 0
    }

    /** Bit i set = finger i closed. Order matches the glove: thumb..pinky. */
    fun setFingers(m: Int) {
        mask = m
        for (i in 0 until VOICES) {
            target[i] = if (m and (1 shl i) != 0) 1f else 0f
        }
    }

    /** Wrist twist in degrees: pitch bend, with octave jumps past 45 deg. */
    fun setBend(deg: Int) {
        bend = (deg / 30f).coerceIn(-1f, 1f)
        octave = when {
            deg > 45 -> 1
            deg < -45 -> -1
            else -> 0
        }
    }

    /** Note name currently held by a finger, for the UI. */
    fun noteName(finger: Int): String {
        val shift = octave
        return NOTE_NAMES[finger.coerceIn(0, VOICES - 1)] + (5 + shift)
    }

    val activeMask: Int get() = mask

    private fun pump(t: AudioTrack) {
        val buf = ShortArray(CHUNK)
        while (running) {
            val bendMul = 2.0.pow(bend / 12.0)      // +/-2 semitones
            val octMul = 2.0.pow(octave.toDouble())

            for (v in 0 until VOICES) {
                // Only recompute the step when something actually changed;
                // this is cheap enough that it can run per chunk.
                inc[v] = 2.0 * PI * BASE_HZ[v] * bendMul * octMul / SR
            }

            for (n in 0 until CHUNK) {
                var s = 0f
                for (v in 0 until VOICES) {
                    val goal = target[v]
                    val g = gate[v]
                    gate[v] = g + (goal - g) * (if (goal > g) ATTACK else RELEASE)

                    var p = phase[v]
                    p += inc[v]
                    if (p >= 2.0 * PI) p -= 2.0 * PI
                    phase[v] = p

                    val idx = (p / (2.0 * PI) * TABLE_SIZE).toInt()
                    val frac = ((p / (2.0 * PI) * TABLE_SIZE) - idx).toFloat()
                    val a = sine[idx]
                    val b = sine[(idx + 1) % TABLE_SIZE]
                    val wave = a + (b - a) * frac

                    s += gate[v] * wave
                }
                val mixed = (s * MASTER).coerceIn(-1f, 1f)
                buf[n] = (mixed * 32767f).toInt().toShort()
            }

            val written = runCatching { t.write(buf, 0, CHUNK) }.getOrDefault(-1)
            if (written < 0) break
        }
    }
}
