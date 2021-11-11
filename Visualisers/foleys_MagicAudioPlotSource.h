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
  class MagicAudioPlotSource : public MagicPlotSource
{
public:

    /**
     Constructor.
     @param maxPlotLength, if positive, gives the maximum expected preferred length of
            each plot in samples (e.g., one period, or a multiple of the period).
            The default plot length is 10 ms expressed in samples (sampleRate/100).
            Increase it to avoid truncated triggered plots at fundamentals below 100 Hz.
     */
    MagicAudioPlotSource(int maxPlotLengthExpected=0)
    : maxPlotLength(maxPlotLengthExpected) {}

    /** Destructor. */
    virtual ~MagicAudioPlotSource() override =default;

    virtual void prepareToPlay (double sampleRateToUse, int samplesPerBlockExpected) override
    {
        sampleRate = sampleRateToUse;
        samples.setSize (1, static_cast<int> (sampleRate));
        samples.clear();
        writePosition.store (0);
    }

    virtual void pushSamples (const juce::AudioBuffer<float>& buffer) override = 0; // { MagicPlotSource:pushSamples(buffer);};

    /**
     This form of the pushSamples() callback provides two channels of
     plot data, needed for XY scatterplots.

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
    virtual void setChannel (int channelCode) { channel = channelCode; }

    /**
     Set number of audio channels to plot in overlay mode, or to average if not overlaid, with 0 meaning all channels.
     */
    virtual void setNumChannels (int nChans)
    {
        if (nChans > samples.getNumChannels())
        {
            samples.setSize (nChans, static_cast<int> (sampleRate));
            samples.clear();
        }
    }

    /**
     Set audio plot length in samples.
     */
    virtual void setPlotLength (int pl)
    {
        plotLength = pl;
        if (plotLength > samples.getNumSamples())
            samples.setSize(samples.getNumChannels(), plotLength);
    }

    /**
     Set offset between plots as a fractional value between 0 and 1.
     */
    virtual void setPlotOffset (float po)
    {
        plotOffset = po;
    }

    /**
     This is the callback that creates the plot for drawing.

     @param path is the path instance that is constructed by the MagicPlotSource
     @param filledPath is the path instance that is constructed by the MagicPlotSource to be filled
     @param bounds the bounds of the plot
     @param component grants access to the plot component, e.g. to find the colours from it
     */
    virtual void createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicPlotComponent& component) override = 0;

protected:
    double                   sampleRate = 0.0;
    juce::AudioBuffer<float> samples;
    std::atomic<int>         writePosition;
    bool triggered = true;
    bool overlayPlots = false; // When false, plot either a single channel or the sum of all channels
    int channel = -1;          // -1 denotes the sum of all channels (note that we could use -2 in place of bool overlayPlots)
    int maxPlotLength = 0;     // when this is right, samples array never needs to resize itself while plotting
    int plotLength = 0;
    int plotOffset = 0;

    inline void averageAllChannelsToSamplesChannel0(const juce::AudioBuffer<float>& buffer)
    {
        int w = writePosition.load();
        const auto available  = samples.getNumSamples() - w;

        const auto numSamples = buffer.getNumSamples();
        const auto numChannelsIn = buffer.getNumChannels();
        const auto gain = 1.0f /  numChannelsIn;
        if (available >= numSamples)
        {
            samples.copyFrom (0, w, buffer.getReadPointer (0), numSamples, gain);
            for (int c = 1; c <  numChannelsIn; ++c)
                samples.addFrom (0, w, buffer.getReadPointer (c), numSamples, gain);
        }
        else
        {
            samples.copyFrom (0, w, buffer.getReadPointer (0),            available, gain);
            samples.copyFrom (0, 0, buffer.getReadPointer (0, available), numSamples - available, gain);
            for (int c = 1; c <  numChannelsIn; ++c)
            {
                samples.addFrom (0, w, buffer.getReadPointer (c),            available, gain);
                samples.addFrom (0, 0, buffer.getReadPointer (c, available), numSamples - available, gain);
            }
        }
    }

    int getReadPosition(const float* data, const int pos0)
    {
        int pos = pos0;
        if (pos < 0)
            pos += samples.getNumSamples();

        if (triggered) // find first zero-crossing in circular plot-buffer samplesX, giving up after 50 ms <-> 20 Hz fundamental:
        {
            auto positive = data [pos] > 0.0f;
            auto bail = int (sampleRate / 20.0f);

            while (positive == false && --bail > 0)
            {
                if (--pos < 0)
                    pos += samples.getNumSamples();

                positive = data [pos] > 0.0f;
            }

            while (positive == true && --bail > 0)
            {
                if (--pos < 0)
                    pos += samples.getNumSamples();

                positive = data [pos] > 0.0f;
            }
        }
        return pos;
    }

    JUCE_DECLARE_WEAK_REFERENCEABLE (MagicAudioPlotSource)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicAudioPlotSource)
}; // MagicAudioPlotSource

} // namespace foleys
