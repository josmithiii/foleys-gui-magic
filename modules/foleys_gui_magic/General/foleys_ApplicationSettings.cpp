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

#include <juce_cryptography/juce_cryptography.h>

#include "foleys_ApplicationSettings.h"
#include "../Helpers/foleys_ScopedInterProcessLock.h"

namespace foleys
{

ApplicationSettings::ApplicationSettings()
{
    settings.addListener (this);
}

ApplicationSettings::~ApplicationSettings()
{
    stopTimer();
    settings.removeListener (this);
}

void ApplicationSettings::setFileName (juce::File file)
{
    if (file == settingsFile)
        return;

    settingsFile = file;
    startTimerHz (1);
}

void ApplicationSettings::load() // load settingsFile if it has changed, merging it with current settings, and save that out
{
    ScopedInterProcessLock lock (settingsFile.getFileName() + ".lock", 0,
    [this]
    {
        auto newChecksum = juce::MD5 (settingsFile).toHexString();
        if (checksum == newChecksum)
            return;

        auto stream = settingsFile.createInputStream();
        if (stream.get() == nullptr)
            return;

        auto tree = juce::ValueTree::fromXml (stream->readEntireStreamAsString());
        if (! tree.isValid())
            return;

        isLoading = true;
        settings.copyPropertiesAndChildrenFrom (tree, nullptr);
        settings.addListener (this);
        isLoading = false;

        checksum = newChecksum;
        sendChangeMessage();
    });
}

void ApplicationSettings::save()
{
    if (! settingsFile.existsAsFile() && ! settingsFile.getParentDirectory().exists())
    {
        auto result = settingsFile.getParentDirectory().createDirectory();
        if (result.failed())
        {
            DBG ("ApplicationSettings::save() - Failed to create directory: " << result.getErrorMessage());
            return;
        }
    }

    // Use non-blocking lock (timeout=0) to avoid hanging the GUI thread
    // If we can't get the lock, just skip this save - we'll save on the next change
    ScopedInterProcessLock lock (settingsFile.getFileName() + ".lock", 0,
    [this] {
        // Write to temp file and rename (atomic, avoids truncate blocking)
        auto tempFile = settingsFile.getSiblingFile (settingsFile.getFileName() + ".tmp");
        if (tempFile.replaceWithText (settings.toXmlString()))
        {
            tempFile.moveFileTo (settingsFile);
            checksum = juce::MD5 (settingsFile).toHexString();
            DBG ("ApplicationSettings::save() - Saved to " << settingsFile.getFullPathName());
        }
        else
        {
            DBG ("ApplicationSettings::save() - Failed to write temp file");
        }
    });
}

void ApplicationSettings::timerCallback()
{
    load();
}

void ApplicationSettings::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    saveIfNotBusy();
}

void ApplicationSettings::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    saveIfNotBusy();
}

void ApplicationSettings::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    saveIfNotBusy();
}

void ApplicationSettings::saveIfNotBusy()
{
    if (isLoading)
        return;

    // Prevent recursive/reentrant saves
    if (savePending.exchange(true) == false)
    {
        save();
        savePending.store(false);
    }
}

}

