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

class MagicAudioPlotComponent;

/**
 This class collects two buffers of samples in a circular buffer and
 allows the GUI to draw them in the style of an scatterplot, or
 XY-plot.  For example, sin(t) and cos(t) produce a circle.
 */
class MagicScatterPlot : public MagicAudioPlotSource
{
public:

    /**
     Create an XY ScatterPlot adapter to push samples into for later display in the GUI.
     */
    MagicScatterPlot () : MagicAudioPlotSource() {}

    /**
     Push samples to a buffer to be visualised as a scatterplot (XY plot) of channels 0 (X) and 1 (Y).
     */
    void pushSamples (const juce::AudioBuffer<float>& buffer, int currentPlotLength) override;

    /**
     Push samples to a buffer to be visualised as a scatterplot (XY plot).

      @param bufferX is plotted as the X-axis coordinate.
      @param bufferY is plotted as the Y-axis coordinate.
      @param plotLength, if positive, gives the preferred length of
             the next plot in samples (e.g., one period).
             Otherwise, 10 ms of samples is plotted.
     */
    void pushSamples (const juce::AudioBuffer<float>& bufferX, int channelX,
                      const juce::AudioBuffer<float>& bufferY, int channelY,
                      const int plotLengthOverride=0) override;

    /**
     This is the callback that creates the frequency plot for drawing.

      @param path is the path instance that is constructed by the MagicPlotSource
      @param filledPath is the path instance that is constructed by the MagicPlotSource to be filled
      @param bounds the bounds of the plot
      @param component grants access to the plot component, e.g. to find the colours from it
      */
    virtual void createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicAudioPlotComponent& component) override;

    /**
     The unit square (|X| = |Y| = 1, which the sample mapping puts exactly on
     the component bounds) plus a persistent marker on that square wherever a
     pushed sample has ever exceeded it -- an over-unity excursion is
     otherwise INVISIBLE here (mapped outside the bounds and clipped away),
     and a one-block transient is gone before any eye reaches it.  Markers
     accumulate until clearClipMarkers() (e.g. when the plotted string
     changes).
     */
    void drawDecorations (juce::Graphics& g, juce::Rectangle<float> bounds, MagicAudioPlotComponent& component) override;

    /** Forget all clip markers AND the output-clipped state.  Message thread;
        benign against a concurrent audio-thread detection (worst case one
        marker survives the clear). */
    void clearClipMarkers() override;

    /** The final output went over full scale: the unit square turns bright
        red until clearClipMarkers().  Audio thread. */
    void notifyOutputClipped() override { outputClipped_.store (true, std::memory_order_relaxed); }

    /** Whether notifyOutputClipped() fired since the last clear. */
    bool wasOutputClipped() const { return outputClipped_.load (std::memory_order_relaxed); }

    /** How many clip markers have been collected since the last clear. */
    int getNumClipMarkers() const { return numClipMarkers.load (std::memory_order_acquire); }

    /** Marker i in normalised coordinates: always ON the unit square (one
        coordinate is exactly +-1, the other is the escape position). */
    juce::Point<float> getClipMarker (int i) const { return clipMarkers[size_t (i)]; }

    virtual void prepareToPlay (double sampleRate, int samplesPerBlockExpected) override;

private:

    /** Audio thread: mark any |x| > 1 or |y| > 1 excursion in this block on
        the unit square.  Fixed storage, no locks, no allocation: the square's
        perimeter is quantised into cells (4 sides x kCellsPerSide bitmask)
        so a sustained clip episode costs one marker, not one per sample. */
    void detectClipping (const float* dataX, const float* dataY, int numSamples);

    juce::AudioBuffer<float> samplesX;
    juce::AudioBuffer<float> samplesY;

    static constexpr int kCellsPerSide  = 32;
    static constexpr int kMaxClipMarkers = 4 * kCellsPerSide;
    juce::Point<float> clipMarkers[kMaxClipMarkers];  // normalised [-1,1], on the square
    std::atomic<int>          numClipMarkers { 0 };   // single producer: the audio thread
    std::atomic<juce::uint32> clipCells[4] { {0}, {0}, {0}, {0} };  // right/left/top/bottom dedupe masks
    std::atomic<bool>         outputClipped_ { false };  // the DOWNSTREAM fact (notifyOutputClipped)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicScatterPlot)
};

} // namespace foleys
