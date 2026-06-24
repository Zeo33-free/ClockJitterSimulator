cmake_minimum_required(VERSION 3.22)

# ==============================================================================
# Clock Jitter Simulator — JUCE Audio Plugin
# ==============================================================================
project(JitterClockSim VERSION 1.0.0 LANGUAGES CXX)

# Set this to your local JUCE directory path
set(JUCE_PATH "" CACHE PATH "Path to the JUCE framework directory")

if(NOT JUCE_PATH)
    message(FATAL_ERROR "Please set JUCE_PATH to your JUCE framework directory, e.g.:
    cmake -B build -DJUCE_PATH=/path/to/JUCE")
endif()

# Add JUCE as a subdirectory
add_subdirectory(${JUCE_PATH} juce_build)

# ---- Plugin target ----
juce_add_plugin(JitterClockSim
    # Plugin metadata
    COMPANY_NAME          "R1C1N"
    BUNDLE_ID             "com.R1C1N.jitterclocksim"
    PROJECT_VERSION       "1.0.0"
    PLUGIN_MANUFACTURER_CODE  R1C1
    PLUGIN_CODE           Jit1  # "JC"

    # Supported formats (disable what you don't need)
    FORMATS               VST3
    PRODUCT_NAME          "Clock Jitter Simulator"
    VST3_CATEGORIES       "Fx|Delay"

    # Plugin type (Instrument → needs MIDI in; Effect → audio in/out)
    IS_SYNTH              FALSE
    NEEDS_MIDI_INPUT      FALSE
    NEEDS_MIDI_OUTPUT     FALSE
    IS_MIDI_EFFECT        FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE

    # Sources
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# Enable C++17
target_compile_features(JitterClockSim PRIVATE cxx_std_17)

# Link juce_dsp module
target_link_libraries(JitterClockSim PRIVATE juce::juce_dsp)

# Add plugin source files
target_sources(JitterClockSim PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# New plugin — no legacy VST2/VST3 compatibility issues
target_compile_definitions(JitterClockSim PRIVATE
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)

# Platform-specific settings
if(MSVC)
    target_compile_options(JitterClockSim PRIVATE /W4 /utf-8)
else()
    target_compile_options(JitterClockSim PRIVATE -Wall -Wextra -Wpedantic)
endif()
