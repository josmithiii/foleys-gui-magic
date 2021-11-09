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

class MagicPlotComponent;

/**
 The MagicPlotSources act as an interface, so the GUI can visualise an arbitrary plot
 of data. To create a specific new plot, create a subclass and implement drawPlot.
 */
class MagicPlotSource
{
public:

    MagicPlotSource()=default;
    virtual ~MagicPlotSource()=default;

    /**
     Alternate Constructor which can be used to avoid truncated synchronous plots:
     @param maxPlotLength, if positive, gives the maximum expected preferred length of
            each plot in samples (e.g., one period, or a multiple of the period).
            The default plot length is 10 ms expressed in samples (sampleRate/100).
     */
    MagicPlotSource(int maxPlotLengthExpected)
    : maxPlotLength(maxPlotLengthExpected) {}
    /**
     This is the callback whenever new sample data arrives. It is the subclasses
     responsibility to put that into a FIFO and return as quickly as possible.
     */
    virtual void pushSamples (const juce::AudioBuffer<float>& buffer)=0;

    /**
     This form of the callback provides two channels of plot data, as needed for XY scatterplots.

     @param bufferX is the audio buffer to serve as the X axis of the scatterplot.
     @param channelX is the audio channel number (from 0) to use for the X axis of the scatterplot.
     @param bufferY is the audio buffer to serve as the Y axis of the scatterplot.
     @param channelY is the audio channel number (from 0) to use for the Y axis of the scatterplot.
     @param plotLength specifies the desired length of plots involving this audio buffer.
            Default is 0 meaning take system default (10 ms of audio data).
     */
    virtual void pushSamples (const juce::AudioBuffer<float>& bufferX, int channelX,
                               const juce::AudioBuffer<float>& bufferY, int channelY,
                               const int plotLength=0) { }

    /**
     Set whether a multichannel plot is an overlay or sum of all channels. Default is sum.
     @param isTriggered, if true, means each plot begins at a zero-crossing (default = true).
            Otherwise, the latest samples received are plotted for each audio buffer.
     */
    virtual void setTriggered (bool isTriggered) { triggered = isTriggered; }

    /**
     Set whether plot is triggered by a zero-crossing or free runs. Default is triggered.
     */
    virtual void setOverlay (bool overlay) { overlayPlots = overlay; }

    /**
     Set audio channel to plot (numbering from 0) or -1 to plot all channels (overlay or sum). Default is -1.
     */
    virtual void setChannel (int channelCode) { plotChannel = channelCode; }

    /**
     This is the callback that creates the plot for drawing.

     @param path is the path instance that is constructed by the MagicPlotSource
     @param filledPath is the path instance that is constructed by the MagicPlotSource to be filled
     @param bounds the bounds of the plot
     @param component grants access to the plot component, e.g. to find the colours from it
     */
    virtual void createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicPlotComponent& component) = 0;

    /**
     This method is called by the MagicProcessorState to allow the plot computation to be set up
     */
    virtual void prepareToPlay (double sampleRate, int samplesPerBlockExpected)=0;

    /**
     You can add an active state to your plot to allow to paint in different colours
     */
    virtual bool isActive() const { return active; }
    virtual void setActive (bool shouldBeActive) { active = shouldBeActive; }

    /**
     Use this information to invalidate your plot drawing
     */
    juce::int64 getLastDataUpdate() const { return lastData.load(); }

    /**
     Call this to invalidate the lastData flag
     */
    void resetLastDataFlag() { lastData.store (juce::Time::currentTimeMillis()); }

    /**
     If your plot needs background processing, return here a pointer to your TimeSliceClient,
     and it will automatically be added to the common background thread.
     */
    virtual juce::TimeSliceClient* getBackgroundJob() { return nullptr; }

private:
    std::atomic<juce::int64> lastData { 0 };
    bool active = true;

protected:
    bool triggered = true;
    bool overlayPlots = false; // When false, plot either a single channel or the sum of all channels
    int plotChannel = -1; // -1 denotes the sum of all channels
    int maxPlotLength = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MagicPlotSource)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicPlotSource)
}; // MagicPlotSource

} // namespace foleys
