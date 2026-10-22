package com.qd023.glove

import android.app.Application
import timber.log.Timber

class GloveApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        if (BuildConfig.DEBUG) Timber.plant(Timber.DebugTree())
    }
}
