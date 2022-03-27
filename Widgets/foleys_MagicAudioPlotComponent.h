/*
 ==============================================================================
    Copyright (c) 2019-2021 Foleys Finest Audio - Daniel Walz
    All rights reserved.

    License for non-commercial projects:

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

    License for commercial products:

    To sell commercial products containing this module, you are required to buy a
    License from https://foleysfinest.com/developer/pluginguimagic/

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

namespace foleys
{

/**
 The MagicAudioPlotComponent allows drawing the data from a MagicAudioPlotSource.
 */
class MagicAudioPlotComponent : public juce::Component,
                                juce::SettableTooltipClient
{
public:

    enum ColourIds
    {
        plotColourId = 0x2001000,
        plotInactiveColourId,
        plotFillColourId,
        plotInactiveFillColourId
    };

    MagicAudioPlotComponent();

    void setPlotSource (MagicAudioPlotSource* source);
    void setDecayFactor (float decayFactor);
    void setGradientFromString (const juce::String& cssString, Stylesheet& stylesheet);

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool hitTest (int, int) override { return false; }

    bool needsUpdate() const;

    void setTriggered (bool triggered);
    void setOverlay (bool overlay);
    void setChannel (int channel);
    void setNumChannels (int numChannels);
    void setPlotLength (int plotLength);
    void setPlotOffset (int plotOffset);

  private:
    void drawPlot (juce::Graphics& g);
    void drawPlotGlowing (juce::Graphics& g);
    void updateGlowBufferSize();

    juce::WeakReference<MagicAudioPlotSource> plotSource;
    juce::Path                                path;
    juce::Path                                filledPath;
    std::unique_ptr<GradientBackground>       gradient;

    juce::int64 lastDataTimestamp = 0;
    juce::Image glowBuffer;
    float       decay = 0.0f;

    bool        triggered = true;
    bool        overlay = false;
    int         channel = 0;
    int         numChannels = 0;
    int         plotLength = 0;
    int         plotOffset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicAudioPlotComponent)
};

} // namespace foleys
