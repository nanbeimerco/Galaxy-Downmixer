package com.galaxydownmixer

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.galaxydownmixer.ui.DownmixViewModel
import com.galaxydownmixer.ui.MainScreen
import com.galaxydownmixer.ui.theme.GalaxyDownmixerTheme

class MainActivity : ComponentActivity() {
    private val vm: DownmixViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            GalaxyDownmixerTheme {
                MainScreen(vm)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        vm.setVisible(true)
    }

    override fun onStop() {
        vm.setVisible(false)
        super.onStop()
    }
}
