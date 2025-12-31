/*
 ==============================================================================
    Copyright (c) 2020-2023 Foleys Finest Audio - Daniel Walz
    All rights reserved.

    **BSD 3-Clause License**

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

 ==============================================================================

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
    OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
    OF THE POSSIBILITY OF SUCH DAMAGE.
 ==============================================================================
*/

#pragma once

#include <atomic>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace foleys
{

/**
 * ApplicationSettings are persistent settings shared by all plugin instances.
 *
 * They are hierarchically ordered in a ValueTree and loaded via SharedResourcePointer,
 * so they don't exist duplicated in one process.
 *
 * Settings are automatically saved when the ValueTree changes. The save operation is
 * designed to be non-blocking to avoid hanging the GUI thread:
 * - Uses non-blocking inter-process lock (timeout=0)
 * - Writes to a temp file then renames (atomic, avoids blocking truncate)
 * - Skips save if lock unavailable (next change will retry)
 * - Uses atomic flag to prevent reentrant saves
 *
 * The settings file location is set via setFileName() and should be called early,
 * typically in the plugin constructor. Example path:
 *   ~/Library/Application Support/CompanyName/PluginName.pgmsettings
 */
class ApplicationSettings : public juce::ChangeBroadcaster,
                            private juce::Timer,
                            private juce::ValueTree::Listener
{
public:
    ApplicationSettings();
    ~ApplicationSettings() override;

    /**
     * The settings tree is used to hang in your settings trees. The whole tree is stored.
     * It is synchronised instead of replaced on load, so it is safe to add yourself as
     * ValueTree::Listener.
     *
     * MIDI CC-to-parameter mappings are stored under a "mappings" child node.
     */
    juce::ValueTree settings { "Settings" };

    /**
     * Set the file path for persistent storage.
     * @param file The file to save/load settings from
     */
    void setFileName (juce::File file);

private:
    void timerCallback() override;

    /**
     * Load settings from file if checksum changed (called periodically by timer).
     * Uses non-blocking lock to avoid GUI thread hangs.
     */
    void load();

    /**
     * Save settings to file using atomic temp-file-then-rename approach.
     * Uses non-blocking lock - skips save if lock unavailable.
     */
    void save();

    /**
     * Called by ValueTree listeners. Uses atomic flag to prevent reentrant saves.
     */
    void saveIfNotBusy();

    void valueTreeChildAdded (juce::ValueTree& parentTree,
                              juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree&, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    juce::File   settingsFile;
    juce::String checksum;

    /** Atomic flag to prevent reentrant/recursive saves from ValueTree listeners */
    std::atomic<bool> savePending { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApplicationSettings)
};

using SharedApplicationSettings = juce::SharedResourcePointer<ApplicationSettings>;

}
