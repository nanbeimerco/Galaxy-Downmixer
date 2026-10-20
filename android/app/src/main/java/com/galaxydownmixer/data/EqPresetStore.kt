package com.galaxydownmixer.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID

data class EqPreset(
    val id: String = UUID.randomUUID().toString(),
    val name: String,
    val text: String,
    val createdAt: Long = System.currentTimeMillis(),
) {
    val profile: ParsedEqProfile by lazy { ApoParser.parse(text) }
}

class EqPresetStore(private val context: Context) {

    private val presetsFile: File get() = File(context.filesDir, "eq_presets.json")
    private val assignmentsFile: File get() = File(context.filesDir, "device_assignments.json")
    private val prefs = context.getSharedPreferences("eq_presets_prefs", Context.MODE_PRIVATE)

    @Synchronized
    fun loadPresets(): List<EqPreset> {
        if (!presetsFile.exists()) {
            val defaults = createDefaultPresets()
            savePresets(defaults)
            return defaults
        }

        return runCatching {
            val jsonStr = presetsFile.readText(Charsets.UTF_8)
            val array = JSONArray(jsonStr)
            val list = mutableListOf<EqPreset>()
            for (i in 0 until array.length()) {
                val obj = array.getJSONObject(i)
                list.add(
                    EqPreset(
                        id = obj.optString("id", UUID.randomUUID().toString()),
                        name = obj.getString("name"),
                        text = obj.getString("text"),
                        createdAt = obj.optLong("createdAt", System.currentTimeMillis()),
                    )
                )
            }
            if (list.isEmpty()) {
                val defaults = createDefaultPresets()
                savePresets(defaults)
                defaults
            } else {
                list
            }
        }.getOrElse {
            val defaults = createDefaultPresets()
            savePresets(defaults)
            defaults
        }
    }

    @Synchronized
    fun savePreset(name: String, text: String): EqPreset {
        val current = loadPresets().toMutableList()
        val newPreset = EqPreset(name = name, text = text)
        current.add(0, newPreset)
        savePresets(current)
        return newPreset
    }

    @Synchronized
    fun updatePreset(id: String, newName: String, newText: String? = null): EqPreset? {
        val current = loadPresets().toMutableList()
        val idx = current.indexOfFirst { it.id == id }
        if (idx == -1) return null

        val old = current[idx]
        val updated = old.copy(name = newName, text = newText ?: old.text)
        current[idx] = updated
        savePresets(current)
        return updated
    }

    @Synchronized
    fun deletePreset(id: String): Boolean {
        val current = loadPresets().toMutableList()
        val removed = current.removeAll { it.id == id }
        if (removed) {
            savePresets(current)
            // Clean up any assignments pointing to this preset
            val assignments = loadDeviceAssignments().toMutableMap()
            var modified = false
            for ((key, presetId) in assignments) {
                if (presetId == id) {
                    assignments.remove(key)
                    modified = true
                }
            }
            if (modified) saveDeviceAssignments(assignments)
        }
        return removed
    }

    @Synchronized
    fun getPreset(id: String): EqPreset? {
        return loadPresets().find { it.id == id }
    }

    @Synchronized
    private fun savePresets(presets: List<EqPreset>) {
        val array = JSONArray()
        for (p in presets) {
            val obj = JSONObject()
            obj.put("id", p.id)
            obj.put("name", p.name)
            obj.put("text", p.text)
            obj.put("createdAt", p.createdAt)
            array.put(obj)
        }
        presetsFile.writeText(array.toString(), Charsets.UTF_8)
    }

    // --- Device Assignments ---

    @Synchronized
    fun loadDeviceAssignments(): Map<String, String> {
        if (!assignmentsFile.exists()) return emptyMap()
        return runCatching {
            val jsonStr = assignmentsFile.readText(Charsets.UTF_8)
            val obj = JSONObject(jsonStr)
            val map = mutableMapOf<String, String>()
            for (key in obj.keys()) {
                map[key] = obj.getString(key)
            }
            map
        }.getOrDefault(emptyMap())
    }

    @Synchronized
    fun saveDeviceAssignment(deviceKey: String, presetId: String?) {
        val assignments = loadDeviceAssignments().toMutableMap()
        if (presetId == null) {
            assignments.remove(deviceKey)
        } else {
            assignments[deviceKey] = presetId
        }
        saveDeviceAssignments(assignments)
    }

    @Synchronized
    private fun saveDeviceAssignments(map: Map<String, String>) {
        val obj = JSONObject()
        for ((k, v) in map) {
            obj.put(k, v)
        }
        assignmentsFile.writeText(obj.toString(), Charsets.UTF_8)
    }

    var isAutoSwitchEnabled: Boolean
        get() = prefs.getBoolean(KEY_AUTO_SWITCH, true)
        set(value) = prefs.edit().putBoolean(KEY_AUTO_SWITCH, value).apply()

    var activePresetId: String?
        get() = prefs.getString(KEY_ACTIVE_PRESET_ID, null)
        set(value) = prefs.edit().putString(KEY_ACTIVE_PRESET_ID, value).apply()

    private fun createDefaultPresets(): List<EqPreset> {
        val sample = """
            Preamp: -1.5 dB
            Filter 1: ON LSC Fc 23 Hz Gain 1.37 dB Q 0.758
            Filter 2: ON PK Fc 31 Hz Gain -0.75 dB Q 1.414
            Filter 3: ON PK Fc 141 Hz Gain -6.31 dB Q 0.297
            Filter 4: ON PK Fc 697 Hz Gain 2.35 dB Q 0.915
            Filter 5: ON PK Fc 1595 Hz Gain -0.76 dB Q 1.414
            Filter 6: ON PK Fc 3200 Hz Gain -3.73 dB Q 1.789
            Filter 7: ON PK Fc 4456 Hz Gain 1.24 dB Q 1.609
            Channel: L
            Preamp: -1.7 dB
            Filter 1: ON LSC Fc 36 Hz Gain 0.81 dB Q 0.405
            Filter 2: ON HSC Fc 41 Hz Gain 0.07 dB Q 0.895
            Filter 3: ON PK Fc 8978 Hz Gain -1.74 dB Q 3.180
            Channel: R
            Preamp: -1.7 dB
            Filter 1: ON LSC Fc 36 Hz Gain -0.81 dB Q 0.405
            Filter 2: ON HSC Fc 41 Hz Gain -0.07 dB Q 0.895
            Filter 3: ON PK Fc 8978 Hz Gain 1.74 dB Q 3.180
        """.trimIndent()

        return listOf(
            EqPreset(
                name = "AKG IG955 サンプル",
                text = sample,
            )
        )
    }

    companion object {
        private const val KEY_AUTO_SWITCH = "eq_auto_switch_device"
        private const val KEY_ACTIVE_PRESET_ID = "eq_active_preset_id"
    }
}
