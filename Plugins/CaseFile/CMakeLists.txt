juce_add_plugin(CaseFile
    VERSION                     1.0.0
    COMPANY_NAME                "Sound Detective"
    PLUGIN_MANUFACTURER_CODE    SDet
    PLUGIN_CODE                 CsFl
    BUNDLE_ID                   "com.sounddetective.casefile"
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Detective 47s Case File"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             "Fx|Tools"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

# Case File is the mix/production command center, not an audio effect:
# processBlock is a straight passthrough. Everything (brief, evidence,
# analysis, suspects, checklists, song map, libraries, chains, versions,
# report) lives in a ValueTree saved with the DAW session. Audio analysis
# runs offline on imported files, never on the audio thread.
# EDITOR_WANTS_KEYBOARD_FOCUS is TRUE because the UI is mostly text fields.

juce_generate_juce_header(CaseFile)

juce_add_binary_data(CaseFileBinaryData SOURCES
    Assets/logo_detective47_dust1200_BLUE.png
)
target_link_libraries(CaseFileBinaryData PRIVATE juce::juce_core)

target_sources(CaseFile PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TabsForms.cpp
    Source/TabsCase.cpp
)

target_include_directories(CaseFile PRIVATE
    Source
    ../../Common
)

target_compile_definitions(CaseFile PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(CaseFile PRIVATE
    CaseFileBinaryData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Headless logic test (console) — state round-trip, checklist seeding,
# analysis math on synthetic signals, suspect rules, report/JSON export.
# No GUI, no DAW needed. Run after every change.
# ---------------------------------------------------------------------------
juce_add_console_app(CaseFileLogicTest PRODUCT_NAME "CaseFileLogicTest")
juce_generate_juce_header(CaseFileLogicTest)
target_sources(CaseFileLogicTest PRIVATE Tests/LogicTest.cpp Source/PluginProcessor.cpp)
target_include_directories(CaseFileLogicTest PRIVATE Source ../../Common)
target_compile_definitions(CaseFileLogicTest PRIVATE
    CASEFILE_NO_EDITOR=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(CaseFileLogicTest PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
